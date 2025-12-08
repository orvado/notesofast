#include "window.h"
#include "utils.h"
#include <string>
#include <algorithm>

#define ID_LISTVIEW 1
#define ID_RICHEDIT 2
#define ID_TOOLBAR 3
#define ID_STATUS 4
#define ID_SEARCH 5
#define ID_CHECKLIST_LIST 6
#define ID_CHECKLIST_EDIT 7
#define ID_ADD_ITEM 8
#define ID_REMOVE_ITEM 9
#define ID_MOVE_UP 10
#define ID_MOVE_DOWN 11

#define IDM_NEW 101
#define IDM_SAVE 102
#define IDM_DELETE 103
#define IDM_PIN 104
#define IDM_ARCHIVE 105
#define IDM_SHOW_ARCHIVED 106
#define IDM_SORT 107
#define IDM_TOGGLE_CHECKLIST 108
#define IDM_COLOR_BASE 200
#define IDM_SORT_MODIFIED 301
#define IDM_SORT_CREATED 302
#define IDM_SORT_TITLE 303

MainWindow::MainWindow(Database* db) : m_hwnd(NULL), m_hwndList(NULL), m_hwndEdit(NULL), m_hwndSearch(NULL), m_hwndToolbar(NULL), m_hwndStatus(NULL), m_db(db) {
    m_colors = m_db->GetColors();
}

MainWindow::~MainWindow() { }

BOOL MainWindow::Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"NoteSoFastWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    m_hwnd = CreateWindowEx(dwExStyle, L"NoteSoFastWindowClass", lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, GetModuleHandle(NULL), this);

    return (m_hwnd ? TRUE : FALSE);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow *pThis = NULL;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (MainWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
    } else {
        pThis = (MainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_COMMAND:
        OnCommand(wParam, lParam);
        return 0;
    case WM_NOTIFY:
        OnNotify(wParam, lParam);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
            EndPaint(m_hwnd, &ps);
        }
        return 0;
    default:
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    }
}

