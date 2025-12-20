#include <windows.h>
#include <string>
#include "window.h"
#include "database.h"
#include "utils.h"

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
        exeDir + L"\\notes.db",
        exeDir + L"\\..\\notes.db"
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
