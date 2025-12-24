#define NOMINMAX
#include "window.h"
#include "utils.h"
#include "spell_checker.h"
#include "settings_dialog.h"
#include "cloud_sync.h"
#include "credentials.h"
#include "resource.h"
#include <string>
#include <algorithm>
#include <memory>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <process.h>

static LONG GetRichEditTextLength(HWND hwnd) {
    GETTEXTLENGTHEX ltx = {};
    ltx.flags = GTL_DEFAULT;
    ltx.codepage = 1200; // UTF-16LE
    return (LONG)SendMessage(hwnd, EM_GETTEXTLENGTHEX, (WPARAM)&ltx, 0);
}

static std::wstring TrimLeft(const std::wstring& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) {
        ++i;
    }
    return s.substr(i);
}

static std::wstring TrimRightSpaces(const std::wstring& s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == L' ' || s[end - 1] == L'\t')) {
        --end;
    }
    return s.substr(0, end);
}

static bool HasMarkdownHardBreak(const std::wstring& line) {
    // In Markdown, two trailing spaces indicate a hard line break.
    // Count actual spaces (not tabs).
    int spaces = 0;
    for (size_t i = line.size(); i > 0; --i) {
        wchar_t c = line[i - 1];
        if (c == L' ') {
            spaces++;
        } else if (c == L'\t') {
            // tabs don't count toward the "two spaces" convention
            continue;
        } else {
            break;
        }
        if (spaces >= 2) {
            return true;
        }
    }
    return false;
}

static bool IsHorizontalRule(const std::wstring& trimmed) {
    if (trimmed.size() < 3) return false;
    wchar_t ch = trimmed[0];
    if (ch != L'-' && ch != L'*' && ch != L'_') return false;
    int count = 0;
    for (wchar_t c : trimmed) {
        if (c == ch) {
            count++;
        } else if (c != L' ' && c != L'\t') {
            return false;
        }
    }
    return count >= 3;
}

static std::wstring EnsureUrlHasScheme(const std::wstring& url) {
    if (url.empty()) {
        return url;
    }
    if (url.find(L"://") != std::wstring::npos) {
        return url;
    }
    // Allow mailto: and other non-:// schemes
    if (url.find(L":") != std::wstring::npos) {
        return url;
    }
    return L"https://" + url;
}

struct InlineRun {
    std::wstring text;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool link = false;
    std::wstring url;
};