void MainWindow::OnCreate() {
    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Load Rich Edit Library
    LoadLibrary(L"Msftedit.dll");

    // Create Search Box
    m_hwndSearch = CreateWindow(L"EDIT", L"", 
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_SEARCH, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search...");

    // Create List View (Left Panel)
    m_hwndList = CreateWindow(WC_LISTVIEW, L"", 
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_LISTVIEW, GetModuleHandle(NULL), NULL);
    
    // Add a column to the list view so items are visible
    LVCOLUMN lvc;
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.iSubItem = 0;
    lvc.pszText = (LPWSTR)L"Title";
    lvc.cx = 200;
    lvc.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(m_hwndList, 0, &lvc);

    // Create Rich Edit (Right Panel)
    m_hwndEdit = CreateWindow(MSFTEDIT_CLASS, L"", 
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_RICHEDIT, GetModuleHandle(NULL), NULL);
    
    // Set font for Rich Edit
    HFONT hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    SendMessage(m_hwndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Create Toolbar (Top)
    m_hwndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT, 0, 0, 0, 0, m_hwnd, (HMENU)ID_TOOLBAR, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    
    TBBUTTON tbb[7];
    ZeroMemory(tbb, sizeof(tbb));
    
    tbb[0].iBitmap = I_IMAGENONE;
    tbb[0].idCommand = IDM_NEW;
    tbb[0].fsState = TBSTATE_ENABLED;
    tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[0].iString = (INT_PTR)L"New";

    tbb[1].iBitmap = I_IMAGENONE;
    tbb[1].idCommand = IDM_SAVE;
    tbb[1].fsState = TBSTATE_ENABLED;
    tbb[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[1].iString = (INT_PTR)L"Save";

    tbb[2].iBitmap = I_IMAGENONE;
    tbb[2].idCommand = IDM_DELETE;
    tbb[2].fsState = TBSTATE_ENABLED;
    tbb[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[2].iString = (INT_PTR)L"Delete";

    tbb[3].iBitmap = I_IMAGENONE;
    tbb[3].idCommand = IDM_PIN;
    tbb[3].fsState = TBSTATE_ENABLED;
    tbb[3].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[3].iString = (INT_PTR)L"Pin";

    tbb[4].iBitmap = I_IMAGENONE;
    tbb[4].idCommand = IDM_ARCHIVE;
    tbb[4].fsState = TBSTATE_ENABLED;
    tbb[4].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[4].iString = (INT_PTR)L"Archive";

    tbb[5].iBitmap = I_IMAGENONE;
    tbb[5].idCommand = IDM_SHOW_ARCHIVED;
    tbb[5].fsState = TBSTATE_ENABLED;
    tbb[5].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[5].iString = (INT_PTR)L"Show Archived";

    tbb[6].iBitmap = I_IMAGENONE;
    tbb[6].idCommand = IDM_TOGGLE_CHECKLIST;
    tbb[6].fsState = TBSTATE_ENABLED;
    tbb[6].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[6].iString = (INT_PTR)L"Checklist";

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 7, (LPARAM)&tbb);
    
    // Add Sort Button (Dropdown style would be better but let's use a button that opens a menu)
    TBBUTTON tbbSort;
    ZeroMemory(&tbbSort, sizeof(tbbSort));
    tbbSort.iBitmap = I_IMAGENONE;
    tbbSort.idCommand = IDM_SORT;
    tbbSort.fsState = TBSTATE_ENABLED;
    tbbSort.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbbSort.iString = (INT_PTR)L"Sort";
    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 1, (LPARAM)&tbbSort);

    // Create Checklist Controls (initially hidden)
    m_hwndChecklistList = CreateWindow(WC_LISTVIEW, L"", 
        WS_CHILD | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_CHECKLIST_LIST, GetModuleHandle(NULL), NULL);
    
    LVCOLUMN lvcChecklist;
    lvcChecklist.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvcChecklist.iSubItem = 0;
    lvcChecklist.pszText = (LPWSTR)L"Items";
    lvcChecklist.cx = 300;
    lvcChecklist.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(m_hwndChecklistList, 0, &lvcChecklist);

    m_hwndChecklistEdit = CreateWindow(L"EDIT", L"", 
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_CHECKLIST_EDIT, GetModuleHandle(NULL), NULL);

    m_hwndAddItem = CreateWindow(L"BUTTON", L"Add Item", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_ADD_ITEM, GetModuleHandle(NULL), NULL);

    m_hwndRemoveItem = CreateWindow(L"BUTTON", L"Remove", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_REMOVE_ITEM, GetModuleHandle(NULL), NULL);

    m_hwndMoveUp = CreateWindow(L"BUTTON", L"↑", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_MOVE_UP, GetModuleHandle(NULL), NULL);

    m_hwndMoveDown = CreateWindow(L"BUTTON", L"↓", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_MOVE_DOWN, GetModuleHandle(NULL), NULL);

    // Create Status Bar (Bottom)
    m_hwndStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, m_hwnd, (HMENU)ID_STATUS, GetModuleHandle(NULL), NULL);

    LoadNotesList();
}

void MainWindow::OnSize(int width, int height) {
    // Resize Status Bar
    SendMessage(m_hwndStatus, WM_SIZE, 0, 0);
    
    RECT rcStatus;
    GetWindowRect(m_hwndStatus, &rcStatus);
    int statusHeight = rcStatus.bottom - rcStatus.top;

    // Resize Toolbar
    SendMessage(m_hwndToolbar, TB_AUTOSIZE, 0, 0);
    RECT rcToolbar;
    GetWindowRect(m_hwndToolbar, &rcToolbar);
    int toolbarHeight = rcToolbar.bottom - rcToolbar.top;

    // Calculate remaining area
    int clientHeight = height - statusHeight - toolbarHeight;
    int listWidth = 250; // Fixed width for list for now
    int searchHeight = 25;

    // Resize Search Box
    MoveWindow(m_hwndSearch, 0, toolbarHeight, listWidth, searchHeight, TRUE);

    // Resize List View
    MoveWindow(m_hwndList, 0, toolbarHeight + searchHeight, listWidth, clientHeight - searchHeight, TRUE);

    // Resize Edit Control or Checklist Controls
    if (m_checklistMode) {
        int buttonWidth = 80;
        int buttonHeight = 25;
        int editHeight = 25;
        int checklistTop = toolbarHeight + editHeight + 5;
        int checklistHeight = clientHeight - editHeight - buttonHeight - 10;
        
        // Checklist edit box
        MoveWindow(m_hwndChecklistEdit, listWidth + 5, toolbarHeight, width - listWidth - buttonWidth * 4 - 25, editHeight, TRUE);
        
        // Buttons
        MoveWindow(m_hwndAddItem, width - buttonWidth * 4 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndRemoveItem, width - buttonWidth * 3 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndMoveUp, width - buttonWidth * 2 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndMoveDown, width - buttonWidth - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        
        // Checklist list
        MoveWindow(m_hwndChecklistList, listWidth, checklistTop, width - listWidth, checklistHeight, TRUE);
    } else {
        MoveWindow(m_hwndEdit, listWidth, toolbarHeight, width - listWidth, clientHeight, TRUE);
    }
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) {
    case IDM_NEW:
        CreateNewNote();
        break;
    case IDM_SAVE:
        SaveCurrentNote();
        break;
    case IDM_DELETE:
        DeleteCurrentNote();
        break;
    case IDM_PIN:
        TogglePinCurrentNote();
        break;
    case IDM_ARCHIVE:
        ToggleArchiveCurrentNote();
        break;
    case IDM_SHOW_ARCHIVED:
        ToggleShowArchived();
        break;
    case IDM_SORT:
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING | (m_sortBy == Database::SortBy::DateModified ? MF_CHECKED : 0), IDM_SORT_MODIFIED, L"Date Modified");
            AppendMenu(hMenu, MF_STRING | (m_sortBy == Database::SortBy::DateCreated ? MF_CHECKED : 0), IDM_SORT_CREATED, L"Date Created");
            AppendMenu(hMenu, MF_STRING | (m_sortBy == Database::SortBy::Title ? MF_CHECKED : 0), IDM_SORT_TITLE, L"Title");
            
            RECT rc;
            SendMessage(m_hwndToolbar, TB_GETRECT, IDM_SORT, (LPARAM)&rc);
            MapWindowPoints(m_hwndToolbar, HWND_DESKTOP, (LPPOINT)&rc, 2);
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case IDM_TOGGLE_CHECKLIST:
        ToggleChecklistMode();
        break;
    case ID_ADD_ITEM:
        AddChecklistItem();
        break;
    case ID_REMOVE_ITEM:
        RemoveChecklistItem();
        break;
    case ID_MOVE_UP:
        MoveChecklistItemUp();
        break;
    case ID_MOVE_DOWN:
        MoveChecklistItemDown();
        break;
    case IDM_COLOR_BASE:
        // Handle color selection (will be implemented with context menu)
        break;
    case IDM_SORT_MODIFIED:
        SetSortOrder(Database::SortBy::DateModified);
        break;
    case IDM_SORT_CREATED:
        SetSortOrder(Database::SortBy::DateCreated);
        break;
    case IDM_SORT_TITLE:
        SetSortOrder(Database::SortBy::Title);
        break;
    }
        break;
    case IDM_SORT_MODIFIED:
        SetSortOrder(Database::SortBy::DateModified);
        break;
    case IDM_SORT_CREATED:
        SetSortOrder(Database::SortBy::DateCreated);
        break;
    case IDM_SORT_TITLE:
        SetSortOrder(Database::SortBy::Title);
        break;
    case ID_RICHEDIT:
        if (HIWORD(wParam) == EN_CHANGE) {
            m_isDirty = true;
        }
        break;
    case ID_SEARCH:
        if (HIWORD(wParam) == EN_CHANGE) {
            int len = GetWindowTextLength(m_hwndSearch);
            std::vector<wchar_t> buf(len + 1);
            GetWindowText(m_hwndSearch, &buf[0], len + 1);
            LoadNotesList(&buf[0]);
        }
        break;
    }
    
    // Handle Color Commands
    if (LOWORD(wParam) >= IDM_COLOR_BASE && LOWORD(wParam) < IDM_COLOR_BASE + 100) {
        SetCurrentNoteColor(LOWORD(wParam) - IDM_COLOR_BASE);
    }
}

void MainWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    LPNMHDR pnmh = (LPNMHDR)lParam;
    if (pnmh->idFrom == ID_CHECKLIST_LIST) {
        if (pnmh->code == NM_DBLCLK) {
            int selected = ListView_GetNextItem(m_hwndChecklistList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                ToggleChecklistItemCheck(selected);
            }
        }
    } else if (pnmh->idFrom == ID_LISTVIEW) {
        if (pnmh->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                SaveCurrentNote(); // Auto-save previous note
                LoadNoteContent(pnmv->iItem);
            }
        } else if (pnmh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
            switch (lplvcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                SetWindowLongPtr(m_hwnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return;
            case CDDS_ITEMPREPAINT:
                {
                    int index = (int)lplvcd->nmcd.dwItemSpec;
                    if (index >= 0 && index < (int)m_filteredIndices.size()) {
                        int realIndex = m_filteredIndices[index];
                        const Note& note = m_notes[realIndex];
                        
                        // Set background color based on note color
                        for (const auto& color : m_colors) {
                            if (color.id == note.color_id) {
                                if (color.hex_color != "#FFFFFF") {
                                    // Convert hex to COLORREF
                                    int r, g, b;
                                    sscanf_s(color.hex_color.c_str(), "#%02x%02x%02x", &r, &g, &b);
                                    lplvcd->clrTextBk = RGB(r, g, b);
                                }
                                break;
                            }
                        }
                        
                        // Bold font for pinned notes? Or just different text color?
                        // Let's just change text color for pinned notes for now
                        if (note.is_pinned) {
                            lplvcd->clrText = RGB(0, 0, 255); // Blue text for pinned
                        }
                    }
                    SetWindowLongPtr(m_hwnd, DWLP_MSGRESULT, CDRF_NEWFONT);
                }
                return;
            }
        }
        // Handle Right Click for Context Menu
        else if (pnmh->code == NM_RCLICK) {
             LPNMITEMACTIVATE pnmitem = (LPNMITEMACTIVATE)lParam;
             if (pnmitem->iItem != -1) {
                 HMENU hMenu = CreatePopupMenu();
                 
                 // Add Color Submenu
                 HMENU hColorMenu = CreatePopupMenu();
                 for (const auto& color : m_colors) {
                     std::wstring wName = Utils::Utf8ToWide(color.name);
                     AppendMenu(hColorMenu, MF_STRING, IDM_COLOR_BASE + color.id, wName.c_str());
                 }
                 AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hColorMenu, L"Color");
                 
                 POINT pt;
                 GetCursorPos(&pt);
                 TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, NULL);
                 DestroyMenu(hMenu);
             }
        }
    }
}

