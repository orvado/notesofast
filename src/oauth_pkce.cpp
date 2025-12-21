#include "oauth_pkce.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")

static std::string Base64UrlEncode(const std::vector<unsigned char>& data) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        unsigned int v = (data[i] << 16) | (data[i + 1] << 8) | (data[i + 2]);
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(kTable[(v >> 6) & 0x3F]);
        out.push_back(kTable[v & 0x3F]);
        i += 3;
    }

    if (i < data.size()) {
        unsigned int v = (data[i] << 16);
        if (i + 1 < data.size()) {
            v |= (data[i + 1] << 8);
        }

        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        if (i + 1 < data.size()) {
            out.push_back(kTable[(v >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }

    // Convert to base64url
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') {
        out.pop_back();
    }
    return out;
}

static std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

static bool Sha256(const std::vector<unsigned char>& data, std::vector<unsigned char>& outHash) {
    outHash.clear();
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    DWORD cbData = 0;
    DWORD cbHashObject = 0;
    DWORD cbHash = 0;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&cbHashObject, sizeof(cbHashObject), &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&cbHash, sizeof(cbHash), &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<unsigned char> hashObject(cbHashObject);
    outHash.resize(cbHash);

    if (BCryptCreateHash(hAlg, &hHash, hashObject.data(), (ULONG)hashObject.size(), nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    if (BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    if (BCryptFinishHash(hHash, (PUCHAR)outHash.data(), (ULONG)outHash.size(), 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return true;
}

static bool RandomBytes(std::vector<unsigned char>& outBytes, size_t len) {
    outBytes.resize(len);
    return BCryptGenRandom(nullptr, (PUCHAR)outBytes.data(), (ULONG)outBytes.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

static std::string MakeCodeVerifier() {
    // PKCE verifier must be 43-128 chars. We'll generate 32 random bytes => 43 chars base64url.
    std::vector<unsigned char> rnd;
    if (!RandomBytes(rnd, 32)) {
        // fallback: weak, but only used if RNG fails
        const char* fallback = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
        return std::string(fallback, fallback + 64).substr(0, 43);
    }
    return Base64UrlEncode(rnd);
}

static std::string MakeState() {
    std::vector<unsigned char> rnd;
    if (!RandomBytes(rnd, 16)) {
        return "state";
    }
    return Base64UrlEncode(rnd);
}

static std::string MakeCodeChallengeS256(const std::string& verifier) {
    std::vector<unsigned char> data(verifier.begin(), verifier.end());
    std::vector<unsigned char> hash;
    if (!Sha256(data, hash)) {
        return "";
    }
    return Base64UrlEncode(hash);
}

static bool ExtractQueryParam(const std::string& url, const std::string& key, std::string& outValue) {
    // url is expected to be path + query, like /callback?code=...&state=...
    size_t q = url.find('?');
    if (q == std::string::npos) return false;
    std::string query = url.substr(q + 1);

    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = (amp == std::string::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string k = (eq == std::string::npos) ? pair : pair.substr(0, eq);
        std::string v = (eq == std::string::npos) ? "" : pair.substr(eq + 1);
        if (k == key) {
            outValue = v;
            return true;
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return false;
}

static int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static std::string UrlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int hi = HexVal(s[i + 1]);
            int lo = HexVal(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        } else if (c == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

static bool CreateLoopbackListener(SOCKET& outSocket, unsigned short& outPort, std::string& outError) {
    outSocket = INVALID_SOCKET;
    outPort = 0;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        outError = "socket() failed";
        return false;
    }

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        outError = "bind() failed";
        closesocket(s);
        return false;
    }

    sockaddr_in bound = {};
    int boundLen = sizeof(bound);
    if (getsockname(s, (sockaddr*)&bound, &boundLen) == SOCKET_ERROR) {
        outError = "getsockname() failed";
        closesocket(s);
        return false;
    }

    if (listen(s, 1) == SOCKET_ERROR) {
        outError = "listen() failed";
        closesocket(s);
        return false;
    }

    outPort = ntohs(bound.sin_port);
    outSocket = s;
    return true;
}

static bool WaitForSingleHttpGetRequest(SOCKET listener, std::string& outRequestTarget, std::string& outError) {
    outRequestTarget.clear();

    // Wait up to 5 minutes for the browser redirect.
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(listener, &readSet);
    timeval tv = {};
    tv.tv_sec = 300;

    int sel = select(0, &readSet, nullptr, nullptr, &tv);
    if (sel <= 0) {
        outError = "Timed out waiting for browser redirect";
        return false;
    }

    SOCKET c = accept(listener, nullptr, nullptr);
    if (c == INVALID_SOCKET) {
        outError = "accept() failed";
        return false;
    }

    char buf[8192];
    int n = recv(c, buf, (int)sizeof(buf) - 1, 0);
    if (n <= 0) {
        outError = "recv() failed";
        closesocket(c);
        return false;
    }
    buf[n] = 0;

    // Parse request line: GET /callback?... HTTP/1.1
    std::string req(buf);
    size_t lineEnd = req.find("\r\n");
    std::string firstLine = (lineEnd == std::string::npos) ? req : req.substr(0, lineEnd);

    std::string target;
    if (firstLine.rfind("GET ", 0) == 0) {
        size_t sp2 = firstLine.find(' ', 4);
        if (sp2 != std::string::npos) {
            target = firstLine.substr(4, sp2 - 4);
        }
    }

    outRequestTarget = target;

    const char* body = "<html><body><h3>NoteSoFast</h3><p>Connected. You can close this window.</p></body></html>";
    char resp[1024];
    std::snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
        (unsigned)std::strlen(body), body);

    send(c, resp, (int)std::strlen(resp), 0);
    closesocket(c);

    if (outRequestTarget.empty()) {
        outError = "Failed to parse redirect request";
        return false;
    }

    return true;
}

static std::string WinHttpPostForm(const std::wstring& host, const std::wstring& path, const std::string& body, std::string& outError) {
    outError.clear();

    HINTERNET hSession = WinHttpOpen(L"NoteSoFast/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = "WinHttpOpen failed";
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        outError = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        outError = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    const wchar_t* headers =
        L"Content-Type: application/x-www-form-urlencoded\r\n"
        L"Accept: application/json\r\n";
    BOOL b = WinHttpSendRequest(
        hRequest,
        headers,
        (DWORD)-1,
        (LPVOID)(body.empty() ? nullptr : body.data()),
        (DWORD)body.size(),
        (DWORD)body.size(),
        0);

    if (!b) {
        outError = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        outError = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::string response;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
            break;
        }
        if (avail == 0) {
            break;
        }
        std::string chunk;
        chunk.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0) {
            break;
        }
        chunk.resize(read);
        response += chunk;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return response;
}

static bool ExtractJsonString(const std::string& json, const char* key, std::string& outValue) {
    outValue.clear();
    std::string needle = std::string("\"") + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return false;
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && std::isspace((unsigned char)json[p])) p++;
    if (p >= json.size() || json[p] != '"') return false;
    p++;
    size_t end = json.find('"', p);
    if (end == std::string::npos) return false;
    outValue = json.substr(p, end - p);
    return true;
}

OAuthPkceResult OAuthPkce::ConnectGoogleDriveAppDataPkce(const std::string& clientId, const std::string& clientSecret) {
    OAuthPkceResult result;

    if (clientId.empty()) {
        result.error = "Missing OAuth Client ID";
        return result;
    }

    const std::string verifier = MakeCodeVerifier();
    const std::string challenge = MakeCodeChallengeS256(verifier);
    if (challenge.empty()) {
        result.error = "Failed to compute PKCE challenge";
        return result;
    }

    const std::string state = MakeState();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        result.error = "WSAStartup failed";
        return result;
    }

    SOCKET listener = INVALID_SOCKET;
    unsigned short port = 0;
    std::string listenError;
    if (!CreateLoopbackListener(listener, port, listenError)) {
        WSACleanup();
        result.error = listenError;
        return result;
    }

    const std::string redirectUri = "http://127.0.0.1:" + std::to_string(port) + "/callback";
    const std::string scope = "https://www.googleapis.com/auth/drive.appdata";

    std::string authUrl = "https://accounts.google.com/o/oauth2/v2/auth";
    authUrl += "?client_id=" + UrlEncode(clientId);
    authUrl += "&redirect_uri=" + UrlEncode(redirectUri);
    authUrl += "&response_type=code";
    authUrl += "&scope=" + UrlEncode(scope);
    authUrl += "&code_challenge=" + UrlEncode(challenge);
    authUrl += "&code_challenge_method=S256";
    authUrl += "&access_type=offline";
    authUrl += "&prompt=consent";
    authUrl += "&state=" + UrlEncode(state);

    // Open the browser after the listener is ready.
    ShellExecuteW(nullptr, L"open", Utf8ToWide(authUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    std::string requestTarget;
    std::string waitError;
    bool gotRequest = WaitForSingleHttpGetRequest(listener, requestTarget, waitError);
    closesocket(listener);
    WSACleanup();
    if (!gotRequest) {
        result.error = waitError;
        return result;
    }

    std::string codeEnc;
    std::string stateEnc;

    if (!ExtractQueryParam(requestTarget, "code", codeEnc)) {
        // might be an error redirect
        std::string err;
        if (ExtractQueryParam(requestTarget, "error", err)) {
            result.error = "OAuth error: " + UrlDecode(err);
        } else {
            result.error = "No authorization code received";
        }
        return result;
    }
    ExtractQueryParam(requestTarget, "state", stateEnc);

    const std::string code = UrlDecode(codeEnc);
    const std::string returnedState = UrlDecode(stateEnc);
    if (!returnedState.empty() && returnedState != state) {
        result.error = "State mismatch";
        return result;
    }

    std::string tokenBody;
    tokenBody += "code=" + UrlEncode(code);
    tokenBody += "&client_id=" + UrlEncode(clientId);
    if (!clientSecret.empty()) {
        tokenBody += "&client_secret=" + UrlEncode(clientSecret);
    }
    tokenBody += "&code_verifier=" + UrlEncode(verifier);
    tokenBody += "&redirect_uri=" + UrlEncode(redirectUri);
    tokenBody += "&grant_type=authorization_code";

    std::string httpError;
    std::string tokenJson = WinHttpPostForm(L"oauth2.googleapis.com", L"/token", tokenBody, httpError);
    if (tokenJson.empty()) {
        result.error = httpError.empty() ? "Token exchange failed" : httpError;
        return result;
    }

    std::string refreshToken;
    std::string accessToken;
    ExtractJsonString(tokenJson, "refresh_token", refreshToken);
    ExtractJsonString(tokenJson, "access_token", accessToken);

    if (accessToken.empty()) {
        std::string err;
        std::string errDesc;
        ExtractJsonString(tokenJson, "error_description", errDesc);
        if (ExtractJsonString(tokenJson, "error", err)) {
            if (err == "invalid_request" && errDesc.find("client_secret is missing") != std::string::npos) {
                result.error = "Token error: client_secret is missing.";
                if (clientSecret.empty()) {
                    result.error += " Enter your OAuth Client Secret in the Cloud Sync settings, then Connect again.";
                }
            } else {
                result.error = "Token error: " + err;
                if (!errDesc.empty()) {
                    result.error += " (" + errDesc + ")";
                }
            }
        } else {
            // Provide the raw response to make debugging possible.
            std::string snippet = tokenJson;
            if (snippet.size() > 512) {
                snippet.resize(512);
                snippet += "...";
            }
            result.error = "Token exchange failed: " + snippet;
        }
        return result;
    }

    if (refreshToken.empty()) {
        // This can happen if Google doesn't return a refresh token (e.g., already granted).
        // We request prompt=consent to encourage returning it, but still handle the case.
        result.error = "No refresh token received (try Disconnect then Connect again)";
        return result;
    }

    result.success = true;
    result.accessToken = accessToken;
    result.refreshToken = refreshToken;
    return result;
}