static std::vector<InlineRun> ParseInlineMarkdown(const std::wstring& text) {
    std::vector<InlineRun> runs;
    bool bold = false;
    bool italic = false;
    bool strike = false;

    auto flush = [&](std::wstring& buf) {
        if (!buf.empty()) {
            InlineRun r;
            r.text = buf;
            r.bold = bold;
            r.italic = italic;
            r.strike = strike;
            runs.push_back(std::move(r));
            buf.clear();
        }
    };

    std::wstring buf;
    for (size_t i = 0; i < text.size();) {
        // Link: [text](url)
        if (text[i] == L'[') {
            size_t closeBracket = text.find(L']', i + 1);
            if (closeBracket != std::wstring::npos && closeBracket + 1 < text.size() && text[closeBracket + 1] == L'(') {
                size_t closeParen = text.find(L')', closeBracket + 2);
                if (closeParen != std::wstring::npos) {
                    flush(buf);
                    InlineRun r;
                    r.text = text.substr(i + 1, closeBracket - (i + 1));
                    r.bold = bold;
                    r.italic = italic;
                    r.strike = strike;
                    r.link = true;
                    r.url = text.substr(closeBracket + 2, closeParen - (closeBracket + 2));
                    runs.push_back(std::move(r));
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        // Strike: ~~
        if (i + 1 < text.size() && text[i] == L'~' && text[i + 1] == L'~') {
            flush(buf);
            strike = !strike;
            i += 2;
            continue;
        }

        // Bold: ** or __
        if (i + 1 < text.size() && ((text[i] == L'*' && text[i + 1] == L'*') || (text[i] == L'_' && text[i + 1] == L'_'))) {
            flush(buf);
            bold = !bold;
            i += 2;
            continue;
        }

        // Italic: * or _
        if (text[i] == L'*' || text[i] == L'_') {
            flush(buf);
            italic = !italic;
            i += 1;
            continue;
        }

        buf.push_back(text[i]);
        i += 1;
    }
    flush(buf);
    return runs;
}

static void ApplyCharStyle(HWND hwnd, const InlineRun& run, bool enableLinks) {
    CHARFORMAT2 cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_ITALIC | CFM_STRIKEOUT | CFM_UNDERLINE | CFM_LINK | CFM_COLOR;
    cf.dwEffects = 0;
    cf.crTextColor = RGB(0, 0, 0);

    if (run.bold) cf.dwEffects |= CFE_BOLD;
    if (run.italic) cf.dwEffects |= CFE_ITALIC;
    if (run.strike) cf.dwEffects |= CFE_STRIKEOUT;
    if (enableLinks && run.link) {
        cf.dwEffects |= CFE_UNDERLINE;
        cf.dwEffects |= CFE_LINK;
        cf.crTextColor = RGB(0, 0, 238);
    }

    SendMessage(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void ApplyHeaderCharStyle(HWND hwnd, int level) {
    // Scale by H1..H6 using points; RichEdit uses twips.
    int pt = 20;
    if (level == 1) pt = 22;
    else if (level == 2) pt = 20;
    else if (level == 3) pt = 18;
    else if (level == 4) pt = 16;
    else if (level == 5) pt = 14;
    else if (level == 6) pt = 13;

    CHARFORMAT2 cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_BOLD | CFM_SIZE;
    cf.dwEffects = CFE_BOLD;
    cf.yHeight = pt * 20; // twips
    SendMessage(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void ApplyParaIndent(HWND hwnd, int leftTwips, int firstLineTwips) {
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_STARTINDENT | PFM_OFFSET;
    pf.dxStartIndent = leftTwips;
    pf.dxOffset = firstLineTwips;
    SendMessage(hwnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void ApplyParaBullets(HWND hwnd, bool numbered) {
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_NUMBERINGSTART | PFM_STARTINDENT | PFM_OFFSET;
    pf.wNumbering = numbered ? PFN_ARABIC : PFN_BULLET;
    pf.wNumberingStart = 1;
    pf.dxStartIndent = 360; // quarter inch
    pf.dxOffset = -360;
    SendMessage(hwnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static void ApplyParaNormal(HWND hwnd) {
    PARAFORMAT2 pf = {};
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET;
    pf.wNumbering = 0;
    pf.dxStartIndent = 0;
    pf.dxOffset = 0;
    SendMessage(hwnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
}

static std::wstring FormatFileSize(ULONGLONG bytes) {
    wchar_t buffer[64] = {0};
    const double KB = 1024.0;
    const double MB = KB * 1024.0;
    const double GB = MB * 1024.0;
    const size_t bufferSize = sizeof(buffer) / sizeof(buffer[0]);
    if (bytes >= (ULONGLONG)GB) {
        swprintf_s(buffer, bufferSize, L"%.1f GB", bytes / GB);
    } else if (bytes >= (ULONGLONG)MB) {
        swprintf_s(buffer, bufferSize, L"%.1f MB", bytes / MB);
    } else if (bytes >= (ULONGLONG)KB) {
        swprintf_s(buffer, bufferSize, L"%.0f KB", bytes / KB);
    } else {
        swprintf_s(buffer, bufferSize, L"%llu bytes", bytes);
    }
    return std::wstring(buffer);
}

static std::string NowLocalTimeStringA() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

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
#define ID_PREVIEW 13
#define ID_SPELLCHECK_TIMER 2001
#define ID_CLOUDSYNC_TIMER 2002

static const UINT WM_APP_CLOUD_AUTO_SYNC_DONE = WM_APP + 130;

struct CloudAutoSyncThreadParams {
    HWND hwnd;
    Database* db;
    std::wstring dbPath;
    std::string clientId;
};

struct CloudAutoSyncResultMsg {
    bool success = false;
    std::string error;
    std::string localTime;
};

static unsigned __stdcall CloudAutoSyncThread(void* p) {
    std::unique_ptr<CloudAutoSyncThreadParams> params((CloudAutoSyncThreadParams*)p);
    std::unique_ptr<CloudAutoSyncResultMsg> res(new CloudAutoSyncResultMsg());

    CloudSyncResult r = CloudSync::UploadDatabaseSnapshot(params->db, params->dbPath, params->clientId);
    res->success = r.success;
    res->error = r.error;
    res->localTime = NowLocalTimeStringA();

    if (IsWindow(params->hwnd)) {
        PostMessage(params->hwnd, WM_APP_CLOUD_AUTO_SYNC_DONE, 0, (LPARAM)res.release());
    }
    return 0;
}

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
#define IDM_PRINT 503
#define IDM_HIST_BACK 601
#define IDM_HIST_FORWARD 602
#define IDM_SEARCH_MODE_TOGGLE 502

WNDPROC g_oldEditProc = NULL;
WNDPROC g_oldSearchProc = NULL;

static LRESULT CALLBACK PreviewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData) {
    if (uMsg == WM_LBUTTONDBLCLK) {
        // Double-clicking preview returns to editable mode.
        HWND parent = GetParent(hwnd);
        if (parent) {
            PostMessage(parent, WM_COMMAND, MAKEWPARAM(IDM_MARKDOWN_PREVIEW, 0), 0);
            HWND edit = GetDlgItem(parent, ID_RICHEDIT);
            if (edit) {
                SetFocus(edit);
            }
        }
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

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

LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MainWindow* pWindow = (MainWindow*)GetWindowLongPtr(GetParent(hwnd), GWLP_USERDATA);
    
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_UP) {
            if (pWindow) {
                pWindow->NavigateSearchHistory(-1);
            }
            return 0;
        } else if (wParam == VK_DOWN) {
            if (pWindow) {
                pWindow->NavigateSearchHistory(1);
            }
            return 0;
        }
    }
    
    return CallWindowProc(g_oldSearchProc, hwnd, uMsg, wParam, lParam);
}

MainWindow::MainWindow(Database* db) : m_hwnd(NULL), m_hwndList(NULL), m_hwndEdit(NULL), m_hwndSearch(NULL), 
    m_hwndToolbar(NULL), m_hwndMarkdownToolbar(NULL), m_hwndStatus(NULL), 
    m_hwndChecklistList(NULL), m_hwndChecklistEdit(NULL), m_hwndAddItem(NULL), 
    m_hwndRemoveItem(NULL), m_hwndMoveUp(NULL), m_hwndMoveDown(NULL), m_db(db) {
    m_colors = m_db->GetColors();
    m_searchHistory = m_db->GetSearchHistory();

    std::string selectedTagStr = m_db->GetSetting("SelectedTagId", "-1");
    try {
        m_selectedTagId = std::stoi(selectedTagStr);
    } catch (...) {
        m_selectedTagId = -1;
    }

    std::string lastViewedStr = m_db->GetSetting("LastViewedNoteId", "-1");
    try {
        int parsed = std::stoi(lastViewedStr);
        m_lastViewedNoteId = parsed;
    } catch (...) {
        m_lastViewedNoteId = -1;
    }
}

MainWindow::~MainWindow() {
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hMarkdownToolbarImages) {
        ImageList_Destroy(m_hMarkdownToolbarImages);
        m_hMarkdownToolbarImages = NULL;
    }
}

BOOL MainWindow::Create(PCWSTR lpWindowName, DWORD dwStyle, DWORD dwExStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu) {
    HINSTANCE hInst = GetModuleHandle(NULL);

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"NoteSoFastWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    RegisterClassExW(&wc);

    m_hwnd = CreateWindowEx(dwExStyle, L"NoteSoFastWindowClass", lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInst, this);

    if (m_hwnd) {
        if (wc.hIcon) SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
        if (wc.hIconSm) SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);
    }

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
    case WM_TIMER:
        OnTimer(wParam);
        return 0;
    case WM_APP_CLOUD_AUTO_SYNC_DONE:
        {
            std::unique_ptr<CloudAutoSyncResultMsg> res((CloudAutoSyncResultMsg*)lParam);
            m_cloudSyncInProgress = false;

            if (m_db && res) {
                if (res->success) {
                    m_db->SetSetting("cloud_last_sync_time", res->localTime);
                    m_db->SetSetting("cloud_sync_last_error", "");
                } else {
                    if (!res->error.empty()) {
                        m_db->SetSetting("cloud_sync_last_error", res->error);
                    }
                }
            }
        }
        return 0;
    case WM_CLOSE:
        SaveCurrentNote();
        KillTimer(m_hwnd, ID_CLOUDSYNC_TIMER);
        SyncDatabaseOnExitIfEnabled();
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        SaveCurrentNote();
        UnregisterHotkeys();
        KillTimer(m_hwnd, ID_SPELLCHECK_TIMER);
        KillTimer(m_hwnd, ID_CLOUDSYNC_TIMER);
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
            // Debug: Show global Ctrl+S was triggered
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Global Ctrl+S hotkey triggered");
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

void MainWindow::SyncDatabaseOnExitIfEnabled() {
    if (!m_db) {
        return;
    }
    if (m_cloudSyncInProgress) {
        return;
    }
    if (m_db->GetSetting("cloud_sync_enabled", "0") != "1") {
        return;
    }
    if (m_db->GetSetting("cloud_sync_on_exit", "1") != "1") {
        return;
    }
    const std::string clientId = m_db->GetSetting("cloud_oauth_client_id", "");
    if (clientId.empty()) {
        return;
    }

    CloudSyncResult r = CloudSync::UploadDatabaseSnapshot(m_db, m_dbPath, clientId);
    if (r.success) {
        m_db->SetSetting("cloud_last_sync_time", NowLocalTimeStringA());
        m_db->SetSetting("cloud_sync_last_error", "");
    } else {
        if (!r.error.empty()) {
            m_db->SetSetting("cloud_sync_last_error", r.error);
        }
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
    SendMessage(m_hwndSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search (↑↓ for History)");
    
    // Subclass search box to handle arrow keys
    g_oldSearchProc = (WNDPROC)SetWindowLongPtr(m_hwndSearch, GWLP_WNDPROC, (LONG_PTR)SearchEditProc);

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

    SetWindowSubclass(m_hwndEdit, RichEditSubclassProc, 1, (DWORD_PTR)this);
    
    // Set font for Rich Edit
    m_hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    SendMessage(m_hwndEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);

    // Enable Auto-URL detection
    bool clickableLinks = (m_db && m_db->GetSetting("clickable_links", "1") == "1");
    SendMessage(m_hwndEdit, EM_AUTOURLDETECT, clickableLinks ? TRUE : FALSE, 0);
    SendMessage(m_hwndEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | (clickableLinks ? ENM_LINK : 0));

    // Create Markdown Preview Rich Edit (Right Panel, initially hidden)
    m_hwndPreview = CreateWindow(MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL,
        0, 0, 0, 0, m_hwnd, (HMENU)ID_PREVIEW, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndPreview, WM_SETFONT, (WPARAM)m_hFont, TRUE);
    SendMessage(m_hwndPreview, EM_SETREADONLY, TRUE, 0);
    SendMessage(m_hwndPreview, EM_AUTOURLDETECT, clickableLinks ? TRUE : FALSE, 0);
    SendMessage(m_hwndPreview, EM_SETEVENTMASK, 0, clickableLinks ? ENM_LINK : 0);
    SetWindowSubclass(m_hwndPreview, PreviewSubclassProc, 2, (DWORD_PTR)this);

    // Create Toolbar (Top)
    m_hwndToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | CCS_NODIVIDER, 0, 0, 0, 0, m_hwnd, (HMENU)ID_TOOLBAR, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    
    // Add strings to toolbar
    int iTC = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"T+C\0");
    int iB = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"B\0");
    int iI = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"I\0");
    int iU = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"U\0");
    int iSettings = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"Settings\0");

    // Load Standard Images
    int stdIdx = (int)SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    int viewIdx = (int)SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_VIEW_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);
    int histIdx = (int)SendMessage(m_hwndToolbar, TB_LOADIMAGES, (WPARAM)IDB_HIST_SMALL_COLOR, (LPARAM)HINST_COMMCTRL);

    TBBUTTON tbb[13];
    ZeroMemory(tbb, sizeof(tbb));

    tbb[0].iBitmap = stdIdx + STD_FILENEW;
    tbb[0].idCommand = IDM_NEW;
    tbb[0].fsState = TBSTATE_ENABLED;
    tbb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[0].iString = -1;

    tbb[1].iBitmap = stdIdx + STD_FILESAVE;
    tbb[1].idCommand = IDM_SAVE;
    tbb[1].fsState = TBSTATE_ENABLED;
    tbb[1].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[1].iString = -1;

    tbb[2].iBitmap = stdIdx + STD_DELETE;
    tbb[2].idCommand = IDM_DELETE;
    tbb[2].fsState = TBSTATE_ENABLED;
    tbb[2].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[2].iString = -1;

    tbb[3].iBitmap = viewIdx + 6; // VIEW_SORTDATE
    tbb[3].idCommand = IDM_SORT;
    tbb[3].fsState = TBSTATE_ENABLED;
    tbb[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[3].iString = -1;

    tbb[4].iBitmap = histIdx + HIST_BACK;
    tbb[4].idCommand = IDM_HIST_BACK;
    tbb[4].fsState = TBSTATE_ENABLED;
    tbb[4].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[4].iString = -1;

    tbb[5].iBitmap = histIdx + HIST_FORWARD;
    tbb[5].idCommand = IDM_HIST_FORWARD;
    tbb[5].fsState = TBSTATE_ENABLED;
    tbb[5].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[5].iString = -1;

    tbb[6].iBitmap = I_IMAGENONE;
    tbb[6].idCommand = IDM_SEARCH_MODE_TOGGLE;
    tbb[6].fsState = TBSTATE_ENABLED;
    tbb[6].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[6].iString = iTC;

    tbb[7].iBitmap = stdIdx + STD_PRINT;
    tbb[7].idCommand = IDM_PRINT;
    tbb[7].fsState = TBSTATE_ENABLED;
    tbb[7].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[7].iString = -1;

    tbb[8].iBitmap = histIdx + HIST_FAVORITES;
    tbb[8].idCommand = IDM_PIN;
    tbb[8].fsState = TBSTATE_ENABLED;
    tbb[8].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[8].iString = -1;

    tbb[9].iBitmap = stdIdx + STD_FILEOPEN; // Using File Open (folder) for Archive
    tbb[9].idCommand = IDM_ARCHIVE;
    tbb[9].fsState = TBSTATE_ENABLED;
    tbb[9].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[9].iString = -1;

    tbb[10].iBitmap = viewIdx + 8; // VIEW_PARENTFOLDER
    tbb[10].idCommand = IDM_SHOW_ARCHIVED;
    tbb[10].fsState = TBSTATE_ENABLED;
    tbb[10].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[10].iString = -1;

    tbb[11].iBitmap = viewIdx + 2; // VIEW_LIST
    tbb[11].idCommand = IDM_TOGGLE_CHECKLIST;
    tbb[11].fsState = TBSTATE_ENABLED;
    tbb[11].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbb[11].iString = -1;

    tbb[12].iBitmap = stdIdx + STD_PROPERTIES;
    tbb[12].idCommand = IDM_SETTINGS;
    tbb[12].fsState = TBSTATE_ENABLED;
    tbb[12].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbb[12].iString = iSettings;

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 13, (LPARAM)&tbb);
    
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
    tbbFormat[0].iString = iB;

    tbbFormat[1].iBitmap = I_IMAGENONE;
    tbbFormat[1].idCommand = IDM_FORMAT_ITALIC;
    tbbFormat[1].fsState = TBSTATE_ENABLED;
    tbbFormat[1].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbbFormat[1].iString = iI;

    tbbFormat[2].iBitmap = I_IMAGENONE;
    tbbFormat[2].idCommand = IDM_FORMAT_UNDERLINE;
    tbbFormat[2].fsState = TBSTATE_ENABLED;
    tbbFormat[2].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE;
    tbbFormat[2].iString = iU;

    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 3, (LPARAM)&tbbFormat);

    // Add Tag Filter
    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 1, (LPARAM)&tbbSep);
    
    int iTagLabel = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)L"Filter:\0");
    
    std::wstring tagButtonText = L"<None>";
    if (m_selectedTagId != -1) {
        std::vector<Database::Tag> tags = m_db->GetTags();
        for (const auto& tag : tags) {
            if (tag.id == m_selectedTagId) {
                tagButtonText = tag.name;
                break;
            }
        }
    }
    tagButtonText += L"\0";
    int iTagValue = (int)SendMessage(m_hwndToolbar, TB_ADDSTRING, 0, (LPARAM)tagButtonText.c_str());
    
    TBBUTTON tbbTag[2];
    ZeroMemory(tbbTag, sizeof(tbbTag));
    
    tbbTag[0].iBitmap = I_IMAGENONE;
    tbbTag[0].idCommand = IDM_TAG_FILTER_LABEL;
    tbbTag[0].fsState = TBSTATE_ENABLED;
    tbbTag[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
    tbbTag[0].iString = iTagLabel;
    
    tbbTag[1].iBitmap = I_IMAGENONE;
    tbbTag[1].idCommand = IDM_TAG_FILTER_BUTTON;
    tbbTag[1].fsState = TBSTATE_ENABLED;
    tbbTag[1].fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE;
    tbbTag[1].iString = iTagValue;
    
    SendMessage(m_hwndToolbar, TB_ADDBUTTONS, 2, (LPARAM)&tbbTag);

    // Create Markdown Toolbar (initially hidden or shown depending on mode)
    // Use mixed buttons: most are icon-only, but some (Header, Tag) show text.
    m_hwndMarkdownToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, 
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE, 
        0, 0, 0, 0, m_hwnd, (HMENU)ID_MARKDOWN_TOOLBAR, GetModuleHandle(NULL), NULL);
    SendMessage(m_hwndMarkdownToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    // Ensure text (for mixed buttons) stays on one row.
    SendMessage(m_hwndMarkdownToolbar, TB_SETMAXTEXTROWS, 1, 0);

    // Allow some buttons to display text next to (or without) images.
    {
        DWORD ex = (DWORD)SendMessage(m_hwndMarkdownToolbar, TB_GETEXTENDEDSTYLE, 0, 0);
        ex |= TBSTYLE_EX_MIXEDBUTTONS;
        SendMessage(m_hwndMarkdownToolbar, TB_SETEXTENDEDSTYLE, 0, (LPARAM)ex);
    }

    // Keep markdown toolbar buttons aligned with the main toolbar sizing (but never smaller than the icon size)
    DWORD mainBtnSize = (DWORD)SendMessage(m_hwndToolbar, TB_GETBUTTONSIZE, 0, 0);
    int mdBtnW = (mainBtnSize != 0) ? (int)LOWORD(mainBtnSize) : 0;
    int mdBtnH = (mainBtnSize != 0) ? (int)HIWORD(mainBtnSize) : 0;

    // Render toolbar icons at 24x24 (good visual balance and fits typical toolbar heights without clipping).
    const int iconCx = 24;
    const int iconCy = 24;

    if (mdBtnW <= 0) mdBtnW = iconCx + 8;
    if (mdBtnH <= 0) mdBtnH = iconCy + 8;
    if (mdBtnW < iconCx + 8) mdBtnW = iconCx + 8;
    if (mdBtnH < iconCy + 8) mdBtnH = iconCy + 8;
    SendMessage(m_hwndMarkdownToolbar, TB_SETBUTTONSIZE, 0, MAKELONG(mdBtnW, mdBtnH));

    // Icon imagelist for markdown toolbar
    SendMessage(m_hwndMarkdownToolbar, TB_SETBITMAPSIZE, 0, MAKELONG(iconCx, iconCy));

    if (m_hMarkdownToolbarImages) {
        ImageList_Destroy(m_hMarkdownToolbarImages);
        m_hMarkdownToolbarImages = NULL;
    }
    // Use 32-bit imagelist for crisp icons; avoid masks which can degrade alpha edges.
    m_hMarkdownToolbarImages = ImageList_Create(iconCx, iconCy, ILC_COLOR32, 16, 8);
    if (m_hMarkdownToolbarImages) {
        ImageList_SetBkColor(m_hMarkdownToolbarImages, CLR_NONE);
    }

    auto addResIcon = [&](int resId) -> int {
        if (!m_hMarkdownToolbarImages) return I_IMAGENONE;
        HICON hIcon = NULL;
        // Load the icon at the requested size. Keeping this to LoadImageW avoids COMCTL32 ordinal imports
        // that can prevent the app from starting on systems without newer common-controls.
        hIcon = (HICON)LoadImageW(GetModuleHandle(NULL), MAKEINTRESOURCEW(resId), IMAGE_ICON, iconCx, iconCy, LR_DEFAULTCOLOR);
        if (!hIcon) return I_IMAGENONE;

        // Render into a 32-bit DIB and add that to the imagelist.
        // This produces cleaner results than ImageList_AddIcon on some systems.
        BITMAPV5HEADER bi = {};
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = iconCx;
        bi.bV5Height = -iconCy; // top-down
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;

        void* pvBits = nullptr;
        HDC hdc = GetDC(m_hwnd);
        HBITMAP hbm = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &pvBits, NULL, 0);
        if (hdc) ReleaseDC(m_hwnd, hdc);

        int idx = I_IMAGENONE;
        if (hbm) {
            HDC memdc = CreateCompatibleDC(NULL);
            HGDIOBJ old = SelectObject(memdc, hbm);
            if (pvBits) {
                memset(pvBits, 0, (size_t)iconCx * (size_t)iconCy * 4);
            }
            DrawIconEx(memdc, 0, 0, hIcon, iconCx, iconCy, 0, NULL, DI_NORMAL);
            SelectObject(memdc, old);
            DeleteDC(memdc);

            int added = ImageList_Add(m_hMarkdownToolbarImages, hbm, NULL);
            if (added >= 0) idx = added;
            DeleteObject(hbm);
        } else {
            int added = ImageList_AddIcon(m_hMarkdownToolbarImages, hIcon);
            if (added >= 0) idx = added;
        }

        DestroyIcon(hIcon);
        return (idx >= 0) ? idx : I_IMAGENONE;
    };

    int imgBold = addResIcon(IDI_MD_BOLD);
    int imgItalic = addResIcon(IDI_MD_ITALIC);
    int imgStrike = addResIcon(IDI_MD_STRIKETHROUGH);
    int imgQuote = addResIcon(IDI_MD_BLOCKQUOTE);
    int imgOL = addResIcon(IDI_MD_NUMBERLIST);
    int imgUL = addResIcon(IDI_MD_BULLETLIST);
    int imgSub = addResIcon(IDI_MD_SUBSCRIPT);
    int imgSuper = addResIcon(IDI_MD_SUPERSCRIPT);
    int imgTable = addResIcon(IDI_MD_TABLE);
    int imgLink = addResIcon(IDI_MD_LINK);
    int imgView = addResIcon(IDI_MD_VIEW);
    int imgUndo = addResIcon(IDI_MD_UNDO);
    int imgRedo = addResIcon(IDI_MD_REDO);

    SendMessage(m_hwndMarkdownToolbar, TB_SETIMAGELIST, 0, (LPARAM)m_hMarkdownToolbarImages);

    int iMPara = (int)SendMessage(m_hwndMarkdownToolbar, TB_ADDSTRING, 0, (LPARAM)L"Header\0");
    int iMLine = (int)SendMessage(m_hwndMarkdownToolbar, TB_ADDSTRING, 0, (LPARAM)L"Line\0");
    int iMTagButton = (int)SendMessage(m_hwndMarkdownToolbar, TB_ADDSTRING, 0, (LPARAM)L"<None>\0");

    TBBUTTON mtbb[20];
    ZeroMemory(mtbb, sizeof(mtbb));

    int i = 0;
    mtbb[i].iBitmap = imgBold; mtbb[i].idCommand = IDM_MARKDOWN_BOLD; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgItalic; mtbb[i].idCommand = IDM_MARKDOWN_ITALIC; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgStrike; mtbb[i].idCommand = IDM_MARKDOWN_STRIKE; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    
    mtbb[i].fsStyle = BTNS_SEP; i++;

    mtbb[i].iBitmap = I_IMAGENONE; mtbb[i].idCommand = IDM_MARKDOWN_PARA; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE | BTNS_SHOWTEXT; mtbb[i].iString = iMPara; i++;
    
    mtbb[i].fsStyle = BTNS_SEP; i++;

    mtbb[i].iBitmap = imgQuote; mtbb[i].idCommand = IDM_MARKDOWN_QUOTE; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgOL; mtbb[i].idCommand = IDM_MARKDOWN_OL; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgUL; mtbb[i].idCommand = IDM_MARKDOWN_UL; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;

    // New (currently no-op) buttons: Subscript / Superscript / Table
    mtbb[i].iBitmap = imgSub; mtbb[i].idCommand = IDM_MARKDOWN_SUBSCRIPT; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgSuper; mtbb[i].idCommand = IDM_MARKDOWN_SUPERSCRIPT; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgTable; mtbb[i].idCommand = IDM_MARKDOWN_TABLE; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    
    mtbb[i].fsStyle = BTNS_SEP; i++;

    mtbb[i].iBitmap = imgLink; mtbb[i].idCommand = IDM_MARKDOWN_LINK; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = I_IMAGENONE; mtbb[i].idCommand = IDM_MARKDOWN_HR; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT; mtbb[i].iString = iMLine; i++;
    
    mtbb[i].fsStyle = BTNS_SEP; i++;

    mtbb[i].iBitmap = imgView; mtbb[i].idCommand = IDM_MARKDOWN_PREVIEW; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    
    mtbb[i].fsStyle = BTNS_SEP; i++;

    mtbb[i].iBitmap = imgUndo; mtbb[i].idCommand = IDM_MARKDOWN_UNDO; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;
    mtbb[i].iBitmap = imgRedo; mtbb[i].idCommand = IDM_MARKDOWN_REDO; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; mtbb[i].iString = -1; i++;

    // Tag button lives inside the markdown toolbar so it renders like other toolbar buttons (e.g., View)
    mtbb[i].fsStyle = BTNS_SEP; i++;
    mtbb[i].iBitmap = I_IMAGENONE; mtbb[i].idCommand = IDM_NOTE_TAG_BUTTON; mtbb[i].fsState = TBSTATE_ENABLED; mtbb[i].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT; mtbb[i].iString = iMTagButton; i++;

    SendMessage(m_hwndMarkdownToolbar, TB_ADDBUTTONS, i, (LPARAM)&mtbb);

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

    // Initialize spell checker (hunspell) if dictionaries are present
    wchar_t modulePath[MAX_PATH] = {0};
    GetModuleFileName(NULL, modulePath, MAX_PATH);
    std::wstring exeDir(modulePath);
    size_t slash = exeDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        exeDir = exeDir.substr(0, slash);
    }
    std::wstring dictDir = exeDir + L"\\dict\\";
    std::wstring affPath = dictDir + L"en_US.aff";
    std::wstring dicPath = dictDir + L"en_US.dic";
    m_spellChecker = std::make_unique<SpellChecker>();
    m_spellChecker->Initialize(affPath, dicPath);

    LoadNotesList(L"", false, true, m_lastViewedNoteId);
}

