#include "spell_checker.h"
#include <cwctype>

bool SpellChecker::Initialize(const std::wstring& affPath, const std::wstring& dicPath) {
    std::string affUtf8 = Utils::WideToUtf8(affPath);
    std::string dicUtf8 = Utils::WideToUtf8(dicPath);
    try {
        m_hunspell = std::make_unique<Hunspell>(affUtf8.c_str(), dicUtf8.c_str());
    } catch (...) {
        m_hunspell.reset();
    }
    return IsReady();
}

std::vector<SpellChecker::Range> SpellChecker::FindMisspellings(const std::wstring& text) const {
    std::vector<Range> misses;
    if (!m_hunspell || text.empty()) {
        return misses;
    }

    const size_t n = text.size();
    size_t i = 0;
    while (i < n) {
        // Skip non-alphabetic characters
        while (i < n && !iswalpha(text[i])) {
            ++i;
        }
        if (i >= n) break;
        
        size_t start = i;
        
        // Collect alphabetic characters only (simple, reliable word boundaries)
        while (i < n && iswalpha(text[i])) {
            ++i;
        }
        
        // Extract the word and check spelling
        std::wstring word = text.substr(start, i - start);
        std::string utf8 = Utils::WideToUtf8(word);
        
        // Validate that the UTF-8 conversion produced a reasonable result
        if (!utf8.empty() && !m_hunspell->spell(utf8.c_str())) {
            Range r;
            r.start = static_cast<LONG>(start);
            r.length = static_cast<LONG>(i - start);
            misses.push_back(r);
        }
    }
    return misses;
}
