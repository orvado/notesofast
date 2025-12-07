#include <windows.h>
#include "window.h"
#include "database.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize Database
    Database db;
    if (!db.Initialize("notes.db")) {
        MessageBox(NULL, L"Failed to initialize database.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    MainWindow window(&db);
    if (!window.Create(L"NoteSoFast", WS_OVERLAPPEDWINDOW)) {
        return 0;
    }

    ShowWindow(window.Window(), nCmdShow);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
