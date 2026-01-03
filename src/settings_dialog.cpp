#include "settings_dialog.h"
#include "resource.h"
#include "utils.h"
#include "credentials.h"
#include "oauth_pkce.h"
#include "cloud_sync.h"
#include <commctrl.h>
#include <vector>
#include <string>
#include <map>
#include <process.h>
#include <memory>
#include <fstream>

// Forward declarations of procedures
INT_PTR CALLBACK SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AppearanceTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MarkdownTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK TagsTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SnippetsTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK CloudSyncTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

static const UINT WM_APP_CLOUD_CONNECT_DONE = WM_APP + 120;
static const UINT WM_APP_CLOUD_SYNC_DONE = WM_APP + 121;

struct CloudConnectResult {
    bool success = false;
    std::string refreshToken;
    std::string error;
};

struct CloudConnectThreadParams {
    HWND hDlg;
    std::string clientId;
    std::string clientSecret;
};

static unsigned __stdcall CloudConnectThread(void* p) {
    std::unique_ptr<CloudConnectThreadParams> params((CloudConnectThreadParams*)p);

    std::unique_ptr<CloudConnectResult> res(new CloudConnectResult());
    OAuthPkceResult oauth = OAuthPkce::ConnectGoogleDriveAppDataPkce(params->clientId, params->clientSecret);
    res->success = oauth.success;
    res->refreshToken = oauth.refreshToken;
    res->error = oauth.error;

    // Store the refresh token on the worker thread so the connection persists
    // even if the Settings dialog closes before WM_APP_CLOUD_CONNECT_DONE is handled.
    if (res->success) {
        if (!Credentials::WriteUtf8String(CloudSync::kCloudRefreshTokenCredTarget, res->refreshToken)) {
            res->success = false;
            res->error = "Failed to store refresh token";
        }
    }

    if (IsWindow(params->hDlg)) {
        PostMessage(params->hDlg, WM_APP_CLOUD_CONNECT_DONE, 0, (LPARAM)res.release());
    }
    return 0;
}

struct SettingsData {
    HWND hTab;
    HWND hPages[5];
    int currentPage;
    Database* db;
    std::wstring dbPath;
};

struct SettingsInitParams {
    Database* db;
    std::wstring dbPath;
};

struct CloudSyncThreadParams {
    HWND hDlg;
    Database* db;
    std::wstring dbPath;
    std::string clientId;
};

struct CloudSyncResultMsg {
    bool success = false;
    std::string error;
    std::string remoteModifiedTime;
    std::string localTime;
};

static std::string NowLocalTimeString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

static unsigned __stdcall CloudSyncNowThread(void* p) {
    std::unique_ptr<CloudSyncThreadParams> params((CloudSyncThreadParams*)p);
    std::unique_ptr<CloudSyncResultMsg> res(new CloudSyncResultMsg());

    if (!params->db) {
        res->error = "Missing database";
        if (IsWindow(params->hDlg)) PostMessage(params->hDlg, WM_APP_CLOUD_SYNC_DONE, 0, (LPARAM)res.release());
        return 0;
    }

    CloudSyncResult upload = CloudSync::UploadDatabaseSnapshot(params->db, params->dbPath, params->clientId);

    res->success = upload.success;
    res->error = upload.error;
    res->remoteModifiedTime = upload.remoteModifiedTime;
    res->localTime = NowLocalTimeString();

    if (IsWindow(params->hDlg)) {
        PostMessage(params->hDlg, WM_APP_CLOUD_SYNC_DONE, 0, (LPARAM)res.release());
    }
    return 0;
}

void CreateSettingsDialog(HWND hWndParent, Database* db, const std::wstring& dbPath) {
    auto* init = new SettingsInitParams();
    init->db = db;
    init->dbPath = dbPath;
    DialogBoxParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_SETTINGS), hWndParent, SettingsDialogProc, (LPARAM)init);
}

