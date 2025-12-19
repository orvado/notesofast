#include "settings_dialog.h"
#include "resource.h"
#include "utils.h"
#include <commctrl.h>
#include <vector>
#include <string>

// Forward declarations of procedures
INT_PTR CALLBACK SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AppearanceTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MarkdownTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TagsTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

struct SettingsData {
    HWND hTab;
    HWND hPages[3];
    int currentPage;
    Database* db;
};

void CreateSettingsDialog(HWND hWndParent, Database* db) {
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hWndParent, SettingsDialogProc, (LPARAM)db);
}

void OnSelChanged(HWND hDlg, SettingsData* pData) {
    int iSel = TabCtrl_GetCurSel(pData->hTab);
    
    for (int i = 0; i < 3; i++) {
        if (i == iSel) {
            ShowWindow(pData->hPages[i], SW_SHOW);
        } else {
            ShowWindow(pData->hPages[i], SW_HIDE);
        }
    }
    pData->currentPage = iSel;
}

INT_PTR CALLBACK SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    switch (message) {
    case WM_INITDIALOG:
        {
            pData = new SettingsData();
            pData->db = (Database*)lParam;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, (LONG_PTR)pData);

            pData->hTab = GetDlgItem(hDlg, IDC_TAB_SETTINGS);
            
            TCITEM tie;
            tie.mask = TCIF_TEXT;
            
            tie.pszText = (LPWSTR)L"Appearance";
            TabCtrl_InsertItem(pData->hTab, 0, &tie);
            tie.pszText = (LPWSTR)L"Markdown";
            TabCtrl_InsertItem(pData->hTab, 1, &tie);
            tie.pszText = (LPWSTR)L"Tags";
            TabCtrl_InsertItem(pData->hTab, 2, &tie);

            RECT rcTab;
            GetWindowRect(pData->hTab, &rcTab);
            TabCtrl_AdjustRect(pData->hTab, FALSE, &rcTab);
            MapWindowPoints(NULL, hDlg, (LPPOINT)&rcTab, 2);

            pData->hPages[0] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_APPEARANCE), hDlg, AppearanceTabProc, (LPARAM)pData);
            pData->hPages[1] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_MARKDOWN), hDlg, MarkdownTabProc, (LPARAM)pData);
            pData->hPages[2] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_TAGS), hDlg, TagsTabProc, (LPARAM)pData);

            for (int i = 0; i < 3; i++) {
                MoveWindow(pData->hPages[i], rcTab.left, rcTab.top, rcTab.right - rcTab.left, rcTab.bottom - rcTab.top, FALSE);
            }

            pData->currentPage = 0;
            OnSelChanged(hDlg, pData);
        }
        return (INT_PTR)TRUE;

    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == IDC_TAB_SETTINGS && pnmh->code == TCN_SELCHANGE) {
                OnSelChanged(hDlg, pData);
            }
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, 0);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        if (pData) {
            for (int i = 0; i < 3; i++) {
                if (pData->hPages[i]) DestroyWindow(pData->hPages[i]);
            }
            delete pData;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK AppearanceTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    switch (message) {
    case WM_INITDIALOG:
        {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pData = (SettingsData*)lParam;
            
            // Theme
            std::string theme = pData->db->GetSetting("theme", "system");
            if (theme == "light") CheckDlgButton(hDlg, IDC_RADIO_THEME_LIGHT, BST_CHECKED);
            else if (theme == "dark") CheckDlgButton(hDlg, IDC_RADIO_THEME_DARK, BST_CHECKED);
            else CheckDlgButton(hDlg, IDC_RADIO_THEME_SYSTEM, BST_CHECKED);

            // Font Face
            HWND hComboFont = GetDlgItem(hDlg, IDC_COMBO_FONT_FACE);
            const wchar_t* fonts[] = { L"Segoe UI", L"Arial", L"Courier New", L"Consolas", L"Georgia", L"Times New Roman", L"Verdana" };
            for (auto f : fonts) SendMessage(hComboFont, CB_ADDSTRING, 0, (LPARAM)f);
            
            std::string fontFace = pData->db->GetSetting("font_face", "Segoe UI");
            std::wstring wFontFace = Utils::Utf8ToWide(fontFace);
            SendMessage(hComboFont, CB_SELECTSTRING, -1, (LPARAM)wFontFace.c_str());

            // Font Size
            HWND hComboSize = GetDlgItem(hDlg, IDC_COMBO_FONT_SIZE);
            const wchar_t* sizes[] = { L"8", L"9", L"10", L"11", L"12", L"14", L"16", L"18", L"20", L"22", L"24", L"26", L"28", L"36", L"48", L"72" };
            for (auto s : sizes) SendMessage(hComboSize, CB_ADDSTRING, 0, (LPARAM)s);
            
            std::string fontSize = pData->db->GetSetting("font_size", "10");
            std::wstring wFontSize = Utils::Utf8ToWide(fontSize);
            SendMessage(hComboSize, CB_SELECTSTRING, -1, (LPARAM)wFontSize.c_str());

            // Checkboxes
            CheckDlgButton(hDlg, IDC_CHECK_CLICKABLE_LINKS, pData->db->GetSetting("clickable_links", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_CLICKABLE_EMAILS, pData->db->GetSetting("clickable_emails", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            if (!pData) break;
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);
            
            if (wmEvent == BN_CLICKED) {
                if (wmId == IDC_RADIO_THEME_LIGHT) {
                    pData->db->SetSetting("theme", "light");
                    CheckRadioButton(hDlg, IDC_RADIO_THEME_LIGHT, IDC_RADIO_THEME_SYSTEM, IDC_RADIO_THEME_LIGHT);
                }
                else if (wmId == IDC_RADIO_THEME_DARK) {
                    pData->db->SetSetting("theme", "dark");
                    CheckRadioButton(hDlg, IDC_RADIO_THEME_LIGHT, IDC_RADIO_THEME_SYSTEM, IDC_RADIO_THEME_DARK);
                }
                else if (wmId == IDC_RADIO_THEME_SYSTEM) {
                    pData->db->SetSetting("theme", "system");
                    CheckRadioButton(hDlg, IDC_RADIO_THEME_LIGHT, IDC_RADIO_THEME_SYSTEM, IDC_RADIO_THEME_SYSTEM);
                }
                else if (wmId == IDC_CHECK_CLICKABLE_LINKS) pData->db->SetSetting("clickable_links", IsDlgButtonChecked(hDlg, IDC_CHECK_CLICKABLE_LINKS) == BST_CHECKED ? "1" : "0");
                else if (wmId == IDC_CHECK_CLICKABLE_EMAILS) pData->db->SetSetting("clickable_emails", IsDlgButtonChecked(hDlg, IDC_CHECK_CLICKABLE_EMAILS) == BST_CHECKED ? "1" : "0");
            } else if (wmEvent == CBN_SELCHANGE) {
                if (wmId == IDC_COMBO_FONT_FACE) {
                    wchar_t buf[256];
                    GetDlgItemText(hDlg, IDC_COMBO_FONT_FACE, buf, 256);
                    pData->db->SetSetting("font_face", Utils::WideToUtf8(buf));
                } else if (wmId == IDC_COMBO_FONT_SIZE) {
                    wchar_t buf[256];
                    GetDlgItemText(hDlg, IDC_COMBO_FONT_SIZE, buf, 256);
                    pData->db->SetSetting("font_size", Utils::WideToUtf8(buf));
                }
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK MarkdownTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);

    switch (message) {
    case WM_INITDIALOG:
        {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pData = (SettingsData*)lParam;

            CheckDlgButton(hDlg, IDC_CHECK_USE_MARKDOWN, pData->db->GetSetting("use_markdown", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_SHOW_FORMAT_MENU, pData->db->GetSetting("show_format_menu", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_RENDER_ON_OPEN, pData->db->GetSetting("render_on_open", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_DOUBLE_CLICK_EDIT, pData->db->GetSetting("double_click_edit", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHECK_DOUBLE_CLICK_MARKDOWN, pData->db->GetSetting("double_click_markdown", "0") == "1" ? BST_CHECKED : BST_UNCHECKED);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            if (!pData) break;
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmEvent == BN_CLICKED) {
                if (wmId == IDC_CHECK_USE_MARKDOWN) pData->db->SetSetting("use_markdown", IsDlgButtonChecked(hDlg, IDC_CHECK_USE_MARKDOWN) == BST_CHECKED ? "1" : "0");
                else if (wmId == IDC_CHECK_SHOW_FORMAT_MENU) pData->db->SetSetting("show_format_menu", IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_FORMAT_MENU) == BST_CHECKED ? "1" : "0");
                else if (wmId == IDC_CHECK_RENDER_ON_OPEN) pData->db->SetSetting("render_on_open", IsDlgButtonChecked(hDlg, IDC_CHECK_RENDER_ON_OPEN) == BST_CHECKED ? "1" : "0");
                else if (wmId == IDC_CHECK_DOUBLE_CLICK_EDIT) pData->db->SetSetting("double_click_edit", IsDlgButtonChecked(hDlg, IDC_CHECK_DOUBLE_CLICK_EDIT) == BST_CHECKED ? "1" : "0");
                else if (wmId == IDC_CHECK_DOUBLE_CLICK_MARKDOWN) pData->db->SetSetting("double_click_markdown", IsDlgButtonChecked(hDlg, IDC_CHECK_DOUBLE_CLICK_MARKDOWN) == BST_CHECKED ? "1" : "0");
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}

WNDPROC g_oldTagEditProc = NULL;

LRESULT CALLBACK TagEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        HWND hDlg = GetParent(hwnd);
        SetWindowText(hwnd, L"");
        SetWindowText(GetDlgItem(hDlg, IDC_BUTTON_ADD_EDIT_TAG), L"Add");
        return 0;
    }
    return CallWindowProc(g_oldTagEditProc, hwnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK TagsTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    HWND hList = GetDlgItem(hDlg, IDC_LIST_TAGS);
    static int editingIdx = -1;

    switch (message) {
    case WM_INITDIALOG:
        {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pData = (SettingsData*)lParam;
            editingIdx = -1;

            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            
            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.pszText = (LPWSTR)L"Tag Name";
            lvc.cx = 140;
            ListView_InsertColumn(hList, 0, &lvc);
            
            lvc.pszText = (LPWSTR)L"Usages";
            lvc.cx = 60;
            ListView_InsertColumn(hList, 1, &lvc);

            lvc.pszText = (LPWSTR)L"ID";
            lvc.cx = 0; // Hidden ID column
            ListView_InsertColumn(hList, 2, &lvc);

            // Subclass the edit box for ESC key
            HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_TAG);
            g_oldTagEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)TagEditProc);

            // Load tags from DB
            if (pData && pData->db) {
                std::vector<Database::Tag> tags = pData->db->GetTags();
                for (const auto& tag : tags) {
                    LVITEM lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.pszText = (LPWSTR)tag.name.c_str();
                    lvi.iItem = ListView_GetItemCount(hList);
                    int idx = ListView_InsertItem(hList, &lvi);
                    
                    ListView_SetItemText(hList, idx, 1, (LPWSTR)L"0"); // Usages

                    wchar_t szId[16];
                    swprintf(szId, 16, L"%d", tag.id);
                    ListView_SetItemText(hList, idx, 2, szId);
                }
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            if (!pData) break;
            int wmId = LOWORD(wParam);
            if (wmId == IDC_BUTTON_ADD_EDIT_TAG) {
                wchar_t buf[256];
                GetDlgItemText(hDlg, IDC_EDIT_TAG, buf, 256);
                if (wcslen(buf) > 0) {
                    if (editingIdx == -1) {
                        // Add
                        Database::Tag tag;
                        tag.name = buf;
                        tag.order = ListView_GetItemCount(hList);
                        if (pData->db->CreateTag(tag)) {
                            LVITEM lvi = {0};
                            lvi.mask = LVIF_TEXT;
                            lvi.pszText = buf;
                            lvi.iItem = ListView_GetItemCount(hList);
                            int idx = ListView_InsertItem(hList, &lvi);
                            
                            ListView_SetItemText(hList, idx, 1, (LPWSTR)L"0"); // Usages

                            wchar_t szId[16];
                            swprintf(szId, 16, L"%d", tag.id);
                            ListView_SetItemText(hList, idx, 2, szId);
                        }
                    } else {
                        // Edit
                        wchar_t szId[16];
                        ListView_GetItemText(hList, editingIdx, 2, szId, 16);
                        int id = _wtoi(szId);
                        
                        Database::Tag tag;
                        tag.id = id;
                        tag.name = buf;
                        if (pData->db->UpdateTag(tag)) {
                            ListView_SetItemText(hList, editingIdx, 0, buf);
                            editingIdx = -1;
                            SetDlgItemText(hDlg, IDC_BUTTON_ADD_EDIT_TAG, L"Add");
                        }
                    }
                    SetDlgItemText(hDlg, IDC_EDIT_TAG, L"");
                }
            } else if (wmId == IDC_BUTTON_DELETE_TAG) {
                int idx = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                if (idx != -1) {
                    wchar_t szId[16];
                    ListView_GetItemText(hList, idx, 2, szId, 16);
                    int id = _wtoi(szId);
                    if (pData->db->DeleteTag(id)) {
                        ListView_DeleteItem(hList, idx);
                        editingIdx = -1;
                        SetDlgItemText(hDlg, IDC_BUTTON_ADD_EDIT_TAG, L"Add");
                        SetDlgItemText(hDlg, IDC_EDIT_TAG, L"");
                        
                        // Update orders
                        for (int i = 0; i < ListView_GetItemCount(hList); i++) {
                            ListView_GetItemText(hList, i, 2, szId, 16);
                            pData->db->ReorderTag(_wtoi(szId), i);
                        }
                    }
                }
            } else if (wmId == IDC_BUTTON_UP_TAG || wmId == IDC_BUTTON_DOWN_TAG) {
                int idx = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                if (idx != -1) {
                    int newIdx = (wmId == IDC_BUTTON_UP_TAG) ? idx - 1 : idx + 1;
                    if (newIdx >= 0 && newIdx < ListView_GetItemCount(hList)) {
                        wchar_t text0[256], text1[256], text2[256];
                        ListView_GetItemText(hList, idx, 0, text0, 256);
                        ListView_GetItemText(hList, idx, 1, text1, 256);
                        ListView_GetItemText(hList, idx, 2, text2, 256);
                        
                        ListView_DeleteItem(hList, idx);
                        
                        LVITEM lvi = {0};
                        lvi.mask = LVIF_TEXT | LVIF_STATE;
                        lvi.pszText = text0;
                        lvi.iItem = newIdx;
                        lvi.state = LVIS_SELECTED | LVIS_FOCUSED;
                        lvi.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                        int insertedIdx = ListView_InsertItem(hList, &lvi);
                        ListView_SetItemText(hList, insertedIdx, 1, text1);
                        ListView_SetItemText(hList, insertedIdx, 2, text2);
                        ListView_EnsureVisible(hList, insertedIdx, FALSE);

                        // Update orders in DB
                        for (int i = 0; i < ListView_GetItemCount(hList); i++) {
                            wchar_t szId[16];
                            ListView_GetItemText(hList, i, 2, szId, 16);
                            pData->db->ReorderTag(_wtoi(szId), i);
                        }
                    }
                }
            }
        }
        break;

    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == IDC_LIST_TAGS) {
                if (pnmh->code == LVN_ITEMCHANGED) {
                    LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                    if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                        editingIdx = pnmv->iItem;
                        wchar_t text[256];
                        ListView_GetItemText(pnmh->hwndFrom, editingIdx, 0, text, 256);
                        SetDlgItemText(hDlg, IDC_EDIT_TAG, text);
                        SetDlgItemText(hDlg, IDC_BUTTON_ADD_EDIT_TAG, L"Edit");
                    }
                }
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}
