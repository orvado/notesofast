#pragma once

#include <string>

struct OAuthPkceResult {
    bool success = false;
    std::string refreshToken;
    std::string accessToken;
    std::string error;
};

namespace OAuthPkce {

// Runs a PKCE OAuth flow for Google Drive appDataFolder.
// - Opens system browser
// - Listens on 127.0.0.1:<ephemeral>/callback
// - Exchanges authorization code for tokens
OAuthPkceResult ConnectGoogleDriveAppDataPkce(const std::string& clientId, const std::string& clientSecret);

}
