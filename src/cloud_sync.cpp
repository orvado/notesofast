#include "cloud_sync.h"

#include "credentials.h"
#include "database.h"
#include "utils.h"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::string error;
};

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
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

static HttpResponse WinHttpRequestBytes(
    const wchar_t* method,
    const std::wstring& host,
    const std::wstring& path,
    const std::wstring& headers,
    const unsigned char* body,
    DWORD bodyLen) {

    HttpResponse resp;

    HINTERNET hSession = WinHttpOpen(L"NoteSoFast/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    // Keep UI responsive; don't hang forever.
    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        resp.error = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        method,
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        resp.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    BOOL ok = WinHttpSendRequest(
        hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)-1,
        (LPVOID)body,
        bodyLen,
        bodyLen,
        0);

    if (!ok) {
        resp.error = "WinHttpSendRequest failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        resp.error = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    resp.status = status;

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
    resp.body = std::move(response);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

static HttpResponse WinHttpPostForm(const std::wstring& host, const std::wstring& path, const std::string& bodyUtf8) {
    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n";
    return WinHttpRequestBytes(L"POST", host, path, headers,
        (const unsigned char*)(bodyUtf8.empty() ? nullptr : bodyUtf8.data()),
        (DWORD)bodyUtf8.size());
}

static bool RefreshAccessToken(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& refreshToken,
    std::string& outAccessToken,
    std::string& outError) {

    outAccessToken.clear();
    outError.clear();

    std::string body;
    body += "client_id=" + UrlEncode(clientId);
    if (!clientSecret.empty()) {
        body += "&client_secret=" + UrlEncode(clientSecret);
    }
    body += "&refresh_token=" + UrlEncode(refreshToken);
    body += "&grant_type=refresh_token";

    HttpResponse resp = WinHttpPostForm(L"oauth2.googleapis.com", L"/token", body);
    if (resp.status != 200) {
        std::string err;
        std::string errDesc;
        ExtractJsonString(resp.body, "error", err);
        ExtractJsonString(resp.body, "error_description", errDesc);
        if (!err.empty()) {
            outError = "Token refresh error: " + err;
            if (!errDesc.empty()) outError += " (" + errDesc + ")";
        } else {
            outError = "Token refresh failed (HTTP " + std::to_string(resp.status) + ")";
        }
        return false;
    }

    if (!ExtractJsonString(resp.body, "access_token", outAccessToken) || outAccessToken.empty()) {
        outError = "Failed to parse access_token";
        return false;
    }

    return true;
}

static bool FindAppDataFile(const std::string& accessToken, const std::string& fileName, std::string& outId, std::string& outModifiedTime, std::string& outError) {
    outId.clear();
    outModifiedTime.clear();
    outError.clear();

    // q=name='notesofast.db'
    std::string q = "name='" + fileName + "'";
    std::string pathUtf8 = "/drive/v3/files?spaces=appDataFolder&fields=files(id,name,modifiedTime,size)&q=" + UrlEncode(q);

    std::wstring headers = L"Accept: application/json\r\n";
    std::wstring auth = L"Authorization: Bearer " + Utf8ToWide(accessToken) + L"\r\n";
    headers += auth;

    HttpResponse resp = WinHttpRequestBytes(L"GET", L"www.googleapis.com", Utf8ToWide(pathUtf8), headers, nullptr, 0);
    if (resp.status != 200) {
        std::string msg;
        ExtractJsonString(resp.body, "message", msg);
        outError = "Drive list failed (HTTP " + std::to_string(resp.status) + ")";
        if (!msg.empty()) {
            outError += ": " + msg;
        }
        return false;
    }

    // Very small parser: find first "id":"..." and "modifiedTime":"..." in the files array.
    size_t filesPos = resp.body.find("\"files\"");
    if (filesPos == std::string::npos) {
        return true; // no files
    }
    std::string id;
    std::string mt;
    ExtractJsonString(resp.body.substr(filesPos), "id", id);
    ExtractJsonString(resp.body.substr(filesPos), "modifiedTime", mt);
    outId = id;
    outModifiedTime = mt;
    return true;
}

static HttpResponse DriveUploadMultipart(
    const wchar_t* method,
    const std::string& accessToken,
    const std::wstring& path,
    const std::string& fileName,
    const std::vector<unsigned char>& content,
    const std::string& mimeType,
    bool includeParents) {

    std::string boundary = "----NoteSoFastBoundary7MA4YWxkTrZu0gW";

    std::string meta = "{";
    meta += "\"name\":\"" + fileName + "\"";
    if (includeParents) {
        meta += ",\"parents\":[\"appDataFolder\"]";
    }
    meta += "}";

    std::string pre;
    pre += "--" + boundary + "\r\n";
    pre += "Content-Type: application/json; charset=UTF-8\r\n\r\n";
    pre += meta + "\r\n";
    pre += "--" + boundary + "\r\n";
    pre += "Content-Type: " + mimeType + "\r\n\r\n";

    std::string post;
    post += "\r\n--" + boundary + "--\r\n";

    std::vector<unsigned char> body;
    body.reserve(pre.size() + content.size() + post.size());
    body.insert(body.end(), pre.begin(), pre.end());
    body.insert(body.end(), content.begin(), content.end());
    body.insert(body.end(), post.begin(), post.end());

    std::wstring headers;
    headers += L"Content-Type: multipart/related; boundary=" + Utf8ToWide(boundary) + L"\r\n";
    headers += L"Accept: application/json\r\n";
    headers += L"Authorization: Bearer " + Utf8ToWide(accessToken) + L"\r\n";

    return WinHttpRequestBytes(method, L"www.googleapis.com", path, headers, body.data(), (DWORD)body.size());
}

static unsigned long long FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

static bool GetFileLastWriteTimeUtcU64(const std::wstring& path, unsigned long long& outFt) {
    outFt = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        return false;
    }
    outFt = FileTimeToU64(fad.ftLastWriteTime);
    return true;
}

static std::string FileNameFromPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < path.size()) {
        return Utils::WideToUtf8(path.substr(slash + 1));
    }
    return "notesofast.db";
}

