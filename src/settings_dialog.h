#pragma once

#include <windows.h>
#include "database.h"

void CreateSettingsDialog(HWND hWndParent, Database* db, const std::wstring& dbPath);