void MainWindow::OnSize(int width, int height) {
    int statusHeight = 0;
    if (m_hwndStatus) {
        // Resize Status Bar
        SendMessage(m_hwndStatus, WM_SIZE, 0, 0);
        UpdateStatusBarParts(width);
        if (m_dbInfoNeedsRefresh) {
            UpdateStatusBarDbInfo();
        }

        RECT rcStatus;
        GetWindowRect(m_hwndStatus, &rcStatus);
        statusHeight = rcStatus.bottom - rcStatus.top;
    }

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
        
        ShowWindow(m_hwndMarkdownToolbar, SW_HIDE);
    } else {
        // Size to fit the markdown toolbar's button height (prevents icon clipping).
        int markdownToolbarHeight = toolbarHeight;
        if (m_hwndMarkdownToolbar) {
            DWORD mdBtnSize = (DWORD)SendMessage(m_hwndMarkdownToolbar, TB_GETBUTTONSIZE, 0, 0);
            int mdBtnH = (mdBtnSize != 0) ? (int)HIWORD(mdBtnSize) : 0;
            if (mdBtnH > 0) {
                markdownToolbarHeight = std::max(markdownToolbarHeight, mdBtnH + 4);
            }
        }

        if (m_markdownPreviewMode) {
            ShowWindow(m_hwndMarkdownToolbar, SW_HIDE);
            ShowWindow(m_hwndEdit, SW_HIDE);
            ShowWindow(m_hwndPreview, SW_SHOW);

            MoveWindow(m_hwndPreview, rightPaneX, toolbarHeight, rightPaneWidth, clientHeight, TRUE);
        } else {
            ShowWindow(m_hwndMarkdownToolbar, SW_SHOW);
            ShowWindow(m_hwndEdit, SW_SHOW);
            ShowWindow(m_hwndPreview, SW_HIDE);

            SendMessage(m_hwndMarkdownToolbar, TB_AUTOSIZE, 0, 0);
            MoveWindow(m_hwndMarkdownToolbar, rightPaneX, toolbarHeight, rightPaneWidth, markdownToolbarHeight, TRUE);
            MoveWindow(m_hwndEdit, rightPaneX, toolbarHeight + markdownToolbarHeight, rightPaneWidth, clientHeight - markdownToolbarHeight, TRUE);
        }

        // Add small horizontal padding to the RichEdit content area
        HDC hdc = GetDC(m_hwnd);
        int dpi = 96;
        if (hdc) {
            dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(m_hwnd, hdc);
        }
        int margin = MulDiv(5, dpi, 96);
        SendMessage(m_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
        SendMessage(m_hwndPreview, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
    }
}

void MainWindow::UpdateStatusBarParts(int statusWidth) {
    if (!m_hwndStatus || statusWidth <= 0) {
        return;
    }

    const int minPaneWidth = 200;
    int paneMax = std::max(statusWidth - 100, 0);
    int desiredWidth = statusWidth / 3;
    int paneWidth = std::min(std::max(desiredWidth, minPaneWidth), paneMax);
    if (paneWidth <= 0) {
        paneWidth = statusWidth;
    }

    int parts[2] = { std::max(statusWidth - paneWidth, 0), -1 };
    SendMessage(m_hwndStatus, SB_SETPARTS, 2, (LPARAM)parts);
    m_statusPartsConfigured = true;
}

void MainWindow::UpdateStatusBarDbInfo() {
    if (!m_hwndStatus || !m_statusPartsConfigured) {
        return;
    }

    std::wstring dbInfo;
    if (!m_dbPath.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA fileInfo = {};
        std::wstring sizeText = L"Unknown size";
        if (GetFileAttributesEx(m_dbPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
            ULONGLONG bytes = ((ULONGLONG)fileInfo.nFileSizeHigh << 32) | fileInfo.nFileSizeLow;
            sizeText = FormatFileSize(bytes);
        }

        dbInfo = L"DB: " + m_dbPath + L" (" + sizeText + L")";
    }

    SendMessage(m_hwndStatus, SB_SETTEXT, 1, (LPARAM)dbInfo.c_str());
    m_dbInfoNeedsRefresh = false;
}

void MainWindow::SetDatabasePath(const std::wstring& path) {
    m_dbPath = path;
    m_dbInfoNeedsRefresh = true;
    if (m_statusPartsConfigured) {
        UpdateStatusBarDbInfo();
    }
    ConfigureCloudSyncTimer();
}

void MainWindow::OnCommand(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(wParam)) {
    case IDM_NEW:
        CreateNewNote();
        break;
    case IDM_SAVE:
        SaveCurrentNote();
        break;
    case IDM_PRINT:
        PrintCurrentNote();
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
    case IDM_SETTINGS:
        CreateSettingsDialog(m_hwnd, m_db, m_dbPath);
        ConfigureCloudSyncTimer();
        break;
    case IDM_TAG_FILTER_BUTTON:
        {
            RECT rc;
            SendMessage(m_hwndToolbar, TB_GETRECT, IDM_TAG_FILTER_BUTTON, (LPARAM)&rc);
            MapWindowPoints(m_hwndToolbar, NULL, (LPPOINT)&rc, 2);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_TAG_NONE, L"<None>");
            
            std::vector<Database::Tag> tags = m_db->GetTags();
            std::map<int, int> counts = m_db->GetTagUsageCounts();
            
            for (const auto& tag : tags) {
                int count = 0;
                if (counts.find(tag.id) != counts.end()) {
                    count = counts[tag.id];
                }
                std::wstring itemText = tag.name + L" (" + std::to_wstring(count) + L")";
                AppendMenu(hMenu, MF_STRING, IDM_TAG_MENU_BASE + tag.id, itemText.c_str());
            }
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case IDM_MARKDOWN_BOLD:
        ApplyMarkdown(L"**", L"**");
        break;
    case IDM_MARKDOWN_ITALIC:
        ApplyMarkdown(L"*", L"*");
        break;
    case IDM_MARKDOWN_STRIKE:
        ApplyMarkdown(L"~~", L"~~");
        break;
    case IDM_MARKDOWN_H1:
        ApplyLineMarkdown(L"# ");
        break;
    case IDM_MARKDOWN_H2:
        ApplyLineMarkdown(L"## ");
        break;
    case IDM_MARKDOWN_H3:
        ApplyLineMarkdown(L"### ");
        break;
    case IDM_MARKDOWN_H4:
        ApplyLineMarkdown(L"#### ");
        break;
    case IDM_MARKDOWN_H5:
        ApplyLineMarkdown(L"##### ");
        break;
    case IDM_MARKDOWN_H6:
        ApplyLineMarkdown(L"###### ");
        break;
    case IDM_MARKDOWN_QUOTE:
        ApplyLineMarkdown(L"> ");
        break;
    case IDM_MARKDOWN_CODE:
        ApplyMarkdown(L"`", L"`");
        break;
    case IDM_MARKDOWN_CODEBLOCK:
        ApplyMarkdown(L"    ", L"");
        break;
    case IDM_MARKDOWN_LINK:
        ApplyMarkdown(L"[", L"](https://)");
        break;
    case IDM_MARKDOWN_UL:
        ApplyLineMarkdown(L"* ");
        break;
    case IDM_MARKDOWN_OL:
        ApplyLineMarkdown(L"1. ", true);
        break;
    case IDM_MARKDOWN_HR:
        ApplyLineMarkdown(L"---\n");
        break;
    case IDM_MARKDOWN_PREVIEW:
        ToggleMarkdownPreview();
        break;
    case IDM_MARKDOWN_SUBSCRIPT:
    case IDM_MARKDOWN_SUPERSCRIPT:
    case IDM_MARKDOWN_TABLE:
        // No-op for now (buttons are present but not implemented yet)
        break;
    case IDM_MARKDOWN_UNDO:
        SendMessage(m_hwndEdit, EM_UNDO, 0, 0);
        break;
    case IDM_MARKDOWN_REDO:
        SendMessage(m_hwndEdit, EM_REDO, 0, 0);
        break;
    case IDM_MARKDOWN_PARA:
        {
            RECT rc;
            SendMessage(m_hwndMarkdownToolbar, TB_GETRECT, IDM_MARKDOWN_PARA, (LPARAM)&rc);
            MapWindowPoints(m_hwndMarkdownToolbar, NULL, (LPPOINT)&rc, 2);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H1, L"Header 1");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H2, L"Header 2");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H3, L"Header 3");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H4, L"Header 4");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H5, L"Header 5");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H6, L"Header 6");
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case ID_RICHEDIT:
        if (HIWORD(wParam) == EN_CHANGE) {
            // If nothing is selected and we're editing a blank buffer, treat it as a new note for the current tag filter.
            if (!m_isNewNote && m_currentNoteId == -1) {
                m_isNewNote = true;
                m_newNoteTagId = m_selectedTagId;
                SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Entering new note mode for current tag filter");
            }

            m_isDirty = true;
            // Debug: Show that EN_CHANGE fired
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"EN_CHANGE: m_isDirty set to true");
            UpdateWindowTitle();
            ScheduleSpellCheck();
        }
        break;
    case ID_SEARCH:
        if (HIWORD(wParam) == EN_CHANGE) {
            int len = GetWindowTextLength(m_hwndSearch);
            std::vector<wchar_t> buf(len + 1);
            GetWindowText(m_hwndSearch, &buf[0], len + 1);
            
            std::string currentTerm = Utils::WideToUtf8(&buf[0]);
            
            // Track when search term changed for history saving
            if (currentTerm != m_lastSearchTerm) {
                // If previous term existed for 15+ seconds and was cleared, save it
                if (!m_lastSearchTerm.empty() && currentTerm.empty()) {
                    DWORD now = GetTickCount();
                    if (m_lastSearchChangeTime > 0 && (now - m_lastSearchChangeTime) >= 15000) {
                        SaveSearchHistory();
                    }
                }
                m_lastSearchTerm = currentTerm;
                m_lastSearchChangeTime = GetTickCount();
                m_searchHistoryPos = -1; // Reset history navigation when typing
            }
            
            bool autoSelect = !m_isNewNote; // avoid pulling focus to list while composing a new note
            LoadNotesList(&buf[0], m_searchTitleOnly, autoSelect);
        }
        break;
    case IDM_NOTE_TAG_BUTTON:
        {
            // Show context menu with tag options
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_TAG_CHANGE_BASE, L"<None>");
            
            std::vector<Database::Tag> tags = m_db->GetTags();
            for (const auto& tag : tags) {
                AppendMenu(hMenu, MF_STRING, IDM_TAG_CHANGE_BASE + tag.id, tag.name.c_str());
            }
            
            RECT rc;
            SendMessage(m_hwndMarkdownToolbar, TB_GETRECT, IDM_NOTE_TAG_BUTTON, (LPARAM)&rc);
            MapWindowPoints(m_hwndMarkdownToolbar, NULL, (LPPOINT)&rc, 2);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    
    // Handle Tag Change Commands
    if (LOWORD(wParam) >= IDM_TAG_CHANGE_BASE && LOWORD(wParam) < IDM_TAG_CHANGE_BASE + 1000) {
        int newTagId = LOWORD(wParam) - IDM_TAG_CHANGE_BASE;
        if (LOWORD(wParam) == IDM_TAG_CHANGE_BASE) newTagId = -1;
        
        // Store the selected tag (don't mark as dirty - tag changes save silently)
        m_currentNoteTagId = newTagId;
        
        // Update the button to show the new tag
        if (m_currentNoteId != -1 || m_isNewNote) {
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Tag changed");
            UpdateNoteTagCombo();
        }

        // Prevent tag-assignment commands from being treated as tag-filter commands
        return;
    }
    
    // Handle Color Commands
    if (LOWORD(wParam) >= IDM_COLOR_BASE && LOWORD(wParam) < IDM_COLOR_BASE + 100) {
        SetCurrentNoteColor(LOWORD(wParam) - IDM_COLOR_BASE);
    }

    // Handle Tag Filter Commands
    if (LOWORD(wParam) >= IDM_TAG_MENU_BASE && LOWORD(wParam) < IDM_TAG_MENU_BASE + 1000) {
        int tagId = LOWORD(wParam) - IDM_TAG_MENU_BASE;
        if (LOWORD(wParam) == IDM_TAG_NONE) tagId = -1;

        if (m_selectedTagId != tagId) {
            int oldNewNoteTag = m_newNoteTagId;
            int oldSelectedTag = m_selectedTagId;

            if (!PromptToSaveIfDirty(-1, false)) {
                m_newNoteTagId = oldNewNoteTag; // restore if cancelled
                m_selectedTagId = oldSelectedTag;
                return;
            }
            
            m_selectedTagId = tagId;
            m_db->SetSetting("SelectedTagId", std::to_string(tagId));
        }
        
        // Update button text
        std::wstring tagButtonText = L"<None>";
        if (tagId != -1) {
            std::vector<Database::Tag> tags = m_db->GetTags();
            for (const auto& tag : tags) {
                if (tag.id == tagId) {
                    tagButtonText = tag.name;
                    break;
                }
            }
        }
        
        TBBUTTONINFO tbbi;
        tbbi.cbSize = sizeof(TBBUTTONINFO);
        tbbi.dwMask = TBIF_TEXT;
        tbbi.pszText = (LPWSTR)tagButtonText.c_str();
        SendMessage(m_hwndToolbar, TB_SETBUTTONINFO, IDM_TAG_FILTER_BUTTON, (LPARAM)&tbbi);
        
        // Reload notes list
        int len = GetWindowTextLength(m_hwndSearch);
        std::vector<wchar_t> buf(len + 1);
        GetWindowText(m_hwndSearch, &buf[0], len + 1);
        LoadNotesList(&buf[0], m_searchTitleOnly, true);

        // Keep the markdown tag button in sync with the active filter, even if no notes match
        UpdateNoteTagCombo();
    }
}

