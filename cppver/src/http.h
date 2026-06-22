#pragma once
#include <string>
#include <map>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Synchronous HTTP request using WinHTTP
HttpResponse HttpPost(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers = {}
);

HttpResponse HttpGet(
    const std::string& url,
    const std::map<std::string, std::string>& headers = {}
);