void OnSelChanged(HWND hDlg, SettingsData* pData) {
    int iSel = TabCtrl_GetCurSel(pData->hTab);
    
    for (int i = 0; i < 5; i++) {
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
            std::unique_ptr<SettingsInitParams> init((SettingsInitParams*)lParam);
            pData->db = init ? init->db : nullptr;
            pData->dbPath = init ? init->dbPath : L"";
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
            tie.pszText = (LPWSTR)L"Snippets";
            TabCtrl_InsertItem(pData->hTab, 3, &tie);
            tie.pszText = (LPWSTR)L"Cloud Sync";
            TabCtrl_InsertItem(pData->hTab, 4, &tie);

            RECT rcTab;
            GetWindowRect(pData->hTab, &rcTab);
            TabCtrl_AdjustRect(pData->hTab, FALSE, &rcTab);
            MapWindowPoints(NULL, hDlg, (LPPOINT)&rcTab, 2);

            pData->hPages[0] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_APPEARANCE), hDlg, AppearanceTabProc, (LPARAM)pData);
            pData->hPages[1] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_MARKDOWN), hDlg, MarkdownTabProc, (LPARAM)pData);
            pData->hPages[2] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_TAGS), hDlg, TagsTabProc, (LPARAM)pData);
            pData->hPages[3] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_SNIPPETS), hDlg, SnippetsTabProc, (LPARAM)pData);
            pData->hPages[4] = CreateDialogParam(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_TAB_CLOUD_SYNC), hDlg, CloudSyncTabProc, (LPARAM)pData);

            for (int i = 0; i < 5; i++) {
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
            for (int i = 0; i < 5; i++) {
                if (pData->hPages[i]) DestroyWindow(pData->hPages[i]);
            }
            delete pData;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void PopulateCloudIntervalCombo(HWND hCombo) {
    const wchar_t* items[] = { L"15", L"30", L"60" };
    for (auto item : items) {
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)item);
    }
}

static void SetCloudStatusText(HWND hDlg, const std::wstring& text) {
    SetDlgItemText(hDlg, IDC_STATIC_CLOUD_STATUS, text.c_str());
}