LRESULT MainWindow::OnNotify(WPARAM wParam, LPARAM lParam) {
    LPNMHDR pnmh = (LPNMHDR)lParam;

    if (pnmh->idFrom == ID_RICHEDIT && pnmh->code == EN_SELCHANGE) {
        const SELCHANGE* sc = (const SELCHANGE*)lParam;
        if (sc->chrg.cpMin == sc->chrg.cpMax && m_spellCheckDeferred) {
            ScheduleSpellCheck();
            m_spellCheckDeferred = false;
        }
        return 0;
    }
    
    if (pnmh->code == TTN_GETDISPINFOW) {
        LPNMTTDISPINFOW pInfo = (LPNMTTDISPINFOW)lParam;
        pInfo->hinst = NULL;
        switch (pInfo->hdr.idFrom) {
            case IDM_NEW: wcscpy_s(pInfo->szText, L"New Note (Ctrl+N)"); break;
            case IDM_SAVE: wcscpy_s(pInfo->szText, L"Save Note (Ctrl+S)"); break;
            case IDM_PRINT: wcscpy_s(pInfo->szText, L"Print Note"); break;
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
            case IDM_SETTINGS: wcscpy_s(pInfo->szText, L"Settings"); break;
            case IDM_TAG_FILTER_LABEL: wcscpy_s(pInfo->szText, L"Filter by Tag"); break;
            case IDM_TAG_FILTER_BUTTON: wcscpy_s(pInfo->szText, L"Select Tag to Filter"); break;
            case IDM_MARKDOWN_BOLD: wcscpy_s(pInfo->szText, L"Bold"); break;
            case IDM_MARKDOWN_ITALIC: wcscpy_s(pInfo->szText, L"Italic"); break;
            case IDM_MARKDOWN_STRIKE: wcscpy_s(pInfo->szText, L"Strikethrough"); break;
            case IDM_MARKDOWN_PARA: wcscpy_s(pInfo->szText, L"Paragraph / Headers"); break;
            case IDM_MARKDOWN_H1: wcscpy_s(pInfo->szText, L"Header 1"); break;
            case IDM_MARKDOWN_H2: wcscpy_s(pInfo->szText, L"Header 2"); break;
            case IDM_MARKDOWN_H3: wcscpy_s(pInfo->szText, L"Header 3"); break;
            case IDM_MARKDOWN_H4: wcscpy_s(pInfo->szText, L"Header 4"); break;
            case IDM_MARKDOWN_H5: wcscpy_s(pInfo->szText, L"Header 5"); break;
            case IDM_MARKDOWN_H6: wcscpy_s(pInfo->szText, L"Header 6"); break;
            case IDM_MARKDOWN_QUOTE: wcscpy_s(pInfo->szText, L"Blockquote"); break;
            case IDM_MARKDOWN_OL: wcscpy_s(pInfo->szText, L"Numbered List"); break;
            case IDM_MARKDOWN_UL: wcscpy_s(pInfo->szText, L"Bullet List"); break;
            case IDM_MARKDOWN_LINK: wcscpy_s(pInfo->szText, L"Insert Link"); break;
            case IDM_MARKDOWN_HR: wcscpy_s(pInfo->szText, L"Horizontal Line"); break;
            case IDM_MARKDOWN_SUBSCRIPT: wcscpy_s(pInfo->szText, L"Subscript"); break;
            case IDM_MARKDOWN_SUPERSCRIPT: wcscpy_s(pInfo->szText, L"Superscript"); break;
            case IDM_MARKDOWN_TABLE: wcscpy_s(pInfo->szText, L"Insert Table"); break;
            case IDM_MARKDOWN_PREVIEW: wcscpy_s(pInfo->szText, L"View"); break;
            case IDM_MARKDOWN_UNDO: wcscpy_s(pInfo->szText, L"Undo"); break;
            case IDM_MARKDOWN_REDO: wcscpy_s(pInfo->szText, L"Redo"); break;
        }
        return 0;
    }

    if (pnmh->code == TBN_DROPDOWN) {
        LPNMTOOLBAR lpnmtb = (LPNMTOOLBAR)lParam;
        if (lpnmtb->iItem == IDM_MARKDOWN_PARA) {
            RECT rc;
            SendMessage(m_hwndMarkdownToolbar, TB_GETRECT, IDM_MARKDOWN_PARA, (LPARAM)&rc);
            MapWindowPoints(m_hwndMarkdownToolbar, NULL, (LPPOINT)&rc, 2);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H1, L"Header 1 (#)");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H2, L"Header 2 (##)");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H3, L"Header 3 (###)");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H4, L"Header 4 (####)");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H5, L"Header 5 (#####)");
            AppendMenu(hMenu, MF_STRING, IDM_MARKDOWN_H6, L"Header 6 (######)");
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
            return TBDDRET_DEFAULT;
        } else if (lpnmtb->iItem == IDM_TAG_FILTER_BUTTON) {
            RECT rc;
            SendMessage(m_hwndToolbar, TB_GETRECT, IDM_TAG_FILTER_BUTTON, (LPARAM)&rc);
            MapWindowPoints(m_hwndToolbar, NULL, (LPPOINT)&rc, 2);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, IDM_TAG_NONE, L"<None>");
            
            std::vector<Database::Tag> tags = m_db->GetTags();
            std::map<int, int> counts = m_db->GetTagUsageCounts();
            
            for (const auto& tag : tags) {
                int count = 0;
                if (counts.find(tag.id) != counts.end()) {
                    count = counts[tag.id];
                }
                std::wstring itemText = tag.name + L" (" + std::to_wstring(count) + L")";
                AppendMenu(hMenu, MF_STRING, IDM_TAG_MENU_BASE + tag.id, itemText.c_str());
            }
            
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
            return TBDDRET_DEFAULT;
        }
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
            if (m_isReloading) return 0;
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
            if (!m_db || m_db->GetSetting("clickable_links", "1") != "1") {
                return 0;
            }

            HWND src = pnmh->hwndFrom;
            std::wstring targetUrl;

            if (src == m_hwndPreview) {
                for (const auto& link : m_previewLinks) {
                    if (pLink->chrg.cpMin >= link.range.cpMin && pLink->chrg.cpMax <= link.range.cpMax) {
                        targetUrl = link.url;
                        break;
                    }
                }
            }

            if (targetUrl.empty()) {
                std::vector<wchar_t> url(pLink->chrg.cpMax - pLink->chrg.cpMin + 1);
                TEXTRANGE tr;
                tr.chrg = pLink->chrg;
                tr.lpstrText = &url[0];
                SendMessage(src, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                targetUrl = &url[0];
            }

            if (!targetUrl.empty()) {
                ShellExecute(NULL, L"open", targetUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
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
    m_isReloading = true;
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

        if (m_selectedTagId != -1) {
            std::vector<Database::Tag> noteTags = m_db->GetNoteTags(m_notes[i].id);
            bool hasTag = false;
            for (const auto& tag : noteTags) {
                if (tag.id == m_selectedTagId) {
                    hasTag = true;
                    break;
                }
            }
            if (!hasTag) match = false;
        }

        if (match && !filter.empty()) {
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

    m_isReloading = false;
}

void MainWindow::LoadNoteContent(int listIndex) {
    if (listIndex >= 0 && listIndex < (int)m_filteredIndices.size()) {
        int previousNoteId = m_currentNoteId;
        int realIndex = m_filteredIndices[listIndex];
        m_isNewNote = false;
        m_currentNoteIndex = realIndex;
        m_currentNoteId = m_notes[realIndex].id;
        m_lastCurrentNoteId = m_currentNoteId;
        m_currentNoteTagId = -2; // Reset pending tag change
        PersistLastViewedNote();

        bool renderOnOpen = false;
        if (m_db) {
            renderOnOpen = (m_db->GetSetting("render_on_open", "1") == "1");
        }

        // If the user is switching notes, only keep markdown preview active when render_on_open is enabled.
        if (previousNoteId != m_currentNoteId) {
            if (renderOnOpen) {
                m_markdownPreviewMode = true;
            } else {
                m_markdownPreviewMode = false;
            }
        }

        std::wstring wContent = Utils::Utf8ToWide(m_notes[realIndex].content);
        
        // Only update editor if content is different to preserve cursor/undo
        int len = GetWindowTextLength(m_hwndEdit);
        std::vector<wchar_t> currentBuf(len + 1);
        GetWindowText(m_hwndEdit, &currentBuf[0], len + 1);
        if (wContent != &currentBuf[0]) {
            SetWindowText(m_hwndEdit, wContent.c_str());
            ResetWordUndoState();
        }

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
        UpdateNoteTagCombo();

        if (m_markdownPreviewMode) {
            RenderMarkdownPreview();
        }

        if (!m_navigatingHistory) {
            RecordHistory(realIndex);
        }
        
        UpdateWindowTitle();
        ScheduleSpellCheck();
    } else {
        m_currentNoteIndex = -1;
        m_currentNoteId = -1;
        PersistLastViewedNote();
        m_markdownPreviewMode = false;
        SetWindowText(m_hwndEdit, L"");
        ResetWordUndoState();
        m_isDirty = false;
        m_isNewNote = false;
        m_checklistMode = false;
        
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
        SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
        UpdateChecklistUI();
        UpdateNoteTagCombo();
        UpdateWindowTitle();
        ScheduleSpellCheck();

        if (m_markdownPreviewMode) {
            RenderMarkdownPreview();
        }
    }
}

void MainWindow::PersistLastViewedNote() {
    if (!m_db) {
        return;
    }

    int noteToRemember = m_currentNoteId;
    if (noteToRemember == m_lastViewedNoteId) {
        return;
    }

    std::string value = (noteToRemember != -1) ? std::to_string(noteToRemember) : "-1";
    m_db->SetSetting("LastViewedNoteId", value);
    m_lastViewedNoteId = noteToRemember;
}

void MainWindow::ToggleMarkdownPreview() {
    if (m_checklistMode) {
        return;
    }

    m_markdownPreviewMode = !m_markdownPreviewMode;

    if (m_markdownPreviewMode) {
        RenderMarkdownPreview();
    }

    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);
    OnSize(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
}

void MainWindow::RenderMarkdownPreview() {
    if (!m_hwndPreview) {
        return;
    }

    bool clickableLinks = (m_db && m_db->GetSetting("clickable_links", "1") == "1");
    SendMessage(m_hwndPreview, EM_AUTOURLDETECT, clickableLinks ? TRUE : FALSE, 0);
    SendMessage(m_hwndPreview, EM_SETEVENTMASK, 0, clickableLinks ? ENM_LINK : 0);

    // Prefer current editor text (includes unsaved changes)
    int len = GetWindowTextLength(m_hwndEdit);
    std::vector<wchar_t> buf(len + 1);
    GetWindowText(m_hwndEdit, &buf[0], len + 1);
    std::wstring markdown = &buf[0];

    m_previewLinks.clear();
    SendMessage(m_hwndPreview, WM_SETREDRAW, FALSE, 0);
    SetWindowText(m_hwndPreview, L"");
    SendMessage(m_hwndPreview, EM_SETSEL, 0, 0);

    // Split into lines (keeping markdown's hard-break behavior separate from paragraph joining)
    std::vector<std::wstring> lines;
    {
        size_t start = 0;
        while (start <= markdown.size()) {
            size_t end = markdown.find(L'\n', start);
            std::wstring line = (end == std::wstring::npos) ? markdown.substr(start) : markdown.substr(start, end - start);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
    }

    // Track how much break spacing we most recently emitted at the end of the document.
    // 0 = none, 1 = ends with one CRLF, 2 = ends with blank line (CRLFCRLF)
    int endBreak = 0;

    auto markTextEmitted = [&]() {
        endBreak = 0;
    };

    auto emitNewlines = [&](int crlfPairs) {
        if (crlfPairs <= 0) {
            return;
        }

        // Clamp+accumulate: we only care about "has newline" vs "has blank line".
        endBreak = (endBreak + crlfPairs >= 2) ? 2 : (endBreak + crlfPairs);

        SendMessage(m_hwndPreview, EM_SETSEL, -1, -1);
        InlineRun nl;
        ApplyCharStyle(m_hwndPreview, nl, clickableLinks);

        if (crlfPairs >= 2) {
            SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n\r\n");
        } else {
            SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
        }
    };

    auto isQuoteLine = [&](const std::wstring& raw) {
        std::wstring t = TrimLeft(raw);
        return (!t.empty() && t[0] == L'>');
    };

    auto emitInline = [&](const std::wstring& s) {
        auto runs = ParseInlineMarkdown(s);
        for (const auto& run : runs) {
            SendMessage(m_hwndPreview, EM_SETSEL, -1, -1);
            ApplyCharStyle(m_hwndPreview, run, clickableLinks);

            LONG start = GetRichEditTextLength(m_hwndPreview);
            SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)run.text.c_str());
            LONG endPos = GetRichEditTextLength(m_hwndPreview);
            markTextEmitted();

            if (clickableLinks && run.link && !run.url.empty() && endPos > start) {
                PreviewLink pl;
                pl.range.cpMin = start;
                pl.range.cpMax = endPos;
                pl.url = EnsureUrlHasScheme(run.url);
                m_previewLinks.push_back(std::move(pl));
            }
        }
    };

    auto isPlainParagraphLine = [&](const std::wstring& rawLine) {
        std::wstring t = TrimLeft(rawLine);
        if (t.empty()) return false;
        if (IsHorizontalRule(t)) return false;
        // header
        if (!t.empty() && t[0] == L'#') {
            size_t k = 0;
            while (k < t.size() && t[k] == L'#') k++;
            if (k > 0 && k <= 6 && k < t.size() && t[k] == L' ') {
                return false;
            }
        }
        // blockquote
        if (!t.empty() && t[0] == L'>') return false;
        // lists
        if (t.size() >= 2 && (t[0] == L'-' || t[0] == L'*' || t[0] == L'+') && t[1] == L' ') return false;
        size_t numEnd = 0;
        while (numEnd < t.size() && iswdigit(t[numEnd])) numEnd++;
        if (numEnd > 0 && numEnd + 1 < t.size() && (t[numEnd] == L'.' || t[numEnd] == L')') && t[numEnd + 1] == L' ') return false;
        return true;
    };

    bool inParagraph = false;

    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const std::wstring& rawLine = lines[lineIndex];
        std::wstring trimmed = TrimLeft(rawLine);

        if (trimmed.empty()) {
            // blank line ends paragraph
            if (inParagraph) {
                emitNewlines(2);
                inParagraph = false;
            } else {
                emitNewlines(1);
            }
            continue;
        }

        // Non-paragraph blocks always break any open paragraph first
        if (!isPlainParagraphLine(rawLine)) {
            if (inParagraph) {
                emitNewlines(1);
                inParagraph = false;
            }

            // Reset paragraph formatting per block
            SendMessage(m_hwndPreview, EM_SETSEL, -1, -1);
            ApplyParaNormal(m_hwndPreview);

            if (IsHorizontalRule(trimmed)) {
                // Use a box-drawing character so the rule looks like a solid line (no visible gaps).
                std::wstring hr(72, L'\x2500'); // U+2500 BOX DRAWINGS LIGHT HORIZONTAL
                SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)hr.c_str());
                markTextEmitted();
                emitNewlines(1);
                continue;
            }

            // Header
            int headerLevel = 0;
            size_t i = 0;
            while (i < trimmed.size() && trimmed[i] == L'#' && headerLevel < 6) {
                headerLevel++;
                i++;
            }
            if (headerLevel > 0 && i < trimmed.size() && trimmed[i] == L' ') {
                std::wstring headerText = trimmed.substr(i + 1);
                ApplyHeaderCharStyle(m_hwndPreview, headerLevel);

                // Render header text and add a small bottom margin (one blank line) unless the
                // markdown already has a blank line next.
                SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)headerText.c_str());
                markTextEmitted();

                // End the header line. If the user wants extra space, a blank line in the markdown
                // will still produce it; otherwise keep the margin tight.
                emitNewlines(1);
                continue;
            }

            // Blockquote
            bool isQuote = false;
            if (!trimmed.empty() && trimmed[0] == L'>') {
                isQuote = true;
                trimmed = TrimLeft(trimmed.substr(1));
            }

            // Add paragraph-like spacing around a blockquote "block" (consecutive quote lines).
            // Only add margins when there is surrounding content; avoid adding at the start or end.
            bool quoteStart = false;
            bool quoteEnd = false;
            if (isQuote) {
                quoteStart = (lineIndex == 0) || !isQuoteLine(lines[lineIndex - 1]);
                quoteEnd = (lineIndex + 1 >= lines.size()) || !isQuoteLine(lines[lineIndex + 1]);

                if (quoteStart) {
                    // Ensure exactly one blank line (CRLFCRLF) before a quote block.
                    // Most blocks already end with one CRLF; in that common case we only add one more.
                    if (GetRichEditTextLength(m_hwndPreview) > 0 && endBreak < 2) {
                        emitNewlines(2 - endBreak);
                    }
                }
            }

            auto isListLine = [](std::wstring t) {
                t = TrimLeft(t);
                if (!t.empty() && t[0] == L'>') {
                    t = TrimLeft(t.substr(1));
                }

                if (t.size() >= 2 && (t[0] == L'-' || t[0] == L'*' || t[0] == L'+') && t[1] == L' ') {
                    return true;
                }

                size_t digits = 0;
                while (digits < t.size() && iswdigit(t[digits])) {
                    digits++;
                }
                if (digits > 0 && digits + 1 < t.size() && (t[digits] == L'.' || t[digits] == L')') && t[digits + 1] == L' ') {
                    return true;
                }
                return false;
            };

            // Ordered list
            bool isOrdered = false;
            size_t numEnd = 0;
            while (numEnd < trimmed.size() && iswdigit(trimmed[numEnd])) {
                numEnd++;
            }
            if (numEnd > 0 && numEnd + 1 < trimmed.size() && (trimmed[numEnd] == L'.' || trimmed[numEnd] == L')') && trimmed[numEnd + 1] == L' ') {
                isOrdered = true;
                trimmed = trimmed.substr(numEnd + 2);
            }

            // Unordered list
            bool isUnordered = false;
            if (!isOrdered && trimmed.size() >= 2 && (trimmed[0] == L'-' || trimmed[0] == L'*' || trimmed[0] == L'+') && trimmed[1] == L' ') {
                isUnordered = true;
                trimmed = trimmed.substr(2);
            }

            if (isQuote) {
                ApplyParaIndent(m_hwndPreview, 360, 0);
            }
            if (isOrdered) {
                ApplyParaBullets(m_hwndPreview, true);
            } else if (isUnordered) {
                ApplyParaBullets(m_hwndPreview, false);
            }

            emitInline(trimmed);

            bool isListItem = isOrdered || isUnordered;
            bool hasNextLine = (lineIndex + 1 < lines.size());
            bool nextIsListItem = false;
            if (isListItem && hasNextLine) {
                const std::wstring& nextRaw = lines[lineIndex + 1];
                nextIsListItem = isListLine(nextRaw);
            }

            // Only insert a list newline when needed; otherwise we leave the caret at the end of the last item
            // to avoid creating an extra empty numbered/bulleted paragraph.
            if (isListItem) {
                if (nextIsListItem) {
                    emitNewlines(1);
                } else if (hasNextLine) {
                    // End the list and reset formatting on the next paragraph.
                    emitNewlines(1);
                    ApplyParaNormal(m_hwndPreview);
                }
            } else {
                emitNewlines(1);
            }

            // If this was the end of a quote block and there is more content after it,
            // ensure there's a blank line below (quote "margin"), without affecting EOF.
            if (isQuote && quoteEnd && hasNextLine) {
                // Ensure exactly one blank line (CRLFCRLF) after a quote block.
                // At this point we've typically already emitted one CRLF for the quote line.
                if (endBreak < 2) {
                    emitNewlines(2 - endBreak);
                }
                // Reset formatting so the next block doesn't inherit quote indentation.
                ApplyParaNormal(m_hwndPreview);
            }
            continue;
        }

        // Plain paragraph line: join with spaces unless hard-break
        if (!inParagraph) {
            SendMessage(m_hwndPreview, EM_SETSEL, -1, -1);
            ApplyParaNormal(m_hwndPreview);
            inParagraph = true;
        }

        bool hardBreak = HasMarkdownHardBreak(rawLine);
        std::wstring content = rawLine;
        if (hardBreak) {
            // Remove trailing spaces used to signal break
            content = TrimRightSpaces(content);
        }
        std::wstring contentTrimLeft = TrimLeft(content);
        emitInline(contentTrimLeft);

        // Lookahead to determine join behavior
        bool nextIsParagraphLine = false;
        if (lineIndex + 1 < lines.size()) {
            const std::wstring& nextLine = lines[lineIndex + 1];
            nextIsParagraphLine = isPlainParagraphLine(nextLine);
            if (TrimLeft(nextLine).empty()) {
                nextIsParagraphLine = false;
            }
        }

        if (hardBreak) {
            emitNewlines(1);
        } else if (nextIsParagraphLine) {
            SendMessage(m_hwndPreview, EM_SETSEL, -1, -1);
            SendMessage(m_hwndPreview, EM_REPLACESEL, FALSE, (LPARAM)L" ");
            markTextEmitted();
        } else {
            emitNewlines(1);
            inParagraph = false;
        }
    }

    SendMessage(m_hwndPreview, EM_SETSEL, 0, 0);
    SendMessage(m_hwndPreview, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(m_hwndPreview, NULL, TRUE);
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
            // Prefer the tag that was active when the note was created; fall back to the current filter if needed.
            int tagToApply = (m_newNoteTagId != -1) ? m_newNoteTagId : m_selectedTagId;
            
            if (tagToApply != -1) {
                m_db->AddTagToNote(newNote.id, tagToApply);
            }
            
            m_isDirty = false;
            m_isNewNote = false;
            m_newNoteTagId = -1;
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Note saved");
            
            // Clear search and reload list to show the new note
            SetWindowText(m_hwndSearch, L"");
            m_currentSearchFilter = L"";
            
            // Reload with empty filter but current tag filter will be applied
            LoadNotesList(L"", false, autoSelectAfterSave, newNote.id);
            
            UpdateWindowTitle();
        } else {
            wchar_t errMsg[256];
            swprintf_s(errMsg, 256, L"ERROR: Failed to create note");
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)errMsg);
        }
        return;
    }

    // Check if there's a pending tag change even if content isn't dirty
    if (!m_isDirty && m_currentNoteTagId != -2 && m_currentNoteId != -1) {
        // Save only the tag change without prompting
        std::vector<Database::Tag> currentTags = m_db->GetNoteTags(m_currentNoteId);
        int currentTagId = !currentTags.empty() ? currentTags[0].id : -1;
        int newTagId = m_currentNoteTagId;
        
        if (currentTagId != newTagId) {
            if (currentTagId != -1) {
                m_db->RemoveTagFromNote(m_currentNoteId, currentTagId);
            }
            if (newTagId != -1) {
                m_db->AddTagToNote(m_currentNoteId, newTagId);
            }
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Tag saved");
            // Reload the list if a filter is active so the note disappears when it no longer matches
            if (m_selectedTagId != -1) {
                bool noteStillMatches = (newTagId == m_selectedTagId);
                int selectId = noteStillMatches ? m_currentNoteId : -1;
                LoadNotesList(m_currentSearchFilter, m_searchTitleOnly, true, selectId);
            }

            m_currentNoteTagId = -2;
            UpdateNoteTagCombo();
        }
        m_currentNoteTagId = -2;
        return;
    }

    if (m_isDirty) {
        int noteIdToSave = -1;
        int noteIndexToSave = -1;

        if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
            noteIdToSave = m_notes[m_currentNoteIndex].id;
            noteIndexToSave = m_currentNoteIndex;
            m_currentNoteId = noteIdToSave;
            m_lastCurrentNoteId = noteIdToSave;
        } else if (m_currentNoteId != -1) {
            noteIdToSave = m_currentNoteId;
            for (int idx = 0; idx < (int)m_notes.size(); ++idx) {
                if (m_notes[idx].id == noteIdToSave) {
                    noteIndexToSave = idx;
                    break;
                }
            }
        }

        // If still no note to save, try the last current note (in case it was filtered out)
        if (noteIdToSave == -1 && m_lastCurrentNoteId != -1) {
            noteIdToSave = m_lastCurrentNoteId;
            for (int idx = 0; idx < (int)m_notes.size(); ++idx) {
                if (m_notes[idx].id == noteIdToSave) {
                    noteIndexToSave = idx;
                    break;
                }
            }
        }

        if (noteIdToSave == -1) {
            // No known note to save.
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"ERROR: No note to save");
            return;
        }

        int len = GetWindowTextLength(m_hwndEdit);
        std::vector<wchar_t> buf(len + 1);
        GetWindowText(m_hwndEdit, &buf[0], len + 1);
        
        std::string content = Utils::WideToUtf8(&buf[0]);
        if (noteIndexToSave != -1) {
            m_notes[noteIndexToSave].content = content;
        }
        
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
        
        Note updateNote;
        updateNote.id = noteIdToSave;
        updateNote.title = newTitle;
        updateNote.content = content;

        if (noteIndexToSave != -1) {
            m_notes[noteIndexToSave].title = newTitle;
        }

        if (m_db->UpdateNote(updateNote)) {
            // Also update the tag if it was changed
            // Get current tag for this note
            std::vector<Database::Tag> currentTags = m_db->GetNoteTags(noteIdToSave);
            int currentTagId = !currentTags.empty() ? currentTags[0].id : -1;
            
            // Only update tag if there's a pending change
            if (m_currentNoteTagId != -2) {
                int newTagId = m_currentNoteTagId;
                
                // Remove old tag if it changed
                if (currentTagId != newTagId) {
                    if (currentTagId != -1) {
                        m_db->RemoveTagFromNote(noteIdToSave, currentTagId);
                    }
                    // Add new tag
                    if (newTagId != -1) {
                        m_db->AddTagToNote(noteIdToSave, newTagId);
                    }
                }
            }
            
            m_isDirty = false;
            m_currentNoteTagId = -2; // Reset pending tag change
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Note saved");
            
            // Refresh the notes list to update tag associations
            LoadNotesList(m_currentSearchFilter, m_searchTitleOnly, false, m_currentNoteId);
        } else {
            wchar_t errMsg[256];
            swprintf_s(errMsg, 256, L"ERROR: Failed to update note %d", noteIdToSave);
            SendMessage(m_hwndStatus, SB_SETTEXT, 0, (LPARAM)errMsg);
        }
        
        // Update list item text
        bool updatedListItem = false;
        for (int i = 0; i < (int)m_filteredIndices.size(); ++i) {
            int realIndex = m_filteredIndices[i];
            if (realIndex >= 0 && realIndex < (int)m_notes.size() && m_notes[realIndex].id == noteIdToSave) {
                std::wstring wTitle = Utils::Utf8ToWide(newTitle);
                ListView_SetItemText(m_hwndList, i, 0, (LPWSTR)wTitle.c_str());
                updatedListItem = true;
                break;
            }
        }

        // If the note isn't in the current filtered list, reload and try to keep selection.
        if (!updatedListItem) {
            LoadNotesList(m_currentSearchFilter, m_searchTitleOnly, autoSelectAfterSave, noteIdToSave);
        }
        
        UpdateWindowTitle();
    }
}

