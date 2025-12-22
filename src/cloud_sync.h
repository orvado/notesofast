#pragma once

#include <string>
#include <vector>

class Database;

struct CloudSyncResult {
    bool success = false;
    std::string error;
    std::string remoteModifiedTime;
};

namespace CloudSync {

// Credential Manager targets
static const wchar_t kCloudRefreshTokenCredTarget[] = L"NoteSoFast.GoogleDrive.RefreshToken";
static const wchar_t kCloudClientSecretCredTarget[] = L"NoteSoFast.GoogleDrive.ClientSecret";

// Uploads the given bytes as a file named fileName into Google Drive appDataFolder.
// Creates the file if it doesn't exist; otherwise updates it.
CloudSyncResult UploadToAppDataFolder(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& refreshToken,
    const std::string& fileName,
    const std::vector<unsigned char>& content,
    const std::string& mimeType);

// If the remote appDataFolder file is newer than localDbLastWriteFileTimeUtc,
// downloads it into outContent.
CloudSyncResult DownloadIfRemoteNewer(
    const std::string& clientId,
    const std::string& clientSecret,
    const std::string& refreshToken,
    const std::string& fileName,
    unsigned long long localDbLastWriteFileTimeUtc,
    std::vector<unsigned char>& outContent);

// Uploads a consistent snapshot of the current database to Drive appDataFolder.
// Reads refresh token + client secret from Windows Credential Manager.
CloudSyncResult UploadDatabaseSnapshot(Database* db, const std::wstring& dbPath, const std::string& clientId);

// Restores the local database file from Drive appDataFolder if the remote copy is newer.
// Reads refresh token + client secret from Windows Credential Manager.
// outRestored is set to true only when the local file was replaced.
CloudSyncResult RestoreDatabaseIfRemoteNewer(const std::wstring& dbPath, const std::string& clientId, bool& outRestored);

} // namespace CloudSync
