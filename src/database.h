#pragma once
#include <string>
#include <vector>
#include "sqlite3.h"
#include "note.h"

class Database {
public:
    struct Color {
        int id;
        std::string name;
        std::string hex_color;
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

private:
    bool CreateSchema();
    bool InitializeColors();
    sqlite3* m_db;
};