INT_PTR CALLBACK CloudSyncTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    static bool s_cloudInit = false;

    switch (message) {
    case WM_INITDIALOG:
        {
            s_cloudInit = true;
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pData = (SettingsData*)lParam;

            std::string clientId = pData->db->GetSetting("cloud_oauth_client_id", "");
            SetDlgItemText(hDlg, IDC_EDIT_CLOUD_CLIENT_ID, Utils::Utf8ToWide(clientId).c_str());

            // Don't display the stored client secret; keep it in Credential Manager.
            // IMPORTANT: do not clear/delete it here.

            // Derive connection status from the presence of the refresh token.
            // Also self-heal the persisted status string to avoid stale UI.
            std::string refresh;
            bool hasCred = Credentials::ReadUtf8String(CloudSync::kCloudRefreshTokenCredTarget, refresh);
            if (hasCred) {
                SetCloudStatusText(hDlg, L"Connected");
                if (pData->db) pData->db->SetSetting("cloud_sync_status", "Connected");
            } else {
                SetCloudStatusText(hDlg, L"Not connected");
                if (pData->db) pData->db->SetSetting("cloud_sync_status", "Not connected");
            }

            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), hasCred ? FALSE : TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), hasCred ? TRUE : FALSE);

            CheckDlgButton(hDlg, IDC_CHECK_CLOUD_SYNC_ENABLED,
                pData->db->GetSetting("cloud_sync_enabled", "0") == "1" ? BST_CHECKED : BST_UNCHECKED);

            CheckDlgButton(hDlg, IDC_CHECK_CLOUD_SYNC_ON_EXIT,
                pData->db->GetSetting("cloud_sync_on_exit", "1") == "1" ? BST_CHECKED : BST_UNCHECKED);

            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_CLOUD_SYNC_INTERVAL);
            PopulateCloudIntervalCombo(hCombo);

            std::string interval = pData->db->GetSetting("cloud_sync_interval_minutes", "30");
            std::wstring wInterval = Utils::Utf8ToWide(interval);
            SendMessage(hCombo, CB_SELECTSTRING, -1, (LPARAM)wInterval.c_str());
            if (SendMessage(hCombo, CB_GETCURSEL, 0, 0) == CB_ERR) {
                SendMessage(hCombo, CB_SETCURSEL, 1, 0); // default to 30
            }

            std::string lastSync = pData->db->GetSetting("cloud_last_sync_time", "");
            SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_SYNC, Utils::Utf8ToWide(lastSync).c_str());

            std::string lastErr = pData->db->GetSetting("cloud_sync_last_error", "");
            SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, Utils::Utf8ToWide(lastErr).c_str());

            s_cloudInit = false;
        }
        return (INT_PTR)TRUE;

    case WM_APP_CLOUD_CONNECT_DONE:
        {
            std::unique_ptr<CloudConnectResult> res((CloudConnectResult*)lParam);

            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), TRUE);
            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), TRUE);

            if (!pData) {
                break;
            }

            if (res && res->success) {
                pData->db->SetSetting("cloud_sync_status", "Connected");
                pData->db->SetSetting("cloud_sync_last_error", "");
                SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, L"");
                SetCloudStatusText(hDlg, L"Connected");
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), FALSE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), TRUE);
            } else {
                std::string err = res ? res->error : "Unknown error";
                if (err.empty()) {
                    err = "Connect failed";
                }
                pData->db->SetSetting("cloud_sync_last_error", err);
                SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, Utils::Utf8ToWide(err).c_str());
                SetCloudStatusText(hDlg, L"Not connected");
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), TRUE);
                EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), FALSE);
            }
        }
        return (INT_PTR)TRUE;

    case WM_APP_CLOUD_SYNC_DONE:
        {
            std::unique_ptr<CloudSyncResultMsg> res((CloudSyncResultMsg*)lParam);

            if (!pData || !pData->db) {
                break;
            }

            EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_SYNC_NOW), TRUE);

            if (res && res->success) {
                pData->db->SetSetting("cloud_last_sync_time", res->localTime);
                pData->db->SetSetting("cloud_sync_last_error", "");
                SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_SYNC, Utils::Utf8ToWide(res->localTime).c_str());
                SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, L"");
            } else {
                std::string err = res ? res->error : "Sync failed";
                if (err.empty()) err = "Sync failed";
                pData->db->SetSetting("cloud_sync_last_error", err);
                SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, Utils::Utf8ToWide(err).c_str());
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            if (!pData) break;
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmEvent == BN_CLICKED) {
                if (wmId == IDC_CHECK_CLOUD_SYNC_ENABLED) {
                    pData->db->SetSetting("cloud_sync_enabled",
                        IsDlgButtonChecked(hDlg, IDC_CHECK_CLOUD_SYNC_ENABLED) == BST_CHECKED ? "1" : "0");
                } else if (wmId == IDC_CHECK_CLOUD_SYNC_ON_EXIT) {
                    pData->db->SetSetting("cloud_sync_on_exit",
                        IsDlgButtonChecked(hDlg, IDC_CHECK_CLOUD_SYNC_ON_EXIT) == BST_CHECKED ? "1" : "0");
                } else if (wmId == IDC_BUTTON_CLOUD_CONNECT) {
                    wchar_t buf[512];
                    GetDlgItemText(hDlg, IDC_EDIT_CLOUD_CLIENT_ID, buf, 512);
                    std::string clientId = Utils::WideToUtf8(buf);
                    if (clientId.empty()) {
                        MessageBox(hDlg, L"Enter your Google OAuth Client ID first.", L"Cloud Sync", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    wchar_t secBuf[512];
                    GetDlgItemText(hDlg, IDC_EDIT_CLOUD_CLIENT_SECRET, secBuf, 512);
                    std::string clientSecret = Utils::WideToUtf8(secBuf);
                    if (!clientSecret.empty()) {
                        Credentials::WriteUtf8String(CloudSync::kCloudClientSecretCredTarget, clientSecret);
                    } else {
                        // If the field is empty, use any previously stored secret.
                        Credentials::ReadUtf8String(CloudSync::kCloudClientSecretCredTarget, clientSecret);
                    }

                    pData->db->SetSetting("cloud_oauth_client_id", clientId);
                    pData->db->SetSetting("cloud_sync_last_error", "");
                    SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, L"");

                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), FALSE);
                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), FALSE);
                    SetCloudStatusText(hDlg, L"Connecting...");

                    auto* params = new CloudConnectThreadParams();
                    params->hDlg = hDlg;
                    params->clientId = clientId;
                    params->clientSecret = clientSecret;
                    uintptr_t th = _beginthreadex(nullptr, 0, CloudConnectThread, params, 0, nullptr);
                    if (th == 0) {
                        delete params;
                        SetCloudStatusText(hDlg, L"Not connected");
                        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), TRUE);
                        MessageBox(hDlg, L"Failed to start connect thread.", L"Cloud Sync", MB_OK | MB_ICONERROR);
                    } else {
                        CloseHandle((HANDLE)th);
                    }
                } else if (wmId == IDC_BUTTON_CLOUD_DISCONNECT) {
                    Credentials::Delete(CloudSync::kCloudRefreshTokenCredTarget);
                    pData->db->SetSetting("cloud_sync_status", "Not connected");
                    SetCloudStatusText(hDlg, L"Not connected");
                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_CONNECT), TRUE);
                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_DISCONNECT), FALSE);
                } else if (wmId == IDC_BUTTON_CLOUD_SYNC_NOW) {
                    // Manual sync uploads a consistent DB snapshot.
                    std::string clientId = pData->db->GetSetting("cloud_oauth_client_id", "");
                    if (clientId.empty()) {
                        MessageBox(hDlg, L"Enter your Google OAuth Client ID first.", L"Cloud Sync", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_SYNC_NOW), FALSE);
                    SetDlgItemText(hDlg, IDC_STATIC_CLOUD_LAST_ERROR, L"");

                    auto* params = new CloudSyncThreadParams();
                    params->hDlg = hDlg;
                    params->db = pData->db;
                    params->dbPath = pData->dbPath;
                    params->clientId = clientId;

                    uintptr_t th = _beginthreadex(nullptr, 0, CloudSyncNowThread, params, 0, nullptr);
                    if (th == 0) {
                        delete params;
                        EnableWindow(GetDlgItem(hDlg, IDC_BUTTON_CLOUD_SYNC_NOW), TRUE);
                        MessageBox(hDlg, L"Failed to start sync thread.", L"Cloud Sync", MB_OK | MB_ICONERROR);
                    } else {
                        CloseHandle((HANDLE)th);
                    }
                }
            } else if (wmEvent == CBN_SELCHANGE) {
                if (wmId == IDC_COMBO_CLOUD_SYNC_INTERVAL) {
                    wchar_t buf[32];
                    GetDlgItemText(hDlg, IDC_COMBO_CLOUD_SYNC_INTERVAL, buf, 32);
                    pData->db->SetSetting("cloud_sync_interval_minutes", Utils::WideToUtf8(buf));
                }
            } else if (wmEvent == EN_CHANGE) {
                if (wmId == IDC_EDIT_CLOUD_CLIENT_ID) {
                    wchar_t buf[512];
                    GetDlgItemText(hDlg, IDC_EDIT_CLOUD_CLIENT_ID, buf, 512);
                    pData->db->SetSetting("cloud_oauth_client_id", Utils::WideToUtf8(buf));
                } else if (wmId == IDC_EDIT_CLOUD_CLIENT_SECRET) {
                    if (s_cloudInit) {
                        break;
                    }
                    wchar_t buf[512];
                    GetDlgItemText(hDlg, IDC_EDIT_CLOUD_CLIENT_SECRET, buf, 512);
                    std::string secret = Utils::WideToUtf8(buf);
                    if (!secret.empty()) {
                        Credentials::WriteUtf8String(CloudSync::kCloudClientSecretCredTarget, secret);
                    }
                }
            }
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

    auto sizeColumns = [&]() {
        if (!hList) return;
        RECT rc;
        GetClientRect(hList, &rc);
        int listWidth = rc.right - rc.left;
        int usageWidth = 60;
        int idWidth = 0;
        int remaining = listWidth - usageWidth - idWidth - 4;
        if (remaining < 80) remaining = 80;
        ListView_SetColumnWidth(hList, 0, remaining);
        ListView_SetColumnWidth(hList, 1, usageWidth);
        ListView_SetColumnWidth(hList, 2, 0);
    };

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

            sizeColumns();

            // Subclass the edit box for ESC key
            HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_TAG);
            g_oldTagEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)TagEditProc);

            // Load tags from DB
            if (pData && pData->db) {
                std::vector<Database::Tag> tags = pData->db->GetTags();
                std::map<int, int> counts = pData->db->GetTagUsageCounts();
                for (const auto& tag : tags) {
                    LVITEM lvi = {0};
                    lvi.mask = LVIF_TEXT;
                    lvi.pszText = (LPWSTR)tag.name.c_str();
                    lvi.iItem = ListView_GetItemCount(hList);
                    int idx = ListView_InsertItem(hList, &lvi);

                    int usage = 0;
                    auto it = counts.find(tag.id);
                    if (it != counts.end()) usage = it->second;
                    wchar_t szUsage[32];
                    swprintf(szUsage, 32, L"%d", usage);
                    ListView_SetItemText(hList, idx, 1, szUsage);

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

            // Custom-draw the header to look like a classic gray header.
            HWND hHeader = hList ? ListView_GetHeader(hList) : NULL;
            if (hHeader && pnmh->hwndFrom == hHeader && pnmh->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
                if (cd->dwDrawStage == CDDS_PREPAINT) {
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return (INT_PTR)TRUE;
                }
                if (cd->dwDrawStage == (CDDS_ITEMPREPAINT)) {
                    int iCol = (int)cd->dwItemSpec;
                    RECT rcItem;
                    Header_GetItemRect(hHeader, iCol, &rcItem);
                    HBRUSH br = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
                    FillRect(cd->hdc, &rcItem, br);
                    DeleteObject(br);

                    wchar_t headerText[128] = {0};
                    HDITEMW hdi = {0};
                    hdi.mask = HDI_TEXT;
                    hdi.pszText = headerText;
                    hdi.cchTextMax = 127;
                    Header_GetItem(hHeader, iCol, &hdi);

                    SetBkMode(cd->hdc, TRANSPARENT);
                    SetTextColor(cd->hdc, GetSysColor(COLOR_BTNTEXT));
                    RECT rcText = rcItem;
                    rcText.left += 6;
                    DrawTextW(cd->hdc, headerText, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
                    return (INT_PTR)TRUE;
                }
            }
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static void ResetSnippetsEditMode(HWND hDlg, int& editingIdx) {
    editingIdx = -1;
    SetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TRIGGER, L"");
    SetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TEXT, L"");
    SetDlgItemText(hDlg, IDC_BUTTON_ADD_EDIT_SNIPPET, L"Add");
    HWND hNew = GetDlgItem(hDlg, IDC_BUTTON_NEW_SNIPPET);
    if (hNew) ShowWindow(hNew, SW_HIDE);
}

static void ReloadSnippetsList(HWND hList, Database* db) {
    if (!hList) return;
    ListView_DeleteAllItems(hList);
    if (!db) return;

    std::vector<Database::Snippet> snippets = db->GetSnippets();
    for (const auto& sn : snippets) {
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.pszText = (LPWSTR)sn.trigger.c_str();
        lvi.iItem = ListView_GetItemCount(hList);
        int idx = ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, idx, 1, (LPWSTR)sn.snippet.c_str());

        wchar_t szId[16];
        swprintf(szId, 16, L"%d", sn.id);
        ListView_SetItemText(hList, idx, 2, szId);
    }
}