void MainWindow::LoadNotesList(const std::wstring& filter) {
    ListView_DeleteAllItems(m_hwndList);
    m_notes = m_db->GetAllNotes(m_showArchived, m_sortBy);
    m_filteredIndices.clear();
    
    LVITEM lvi;
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    
    int listIndex = 0;
    for (size_t i = 0; i < m_notes.size(); ++i) {
        std::wstring wTitle = Utils::Utf8ToWide(m_notes[i].title);
        
        // Add visual indicator for pinned/archived in title
        if (m_notes[i].is_pinned) wTitle = L"[Pin] " + wTitle;
        if (m_notes[i].is_archived) wTitle = L"[Arch] " + wTitle;

        std::wstring wContent = Utils::Utf8ToWide(m_notes[i].content);
        
        bool match = true;
        if (!filter.empty()) {
            // Simple case-insensitive search
            std::wstring wTitleLower = wTitle;
            std::wstring wContentLower = wContent;
            std::wstring filterLower = filter;
            
            for (auto& c : wTitleLower) c = towlower(c);
            for (auto& c : wContentLower) c = towlower(c);
            for (auto& c : filterLower) c = towlower(c);
            
            if (wTitleLower.find(filterLower) == std::wstring::npos && 
                wContentLower.find(filterLower) == std::wstring::npos) {
                match = false;
            }
        }
        
        if (match) {
            lvi.iItem = listIndex;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)wTitle.c_str();
            lvi.lParam = (LPARAM)i; // Store index into m_notes
            ListView_InsertItem(m_hwndList, &lvi);
            m_filteredIndices.push_back((int)i);
            listIndex++;
        }
    }
}

