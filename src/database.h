#pragma once
#include <string>
#include <vector>
#include <map>
#include "sqlite3.h"
#include "note.h"

class Database {
public:
    struct Color {
        int id;
        std::string name;
        std::string hex_color;
    };

    struct Tag {
        int id;
        std::wstring name;
        int order;
    };

    struct Snippet {
        int id;
        std::wstring trigger;
        std::wstring snippet;
    };

    enum class SortBy {
        DateModified,
        DateCreated,
        Title
    };

    Database();
    ~Database();
    bool Initialize(const std::string& dbPath);
    void Close();

    std::vector<Note> GetAllNotes(bool includeArchived = false, SortBy sortBy = SortBy::DateModified);
    bool CreateNote(Note& note);
    bool UpdateNote(const Note& note);
    bool DeleteNote(int id);

    // Phase 2 methods
    std::vector<Color> GetColors();
    bool UpdateNoteColor(int noteId, int colorId);
    bool TogglePin(int noteId, bool isPinned);
    bool ToggleArchive(int noteId, bool isArchived);
    
    // Checklist methods
    std::vector<ChecklistItem> GetChecklistItems(int noteId);
    bool CreateChecklistItem(ChecklistItem& item);
    bool UpdateChecklistItem(const ChecklistItem& item);
    bool DeleteChecklistItem(int itemId);
    bool ToggleChecklistItem(int itemId, bool isChecked);
    bool ReorderChecklistItem(int itemId, int newOrder);
    bool ToggleNoteType(int noteId, bool isChecklist);
    
    // Search history methods
    std::vector<std::string> GetSearchHistory(int limit = 128);
    bool AddSearchHistory(const std::string& searchTerm);
    bool ClearOldSearchHistory(int keepCount = 128);

    // Tag methods
    std::vector<Tag> GetTags();
    std::map<int, int> GetTagUsageCounts();
    bool CreateTag(Tag& tag);
    bool UpdateTag(const Tag& tag);
    bool DeleteTag(int id);
    bool ReorderTag(int tagId, int newOrder);
    std::vector<Tag> GetNoteTags(int noteId);
    bool AddTagToNote(int noteId, int tagId);
    bool RemoveTagFromNote(int noteId, int tagId);

    // Snippet methods
    std::vector<Snippet> GetSnippets();
    bool CreateSnippet(Snippet& snippet);
    bool UpdateSnippet(const Snippet& snippet);
    bool DeleteSnippet(int id);
    bool TryGetSnippetByTrigger(const std::wstring& trigger, std::wstring& outSnippet);

    // Settings methods
    std::string GetSetting(const std::string& key, const std::string& defaultValue = "");
    bool SetSetting(const std::string& key, const std::string& value);

    // Creates a consistent snapshot of the current database into a new SQLite file.
    bool BackupToFile(const std::string& destDbPath);

private:
    bool CreateSchema();
    bool InitializeColors();
    sqlite3* m_db;
};


