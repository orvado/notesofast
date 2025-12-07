#include "window.h"
#include "utils.h"
#include <string>

#define ID_LISTVIEW 1
#define ID_RICHEDIT 2
#define ID_TOOLBAR 3
#define ID_STATUS 4
#define ID_SEARCH 5

#define IDM_NEW 101
#define IDM_SAVE 102
#define IDM_DELETE 103

MainWindow::MainWindow(Database* db) : m_hwnd(NULL), m_hwndList(NULL), m_hwndEdit(NULL), m_hwndSearch(NULL), m_hwndToolbar(NULL), m_hwndStatus(NULL), m_db(db) { }

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
    
    TBBUTTON tbb[3];
    ZeroMemory(tbb, sizeof(tbb));
    tbb[0].iBitmap = I_IMAGENONE;
    tbb[0].idCommand = IDM_NEW;
    tbb[0].fsState = TBSTATE_ENABLED;
    tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[0].iString = (INT_PTR)L"New Note";

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

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 3, (LPARAM)&tbb);

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

    // Resize Edit Control
    MoveWindow(m_hwndEdit, listWidth, toolbarHeight, width - listWidth, clientHeight, TRUE);
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
}

void MainWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    LPNMHDR pnmh = (LPNMHDR)lParam;
    if (pnmh->idFrom == ID_LISTVIEW) {
        if (pnmh->code == LVN_ITEMCHANGED) {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
            if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                SaveCurrentNote(); // Auto-save previous note
                LoadNoteContent(pnmv->iItem);
            }
        }
    }
}

void MainWindow::LoadNotesList(const std::wstring& filter) {
    ListView_DeleteAllItems(m_hwndList);
    m_notes = m_db->GetAllNotes();
    m_filteredIndices.clear();
    
    LVITEM lvi;
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    
    int listIndex = 0;
    for (size_t i = 0; i < m_notes.size(); ++i) {
        std::wstring wTitle = Utils::Utf8ToWide(m_notes[i].title);
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
    } else {
        m_currentNoteIndex = -1;
        SetWindowText(m_hwndEdit, L"");
        m_isDirty = false;
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
