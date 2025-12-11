#include "window.h"
#include "utils.h"
#include <string>
#include <algorithm>

#define ID_LISTVIEW 1
#define ID_RICHEDIT 2
#define ID_TOOLBAR 3
#define ID_STATUS 4
#define ID_SEARCH 5
#define ID_SEARCH_MODE_TOGGLE 12
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
#define IDM_FORMAT_BOLD 401
#define IDM_FORMAT_ITALIC 402
#define IDM_FORMAT_UNDERLINE 403
#define IDM_EXPORT_TXT 501
#define IDM_HIST_BACK 601
#define IDM_HIST_FORWARD 602
#define IDM_SEARCH_MODE_TOGGLE 502

WNDPROC g_oldEditProc = NULL;

LRESULT CALLBACK ChecklistEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND hParent = GetParent(hwnd);
        SendMessage(hParent, WM_COMMAND, MAKEWPARAM(ID_ADD_ITEM, BN_CLICKED), (LPARAM)hwnd);
        return 0;
    }
    if (uMsg == WM_CHAR && wParam == VK_RETURN) {
        return 0; // Prevent beep
    }
    return CallWindowProc(g_oldEditProc, hwnd, uMsg, wParam, lParam);
}

MainWindow::MainWindow(Database* db) : m_hwnd(NULL), m_hwndList(NULL), m_hwndEdit(NULL), m_hwndSearch(NULL), m_hwndToolbar(NULL), m_hwndStatus(NULL), m_db(db) {
    m_colors = m_db->GetColors();
}