static bool WriteAllBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    BOOL ok = TRUE;
    if (!bytes.empty()) {
        ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
    }
    CloseHandle(h);
    return ok && written == (DWORD)bytes.size();
}

static bool ParseRfc3339ToFileTimeUtc(const std::string& s, unsigned long long& outFileTimeUtc) {
    outFileTimeUtc = 0;

    // Expected: YYYY-MM-DDTHH:MM:SS(.sss)Z
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &m, &sec) != 6) {
        return false;
    }

    SYSTEMTIME st = {};
    st.wYear = (WORD)Y;
    st.wMonth = (WORD)M;
    st.wDay = (WORD)D;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)m;
    st.wSecond = (WORD)sec;

    FILETIME ft = {};
    if (!SystemTimeToFileTime(&st, &ft)) {
        return false;
    }
    outFileTimeUtc = FileTimeToU64(ft);
    return true;
}

static HttpResponse DriveDownloadFile(const std::string& accessToken, const std::string& fileId) {
    std::string pathUtf8 = "/drive/v3/files/" + fileId + "?alt=media";

    std::wstring headers;
    headers += L"Authorization: Bearer " + Utf8ToWide(accessToken) + L"\r\n";

    return WinHttpRequestBytes(L"GET", L"www.googleapis.com", Utf8ToWide(pathUtf8), headers, nullptr, 0);
}

} // namespace

CloudSyncResult CloudSync::UploadToAppDataFolder(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& refreshToken,
    const std::string& fileName,
    const std::vector<unsigned char>& content,
    const std::string& mimeType) {

    CloudSyncResult r;

    if (clientId.empty() || refreshToken.empty()) {
        r.error = "Missing clientId or refreshToken";
        return r;
    }

    std::string accessToken;
    std::string err;
    if (!RefreshAccessToken(clientId, clientSecret, refreshToken, accessToken, err)) {
        r.error = err;
        return r;
    }

    std::string fileId;
    std::string modifiedTime;
    if (!FindAppDataFile(accessToken, fileName, fileId, modifiedTime, err)) {
        r.error = err;
        return r;
    }

    if (fileId.empty()) {
        HttpResponse resp = DriveUploadMultipart(L"POST", accessToken,
            L"/upload/drive/v3/files?uploadType=multipart",
            fileName,
            content,
            mimeType,
            true);

        if (resp.status != 200 && resp.status != 201) {
            std::string e;
            std::string eDesc;
            ExtractJsonString(resp.body, "message", eDesc);
            r.error = "Drive upload failed (HTTP " + std::to_string(resp.status) + ")";
            if (!eDesc.empty()) r.error += ": " + eDesc;
            return r;
        }

        std::string mt;
        ExtractJsonString(resp.body, "modifiedTime", mt);
        r.remoteModifiedTime = mt;
        r.success = true;
        return r;
    }

    std::wstring path = L"/upload/drive/v3/files/" + Utf8ToWide(fileId) + L"?uploadType=multipart";
    HttpResponse resp = DriveUploadMultipart(L"PATCH", accessToken, path, fileName, content, mimeType, false);

    if (resp.status != 200) {
        std::string eDesc;
        ExtractJsonString(resp.body, "message", eDesc);
        r.error = "Drive upload failed (HTTP " + std::to_string(resp.status) + ")";
        if (!eDesc.empty()) r.error += ": " + eDesc;
        return r;
    }

    std::string mt;
    ExtractJsonString(resp.body, "modifiedTime", mt);
    r.remoteModifiedTime = mt;
    r.success = true;
    return r;
}

