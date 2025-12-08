#pragma once
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <vector>
#include "database.h"
#include "note.h"

class MainWindow {
public:
    MainWindow(Database* db);
    ~MainWindow();

    BOOL Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle = 0, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int nWidth = CW_USEDEFAULT, int nHeight = CW_USEDEFAULT, HWND hWndParent = 0, HMENU hMenu = 0);
    HWND Window() const { return m_hwnd; }

protected:
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    void OnCreate();
    void OnSize(int width, int height);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    void OnNotify(WPARAM wParam, LPARAM lParam);

    void LoadNotesList(const std::wstring& filter = L"");
    void LoadNoteContent(int index);
    void SaveCurrentNote();
    void CreateNewNote();
    void DeleteCurrentNote();
    void ExportCurrentNote();
    void TogglePinCurrentNote();
    void ToggleArchiveCurrentNote();
    void SetCurrentNoteColor(int colorId);
    void ToggleShowArchived();
    void SetSortOrder(Database::SortBy sort);
    void ToggleChecklistMode();
    void UpdateChecklistUI();
    void AddChecklistItem();
    void RemoveChecklistItem();
    void MoveChecklistItemUp();
    void MoveChecklistItemDown();
    void ToggleChecklistItemCheck(int index);
    void ToggleFormat(DWORD mask, DWORD effect);
    void UpdateFormatButtons();

    HWND m_hwnd;
    HWND m_hwndList;
    HWND m_hwndEdit;
    HWND m_hwndSearch;
    HWND m_hwndToolbar;
    HWND m_hwndStatus;
    HWND m_hwndChecklistList;
    HWND m_hwndChecklistEdit;
    HWND m_hwndAddItem;
    HWND m_hwndRemoveItem;
    HWND m_hwndMoveUp;
    HWND m_hwndMoveDown;

    Database* m_db;
    std::vector<Note> m_notes;
    std::vector<int> m_filteredIndices; // Indices into m_notes
    std::vector<Database::Color> m_colors;
    int m_currentNoteIndex = -1;
    bool m_isDirty = false;
    bool m_showArchived = false;
    Database::SortBy m_sortBy = Database::SortBy::DateModified;
    bool m_checklistMode = false;
};
