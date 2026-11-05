#include "http_client.hpp"

#ifdef WECHESS_WITH_CURL
#include <curl/curl.h>

#include <mutex>

namespace {

std::once_flag gCurlInit;

std::size_t writeCallback(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(data, size * count);
    return size * count;
}

struct HeaderList {
    curl_slist* list = nullptr;
    ~HeaderList() {
        if (list) curl_slist_free_all(list);
    }
};

}  // namespace

#endif  // WECHESS_WITH_CURL

namespace wechess {

bool httpClientAvailable() {
#ifdef WECHESS_WITH_CURL
    return true;
#else
    return false;
#endif
}

HttpPostResult httpPostJson(const std::string& url, const std::vector<std::string>& headers,
                            const std::string& body, int timeoutSeconds) {
    HttpPostResult result;
#ifdef WECHESS_WITH_CURL
    std::call_once(gCurlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "Could not initialise the HTTPS client.";
        return result;
    }

    HeaderList headerList;
    headerList.list = curl_slist_append(headerList.list, "Content-Type: application/json");
    headerList.list = curl_slist_append(headerList.list, "Accept: application/json");
    for (const std::string& header : headers) {
        headerList.list = curl_slist_append(headerList.list, header.c_str());
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList.list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WeChess/1.0");
    // TLS verification stays on; never disable it to "make it work".
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = std::string("HTTPS request failed: ") + curl_easy_strerror(code);
        curl_easy_cleanup(curl);
        return result;
    }
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    result.status = static_cast<int>(status);
    result.body = std::move(response);
    result.ok = status >= 200 && status < 300;
    if (!result.ok && result.error.empty()) {
        result.error = "The server returned HTTP " + std::to_string(status) + ".";
    }
    return result;
#else
    (void)url;
    (void)headers;
    (void)body;
    (void)timeoutSeconds;
    result.error =
        "This build has no HTTPS client, so Gemini explanations are disabled. "
        "Rebuild with libcurl (WECHESS_WITH_CURL) to enable them.";
    return result;
#endif
}

}  // namespace wechess
