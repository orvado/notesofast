#pragma once
#include <string>
#include <windows.h>

namespace Utils {
    std::wstring Utf8ToWide(const std::string& str);
    std::string WideToUtf8(const std::wstring& wstr);
}