void MainWindow::CreateNewNote() {
    SaveCurrentNote();

    m_isNewNote = true;
    m_currentNoteIndex = -1;
    m_currentNoteId = -1;
    m_lastCurrentNoteId = -1;
    m_currentNoteTagId = -2;
    m_isDirty = false;
    m_checklistMode = false;
    m_newNoteTagId = m_selectedTagId; // Capture the current tag filter

    // Clear selection and editor
    ListView_SetItemState(m_hwndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    SetWindowText(m_hwndEdit, L"");
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_PIN, FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_ARCHIVE, FALSE);
    SendMessage(m_hwndToolbar, TB_CHECKBUTTON, IDM_TOGGLE_CHECKLIST, FALSE);
    UpdateChecklistUI();
    UpdateNoteTagCombo();

    // Clear search so user sees full list once saved later
    SetWindowText(m_hwndSearch, L"");
    SetFocus(m_hwndEdit);
    UpdateWindowTitle();
    ScheduleSpellCheck();
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
        ShowWindow(m_hwndPreview, SW_HIDE);
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
        if (m_markdownPreviewMode) {
            ShowWindow(m_hwndEdit, SW_HIDE);
            ShowWindow(m_hwndPreview, SW_SHOW);
        } else {
            ShowWindow(m_hwndEdit, SW_SHOW);
            ShowWindow(m_hwndPreview, SW_HIDE);
        }
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

void MainWindow::UpdateNoteTagCombo() {
    // Update the tag button text to show the current note's tag
    std::wstring buttonText = L"<None>";
    int tagToDisplay = -1;
    
    // If we have a pending tag change, show it
    if (m_currentNoteTagId != -2) {  // -2 means no pending change
        tagToDisplay = m_currentNoteTagId;  // -1 means <None> was selected
    } else if (m_currentNoteId != -1 && m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        std::vector<Database::Tag> noteTags = m_db->GetNoteTags(m_notes[m_currentNoteIndex].id);
        if (!noteTags.empty()) {
            tagToDisplay = noteTags[0].id; // Only one tag per note for now
        }
    } else if (m_isNewNote) {
        // For a new note, prefer the captured tag; if none captured yet, fall back to the active filter
        if (m_newNoteTagId == -1 && m_selectedTagId != -1) {
            m_newNoteTagId = m_selectedTagId;
        }
        tagToDisplay = m_newNoteTagId;
    } else if (m_selectedTagId != -1) {
        // When no note is selected (e.g., filter has zero results), show the active filter tag
        tagToDisplay = m_selectedTagId;
    }
    
    // Find the tag name
    if (tagToDisplay != -1) {
        std::vector<Database::Tag> tags = m_db->GetTags();
        for (const auto& tag : tags) {
            if (tag.id == tagToDisplay) {
                buttonText = tag.name;
                break;
            }
        }
    }
    
    // Update the toolbar button text
    TBBUTTONINFO tbbi = {};
    tbbi.cbSize = sizeof(tbbi);
    tbbi.dwMask = TBIF_TEXT;
    tbbi.pszText = (LPWSTR)buttonText.c_str();
    SendMessage(m_hwndMarkdownToolbar, TB_SETBUTTONINFO, IDM_NOTE_TAG_BUTTON, (LPARAM)&tbbi);
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

void MainWindow::PrintCurrentNote() {
    if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        const Note& note = m_notes[m_currentNoteIndex];
        
        // Prepare content to print
        std::string content;
        if (note.is_checklist) {
            content = note.title + "\n\n";
            for (const auto& item : note.checklist_items) {
                content += (item.is_checked ? "[x] " : "[ ] ") + item.item_text + "\n";
            }
        } else {
            content = note.content;
        }
        
        std::wstring wContent = Utils::Utf8ToWide(content);
        
        // Show print dialog
        PRINTDLG pd;
        ZeroMemory(&pd, sizeof(pd));
        pd.lStructSize = sizeof(pd);
        pd.hwndOwner = m_hwnd;
        pd.Flags = PD_RETURNDC | PD_NOSELECTION;
        
        if (PrintDlg(&pd) == TRUE) {
            HDC hPrinterDC = pd.hDC;
            
            // Start print job
            DOCINFO di;
            ZeroMemory(&di, sizeof(di));
            di.cbSize = sizeof(di);
            std::wstring wTitle = Utils::Utf8ToWide(note.title);
            di.lpszDocName = wTitle.c_str();
            
            if (StartDoc(hPrinterDC, &di) > 0) {
                if (StartPage(hPrinterDC) > 0) {
                    // Get printer capabilities
                    int pageWidth = GetDeviceCaps(hPrinterDC, HORZRES);
                    int pageHeight = GetDeviceCaps(hPrinterDC, VERTRES);
                    int margin = 100; // Margin in device units
                    
                    // Create font for printing
                    int fontHeight = -MulDiv(10, GetDeviceCaps(hPrinterDC, LOGPIXELSY), 72);
                    HFONT hPrintFont = CreateFont(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
                    
                    HFONT hOldFont = (HFONT)SelectObject(hPrinterDC, hPrintFont);
                    
                    // Get line height
                    TEXTMETRIC tm;
                    GetTextMetrics(hPrinterDC, &tm);
                    int lineHeight = tm.tmHeight + tm.tmExternalLeading;
                    
                    // Draw text line by line
                    RECT rcPrint;
                    rcPrint.left = margin;
                    rcPrint.top = margin;
                    rcPrint.right = pageWidth - margin;
                    rcPrint.bottom = pageHeight - margin;
                    
                    // Use DrawText with word wrapping
                    DrawText(hPrinterDC, wContent.c_str(), -1, &rcPrint,
                        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                    
                    SelectObject(hPrinterDC, hOldFont);
                    DeleteObject(hPrintFont);
                    
                    EndPage(hPrinterDC);
                }
                EndDoc(hPrinterDC);
            }
            
            DeleteDC(hPrinterDC);
        }
        
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
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

void MainWindow::OnTimer(UINT_PTR timerId) {
    if (timerId == ID_SPELLCHECK_TIMER) {
        KillTimer(m_hwnd, ID_SPELLCHECK_TIMER);
        RunSpellCheck();
        return;
    }
    if (timerId == ID_CLOUDSYNC_TIMER) {
        TriggerCloudSyncIfIdle();
    }
}

void MainWindow::ConfigureCloudSyncTimer() {
    if (!m_hwnd) {
        return;
    }
    KillTimer(m_hwnd, ID_CLOUDSYNC_TIMER);

    if (!m_db) {
        return;
    }
    if (m_db->GetSetting("cloud_sync_enabled", "0") != "1") {
        return;
    }
    if (m_dbPath.empty()) {
        return;
    }

    int minutes = 30;
    try {
        std::string s = m_db->GetSetting("cloud_sync_interval_minutes", "30");
        if (!s.empty()) {
            minutes = std::stoi(s);
        }
    } catch (...) {
        minutes = 30;
    }

    if (minutes <= 0) {
        return;
    }

    // Clamp to avoid overflow and silly values.
    if (minutes < 1) minutes = 1;
    if (minutes > 24 * 60) minutes = 24 * 60;

    const UINT intervalMs = (UINT)(minutes * 60u * 1000u);
    SetTimer(m_hwnd, ID_CLOUDSYNC_TIMER, intervalMs, NULL);
}

void MainWindow::TriggerCloudSyncIfIdle() {
    if (m_cloudSyncInProgress) {
        return;
    }
    if (!m_db || m_dbPath.empty()) {
        return;
    }
    if (m_db->GetSetting("cloud_sync_enabled", "0") != "1") {
        return;
    }

    const std::string clientId = m_db->GetSetting("cloud_oauth_client_id", "");
    if (clientId.empty()) {
        return;
    }

    // Skip if not connected.
    std::string refresh;
    if (!Credentials::ReadUtf8String(CloudSync::kCloudRefreshTokenCredTarget, refresh) || refresh.empty()) {
        return;
    }

    m_cloudSyncInProgress = true;
    auto* params = new CloudAutoSyncThreadParams();
    params->hwnd = m_hwnd;
    params->db = m_db;
    params->dbPath = m_dbPath;
    params->clientId = clientId;

    uintptr_t th = _beginthreadex(nullptr, 0, CloudAutoSyncThread, params, 0, nullptr);
    if (th == 0) {
        delete params;
        m_cloudSyncInProgress = false;
        return;
    }
    CloseHandle((HANDLE)th);
}

void MainWindow::ScheduleSpellCheck() {
    KillTimer(m_hwnd, ID_SPELLCHECK_TIMER);
    SetTimer(m_hwnd, ID_SPELLCHECK_TIMER, 600, NULL);
}

void MainWindow::RunSpellCheck() {
    if (!m_spellChecker || !m_spellChecker->IsReady()) {
        return;
    }

    // Get cursor position to check for active selection
    CHARRANGE cursorPos = {0};
    SendMessage(m_hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cursorPos);
    if (cursorPos.cpMin != cursorPos.cpMax) {
        m_spellCheckDeferred = true;
        return;
    }
    int cursorEnd = cursorPos.cpMax;

    // Use EM_GETTEXTLENGTHEX + EM_GETTEXTEX so text positions match RichEdit semantics
    GETTEXTLENGTHEX ltx = {};
    ltx.flags = GTL_DEFAULT;
    ltx.codepage = 1200; // UTF-16LE
    int textLen = (int)SendMessage(m_hwndEdit, EM_GETTEXTLENGTHEX, (WPARAM)&ltx, 0);

    std::wstring text;
    if (textLen > 0) {
        GETTEXTEX gtx = {};
        gtx.flags = GT_DEFAULT;
        gtx.codepage = 1200;
        gtx.cb = (textLen + 1) * (DWORD)sizeof(wchar_t);
        text.resize(textLen + 1);
        int actualLen = (int)SendMessage(m_hwndEdit, EM_GETTEXTEX, (WPARAM)&gtx, (LPARAM)&text[0]);
        if (actualLen > 0) {
            text.resize(actualLen);
        } else {
            text.clear();
        }
    }

    auto misses = m_spellChecker->FindMisspellings(text);

    // Filter out words that are incomplete (adjacent to cursor position)
    // Only underline words that are complete (followed by space/punctuation, not at cursor)
    std::vector<SpellChecker::Range> filteredMisses;
    for (const auto& miss : misses) {
        // Skip if word contains or is adjacent to cursor
        if (miss.start <= cursorEnd && cursorEnd <= miss.start + miss.length + 1) {
            continue;  // Don't mark incomplete words being typed
        }
        // Also verify the next character after word is a word boundary (space, punctuation, etc.)
        if (miss.start + miss.length < (LONG)text.size()) {
            wchar_t nextChar = text[miss.start + miss.length];
            if (iswalpha(nextChar)) {
                continue;  // Word not followed by boundary, likely incomplete
            }
        }
        filteredMisses.push_back(miss);
    }

    // Limit work to keep UI snappy
    const size_t kMaxMisses = 128;
    if (filteredMisses.size() > kMaxMisses) {
        filteredMisses.resize(kMaxMisses);
    }

    auto rangesEqual = [](const std::vector<SpellChecker::Range>& a, const std::vector<SpellChecker::Range>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].start != b[i].start || a[i].length != b[i].length) return false;
        }
        return true;
    };

    // If nothing changed, avoid extra redraws
    if (text == m_lastCheckedText && rangesEqual(filteredMisses, m_lastMisses)) {
        m_spellCheckDeferred = false;
        return;
    }

    m_lastCheckedText = text;
    m_lastMisses = filteredMisses;
    m_spellCheckDeferred = false;
    InvalidateRect(m_hwndEdit, NULL, FALSE);
}

bool MainWindow::PromptToSaveIfDirty(int preferredSelectNoteId, bool autoSelectAfterSave) {
    // Handle new dirty notes
    if (m_isNewNote && m_isDirty) {
        SaveCurrentNote(preferredSelectNoteId, autoSelectAfterSave);
        return true;
    }

    // No content edits, but pending tag change: prompt before switching
    if (!m_isDirty && m_currentNoteTagId != -2 && m_currentNoteId != -1) {
        int res = MessageBox(m_hwnd, L"Save tag change before switching notes?", L"Unsaved Tag Change", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDCANCEL) {
            return false;
        }
        if (res == IDYES) {
            SaveCurrentNote(preferredSelectNoteId, autoSelectAfterSave);
            return true;
        }
        // Discard tag change
        m_currentNoteTagId = -2;
        UpdateNoteTagCombo();
        return true;
    }

    // No unsaved changes
    if (!m_isDirty) {
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
    // Discard - reset any pending tag change
    m_isDirty = false;
    m_currentNoteTagId = -2;
    UpdateNoteTagCombo(); // Revert button text to original tag
    return true;
}

LRESULT CALLBACK MainWindow::RichEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData) {
    MainWindow* self = reinterpret_cast<MainWindow*>(refData);
    if (!self) {
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
    case WM_LBUTTONDBLCLK:
        if (!self->m_markdownPreviewMode && !self->m_checklistMode && self->m_db && self->m_db->GetSetting("double_click_markdown", "0") == "1") {
            self->ToggleMarkdownPreview();
            return 0;
        }
        break;
    case WM_PAINT: {
        LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            self->DrawSpellUnderlines(hdc);
            ReleaseDC(hwnd, hdc);
        }
        return res;
    }
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (wParam == 'Z') {
                self->FinalizeCurrentWord();
                if (self->PerformWordUndo()) {
                    return 0;
                }
            } else if (wParam == 'Y') {
                self->FinalizeCurrentWord();
                if (self->PerformWordRedo()) {
                    return 0;
                }
            } else if (wParam == 'S') {
                // Ensure Ctrl+S saves even if the global hotkey is missed.
                self->FinalizeCurrentWord();
                // Debug: Show Ctrl+S was pressed
                SendMessage(self->m_hwndStatus, SB_SETTEXT, 0, (LPARAM)L"Ctrl+S pressed in RichEdit");
                self->SaveCurrentNote();
                return 0;
            }
        } else if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN ||
                   wParam == VK_HOME || wParam == VK_END || wParam == VK_SPACE || wParam == VK_TAB ||
                   wParam == VK_RETURN) {
            self->FinalizeCurrentWord();
        }
        break;
    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
        if (ch == 0x08) {
            if (!self->m_currentWord.empty()) {
                self->m_currentWord.pop_back();
                if (self->m_currentWord.empty()) {
                    self->m_currentWordStart = -1;
                }
            }
        } else if (iswspace(ch)) {
            self->FinalizeCurrentWord();
        } else if (iswgraph(ch)) {
            if (self->m_currentWord.empty()) {
                CHARRANGE cr;
                SendMessage(hwnd, EM_EXGETSEL, 0, (LPARAM)&cr);
                self->m_currentWordStart = cr.cpMin;
            }
            self->m_currentWord.push_back(ch);
            self->m_wordRedoStack.clear();
        } else {
            self->FinalizeCurrentWord();
        }
        break;
    }
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    case WM_SIZE:
        {
            LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
            HDC hdc = GetDC(hwnd);
            if (hdc) {
                self->DrawSpellUnderlines(hdc);
                ReleaseDC(hwnd, hdc);
            }
            return res;
        }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

