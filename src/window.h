#pragma once
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <memory>
#include <vector>
#include "database.h"
#include "note.h"
#include "spell_checker.h"

class MainWindow {
public:
    MainWindow(Database* db);
    ~MainWindow();

    BOOL Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle = 0, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int nWidth = CW_USEDEFAULT, int nHeight = CW_USEDEFAULT, HWND hWndParent = 0, HMENU hMenu = 0);
    HWND Window() const { return m_hwnd; }
    
    // Public for window procedure callbacks
    void NavigateSearchHistory(int offset);

protected:
    virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    void OnCreate();
    void OnSize(int width, int height);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void RegisterHotkeys();
    void UnregisterHotkeys();

    void LoadNotesList(const std::wstring& filter = L"", bool titleOnly = false, bool autoSelectFirst = true, int selectNoteId = -1);
    void LoadNoteContent(int index);
    void SaveCurrentNote(int preferredSelectNoteId = -1, bool autoSelectAfterSave = true);
    void CreateNewNote();
    void DeleteCurrentNote();
    void ExportCurrentNote();
    void PrintCurrentNote();
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
    void ToggleSearchMode();
    void OnTimer(UINT_PTR timerId);
    void ScheduleSpellCheck();
    void RunSpellCheck();
    bool PromptToSaveIfDirty(int preferredSelectNoteId = -1, bool autoSelectAfterSave = true);
    void RecordHistory(int noteIndex);
    void NavigateHistory(int offset);
    void UpdateHistoryButtons();
    int FindListIndexByNoteId(int noteId);
    void UpdateWindowTitle();
    void SaveSearchHistory();

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

    int m_splitPos = 250;
    bool m_isDraggingSplitter = false;
    static const int SPLITTER_WIDTH = 5;
    
    HFONT m_hFont = NULL;
    bool m_hotkeysRegistered = false;
    bool m_searchTitleOnly = false;
    std::wstring m_currentSearchFilter = L"";
    std::vector<int> m_history;
    int m_historyPos = -1;
    bool m_navigatingHistory = false;
    bool m_isNewNote = false;
    bool m_spellCheckDeferred = false;   // Selection active; rerun once selection clears
    
    // Search history
    std::vector<std::string> m_searchHistory;
    int m_searchHistoryPos = -1;
    std::string m_lastSearchTerm;
    DWORD m_lastSearchChangeTime = 0;

    // Spell checking
    std::unique_ptr<SpellChecker> m_spellChecker;
    struct WordAction {
        LONG start;
        std::wstring text;
    };
    std::vector<SpellChecker::Range> m_lastMisses;
    std::wstring m_lastCheckedText;  // Store text that was analyzed for spell check
    std::vector<WordAction> m_wordUndoStack;
    std::vector<WordAction> m_wordRedoStack;
    std::wstring m_currentWord;
    LONG m_currentWordStart = -1;
    static LRESULT CALLBACK RichEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);
    void DrawSpellUnderlines(HDC hdc) const;
    POINT GetCharPosition(int index) const;
    void FinalizeCurrentWord();
    bool PerformWordUndo();
    bool PerformWordRedo();
    void ResetWordUndoState();
};