void MainWindow::LoadNoteContent(int listIndex) {
    if (listIndex >= 0 && listIndex < (int)m_filteredIndices.size()) {
        int realIndex = m_filteredIndices[listIndex];
        m_currentNoteIndex = realIndex;
        std::wstring wContent = Utils::Utf8ToWide(m_notes[realIndex].content);
        SetWindowText(m_hwndEdit, wContent.c_str());
        m_isDirty = false;
        
        // Update Toolbar State
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, m_notes[realIndex].is_pinned ? TRUE : FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, m_notes[realIndex].is_archived ? TRUE : FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, m_notes[realIndex].is_checklist ? TRUE : FALSE);
        
        // Set checklist mode and update UI
        m_checklistMode = m_notes[realIndex].is_checklist;
        
        // Load checklist items if needed
        if (m_checklistMode) {
            m_notes[realIndex].checklist_items = m_db->GetChecklistItems(m_notes[realIndex].id);
        }
        
        UpdateChecklistUI();
    } else {
        m_currentNoteIndex = -1;
        SetWindowText(m_hwndEdit, L"");
        m_isDirty = false;
        m_checklistMode = false;
        
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
        UpdateChecklistUI();
    }
}

void MainWindow::SaveCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size() && m_isDirty) {
        int len = GetWindowTextLength(m_hwndEdit);
        std::vector<wchar_t> buf(len + 1);
        GetWindowText(m_hwndEdit, &buf[0], len + 1);
        
        std::string content = Utils::WideToUtf8(&buf[0]);
        m_notes[m_currentNoteIndex].content = content;
        
        // Update title from first line
        size_t firstLineEnd = content.find('\n');
        std::string newTitle;
        if (firstLineEnd == std::string::npos) {
            newTitle = content;
        } else {
            newTitle = content.substr(0, firstLineEnd);
            // Trim carriage return if present
            if (!newTitle.empty() && newTitle.back() == '\r') {
                newTitle.pop_back();
            }
        }
        
        if (newTitle.empty()) {
            newTitle = "Untitled Note";
        }
        
        // Limit title length
        if (newTitle.length() > 50) {
            newTitle = newTitle.substr(0, 50) + "...";
        }
        
        m_notes[m_currentNoteIndex].title = newTitle;
        
        m_db->UpdateNote(m_notes[m_currentNoteIndex]);
        m_isDirty = false;
        SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Note saved");
        
        // Update list item text
        for(int i=0; i<(int)m_filteredIndices.size(); ++i) {
            if (m_filteredIndices[i] == m_currentNoteIndex) {
                std::wstring wTitle = Utils::Utf8ToWide(m_notes[m_currentNoteIndex].title);
                ListView_SetItemText(m_hwndList, i, 0, (LPWSTR)wTitle.c_str());
                break;
            }
        }
    }
}

void MainWindow::CreateNewNote() {
    SaveCurrentNote();
    
    Note newNote;
    newNote.title = "New Note";
    newNote.content = "";
    
    if (m_db->CreateNote(newNote)) {
        SetWindowText(m_hwndSearch, L""); // Clear search to show new note
        LoadNotesList();
        
        // Find the new note in the list
        for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
            int realIndex = m_filteredIndices[i];
            if (m_notes[realIndex].id == newNote.id) {
                ListView_SetItemState(m_hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                LoadNoteContent(i);
                break;
            }
        }
        SetFocus(m_hwndEdit);
    }
}

void MainWindow::DeleteCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        if (MessageBox(m_hwnd, L"Are you sure you want to delete this note?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            m_db->DeleteNote(m_notes[m_currentNoteIndex].id);
            LoadNotesList(); // This will clear selection
            m_currentNoteIndex = -1;
            SetWindowText(m_hwndEdit, L"");
        }
    }
}

void MainWindow::TogglePinCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        bool newPinState = !m_notes[m_currentNoteIndex].is_pinned;
        if (m_db->TogglePin(m_notes[m_currentNoteIndex].id, newPinState)) {
            m_notes[m_currentNoteIndex].is_pinned = newPinState;
            LoadNotesList(); // Reload to update sort order
            
            // Restore selection
            for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
                if (m_notes[m_filteredIndices[i]].id == m_notes[m_currentNoteIndex].id) {
                    ListView_SetItemState(m_hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    break;
                }
            }
        }
    }
}

