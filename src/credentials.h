#pragma once

#include <string>

namespace Credentials {

// Stores a UTF-8 string secret in Windows Credential Manager under a generic credential.
bool WriteUtf8String(const std::wstring& targetName, const std::string& secretUtf8);

// Reads a UTF-8 string secret from Windows Credential Manager.
// Returns true when found; false when not found or on error.
bool ReadUtf8String(const std::wstring& targetName, std::string& outSecretUtf8);

// Deletes a generic credential.
bool Delete(const std::wstring& targetName);

}