CloudSyncResult CloudSync::DownloadIfRemoteNewer(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& refreshToken,
    const std::string& fileName,
    unsigned long long localDbLastWriteFileTimeUtc,
    std::vector<unsigned char>& outContent) {

    outContent.clear();
    CloudSyncResult r;

    std::string accessToken;
    std::string err;
    if (!RefreshAccessToken(clientId, clientSecret, refreshToken, accessToken, err)) {
        r.error = err;
        return r;
    }

    std::string fileId;
    std::string modifiedTime;
    if (!FindAppDataFile(accessToken, fileName, fileId, modifiedTime, err)) {
        r.error = err;
        return r;
    }

    if (fileId.empty()) {
        r.success = true; // nothing to do
        return r;
    }

    unsigned long long remoteFt = 0;
    if (!modifiedTime.empty() && ParseRfc3339ToFileTimeUtc(modifiedTime, remoteFt)) {
        if (remoteFt <= localDbLastWriteFileTimeUtc) {
            r.success = true;
            r.remoteModifiedTime = modifiedTime;
            return r;
        }
    }

    HttpResponse resp = DriveDownloadFile(accessToken, fileId);
    if (resp.status != 200) {
        r.error = "Drive download failed (HTTP " + std::to_string(resp.status) + ")";
        return r;
    }

    outContent.assign(resp.body.begin(), resp.body.end());
    r.remoteModifiedTime = modifiedTime;
    r.success = true;
    return r;
}

CloudSyncResult CloudSync::UploadDatabaseSnapshot(Database* db, const std::wstring& dbPath, const std::string& clientId) {
    CloudSyncResult r;

    if (!db) {
        r.error = "Missing database";
        return r;
    }
    if (clientId.empty()) {
        r.error = "Missing OAuth Client ID";
        return r;
    }

    std::string refreshToken;
    if (!Credentials::ReadUtf8String(CloudSync::kCloudRefreshTokenCredTarget, refreshToken) || refreshToken.empty()) {
        r.error = "Not connected (missing refresh token)";
        return r;
    }

    std::string clientSecret;
    Credentials::ReadUtf8String(CloudSync::kCloudClientSecretCredTarget, clientSecret);

    wchar_t tmpPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPath);
    wchar_t snapName[128];
    std::swprintf(snapName, 128, L"NoteSoFast.cloud.snapshot.%lu.%lu.db", GetCurrentProcessId(), GetCurrentThreadId());
    std::wstring snapPath = std::wstring(tmpPath) + snapName;
    std::string snapPathUtf8 = Utils::WideToUtf8(snapPath);

    if (!db->BackupToFile(snapPathUtf8)) {
        r.error = "Failed to create DB snapshot";
        return r;
    }

    std::ifstream in(snapPathUtf8, std::ios::binary);
    if (!in) {
        r.error = "Failed to read DB snapshot";
        return r;
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::string fileName = FileNameFromPath(dbPath);
    return CloudSync::UploadToAppDataFolder(clientId, clientSecret, refreshToken, fileName, bytes, "application/x-sqlite3");
}

CloudSyncResult CloudSync::RestoreDatabaseIfRemoteNewer(const std::wstring& dbPath, const std::string& clientId, bool& outRestored) {
    outRestored = false;
    CloudSyncResult r;

    if (clientId.empty()) {
        r.error = "Missing OAuth Client ID";
        return r;
    }

    std::string refreshToken;
    if (!Credentials::ReadUtf8String(CloudSync::kCloudRefreshTokenCredTarget, refreshToken) || refreshToken.empty()) {
        r.error = "Not connected (missing refresh token)";
        return r;
    }

    std::string clientSecret;
    Credentials::ReadUtf8String(CloudSync::kCloudClientSecretCredTarget, clientSecret);

    unsigned long long localFt = 0;
    GetFileLastWriteTimeUtcU64(dbPath, localFt); // if missing, stays 0

    std::vector<unsigned char> content;
    std::string fileName = FileNameFromPath(dbPath);
    r = CloudSync::DownloadIfRemoteNewer(clientId, clientSecret, refreshToken, fileName, localFt, content);
    if (!r.success) {
        return r;
    }

    if (content.empty()) {
        // No remote file, or remote not newer.
        return r;
    }

    // Write to a temp file in the same directory, then replace.
    std::wstring tmp = dbPath + L".cloud.tmp";
    if (!WriteAllBytes(tmp, content)) {
        r.success = false;
        r.error = "Failed to write downloaded database";
        return r;
    }

    // Best-effort backup of the current DB.
    std::wstring bak = dbPath + L".bak";
    MoveFileExW(dbPath.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING);

    if (!MoveFileExW(tmp.c_str(), dbPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // Try to restore original if replace failed.
        MoveFileExW(bak.c_str(), dbPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(tmp.c_str());
        r.success = false;
        r.error = "Failed to replace local database";
        return r;
    }

    outRestored = true;
    r.success = true;
    return r;
}