MainWindow::~MainWindow() {
    if (m_hFont) DeleteObject(m_hFont);
}

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
        return OnNotify(wParam, lParam);
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            SaveCurrentNote();
            UnregisterHotkeys();
        } else {
            RegisterHotkeys();
        }
        return 0;
    case WM_CLOSE:
        SaveCurrentNote();
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        SaveCurrentNote();
        UnregisterHotkeys();
        PostQuitMessage(0);
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (pt.x >= m_splitPos && pt.x < m_splitPos + SPLITTER_WIDTH) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
            EndPaint(m_hwnd, &ps);
        }
        return 0;
    case WM_HOTKEY:
        switch (wParam) {
        case 1: // Ctrl+N
            CreateNewNote();
            break;
        case 2: // Ctrl+S
            SaveCurrentNote();
            break;
        case 3: // Ctrl+D
            DeleteCurrentNote();
            break;
        case 4: // Ctrl+P
            TogglePinCurrentNote();
            break;
        case 5: // Ctrl+F
            SetFocus(m_hwndSearch);
            break;
        case 6: // Ctrl+B
            ToggleFormat(CFM_BOLD, CFE_BOLD);
            break;
        case 7: // Ctrl+I
            ToggleFormat(CFM_ITALIC, CFE_ITALIC);
            break;
        case 8: // Ctrl+U
            ToggleFormat(CFM_UNDERLINE, CFE_UNDERLINE);
            break;
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
    m_hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    SendMessage(m_hwndEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Enable Auto-URL detection
    SendMessage(m_hwndEdit, EM_AUTOURLDETECT, TRUE, 0);
    SendMessage(m_hwndEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_LINK);

    // Create Toolbar (Top)
    m_hwndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | CCS_NODIVIDER, 0, 0, 0, 0, m_hwnd, (HMENU)ID_TOOLBAR, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    
    // Load Standard Images
    SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_VIEW_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_HIST_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);

    TBBUTTON tbb[11];
    ZeroMemory(tbb, sizeof(tbb));

    tbb[0].iBitmap = 15 + 12 + HIST_BACK;  // Offset for IDB_STD (15) + IDB_VIEW (12) + HIST index
    tbb[0].idCommand = IDM_HIST_BACK;
    tbb[0].fsState = TBSTATE_ENABLED;
    tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[0].iString = 0;

    tbb[1].iBitmap = 15 + 12 + HIST_FORWARD;  // Offset for IDB_STD (15) + IDB_VIEW (12) + HIST index
    tbb[1].idCommand = IDM_HIST_FORWARD;
    tbb[1].fsState = TBSTATE_ENABLED;
    tbb[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[1].iString = 0;

    tbb[2].iBitmap = STD_FILENEW;
    tbb[2].idCommand = IDM_NEW;
    tbb[2].fsState = TBSTATE_ENABLED;
    tbb[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[2].iString = 0;

    tbb[3].iBitmap = STD_FILESAVE;
    tbb[3].idCommand = IDM_SAVE;
    tbb[3].fsState = TBSTATE_ENABLED;
    tbb[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[3].iString = 0;

    tbb[4].iBitmap = STD_DELETE;
    tbb[4].idCommand = IDM_DELETE;
    tbb[4].fsState = TBSTATE_ENABLED;
    tbb[4].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[4].iString = 0;

    tbb[5].iBitmap = 15 + 6; // VIEW_SORTDATE
    tbb[5].idCommand = IDM_SORT;
    tbb[5].fsState = TBSTATE_ENABLED;
    tbb[5].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[5].iString = 0;

    tbb[6].iBitmap = I_IMAGENONE;
    tbb[6].idCommand = IDM_SEARCH_MODE_TOGGLE;
    tbb[6].fsState = TBSTATE_ENABLED;
    tbb[6].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[6].iString = (INT_PTR)L"T+C";

    tbb[7].iBitmap = 15 + 12 + HIST_FAVORITES;  // Offset for IDB_STD (15) + IDB_VIEW (12) + HIST index
    tbb[7].idCommand = IDM_PIN;
    tbb[7].fsState = TBSTATE_ENABLED;
    tbb[7].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[7].iString = 0;

    tbb[8].iBitmap = STD_FILEOPEN; // Using File Open (folder) for Archive
    tbb[8].idCommand = IDM_ARCHIVE;
    tbb[8].fsState = TBSTATE_ENABLED;
    tbb[8].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[8].iString = 0;

    tbb[9].iBitmap = 15 + 8; // VIEW_PARENTFOLDER (15 is offset for second image list)
    tbb[9].idCommand = IDM_SHOW_ARCHIVED;
    tbb[9].fsState = TBSTATE_ENABLED;
    tbb[9].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[9].iString = 0;

    tbb[10].iBitmap = 15 + 2; // VIEW_LIST
    tbb[10].idCommand = IDM_TOGGLE_CHECKLIST;
    tbb[10].fsState = TBSTATE_ENABLED;
    tbb[10].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[10].iString = 0;

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 11, (LPARAM)&tbb);
    
    // Add Separator
    TBBUTTON tbbSep;
    ZeroMemory(&tbbSep, sizeof(tbbSep));
    tbbSep.fsStyle = BTNS_SEP;
    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 1, (LPARAM)&tbbSep);

    // Add Formatting Buttons
    TBBUTTON tbbFormat[3];
    ZeroMemory(tbbFormat, sizeof(tbbFormat));

    tbbFormat[0].iBitmap = I_IMAGENONE;
    tbbFormat[0].idCommand = IDM_FORMAT_BOLD;
    tbbFormat[0].fsState = TBSTATE_ENABLED;
    tbbFormat[0].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbbFormat[0].iString = (INT_PTR)L"B";

    tbbFormat[1].iBitmap = I_IMAGENONE;
    tbbFormat[1].idCommand = IDM_FORMAT_ITALIC;
    tbbFormat[1].fsState = TBSTATE_ENABLED;
    tbbFormat[1].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbbFormat[1].iString = (INT_PTR)L"I";

    tbbFormat[2].iBitmap = I_IMAGENONE;
    tbbFormat[2].idCommand = IDM_FORMAT_UNDERLINE;
    tbbFormat[2].fsState = TBSTATE_ENABLED;
    tbbFormat[2].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbbFormat[2].iString = (INT_PTR)L"U";

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 3, (LPARAM)&tbbFormat);

    // Create Checklist Controls (initially hidden)
    m_hwndChecklistList = CreateWindow(WC_LISTVIEW, L"", 
        WS_CHILD | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_CHECKLIST_LIST, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndChecklistList, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    
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
    SendMessage(m_hwndChecklistEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    g_oldEditProc = (WNDPROC)SetWindowLongPtr(m_hwndChecklistEdit, GWLP_WNDPROC, (LONG_PTR)ChecklistEditProc);

    m_hwndAddItem = CreateWindow(L"BUTTON", L"Add Item", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_ADD_ITEM, GetModuleHandle(NULL), NULL);

    m_hwndRemoveItem = CreateWindow(L"BUTTON", L"Remove", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_REMOVE_ITEM, GetModuleHandle(NULL), NULL);

    m_hwndMoveUp = CreateWindow(L"BUTTON", L"Up", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_MOVE_UP, GetModuleHandle(NULL), NULL);

    m_hwndMoveDown = CreateWindow(L"BUTTON", L"Down", 
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
    int listWidth = m_splitPos;
    int searchHeight = 25;

    // Resize Search Box
    MoveWindow(m_hwndSearch, 0, toolbarHeight, listWidth, searchHeight, TRUE);

    // Resize List View
    MoveWindow(m_hwndList, 0, toolbarHeight + searchHeight, listWidth, clientHeight - searchHeight, TRUE);

    // Resize Edit Control or Checklist Controls
    int rightPaneX = listWidth + SPLITTER_WIDTH;
    int rightPaneWidth = width - rightPaneX;
    if (rightPaneWidth < 0) rightPaneWidth = 0;

    if (m_checklistMode) {
        int buttonWidth = 80;
        int buttonHeight = 25;
        int editHeight = 25;
        int checklistTop = toolbarHeight + editHeight + 5;
        int checklistHeight = clientHeight - editHeight - buttonHeight - 10;
        
        // Checklist edit box
        MoveWindow(m_hwndChecklistEdit, rightPaneX + 5, toolbarHeight, rightPaneWidth - buttonWidth * 4 - 25, editHeight, TRUE);
        
        // Buttons
        MoveWindow(m_hwndAddItem, width - buttonWidth * 4 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndRemoveItem, width - buttonWidth * 3 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndMoveUp, width - buttonWidth * 2 - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_hwndMoveDown, width - buttonWidth - 10, toolbarHeight, buttonWidth, buttonHeight, TRUE);
        
        // Checklist list
        MoveWindow(m_hwndChecklistList, rightPaneX, checklistTop, rightPaneWidth, checklistHeight, TRUE);
    } else {
        MoveWindow(m_hwndEdit, rightPaneX, toolbarHeight, rightPaneWidth, clientHeight, TRUE);
    }
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) {
    case IDM_NEW:
        CreateNewNote();
        break;
    case IDM_SAVE:
        ExportCurrentNote();
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
    case IDM_FORMAT_BOLD:
        ToggleFormat(CFM_BOLD, CFE_BOLD);
        break;
    case IDM_FORMAT_ITALIC:
        ToggleFormat(CFM_ITALIC, CFE_ITALIC);
        break;
    case IDM_FORMAT_UNDERLINE:
        ToggleFormat(CFM_UNDERLINE, CFE_UNDERLINE);
        break;
    case IDM_SEARCH_MODE_TOGGLE:
        ToggleSearchMode();
        break;
    case IDM_HIST_BACK:
        NavigateHistory(-1);
        break;
    case IDM_HIST_FORWARD:
        NavigateHistory(1);
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
            bool autoSelect = !m_isNewNote; // avoid pulling focus to list while composing a new note
            LoadNotesList(&buf[0], m_searchTitleOnly, autoSelect);
        }
        break;
    }
    
    // Handle Color Commands
    if (LOWORD(wParam) >= IDM_COLOR_BASE && LOWORD(wParam) < IDM_COLOR_BASE + 100) {
        SetCurrentNoteColor(LOWORD(wParam) - IDM_COLOR_BASE);
    }
}

LRESULT MainWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    LPNMHDR pnmh = (LPNMHDR)lParam;
    
    if (pnmh->code == TTN_GETDISPINFOW) {
        LPNMTTDISPINFOW pInfo = (LPNMTTDISPINFOW)lParam;
        pInfo->hinst = NULL;
        switch (pInfo->hdr.idFrom) {
            case IDM_NEW: wcscpy_s(pInfo->szText, L"New Note (Ctrl+N)"); break;
            case IDM_SAVE: wcscpy_s(pInfo->szText, L"Export to Text (Ctrl+S)"); break;
            case IDM_DELETE: wcscpy_s(pInfo->szText, L"Delete (Ctrl+D)"); break;
            case IDM_PIN: wcscpy_s(pInfo->szText, L"Pin Note (Ctrl+P)"); break;
            case IDM_ARCHIVE: wcscpy_s(pInfo->szText, L"Archive Note"); break;
            case IDM_SHOW_ARCHIVED: wcscpy_s(pInfo->szText, L"Show Archived Notes"); break;
            case IDM_TOGGLE_CHECKLIST: wcscpy_s(pInfo->szText, L"Toggle Checklist"); break;
            case IDM_FORMAT_BOLD: wcscpy_s(pInfo->szText, L"Bold (Ctrl+B)"); break;
            case IDM_FORMAT_ITALIC: wcscpy_s(pInfo->szText, L"Italic (Ctrl+I)"); break;
            case IDM_FORMAT_UNDERLINE: wcscpy_s(pInfo->szText, L"Underline (Ctrl+U)"); break;
            case IDM_SORT: wcscpy_s(pInfo->szText, L"Sort Notes"); break;
            case IDM_HIST_BACK: wcscpy_s(pInfo->szText, L"Back in history"); break;
            case IDM_HIST_FORWARD: wcscpy_s(pInfo->szText, L"Forward in history"); break;
            case IDM_SEARCH_MODE_TOGGLE: wcscpy_s(pInfo->szText, L"Search Title and Content"); break;
        }
        return 0;
    }

    if (pnmh->idFrom == ID_CHECKLIST_LIST) {
        if (pnmh->code == NM_DBLCLK) {
            int selected = ListView_GetNextItem(m_hwndChecklistList, -1, LVNI_SELECTED);
            if (selected >= 0) {
                ToggleChecklistItemCheck(selected);
            }
        } else if (pnmh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
            switch (lplvcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                {
                    int index = (int)lplvcd->nmcd.dwItemSpec;
                    if (m_currentNoteIndex >= 0 && index >= 0 && index < (int)m_notes[m_currentNoteIndex].checklist_items.size()) {
                        const ChecklistItem& it = m_notes[m_currentNoteIndex].checklist_items[index];
                        // Checked items render gray, unchecked items render standard black
                        if (it.is_checked) {
                            lplvcd->clrText = RGB(128, 128, 128);
                        } else {
                            lplvcd->clrText = RGB(0, 0, 0);
                        }
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
    } else if (pnmh->idFrom == ID_LISTVIEW) {
        if (pnmh->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                int targetNoteId = -1;
                if (pnmv->iItem >= 0 && pnmv->iItem < (int)m_filteredIndices.size()) {
                    int realIndex = m_filteredIndices[pnmv->iItem];
                    if (realIndex >= 0 && realIndex < (int)m_notes.size()) {
                        targetNoteId = m_notes[realIndex].id;
                    }
                }

                if (!PromptToSaveIfDirty(targetNoteId, false)) {
                    // Revert selection change
                    ListView_SetItemState(m_hwndList, pnmv->iItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    if (m_currentNoteIndex >= 0) {
                        for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
                            if (m_filteredIndices[i] == m_currentNoteIndex) {
                                ListView_SetItemState(m_hwndList, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                                break;
                            }
                        }
                    }
                    return 0;
                }
                SaveCurrentNote(targetNoteId, false); // Auto-save previous note

                int listIndex = (targetNoteId != -1) ? FindListIndexByNoteId(targetNoteId) : pnmv->iItem;
                if (listIndex != -1) {
                    ListView_SetItemState(m_hwndList, listIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    LoadNoteContent(listIndex);
                } else {
                    LoadNoteContent(-1);
                }
            }
        } else if (pnmh->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
            switch (lplvcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
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
                    return CDRF_NEWFONT;
                }
            }
        }
        // Handle Right Click for Context Menu
        else if (pnmh->code == NM_RCLICK) {
             LPNMITEMACTIVATE pnmitem = (LPNMITEMACTIVATE)lParam;
             if (pnmitem->iItem != -1) {
                 // Select the item
                 ListView_SetItemState(m_hwndList, pnmitem->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                 
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
    } else if (pnmh->code == EN_LINK) {
        ENLINK* pLink = (ENLINK*)lParam;
        if (pLink->msg == WM_LBUTTONDOWN) {
            // Open URL in default browser
            std::vector<wchar_t> url(pLink->chrg.cpMax - pLink->chrg.cpMin + 1);
            TEXTRANGE tr;
            tr.chrg = pLink->chrg;
            tr.lpstrText = &url[0];
            SendMessage(m_hwndEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
            ShellExecute(NULL, L"open", &url[0], NULL, NULL, SW_SHOWNORMAL);
        }
    }
    return 0;
}

void MainWindow::OnLButtonDown(int x, int y) {
    if (x >= m_splitPos && x < m_splitPos + SPLITTER_WIDTH) {
        m_isDraggingSplitter = true;
        SetCapture(m_hwnd);
    }
}

void MainWindow::OnLButtonUp(int x, int y) {
    if (m_isDraggingSplitter) {
        m_isDraggingSplitter = false;
        ReleaseCapture();
    }
}

void MainWindow::OnMouseMove(int x, int y) {
    if (m_isDraggingSplitter) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        
        // Limit splitter range
        int minWidth = 100;
        if (x < minWidth) x = minWidth;
        if (x > rc.right - minWidth) x = rc.right - minWidth;
        
        m_splitPos = x;
        OnSize(rc.right, rc.bottom); // Trigger layout update
    }
}

void MainWindow::RegisterHotkeys() {
    if (m_hotkeysRegistered || !m_hwnd) {
        return;
    }

    struct Hotkey { int id; UINT modifiers; UINT key; };
    const Hotkey hotkeys[] = {
        {1, MOD_CONTROL, 'N'},
        {2, MOD_CONTROL, 'S'},
        {3, MOD_CONTROL, 'D'},
        {4, MOD_CONTROL, 'P'},
        {5, MOD_CONTROL, 'F'},
        {6, MOD_CONTROL, 'B'},
        {7, MOD_CONTROL, 'I'},
        {8, MOD_CONTROL, 'U'}
    };

    bool success = true;
    for (const auto& hk : hotkeys) {
        if (!RegisterHotKey(m_hwnd, hk.id, hk.modifiers, hk.key)) {
            success = false;
            break;
        }
    }

    if (success) {
        m_hotkeysRegistered = true;
    } else {
        for (const auto& hk : hotkeys) {
            UnregisterHotKey(m_hwnd, hk.id);
        }
    }
}

void MainWindow::UnregisterHotkeys() {
    if (!m_hotkeysRegistered || !m_hwnd) {
        return;
    }

    for (int id = 1; id <= 8; ++id) {
        UnregisterHotKey(m_hwnd, id);
    }

    m_hotkeysRegistered = false;
}

void MainWindow::LoadNotesList(const std::wstring& filter, bool titleOnly, bool autoSelectFirst, int selectNoteId) {
    ListView_DeleteAllItems(m_hwndList);
    m_notes = m_db->GetAllNotes(m_showArchived, m_sortBy);
    m_filteredIndices.clear();
    m_currentSearchFilter = filter;
    
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
            
            if (titleOnly) {
                // Title only search
                if (wTitleLower.find(filterLower) == std::wstring::npos) {
                    match = false;
                }
            } else {
                // Title and content search
                if (wTitleLower.find(filterLower) == std::wstring::npos && 
                    wContentLower.find(filterLower) == std::wstring::npos) {
                    match = false;
                }
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
    
    int targetListIndex = -1;
    if (selectNoteId != -1) {
        for (int i = 0; i < listIndex; ++i) {
            int realIndex = m_filteredIndices[i];
            if (m_notes[realIndex].id == selectNoteId) {
                targetListIndex = i;
                break;
            }
        }
    }

    // Avoid auto-selecting another note while composing a new unsaved note,
    // but still honor explicit target selections (e.g., after saving or navigation).
    bool shouldSelect = (targetListIndex != -1) || (autoSelectFirst && !m_isNewNote);
    if (shouldSelect) {
        if (targetListIndex != -1) {
            ListView_SetItemState(m_hwndList, targetListIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            LoadNoteContent(targetListIndex);
        } else if (listIndex > 0) {
            ListView_SetItemState(m_hwndList, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            LoadNoteContent(0);
        } else {
            LoadNoteContent(-1);
        }
    }
}

void MainWindow::LoadNoteContent(int listIndex) {
    if (listIndex >= 0 && listIndex < (int)m_filteredIndices.size()) {
        int realIndex = m_filteredIndices[listIndex];
        m_isNewNote = false;
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

        if (!m_navigatingHistory) {
            RecordHistory(realIndex);
        }
    } else {
        m_currentNoteIndex = -1;
        SetWindowText(m_hwndEdit, L"");
        m_isDirty = false;
        m_isNewNote = false;
        m_checklistMode = false;
        
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
        UpdateChecklistUI();
    }
}

void MainWindow::SaveCurrentNote(int preferredSelectNoteId, bool autoSelectAfterSave) {
    if (m_isNewNote) {
        if (!m_isDirty) {
            return;
        }

        int len = GetWindowTextLength(m_hwndEdit);
        std::vector<wchar_t> buf(len + 1);
        GetWindowText(m_hwndEdit, &buf[0], len + 1);
        std::string content = Utils::WideToUtf8(&buf[0]);

        // Derive title from first line
        size_t firstLineEnd = content.find('\n');
        std::string newTitle = (firstLineEnd == std::string::npos) ? content : content.substr(0, firstLineEnd);
        if (!newTitle.empty() && newTitle.back() == '\r') {
            newTitle.pop_back();
        }
        if (newTitle.empty()) {
            newTitle = "Untitled Note";
        }
        if (newTitle.length() > 50) {
            newTitle = newTitle.substr(0, 50) + "...";
        }

        Note newNote;
        newNote.title = newTitle;
        newNote.content = content;

        if (m_db->CreateNote(newNote)) {
            m_isDirty = false;
            m_isNewNote = false;
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Note saved");
            LoadNotesList(m_currentSearchFilter, m_searchTitleOnly, autoSelectAfterSave, preferredSelectNoteId == -1 ? newNote.id : preferredSelectNoteId);
        }
        return;
    }

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

    m_isNewNote = true;
    m_currentNoteIndex = -1;
    m_isDirty = false;
    m_checklistMode = false;

    // Clear selection and editor
    ListView_SetItemState(m_hwndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    SetWindowText(m_hwndEdit, L"");
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
    UpdateChecklistUI();

    // Clear search so user sees full list once saved later
    SetWindowText(m_hwndSearch, L"");
    SetFocus(m_hwndEdit);
}

void MainWindow::DeleteCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        if (MessageBox(m_hwnd, L"Are you sure you want to delete this note?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            int deletedNoteId = m_notes[m_currentNoteIndex].id;
            m_db->DeleteNote(deletedNoteId);
            
            // Try to navigate to previous note in history
            int targetNoteId = -1;
            if (m_historyPos > 0) {
                // Look backward in history for a valid note (not the deleted one)
                for (int i = m_historyPos - 1; i >= 0; --i) {
                    int histNoteIndex = m_history[i];
                    if (histNoteIndex >= 0 && histNoteIndex < (int)m_notes.size() && 
                        m_notes[histNoteIndex].id != deletedNoteId) {
                        targetNoteId = m_notes[histNoteIndex].id;
                        m_historyPos = i;
                        break;
                    }
                }
            }
            
            LoadNotesList(m_currentSearchFilter, m_searchTitleOnly, true, targetNoteId);
            
            // If no note selected (e.g., list is empty), disable editor
            if (ListView_GetSelectedCount(m_hwndList) == 0) {
                m_currentNoteIndex = -1;
                SetWindowText(m_hwndEdit, L"");
                m_isDirty = false;
                m_isNewNote = false;
                EnableWindow(m_hwndEdit, FALSE);
                SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
                SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
                SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
            } else {
                EnableWindow(m_hwndEdit, TRUE);
            }
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
    // Force layout update to ensure controls are sized correctly
    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);
    OnSize(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);

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
            
            std::wstring displayText = item.is_checked ? L"[x] " : L"[ ] ";
            displayText += Utils::Utf8ToWide(item.item_text);
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
        std::string itemText = Utils::WideToUtf8(buffer);
        
        if (!itemText.empty()) {
            ChecklistItem newItem;
            newItem.note_id = m_notes[m_currentNoteIndex].id;
            newItem.item_text = itemText;
            newItem.item_order = (int)m_notes[m_currentNoteIndex].checklist_items.size();
            
            if (m_db->CreateChecklistItem(newItem)) {
                m_notes[m_currentNoteIndex].checklist_items.push_back(newItem);
                UpdateChecklistUI();
                SetWindowText(m_hwndChecklistEdit, L"");
                SetFocus(m_hwndChecklistEdit);
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

void MainWindow::ToggleFormat(DWORD mask, DWORD effect) {
    CHARFORMAT2 cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = mask;
    
    // Get current formatting
    SendMessage(m_hwndEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    
    // Toggle effect
    if (cf.dwEffects & effect) {
        cf.dwEffects &= ~effect;
    } else {
        cf.dwEffects |= effect;
    }
    
    // Apply new formatting
    SendMessage(m_hwndEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    
    // Update button state
    UpdateFormatButtons();
}

void MainWindow::UpdateFormatButtons() {
    CHARFORMAT2 cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    
    SendMessage(m_hwndEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_FORMAT_BOLD, (cf.dwEffects & CFE_BOLD) ? TRUE : FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_FORMAT_ITALIC, (cf.dwEffects & CFE_ITALIC) ? TRUE : FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_FORMAT_UNDERLINE, (cf.dwEffects & CFE_UNDERLINE) ? TRUE : FALSE);
}

void MainWindow::ExportCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        const Note& note = m_notes[m_currentNoteIndex];
        
        // Prepare default filename
        std::wstring wTitle = Utils::Utf8ToWide(note.title);
        // Sanitize filename
        for (auto& c : wTitle) {
            if (wcschr(L"<>:\"/\\|?*", c)) c = L'_';
        }
        wTitle += L".txt";
        
        OPENFILENAME ofn;
        wchar_t szFile[260];
        wcscpy_s(szFile, wTitle.c_str());
        
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrDefExt = L"txt";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        
        if (GetSaveFileName(&ofn) == TRUE) {
            HANDLE hFile = CreateFile(ofn.lpstrFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                std::string content = note.content;
                if (note.is_checklist) {
                    content = note.title + "\n\n";
                    for (const auto& item : note.checklist_items) {
                        content += (item.is_checked ? "[x] " : "[ ] ") + item.item_text + "\n";
                    }
                }
                
                DWORD dwWritten;
                WriteFile(hFile, content.c_str(), (DWORD)content.length(), &dwWritten, NULL);
                CloseHandle(hFile);
                MessageBox(m_hwnd, L"Note exported successfully.", L"Export", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBox(m_hwnd, L"Failed to save file.", L"Error", MB_OK | MB_ICONERROR);
            }
        }
    }
}

void MainWindow::ToggleSearchMode() {
    m_searchTitleOnly = !m_searchTitleOnly;
    
    // Update toolbar button state
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_SEARCH_MODE_TOGGLE, m_searchTitleOnly ? FALSE : TRUE);
    
    // Re-apply current search with new mode
    LoadNotesList(m_currentSearchFilter, m_searchTitleOnly);
    
    // Show notification
    const wchar_t* mode = m_searchTitleOnly ? L"Title only" : L"Title + Content";
    SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)(L"Search mode: " + std::wstring(mode)).c_str());
}

bool MainWindow::PromptToSaveIfDirty(int preferredSelectNoteId, bool autoSelectAfterSave) {
    if (!m_isDirty) {
        return true;
    }

    if (m_isNewNote) {
        SaveCurrentNote(preferredSelectNoteId, autoSelectAfterSave);
        return true;
    }

    int res = MessageBox(m_hwnd, L"You have unsaved changes. Save them?", L"Unsaved Changes", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (res == IDCANCEL) {
        return false;
    }
    if (res == IDYES) {
        SaveCurrentNote(preferredSelectNoteId, autoSelectAfterSave);
        return true;
    }
    // Discard
    m_isDirty = false;
    return true;
}

void MainWindow::RecordHistory(int noteIndex) {
    if (noteIndex < 0) {
        UpdateHistoryButtons();
        return;
    }

    // If we navigated back in history and then choose a new note, drop forward history
    if (m_historyPos + 1 < (int)m_history.size()) {
        m_history.erase(m_history.begin() + m_historyPos + 1, m_history.end());
    }

    if (!m_history.empty() && m_history.back() == noteIndex) {
        UpdateHistoryButtons();
        return;
    }

    m_history.push_back(noteIndex);
    m_historyPos = (int)m_history.size() - 1;
    UpdateHistoryButtons();
}

void MainWindow::NavigateHistory(int offset) {
    int newPos = m_historyPos + offset;
    if (newPos < 0 || newPos >= (int)m_history.size()) {
        UpdateHistoryButtons();
        return;
    }

    int targetNoteIndex = m_history[newPos];

    if (!PromptToSaveIfDirty(targetNoteIndex >= 0 && targetNoteIndex < (int)m_notes.size() ? m_notes[targetNoteIndex].id : -1, false)) {
        return;
    }

    // Find the visible list index for this note
    int listIndex = -1;
    if (targetNoteIndex >= 0 && targetNoteIndex < (int)m_notes.size()) {
        listIndex = FindListIndexByNoteId(m_notes[targetNoteIndex].id);
    }

    if (listIndex == -1) {
        // Note not visible under current filter; do nothing but update buttons
        UpdateHistoryButtons();
        return;
    }

    m_navigatingHistory = true;
    m_historyPos = newPos;
    ListView_SetItemState(m_hwndList, listIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    LoadNoteContent(listIndex);
    m_navigatingHistory = false;
    UpdateHistoryButtons();
}

int MainWindow::FindListIndexByNoteId(int noteId) {
    for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
        int realIndex = m_filteredIndices[i];
        if (realIndex >= 0 && realIndex < (int)m_notes.size()) {
            if (m_notes[realIndex].id == noteId) {
                return i;
            }
        }
    }
    return -1;
}

void MainWindow::UpdateHistoryButtons() {
    bool canBack = m_historyPos > 0;
    bool canForward = m_historyPos >= 0 && m_historyPos + 1 < (int)m_history.size();
    SendMessage(m_hwndToolbar, TB_ENABLEBUTTON, IDM_HIST_BACK, canBack ? TRUE : FALSE);
    SendMessage(m_hwndToolbar, TB_ENABLEBUTTON, IDM_HIST_FORWARD, canForward ? TRUE : FALSE);
}
