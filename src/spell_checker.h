#pragma once
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include "utils.h"
#include <hunspell/hunspell.hxx>

class SpellChecker {
public:
    SpellChecker() = default;

    bool Initialize(const std::wstring& affPath, const std::wstring& dicPath);
    bool IsReady() const { return static_cast<bool>(m_hunspell); }

    struct Range {
        LONG start = 0; // character index (wchar_t units)
        LONG length = 0;
    };

    std::vector<Range> FindMisspellings(const std::wstring& text) const;

private:
    std::unique_ptr<Hunspell> m_hunspell;
};
