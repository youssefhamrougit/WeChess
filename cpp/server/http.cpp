#include "http.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace wechess {
namespace {

constexpr std::size_t kMaxHeaderBytes = 32 * 1024;
constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;
constexpr int kReadTimeoutMs = 20000;

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

void parseUrlEncoded(const std::string& input, std::map<std::string, std::string>& out) {
    std::size_t position = 0;
    while (position <= input.size()) {
        const std::size_t amp = input.find('&', position);
        const std::string pair =
            input.substr(position, amp == std::string::npos ? std::string::npos : amp - position);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            const std::string key = equals == std::string::npos ? pair : pair.substr(0, equals);
            const std::string value = equals == std::string::npos ? std::string() : pair.substr(equals + 1);
            std::string decodedKey = urlDecode(key);
            if (!decodedKey.empty()) out[decodedKey] = urlDecode(value);
        }
        if (amp == std::string::npos) break;
        position = amp + 1;
    }
}

std::string headerParam(const std::string& header, const std::string& key) {
    const std::string lowered = toLower(header);
    const std::string needle = toLower(key) + "=";
    const std::size_t at = lowered.find(needle);
    if (at == std::string::npos) return std::string();
    std::size_t begin = at + needle.size();
    std::string value;
    if (begin < header.size() && header[begin] == '"') {
        ++begin;
        while (begin < header.size() && header[begin] != '"') value.push_back(header[begin++]);
    } else {
        while (begin < header.size() && header[begin] != ';' && header[begin] != ' ') {
            value.push_back(header[begin++]);
        }
    }
    return trim(value);
}

// multipart/form-data parser sufficient for a single PGN upload.
void parseMultipart(const std::string& body, const std::string& contentType, HttpRequest& request) {
    const std::string boundary = headerParam(contentType, "boundary");
    if (boundary.empty()) return;
    const std::string delimiter = "--" + boundary;
    std::size_t position = body.find(delimiter);
    if (position == std::string::npos) return;

    while (true) {
        position += delimiter.size();
        if (position + 1 < body.size() && body.compare(position, 2, "--") == 0) break;
        if (position + 1 < body.size() && body.compare(position, 2, "\r\n") == 0) position += 2;

        const std::size_t headerEnd = body.find("\r\n\r\n", position);
        if (headerEnd == std::string::npos) break;
        const std::string partHeaders = body.substr(position, headerEnd - position);
        const std::size_t dataBegin = headerEnd + 4;

        std::size_t next = body.find(delimiter, dataBegin);
        if (next == std::string::npos) next = body.size();
        std::size_t dataEnd = next;
        // Strip the CRLF that precedes the boundary.
        if (dataEnd >= dataBegin + 2 && body.compare(dataEnd - 2, 2, "\r\n") == 0) dataEnd -= 2;
        const std::string data = body.substr(dataBegin, dataEnd - dataBegin);

        const std::string name = headerParam(partHeaders, "name");
        const std::string filename = headerParam(partHeaders, "filename");
        if (!name.empty()) {
            if (!filename.empty()) {
                UploadedFile file;
                file.filename = filename;
                file.contentType = headerParam(partHeaders, "Content-Type");
                file.data = data;
                request.files[name] = file;
            } else {
                request.form[name] = data;
            }
        }
        position = next;
        if (position >= body.size()) break;
    }
}

bool sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

const char* statusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "OK";
    }
}

}  // namespace

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size() &&
                   std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            const int code = std::stoi(value.substr(i + 1, 2), nullptr, 16);
            out.push_back(static_cast<char>(code));
            i += 2;
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string urlEncode(const std::string& value) {
    std::string out;
    char buffer[4];
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
            out += buffer;
        }
    }
    return out;
}

std::string htmlEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string HttpRequest::param(const std::string& name, const std::string& fallback) const {
    auto q = query.find(name);
    if (q != query.end()) return q->second;
    auto f = form.find(name);
    if (f != form.end()) return f->second;
    return fallback;
}

int HttpRequest::intParam(const std::string& name, int fallback) const {
    const std::string value = param(name);
    if (value.empty()) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

bool HttpRequest::has(const std::string& name) const {
    return query.count(name) > 0 || form.count(name) > 0 || files.count(name) > 0;
}

HttpResponse HttpResponse::html(std::string body, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/html; charset=utf-8";
    response.body = std::move(body);
    return response;
}

HttpResponse HttpResponse::text(std::string body, const std::string& type) {
    HttpResponse response;
    response.contentType = type;
    response.body = std::move(body);
    return response;
}

HttpResponse HttpResponse::redirect(const std::string& location, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/html; charset=utf-8";
    response.headers.push_back({"Location", location});
    response.body = "<!doctype html><title>Redirecting</title><p>Moved to <a href=\"" +
                    htmlEscape(location) + "\">" + htmlEscape(location) + "</a>.</p>";
    return response;
}

HttpResponse HttpResponse::error(int status, const std::string& message) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/html; charset=utf-8";
    response.body = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                    "<title>WeChess - error</title></head><body style=\"font-family:system-ui;"
                    "background:#14161a;color:#f1eee6;padding:24px\"><h1 style=\"font-size:20px\">" +
                    std::to_string(status) + " " + statusText(status) + "</h1><p>" +
                    htmlEscape(message) + "</p></body></html>";
    return response;
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::listen(const std::string& host, int port, std::string& error) {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        error = std::string("socket() failed: ") + std::strerror(errno);
        return false;
    }
    int reuse = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "Invalid listen address: " + host;
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = std::string("bind() failed: ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, 32) != 0) {
        error = std::string("listen() failed: ") + std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    sockaddr_in bound{};
    socklen_t length = sizeof(bound);
    ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&bound), &length);
    port_ = ntohs(bound.sin_port);
    host_ = host;
    return true;
}