void MainWindow::ToggleArchiveCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        bool newArchiveState = !m_notes[m_currentNoteIndex].is_archived;
        if (m_db->ToggleArchive(m_notes[m_currentNoteIndex].id, newArchiveState)) {
            m_notes[m_currentNoteIndex].is_archived = newArchiveState;
            LoadNotesList(); // Reload to update list (might disappear if not showing archived)
            
            if (m_showArchived) {
                 // Restore selection
                for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
                    if (m_notes[m_filteredIndices[i]].id == m_notes[m_currentNoteIndex].id) {
                        ListView_SetItemState(m_hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        break;
                    }
                }
            } else {
                // Select first item or clear
                if (!m_filteredIndices.empty()) {
                    ListView_SetItemState(m_hwndList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    LoadNoteContent(0);
                } else {
                    LoadNoteContent(-1);
                }
            }
        }
    }
}

void MainWindow::SetCurrentNoteColor(int colorId) {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        if (m_db->UpdateNoteColor(m_notes[m_currentNoteIndex].id, colorId)) {
            m_notes[m_currentNoteIndex].color_id = colorId;
            // Redraw list item
             for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
                if (m_notes[m_filteredIndices[i]].id == m_notes[m_currentNoteIndex].id) {
                    ListView_RedrawItems(m_hwndList, i, i);
                    break;
                }
            }
        }
    }
}

void MainWindow::ToggleShowArchived() {
    m_showArchived = !m_showArchived;
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_SHOW_ARCHIVED, m_showArchived ? TRUE : FALSE);
    LoadNotesList();
}

void MainWindow::SetSortOrder(Database::SortBy sort) {
    m_sortBy = sort;
    LoadNotesList();
}

void MainWindow::ToggleChecklistMode() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        m_checklistMode = !m_checklistMode;
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, m_checklistMode ? TRUE : FALSE);
        
        if (m_db->ToggleNoteType(m_notes[m_currentNoteIndex].id, m_checklistMode)) {
            m_notes[m_currentNoteIndex].is_checklist = m_checklistMode;
            UpdateChecklistUI();
        }
    }
}

void MainWindow::UpdateChecklistUI() {
    if (m_checklistMode && m_currentNoteIndex >= 0) {
        ShowWindow(m_hwndEdit, SW_HIDE);
        ShowWindow(m_hwndChecklistList, SW_SHOW);
        ShowWindow(m_hwndChecklistEdit, SW_SHOW);
        ShowWindow(m_hwndAddItem, SW_SHOW);
        ShowWindow(m_hwndRemoveItem, SW_SHOW);
        ShowWindow(m_hwndMoveUp, SW_SHOW);
        ShowWindow(m_hwndMoveDown, SW_SHOW);
        
        // Load checklist items
        ListView_DeleteAllItems(m_hwndChecklistList);
        int checkedCount = 0;
        for (const auto& item : m_notes[m_currentNoteIndex].checklist_items) {
            if (item.is_checked) checkedCount++;
            
            LVITEM lvi;
            lvi.mask = LVIF_TEXT | LVIF_PARAM;
            lvi.iItem = ListView_GetItemCount(m_hwndChecklistList);
            lvi.iSubItem = 0;
            lvi.lParam = item.id;
            
            std::wstring displayText = item.is_checked ? L"☑ " : L"☐ ";
            displayText += std::wstring(item.item_text.begin(), item.item_text.end());
            lvi.pszText = (LPWSTR)displayText.c_str();
            
            ListView_InsertItem(m_hwndChecklistList, &lvi);
        }
        
        // Update status bar with progress
        int totalItems = (int)m_notes[m_currentNoteIndex].checklist_items.size();
        std::wstring statusText = L"Notes: " + std::to_wstring(m_notes.size()) + L" | ";
        if (totalItems > 0) {
            statusText += L"Progress: " + std::to_wstring(checkedCount) + L"/" + std::to_wstring(totalItems);
        } else {
            statusText += L"No items";
        }
        SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)statusText.c_str());
    } else {
        ShowWindow(m_hwndEdit, SW_SHOW);
        ShowWindow(m_hwndChecklistList, SW_HIDE);
        ShowWindow(m_hwndChecklistEdit, SW_HIDE);
        ShowWindow(m_hwndAddItem, SW_HIDE);
        ShowWindow(m_hwndRemoveItem, SW_HIDE);
        ShowWindow(m_hwndMoveUp, SW_HIDE);
        ShowWindow(m_hwndMoveDown, SW_HIDE);
        
        // Reset status bar
        std::wstring statusText = L"Notes: " + std::to_wstring(m_notes.size());
        SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)statusText.c_str());
    }
}