POINT MainWindow::GetCharPosition(int index) const {
    POINT pt = {0, 0};
    LRESULT res = SendMessage(m_hwndEdit, EM_POSFROMCHAR, index, 0);
    if (res != -1) {
        pt.x = LOWORD(res);
        pt.y = HIWORD(res);
    }
    return pt;
}

void MainWindow::DrawSpellUnderlines(HDC hdc) const {
    if (m_lastMisses.empty() || hdc == NULL) {
        return;
    }

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(200, 0, 0));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HFONT font = (HFONT)SendMessage(m_hwndEdit, WM_GETFONT, 0, 0);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int underlineY = tm.tmAscent + 2;
    int safeTextLen = (int)m_lastCheckedText.size();

    for (const auto& miss : m_lastMisses) {
        int start = miss.start;
        int end = start + miss.length;
        while (start < end) {
            int line = (int)SendMessage(m_hwndEdit, EM_LINEFROMCHAR, start, 0);
            if (line == -1) {
                break;
            }
            (void)SendMessage(m_hwndEdit, EM_LINEINDEX, line, 0);
            int nextLineStart = (int)SendMessage(m_hwndEdit, EM_LINEINDEX, line + 1, 0);
            if (nextLineStart == -1) {
                nextLineStart = safeTextLen;
            }
            int segmentEnd = (end < nextLineStart) ? end : nextLineStart;

            POINT pStart = GetCharPosition(start);
            POINT pEnd;
            if (segmentEnd < safeTextLen) {
                pEnd = GetCharPosition(segmentEnd);
            } else if (segmentEnd > start) {
                pEnd = GetCharPosition(segmentEnd - 1);
                if (segmentEnd - 1 >= 0 && segmentEnd - 1 < safeTextLen) {
                    SIZE sz = {0};
                    const wchar_t ch = m_lastCheckedText[segmentEnd - 1];
                    GetTextExtentPoint32(hdc, &ch, 1, &sz);
                    pEnd.x += sz.cx;
                }
            } else {
                pEnd = pStart;
            }

            int y = pStart.y + underlineY;
            MoveToEx(hdc, pStart.x, y, NULL);
            LineTo(hdc, pEnd.x, y);

            start = segmentEnd;
        }
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void MainWindow::ResetWordUndoState() {
    m_wordUndoStack.clear();
    m_wordRedoStack.clear();
    m_currentWord.clear();
    m_currentWordStart = -1;
}

void MainWindow::FinalizeCurrentWord() {
    if (m_currentWord.empty()) {
        m_currentWordStart = -1;
        return;
    }

    LONG start = m_currentWordStart;
    if (start < 0) {
        CHARRANGE cr;
        SendMessage(m_hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        start = cr.cpMin - (LONG)m_currentWord.size();
        if (start < 0) start = 0;
    }

    WordAction action;
    action.start = start;
    action.text = m_currentWord;
    m_wordUndoStack.push_back(action);
    m_wordRedoStack.clear();
    m_currentWord.clear();
    m_currentWordStart = -1;
}

bool MainWindow::PerformWordUndo() {
    if (m_wordUndoStack.empty()) {
        return false;
    }

    WordAction action = m_wordUndoStack.back();
    m_wordUndoStack.pop_back();

    LRESULT textLen = SendMessage(m_hwndEdit, WM_GETTEXTLENGTH, 0, 0);
    LONG start = action.start;
    if (start < 0) start = 0;
    if (start > textLen) start = (LONG)textLen;

    LONG end = start + (LONG)action.text.size();
    if (end > textLen) {
        end = (LONG)textLen;
    }

    CHARRANGE cr = { start, end };
    SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    m_wordRedoStack.push_back(action);
    m_currentWord.clear();
    m_currentWordStart = -1;
    InvalidateRect(m_hwndEdit, NULL, TRUE);
    return true;
}

bool MainWindow::PerformWordRedo() {
    if (m_wordRedoStack.empty()) {
        return false;
    }

    WordAction action = m_wordRedoStack.back();
    m_wordRedoStack.pop_back();

    LRESULT textLen = SendMessage(m_hwndEdit, WM_GETTEXTLENGTH, 0, 0);
    LONG start = action.start;
    if (start < 0) start = 0;
    if (start > textLen) start = (LONG)textLen;

    CHARRANGE cr = { start, start };
    SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)action.text.c_str());
    m_wordUndoStack.push_back(action);
    m_currentWord.clear();
    m_currentWordStart = -1;
    InvalidateRect(m_hwndEdit, NULL, TRUE);
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

void MainWindow::UpdateWindowTitle() {
    std::wstring title = L"Note So Fast";
    
    if (m_isNewNote) {
        title += L" - Untitled Note *";
    } else if (m_currentNoteIndex >= 0 && m_currentNoteIndex < (int)m_notes.size()) {
        title += L" - ";
        title += Utils::Utf8ToWide(m_notes[m_currentNoteIndex].title);
        if (m_isDirty) {
            title += L" *";
        }
    }
    
    SetWindowText(m_hwnd, title.c_str());
}

void MainWindow::ApplyMarkdown(const std::wstring& prefix, const std::wstring& suffix) {
    CHARRANGE cr;
    SendMessage(m_hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    
    if (cr.cpMin == cr.cpMax) {
        std::wstring text = prefix + suffix;
        SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)text.c_str());
        cr.cpMin += (LONG)prefix.length();
        cr.cpMax = cr.cpMin;
        SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    } else {
        int len = cr.cpMax - cr.cpMin;
        wchar_t* buf = new wchar_t[len + 1];
        
        TEXTRANGE tr;
        tr.chrg = cr;
        tr.lpstrText = buf;
        SendMessage(m_hwndEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        
        std::wstring selectedText = buf;
        delete[] buf;
        
        std::wstring newText = prefix + selectedText + suffix;
        SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)newText.c_str());
    }
    SetFocus(m_hwndEdit);
}