INT_PTR CALLBACK SnippetsTabProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsData* pData = (SettingsData*)GetWindowLongPtr(hDlg, GWLP_USERDATA);
    HWND hList = GetDlgItem(hDlg, IDC_LIST_SNIPPETS);
    static int editingIdx = -1;

    auto sizeColumns = [&]() {
        if (!hList) return;
        RECT rc;
        GetClientRect(hList, &rc);
        int listWidth = rc.right - rc.left;
        int triggerWidth = 70;
        int idWidth = 0;
        int remaining = listWidth - triggerWidth - idWidth - 4;
        if (remaining < 60) remaining = 60;
        ListView_SetColumnWidth(hList, 0, triggerWidth);
        ListView_SetColumnWidth(hList, 1, remaining);
        ListView_SetColumnWidth(hList, 2, 0);
    };

    switch (message) {
    case WM_INITDIALOG:
        {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pData = (SettingsData*)lParam;
            editingIdx = -1;

            ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            LVCOLUMN lvc = {0};
            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.pszText = (LPWSTR)L"Trigger";
            lvc.cx = 70;
            ListView_InsertColumn(hList, 0, &lvc);

            lvc.pszText = (LPWSTR)L"Snippet";
            lvc.cx = 130;
            ListView_InsertColumn(hList, 1, &lvc);

            lvc.pszText = (LPWSTR)L"ID";
            lvc.cx = 0;
            ListView_InsertColumn(hList, 2, &lvc);

            sizeColumns();

            HWND hNew = GetDlgItem(hDlg, IDC_BUTTON_NEW_SNIPPET);
            if (hNew) ShowWindow(hNew, SW_HIDE);

            if (pData && pData->db) {
                CheckDlgButton(hDlg, IDC_CHECK_SNIPPETS_ENABLED_NOTES,
                    pData->db->GetSetting("snippets_enabled_notes", "0") == "1" ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_CHECK_SNIPPETS_ENABLED_CHECKLISTS,
                    pData->db->GetSetting("snippets_enabled_checklists", "0") == "1" ? BST_CHECKED : BST_UNCHECKED);

                ReloadSnippetsList(hList, pData->db);
            }
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        {
            if (!pData || !pData->db) break;
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            if (wmEvent == BN_CLICKED) {
                if (wmId == IDC_CHECK_SNIPPETS_ENABLED_NOTES) {
                    pData->db->SetSetting("snippets_enabled_notes", IsDlgButtonChecked(hDlg, IDC_CHECK_SNIPPETS_ENABLED_NOTES) == BST_CHECKED ? "1" : "0");
                } else if (wmId == IDC_CHECK_SNIPPETS_ENABLED_CHECKLISTS) {
                    pData->db->SetSetting("snippets_enabled_checklists", IsDlgButtonChecked(hDlg, IDC_CHECK_SNIPPETS_ENABLED_CHECKLISTS) == BST_CHECKED ? "1" : "0");
                }
            }

            if (wmId == IDC_BUTTON_NEW_SNIPPET) {
                ResetSnippetsEditMode(hDlg, editingIdx);
            } else if (wmId == IDC_BUTTON_ADD_EDIT_SNIPPET) {
                wchar_t triggerBuf[256];
                wchar_t snippetBuf[1024];
                GetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TRIGGER, triggerBuf, 256);
                GetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TEXT, snippetBuf, 1024);

                if (wcslen(triggerBuf) == 0) break;

                if (editingIdx == -1) {
                    Database::Snippet sn;
                    sn.trigger = triggerBuf;
                    sn.snippet = snippetBuf;
                    if (pData->db->CreateSnippet(sn)) {
                        ReloadSnippetsList(hList, pData->db);
                        ResetSnippetsEditMode(hDlg, editingIdx);
                    }
                } else {
                    wchar_t szId[16];
                    ListView_GetItemText(hList, editingIdx, 2, szId, 16);
                    int id = _wtoi(szId);

                    Database::Snippet sn;
                    sn.id = id;
                    sn.trigger = triggerBuf;
                    sn.snippet = snippetBuf;
                    if (pData->db->UpdateSnippet(sn)) {
                        ReloadSnippetsList(hList, pData->db);
                        ResetSnippetsEditMode(hDlg, editingIdx);
                    }
                }
            } else if (wmId == IDC_BUTTON_DELETE_SNIPPET) {
                int idx = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                if (idx != -1) {
                    wchar_t szId[16];
                    ListView_GetItemText(hList, idx, 2, szId, 16);
                    int id = _wtoi(szId);
                    if (pData->db->DeleteSnippet(id)) {
                        ReloadSnippetsList(hList, pData->db);
                        ResetSnippetsEditMode(hDlg, editingIdx);
                    }
                }
            }
        }
        break;

    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->idFrom == IDC_LIST_SNIPPETS && pnmh->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                    editingIdx = pnmv->iItem;
                    wchar_t trigger[256];
                    wchar_t snippet[1024];
                    ListView_GetItemText(pnmh->hwndFrom, editingIdx, 0, trigger, 256);
                    ListView_GetItemText(pnmh->hwndFrom, editingIdx, 1, snippet, 1024);
                    SetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TRIGGER, trigger);
                    SetDlgItemText(hDlg, IDC_EDIT_SNIPPET_TEXT, snippet);
                    SetDlgItemText(hDlg, IDC_BUTTON_ADD_EDIT_SNIPPET, L"Edit");

                    HWND hNew = GetDlgItem(hDlg, IDC_BUTTON_NEW_SNIPPET);
                    if (hNew) ShowWindow(hNew, SW_SHOW);
                }
            }

            // Custom-draw the header to look like a classic gray header.
            HWND hHeader = hList ? ListView_GetHeader(hList) : NULL;
            if (hHeader && pnmh->hwndFrom == hHeader && pnmh->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
                if (cd->dwDrawStage == CDDS_PREPAINT) {
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return (INT_PTR)TRUE;
                }
                if (cd->dwDrawStage == (CDDS_ITEMPREPAINT)) {
                    int iCol = (int)cd->dwItemSpec;
                    RECT rcItem;
                    Header_GetItemRect(hHeader, iCol, &rcItem);
                    HBRUSH br = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
                    FillRect(cd->hdc, &rcItem, br);
                    DeleteObject(br);

                    wchar_t text[128] = {0};
                    HDITEMW hdi = {0};
                    hdi.mask = HDI_TEXT;
                    hdi.pszText = text;
                    hdi.cchTextMax = 127;
                    Header_GetItem(hHeader, iCol, &hdi);

                    SetBkMode(cd->hdc, TRANSPARENT);
                    SetTextColor(cd->hdc, GetSysColor(COLOR_BTNTEXT));
                    RECT rcText = rcItem;
                    rcText.left += 6;
                    DrawTextW(cd->hdc, text, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
                    SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
                    return (INT_PTR)TRUE;
                }
            }
        }
        break;
    }

    return (INT_PTR)FALSE;
}