void MainWindow::AddChecklistItem() {
    if (m_currentNoteIndex >= 0) {
        wchar_t buffer[256];
        GetWindowText(m_hwndChecklistEdit, buffer, 256);
        std::string itemText(buffer, buffer + wcslen(buffer));
        
        if (!itemText.empty()) {
            ChecklistItem newItem;
            newItem.note_id = m_notes[m_currentNoteIndex].id;
            newItem.item_text = itemText;
            newItem.item_order = (int)m_notes[m_currentNoteIndex].checklist_items.size();
            
            if (m_db->CreateChecklistItem(newItem)) {
                m_notes[m_currentNoteIndex].checklist_items.push_back(newItem);
                UpdateChecklistUI();
                SetWindowText(m_hwndChecklistEdit, L"");
            }
        }
    }
}

void MainWindow::RemoveChecklistItem() {
    if (m_currentNoteIndex >= 0) {
        int selected = ListView_GetNextItem(m_hwndChecklistList, -1, LVNI_SELECTED);
        if (selected >= 0) {
            LVITEM lvi;
            lvi.mask = LVIF_PARAM;
            lvi.iItem = selected;
            lvi.iSubItem = 0;
            ListView_GetItem(m_hwndChecklistList, &lvi);
            
            int itemId = (int)lvi.lParam;
            if (m_db->DeleteChecklistItem(itemId)) {
                auto& items = m_notes[m_currentNoteIndex].checklist_items;
                items.erase(std::remove_if(items.begin(), items.end(),
                    [itemId](const ChecklistItem& item) { return item.id == itemId; }), items.end());
                
                // Reorder remaining items
                for (size_t i = 0; i < items.size(); ++i) {
                    items[i].item_order = (int)i;
                    m_db->UpdateChecklistItem(items[i]);
                }
                
                UpdateChecklistUI();
            }
        }
    }
}

void MainWindow::MoveChecklistItemUp() {
    if (m_currentNoteIndex >= 0) {
        int selected = ListView_GetNextItem(m_hwndChecklistList, -1, LVNI_SELECTED);
        if (selected > 0) {
            auto& items = m_notes[m_currentNoteIndex].checklist_items;
            std::swap(items[selected], items[selected - 1]);
            items[selected].item_order = selected;
            items[selected - 1].item_order = selected - 1;
            
            m_db->UpdateChecklistItem(items[selected]);
            m_db->UpdateChecklistItem(items[selected - 1]);
            
            UpdateChecklistUI();
            ListView_SetItemState(m_hwndChecklistList, selected - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

void MainWindow::MoveChecklistItemDown() {
    if (m_currentNoteIndex >= 0) {
        int selected = ListView_GetNextItem(m_hwndChecklistList, -1, LVNI_SELECTED);
        auto& items = m_notes[m_currentNoteIndex].checklist_items;
        if (selected >= 0 && selected < (int)items.size() - 1) {
            std::swap(items[selected], items[selected + 1]);
            items[selected].item_order = selected;
            items[selected + 1].item_order = selected + 1;
            
            m_db->UpdateChecklistItem(items[selected]);
            m_db->UpdateChecklistItem(items[selected + 1]);
            
            UpdateChecklistUI();
            ListView_SetItemState(m_hwndChecklistList, selected + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

void MainWindow::ToggleChecklistItemCheck(int index) {
    if (m_currentNoteIndex >= 0 && index >= 0 && index < (int)m_notes[m_currentNoteIndex].checklist_items.size()) {
        auto& item = m_notes[m_currentNoteIndex].checklist_items[index];
        item.is_checked = !item.is_checked;
        
        if (m_db->ToggleChecklistItem(item.id, item.is_checked)) {
            UpdateChecklistUI();
            ListView_SetItemState(m_hwndChecklistList, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}
