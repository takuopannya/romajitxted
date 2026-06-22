#include "http.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

// Parse URL into components
struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool useSSL = true;
};

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

static UrlParts ParseUrl(const std::string& url) {
    UrlParts parts;
    std::wstring wurl = Utf8ToWide(url);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);

    wchar_t hostBuf[256] = {};
    wchar_t pathBuf[2048] = {};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;

    WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc);

    parts.host = hostBuf;
    parts.path = pathBuf;
    parts.port = uc.nPort;
    parts.useSSL = (uc.nScheme == INTERNET_SCHEME_HTTPS);

    return parts;
}

static HttpResponse DoRequest(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers
) {
    HttpResponse resp;

    auto parts = ParseUrl(url);
    if (parts.host.empty()) {
        resp.statusCode = 0;
        resp.body = "Failed to parse URL";
        return resp;
    }

    HINTERNET hSession = WinHttpOpen(
        L"RomajiTxted/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) {
        resp.body = "WinHttpOpen failed";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, parts.host.c_str(), parts.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpConnect failed";
        return resp;
    }

    std::wstring wmethod = Utf8ToWide(method);
    DWORD flags = parts.useSSL ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, wmethod.c_str(), parts.path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpOpenRequest failed";
        return resp;
    }

    // Set timeout (30 seconds)
    WinHttpSetTimeouts(hRequest, 30000, 30000, 30000, 60000);

    // Add headers
    for (auto& [key, val] : headers) {
        std::wstring header = Utf8ToWide(key + ": " + val);
        WinHttpAddRequestHeaders(hRequest, header.c_str(), (DWORD)header.size(),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Send request
    BOOL ok = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.c_str(),
        (DWORD)body.size(),
        (DWORD)body.size(),
        0
    );

    if (!ok) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpSendRequest failed: " + std::to_string(err);
        return resp;
    }

    ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.body = "WinHttpReceiveResponse failed";
        return resp;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    resp.statusCode = (int)statusCode;

    // Read response body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::string chunk(bytesAvailable, 0);
        DWORD bytesRead = 0;
        WinHttpReadData(hRequest, &chunk[0], bytesAvailable, &bytesRead);
        chunk.resize(bytesRead);
        responseBody += chunk;
    }
    resp.body = responseBody;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
}

HttpResponse HttpPost(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers
) {
    return DoRequest("POST", url, body, headers);
}

HttpResponse HttpGet(
    const std::string& url,
    const std::map<std::string, std::string>& headers
) {
    return DoRequest("GET", url, "", headers);
}