void MainWindow::ApplyLineMarkdown(const std::wstring& prefix, bool sequential) {
    CHARRANGE cr;
    SendMessage(m_hwndEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

    if (cr.cpMin == cr.cpMax) {
        std::wstring linePrefix = sequential ? (std::to_wstring(1) + L". ") : prefix;

        LRESULT lineIndex = SendMessage(m_hwndEdit, EM_EXLINEFROMCHAR, 0, cr.cpMin);
        LRESULT lineStart = SendMessage(m_hwndEdit, EM_LINEINDEX, lineIndex, 0);

        CHARRANGE lineCr;
        lineCr.cpMin = (LONG)lineStart;
        lineCr.cpMax = (LONG)lineStart;
        SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&lineCr);
        SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)linePrefix.c_str());

        cr.cpMin += (LONG)linePrefix.length();
        cr.cpMax += (LONG)linePrefix.length();
        SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SetFocus(m_hwndEdit);
        return;
    }

    int textLen = (int)SendMessage(m_hwndEdit, WM_GETTEXTLENGTH, 0, 0);
    int startLine = (int)SendMessage(m_hwndEdit, EM_EXLINEFROMCHAR, 0, cr.cpMin);
    int endChar = (cr.cpMax > 0) ? cr.cpMax - 1 : cr.cpMax;
    int endLine = (int)SendMessage(m_hwndEdit, EM_EXLINEFROMCHAR, 0, endChar);

    struct LineInsert { int pos; std::wstring text; };
    std::vector<LineInsert> inserts;
    int counter = 1;

    for (int line = startLine; line <= endLine; ++line) {
        int lineStart = (int)SendMessage(m_hwndEdit, EM_LINEINDEX, line, 0);
        if (lineStart == -1) continue;
        int nextLineStart = (int)SendMessage(m_hwndEdit, EM_LINEINDEX, line + 1, 0);
        if (nextLineStart == -1) {
            nextLineStart = textLen;
        }

        int overlapStart = (lineStart > (int)cr.cpMin) ? lineStart : (int)cr.cpMin;
        int overlapEnd = (nextLineStart < (int)cr.cpMax) ? nextLineStart : (int)cr.cpMax;
        if (overlapEnd <= overlapStart) {
            continue;
        }

        std::wstring linePrefix = sequential ? (std::to_wstring(counter++) + L". ") : prefix;
        LineInsert insert;
        insert.pos = lineStart;
        insert.text = linePrefix;
        inserts.push_back(insert);
    }

    int shiftBeforeStart = 0;
    int shiftBeforeEnd = 0;
    for (const auto& insert : inserts) {
        if (insert.pos <= cr.cpMin) {
            shiftBeforeStart += (int)insert.text.length();
        }
        if (insert.pos < cr.cpMax) {
            shiftBeforeEnd += (int)insert.text.length();
        }
    }

    for (auto it = inserts.rbegin(); it != inserts.rend(); ++it) {
        SendMessage(m_hwndEdit, EM_SETSEL, it->pos, it->pos);
        SendMessage(m_hwndEdit, EM_REPLACESEL, TRUE, (LPARAM)it->text.c_str());
    }

    cr.cpMin += shiftBeforeStart;
    cr.cpMax += shiftBeforeEnd;
    SendMessage(m_hwndEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SetFocus(m_hwndEdit);
}

void MainWindow::SaveSearchHistory() {
    if (!m_lastSearchTerm.empty()) {
        if (m_db->AddSearchHistory(m_lastSearchTerm)) {
            // Reload search history from database
            m_searchHistory = m_db->GetSearchHistory();
            m_searchHistoryPos = -1;
        }
    }
}

void MainWindow::NavigateSearchHistory(int offset) {
    if (m_searchHistory.empty()) {
        return;
    }
    
    // Initialize position if not set
    if (m_searchHistoryPos == -1) {
        if (offset < 0) {
            m_searchHistoryPos = 0;
        } else {
            return; // Can't go forward from current position
        }
    } else {
        int newPos = m_searchHistoryPos + offset;
        if (newPos < 0 || newPos >= (int)m_searchHistory.size()) {
            return; // Out of bounds
        }
        m_searchHistoryPos = newPos;
    }
    
    // Set the search box to the historical term
    std::wstring wTerm = Utils::Utf8ToWide(m_searchHistory[m_searchHistoryPos]);
    SetWindowText(m_hwndSearch, wTerm.c_str());
    
    // Move cursor to end
    SendMessage(m_hwndSearch, EM_SETSEL, wTerm.length(), wTerm.length());
}
