#include "credentials.h"

#include <windows.h>
#include <wincred.h>

namespace Credentials {

static std::wstring GetCredUserName() {
    wchar_t userName[256];
    DWORD userNameLen = (DWORD)(sizeof(userName) / sizeof(userName[0]));
    if (GetUserNameW(userName, &userNameLen) && userNameLen > 1) {
        return std::wstring(userName);
    }
    return L"NoteSoFast";
}

bool WriteUtf8String(const std::wstring& targetName, const std::string& secretUtf8) {
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
    cred.UserName = const_cast<LPWSTR>(GetCredUserName().c_str());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    cred.CredentialBlobSize = (DWORD)secretUtf8.size();
    cred.CredentialBlob = (LPBYTE)(secretUtf8.empty() ? nullptr : secretUtf8.data());

    return CredWriteW(&cred, 0) == TRUE;
}

bool ReadUtf8String(const std::wstring& targetName, std::string& outSecretUtf8) {
    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &pcred) || !pcred) {
        return false;
    }

    bool ok = false;
    if (pcred->CredentialBlob && pcred->CredentialBlobSize > 0) {
        outSecretUtf8.assign((const char*)pcred->CredentialBlob, (size_t)pcred->CredentialBlobSize);
        ok = true;
    }

    CredFree(pcred);
    return ok;
}

bool Delete(const std::wstring& targetName) {
    // CredDelete returns FALSE if the cred doesn't exist.
    CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0);
    return true;
}

} // namespace Credentials
