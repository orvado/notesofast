#include <windows.h>
#include <string>
#include "window.h"
#include "database.h"
#include "utils.h"
#include "cloud_sync.h"

static std::string NowLocalTimeString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

static std::wstring ResolveDatabasePath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    std::wstring exeDir = exePath;
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        exeDir = exeDir.substr(0, pos);
    }

    auto normalize = [](const std::wstring& path) {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetFullPathName(path.c_str(), MAX_PATH, buffer, nullptr);
        if (len > 0 && len < MAX_PATH) {
            return std::wstring(buffer);
        }
        return path;
    };

    std::wstring candidates[] = {
        exeDir + L"\\notesofast.db",
        exeDir + L"\\..\\notesofast.db"
    };

    for (const auto& candidate : candidates) {
        if (GetFileAttributes(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return normalize(candidate);
        }
    }

    return normalize(candidates[0]);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize Database
    Database db;
    std::wstring dbPathW = ResolveDatabasePath();
    std::string dbPath = Utils::WideToUtf8(dbPathW);
    if (!db.Initialize(dbPath)) {
        MessageBox(NULL, L"Failed to initialize database.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Auto-restore from cloud if enabled and remote copy is newer.
    bool restored = false;
    CloudSyncResult restoreRes;
    const bool cloudEnabled = (db.GetSetting("cloud_sync_enabled", "0") == "1");
    const std::string clientId = db.GetSetting("cloud_oauth_client_id", "");
    if (cloudEnabled && !clientId.empty()) {
        db.Close();
        restoreRes = CloudSync::RestoreDatabaseIfRemoteNewer(dbPathW, clientId, restored);
        // Re-open DB after restore attempt.
        if (!db.Initialize(dbPath)) {
            MessageBox(NULL, L"Failed to initialize database.", L"Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (!restoreRes.success && !restoreRes.error.empty()) {
            db.SetSetting("cloud_sync_last_error", restoreRes.error);
        } else if (restored) {
            db.SetSetting("cloud_sync_last_error", "");
            db.SetSetting("cloud_last_restore_time", NowLocalTimeString());
        }
    }

    MainWindow window(&db);
    if (!window.Create(L"NoteSoFast", WS_OVERLAPPEDWINDOW)) {
        return 0;
    }

    window.SetDatabasePath(dbPathW);

    ShowWindow(window.Window(), nCmdShow);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
