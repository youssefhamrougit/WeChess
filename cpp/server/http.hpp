// WeChess - minimal embedded HTTP/1.1 server.
//
// Deliberately small: one request per connection, no chunked encoding, no
// keep-alive. It only ever listens on the loopback interface and serves the
// local UI, so a full framework would add risk without adding value.
#ifndef WECHESS_SERVER_HTTP_HPP
#define WECHESS_SERVER_HTTP_HPP

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace wechess {

struct UploadedFile {
    std::string filename;
    std::string contentType;
    std::string data;
};

struct HttpRequest {
    std::string method;
    std::string path;      // decoded path, no query
    std::string rawQuery;  // undecoded query string
    std::string httpVersion;
    std::string body;
    std::string clientIp;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;  // lower-case keys
    std::map<std::string, std::string> form;
    std::map<std::string, UploadedFile> files;

    // Query parameter first, then form field, then the default.
    std::string param(const std::string& name, const std::string& fallback = "") const;
    int intParam(const std::string& name, int fallback) const;
    bool has(const std::string& name) const;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    static HttpResponse html(std::string body, int status = 200);
    static HttpResponse text(std::string body, const std::string& type = "text/plain; charset=utf-8");
    static HttpResponse redirect(const std::string& location, int status = 303);
    static HttpResponse error(int status, const std::string& message);
};

class HttpServer {
   public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer() = default;
    ~HttpServer();

    // Binds to `host:port` (port 0 picks a free port). Returns false and fills
    // `error` on failure; the bound port is available through port().
    bool listen(const std::string& host, int port, std::string& error);
    int port() const { return port_; }

    // Starts the accept loop on a background thread.
    void start();
    void stop();

    Handler handler;

   private:
    void acceptLoop();
    void handleConnection(int fd);
    void handleConnection(int fd, const std::string& clientIp);

    int listenFd_ = -1;
    int port_ = 0;
    std::string host_;
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
};

// Helpers shared with tests.
std::string urlDecode(const std::string& value);
std::string urlEncode(const std::string& value);
std::string htmlEscape(const std::string& value);

}  // namespace wechess

#endif  // WECHESS_SERVER_HTTP_HPP