void HttpServer::start() {
    if (listenFd_ < 0) return;
    running_.store(true);
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void HttpServer::acceptLoop() {
    while (running_.load()) {
        struct pollfd pfd;
        pfd.fd = listenFd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int ready = ::poll(&pfd, 1, 200);
        if (ready <= 0) continue;

        sockaddr_in peer{};
        socklen_t length = sizeof(peer);
        const int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &length);
        if (fd < 0) continue;

        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        const std::string clientIp(ip);

        std::thread([this, fd, clientIp] {
            handleConnection(fd, clientIp);
            ::close(fd);
        }).detach();
    }
}

void HttpServer::handleConnection(int fd) { handleConnection(fd, "127.0.0.1"); }

void HttpServer::handleConnection(int fd, const std::string& clientIp) {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    std::string buffer;
    std::string headerBlock;
    std::size_t headerEnd = std::string::npos;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadTimeoutMs);

    // ---- read headers ------------------------------------------------------
    while (headerEnd == std::string::npos) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) return;
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready <= 0) return;
        char chunk[8192];
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) return;
        buffer.append(chunk, static_cast<std::size_t>(n));
        if (buffer.size() > kMaxHeaderBytes) {
            sendAll(fd, "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n");
            return;
        }
        headerEnd = buffer.find("\r\n\r\n");
    }

    headerBlock = buffer.substr(0, headerEnd);
    std::string body = buffer.substr(headerEnd + 4);

    HttpRequest request;
    request.clientIp = clientIp;

    std::istringstream lines(headerBlock);
    std::string line;
    if (!std::getline(lines, line)) return;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream first(line);
        std::string target;
        if (!(first >> request.method >> target >> request.httpVersion)) return;
        const std::size_t question = target.find('?');
        request.path = urlDecode(target.substr(0, question));
        if (question != std::string::npos) {
            request.rawQuery = target.substr(question + 1);
            parseUrlEncoded(request.rawQuery, request.query);
        }
        if (request.path.empty() || request.path[0] != '/') return;
    }

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        request.headers[toLower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    std::size_t contentLength = 0;
    if (auto it = request.headers.find("content-length"); it != request.headers.end()) {
        try {
            contentLength = static_cast<std::size_t>(std::stoul(it->second));
        } catch (...) {
            contentLength = 0;
        }
    }
    if (contentLength > kMaxBodyBytes) {
        sendAll(fd, "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n");
        return;
    }

    // ---- read body ---------------------------------------------------------
    while (body.size() < contentLength) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) return;
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (ready <= 0) return;
        char chunk[8192];
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        body.append(chunk, static_cast<std::size_t>(n));
    }
    body.resize(std::min(body.size(), contentLength));
    request.body = std::move(body);

    const std::string contentType = [&] {
        auto it = request.headers.find("content-type");
        return it == request.headers.end() ? std::string() : it->second;
    }();

    if (contentType.rfind("application/x-www-form-urlencoded", 0) == 0) {
        parseUrlEncoded(request.body, request.form);
    } else if (contentType.rfind("multipart/form-data", 0) == 0) {
        parseMultipart(request.body, contentType, request);
    }

    // ---- dispatch ----------------------------------------------------------
    HttpResponse response;
    if (!handler) {
        response = HttpResponse::error(500, "The application has no request handler.");
    } else {
        try {
            response = handler(request);
        } catch (const std::exception& ex) {
            response = HttpResponse::error(500, std::string("Internal error: ") + ex.what());
        } catch (...) {
            response = HttpResponse::error(500, "Internal error while handling the request.");
        }
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << statusText(response.status) << "\r\n";
    out << "Content-Type: " << response.contentType << "\r\n";
    out << "Content-Length: " << response.body.size() << "\r\n";
    out << "Cache-Control: no-store\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    out << "X-Frame-Options: DENY\r\n";
    out << "Referrer-Policy: no-referrer\r\n";
    out << "Content-Security-Policy: default-src 'none'; script-src 'none'; style-src 'self' "
           "'unsafe-inline'; "
           "img-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'\r\n";
    out << "Connection: close\r\n";
    for (const auto& header : response.headers) {
        out << header.first << ": " << header.second << "\r\n";
    }
    out << "\r\n";
    out << response.body;
    sendAll(fd, out.str());
}

void HttpServer::stop() {
    if (!running_.exchange(false)) {
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
        return;
    }
    if (listenFd_ >= 0) {
        ::shutdown(listenFd_, SHUT_RDWR);
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
}

}  // namespace wechess
