#include "database.h"
#include <iostream>
#include "utils.h"

Database::Database() {
    m_db = nullptr;
}

Database::~Database() {
    Close();
}

bool Database::Initialize(const std::string& dbPath) {
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(m_db) << std::endl;
        return false;
    }
    
    if (!CreateSchema()) return false;

    // Migration: Check for is_checklist column
    const char* checkSql = "SELECT is_checklist FROM notes LIMIT 1";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &stmt, nullptr) != SQLITE_OK) {
        // Column likely missing, add it
        const char* alterSql = "ALTER TABLE notes ADD COLUMN is_checklist INTEGER DEFAULT 0";
        char* errMsg = nullptr;
        if (sqlite3_exec(m_db, alterSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Migration error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    } else {
        sqlite3_finalize(stmt);
    }

    // Migration: ensure search_history exists for older databases
    const char* checkSearchSql = "SELECT search_term FROM search_history LIMIT 1";
    if (sqlite3_prepare_v2(m_db, checkSearchSql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* createSearchSql =
            "CREATE TABLE IF NOT EXISTS search_history ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    search_term TEXT NOT NULL UNIQUE,"
            "    last_used DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        char* errMsg = nullptr;
        if (sqlite3_exec(m_db, createSearchSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Migration error (search_history): " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    } else {
        sqlite3_finalize(stmt);
    }
    
    // Migration: Check for tags and note_tags tables
    const char* checkTagsSql = "SELECT id FROM tags LIMIT 1";
    if (sqlite3_prepare_v2(m_db, checkTagsSql, -1, &stmt, nullptr) != SQLITE_OK) {
        const char* createTagsSql =
            "CREATE TABLE tags ("
            "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    name TEXT NOT NULL,"
            "    tag_order INTEGER"
            ");"
            "CREATE TABLE note_tags ("
            "    note_id INTEGER,"
            "    tag_id INTEGER,"
            "    PRIMARY KEY (note_id, tag_id),"
            "    FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE,"
            "    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE"
            ");";
        char* errMsg = nullptr;
        if (sqlite3_exec(m_db, createTagsSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "Migration error (tags): " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    } else {
        sqlite3_finalize(stmt);
    }


    return InitializeColors();
}

bool Database::BackupToFile(const std::string& destDbPath) {
    if (!m_db) {
        return false;
    }

    sqlite3* outDb = nullptr;
    if (sqlite3_open(destDbPath.c_str(), &outDb) != SQLITE_OK) {
        if (outDb) sqlite3_close(outDb);
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(outDb, "main", m_db, "main");
    if (!backup) {
        sqlite3_close(outDb);
        return false;
    }

    int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);

    bool ok = (rc == SQLITE_DONE);
    sqlite3_close(outDb);
    return ok;
}

std::vector<Note> Database::GetAllNotes(bool includeArchived, SortBy sortBy) {
    std::vector<Note> notes;
    std::string sql = "SELECT id, title, content, color_id, is_archived, is_pinned, is_checklist, created_at, modified_at FROM notes ";
    
    if (!includeArchived) {
        sql += "WHERE is_archived = 0 ";
    }
    
    sql += "ORDER BY is_pinned DESC, ";
    
    switch (sortBy) {
        case SortBy::DateCreated:
            sql += "created_at DESC";
            break;
        case SortBy::Title:
            sql += "title ASC";
            break;
        case SortBy::DateModified:
        default:
            sql += "modified_at DESC";
            break;
    }
    
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Note note;
            note.id = sqlite3_column_int(stmt, 0);
            note.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            note.content = content ? content : "";
            note.color_id = sqlite3_column_int(stmt, 3);
            note.is_archived = sqlite3_column_int(stmt, 4) != 0;
            note.is_pinned = sqlite3_column_int(stmt, 5) != 0;
            note.is_checklist = sqlite3_column_int(stmt, 6) != 0;
            note.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            note.modified_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            
            if (note.is_checklist) {
                note.checklist_items = GetChecklistItems(note.id);
            }
            notes.push_back(note);
        }
        sqlite3_finalize(stmt);
    }
    return notes;
}

bool Database::CreateNote(Note& note) {
    const char* sql = "INSERT INTO notes (title, content, created_at, modified_at) VALUES (?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, note.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, note.content.c_str(), -1, SQLITE_STATIC);

        int result = sqlite3_step(stmt);
        if (result == SQLITE_DONE) {
            note.id = (int)sqlite3_last_insert_rowid(m_db);
            sqlite3_finalize(stmt);
            return true;
        } else {
            // Debug: Show the error
            const char* errMsg = sqlite3_errmsg(m_db);
            fprintf(stderr, "CreateNote failed: %s (result=%d)\n", errMsg, result);
            sqlite3_finalize(stmt);
        }
    } else {
        fprintf(stderr, "CreateNote prepare failed: %s\n", sqlite3_errmsg(m_db));
    }
    return false;
}

bool Database::UpdateNote(const Note& note) {
    const char* sql = "UPDATE notes SET title = ?, content = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, note.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, note.content.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, note.id);

        int result = sqlite3_step(stmt);
        if (result == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        } else {
            // Debug: Show the error
            const char* errMsg = sqlite3_errmsg(m_db);
            fprintf(stderr, "UpdateNote failed: note.id=%d, %s (result=%d)\n", note.id, errMsg, result);
            sqlite3_finalize(stmt);
        }
    } else {
        fprintf(stderr, "UpdateNote prepare failed: %s\n", sqlite3_errmsg(m_db));
    }
    return false;
}

bool Database::DeleteNote(int id) {
    const char* sql = "DELETE FROM notes WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

std::vector<Database::Color> Database::GetColors() {
    std::vector<Color> colors;
    const char* sql = "SELECT id, name, hex_color FROM colors ORDER BY id";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Color color;
            color.id = sqlite3_column_int(stmt, 0);
            color.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            color.hex_color = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            colors.push_back(color);
        }
        sqlite3_finalize(stmt);
    }
    return colors;
}

bool Database::UpdateNoteColor(int noteId, int colorId) {
    const char* sql = "UPDATE notes SET color_id = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, colorId);
        sqlite3_bind_int(stmt, 2, noteId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::TogglePin(int noteId, bool isPinned) {
    const char* sql = "UPDATE notes SET is_pinned = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, isPinned ? 1 : 0);
        sqlite3_bind_int(stmt, 2, noteId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::ToggleArchive(int noteId, bool isArchived) {
    const char* sql = "UPDATE notes SET is_archived = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, isArchived ? 1 : 0);
        sqlite3_bind_int(stmt, 2, noteId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::CreateSchema() {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS notes ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    title TEXT NOT NULL,"
        "    content TEXT,"
        "    color_id INTEGER DEFAULT 0,"
        "    is_archived INTEGER DEFAULT 0,"
        "    is_pinned INTEGER DEFAULT 0,"
        "    is_checklist INTEGER DEFAULT 0,"
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    modified_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS colors ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT NOT NULL,"
        "    hex_color TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS checklist_items ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    note_id INTEGER NOT NULL,"
        "    item_text TEXT NOT NULL,"
        "    is_checked INTEGER DEFAULT 0,"
        "    item_order INTEGER DEFAULT 0,"
        "    FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS search_history ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    search_term TEXT NOT NULL UNIQUE,"
        "    last_used DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS tags ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT NOT NULL,"
        "    tag_order INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS note_tags ("
        "    note_id INTEGER,"
        "    tag_id INTEGER,"
        "    PRIMARY KEY (note_id, tag_id),"
        "    FOREIGN KEY (note_id) REFERENCES notes(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS snippets ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    trigger TEXT NOT NULL,"
        "    snippet TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS settings ("
        "    key TEXT PRIMARY KEY,"
        "    value TEXT"
        ");";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

std::vector<Database::Snippet> Database::GetSnippets() {
    std::vector<Snippet> snippets;
    const char* sql = "SELECT id, trigger, snippet FROM snippets ORDER BY trigger ASC";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Snippet sn;
            sn.id = sqlite3_column_int(stmt, 0);
            sn.trigger = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            sn.snippet = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 2));
            snippets.push_back(std::move(sn));
        }
        sqlite3_finalize(stmt);
    }
    return snippets;
}

bool Database::CreateSnippet(Snippet& snippet) {
    const char* sql = "INSERT INTO snippets (trigger, snippet) VALUES (?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, snippet.trigger.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text16(stmt, 2, snippet.snippet.c_str(), -1, SQLITE_STATIC);
        int result = sqlite3_step(stmt);
        bool success = (result == SQLITE_DONE);
        if (success) {
            snippet.id = (int)sqlite3_last_insert_rowid(m_db);
        }
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::UpdateSnippet(const Snippet& snippet) {
    const char* sql = "UPDATE snippets SET trigger = ?, snippet = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, snippet.trigger.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text16(stmt, 2, snippet.snippet.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, snippet.id);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::DeleteSnippet(int id) {
    const char* sql = "DELETE FROM snippets WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::TryGetSnippetByTrigger(const std::wstring& trigger, std::wstring& outSnippet) {
    const char* sql = "SELECT snippet FROM snippets WHERE trigger = ? LIMIT 1";
    sqlite3_stmt* stmt;
    outSnippet.clear();

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, trigger.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const wchar_t* txt = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 0));
            if (txt) outSnippet = txt;
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool Database::InitializeColors() {
    // Check if colors exist
    const char* checkSql = "SELECT COUNT(*) FROM colors";
    sqlite3_stmt* stmt;
    int count = 0;
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (count == 0) {
        const char* insertSql = "INSERT INTO colors (name, hex_color) VALUES "
            "('None', '#FFFFFF'),"
            "('Personal', '#D6EAF8'),"
            "('Work', '#FADBD8'),"
            "('Ideas', '#FCF3CF'),"
            "('Important', '#FAE5D3'),"
            "('Shopping', '#D5F5E3')";
        
        char* errMsg = nullptr;
        int rc = sqlite3_exec(m_db, insertSql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "SQL error initializing colors: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
    } else {
        // Migration: Update old vibrant colors to muted ones
        const char* updateSql = 
            "UPDATE colors SET hex_color = '#D6EAF8' WHERE name = 'Personal' AND hex_color = '#3498db';"
            "UPDATE colors SET hex_color = '#FADBD8' WHERE name = 'Work' AND hex_color = '#e74c3c';"
            "UPDATE colors SET hex_color = '#FCF3CF' WHERE name = 'Ideas' AND hex_color = '#f1c40f';"
            "UPDATE colors SET hex_color = '#FAE5D3' WHERE name = 'Important' AND hex_color = '#e67e22';"
            "UPDATE colors SET hex_color = '#D5F5E3' WHERE name = 'Shopping' AND hex_color = '#2ecc71';";
            
        char* errMsg = nullptr;
        sqlite3_exec(m_db, updateSql, nullptr, nullptr, &errMsg);
        if (errMsg) {
            std::cerr << "Color migration error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    }
    return true;
}

std::vector<ChecklistItem> Database::GetChecklistItems(int noteId) {
    std::vector<ChecklistItem> items;
    const char* sql = "SELECT id, note_id, item_text, is_checked, item_order FROM checklist_items WHERE note_id = ? ORDER BY item_order";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, noteId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ChecklistItem item;
            item.id = sqlite3_column_int(stmt, 0);
            item.note_id = sqlite3_column_int(stmt, 1);
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item.item_text = text ? text : "";
            item.is_checked = sqlite3_column_int(stmt, 3) != 0;
            item.item_order = sqlite3_column_int(stmt, 4);
            items.push_back(item);
        }
        sqlite3_finalize(stmt);
    }
    return items;
}

bool Database::CreateChecklistItem(ChecklistItem& item) {
    const char* sql = "INSERT INTO checklist_items (note_id, item_text, is_checked, item_order) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, item.note_id);
        sqlite3_bind_text(stmt, 2, item.item_text.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, item.is_checked ? 1 : 0);
        sqlite3_bind_int(stmt, 4, item.item_order);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            item.id = (int)sqlite3_last_insert_rowid(m_db);
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool Database::UpdateChecklistItem(const ChecklistItem& item) {
    const char* sql = "UPDATE checklist_items SET item_text = ?, is_checked = ?, item_order = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, item.item_text.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, item.is_checked ? 1 : 0);
        sqlite3_bind_int(stmt, 3, item.item_order);
        sqlite3_bind_int(stmt, 4, item.id);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::DeleteChecklistItem(int itemId) {
    const char* sql = "DELETE FROM checklist_items WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, itemId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::ToggleChecklistItem(int itemId, bool isChecked) {
    const char* sql = "UPDATE checklist_items SET is_checked = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, isChecked ? 1 : 0);
        sqlite3_bind_int(stmt, 2, itemId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::ReorderChecklistItem(int itemId, int newOrder) {
    const char* sql = "UPDATE checklist_items SET item_order = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, newOrder);
        sqlite3_bind_int(stmt, 2, itemId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::ToggleNoteType(int noteId, bool isChecklist) {
    const char* sql = "UPDATE notes SET is_checklist = ?, modified_at = CURRENT_TIMESTAMP WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, isChecklist ? 1 : 0);
        sqlite3_bind_int(stmt, 2, noteId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<std::string> Database::GetSearchHistory(int limit) {
    std::vector<std::string> history;
    const char* sql = "SELECT search_term FROM search_history ORDER BY last_used DESC LIMIT ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* term = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (term) {
                history.push_back(term);
            }
        }
        sqlite3_finalize(stmt);
    }
    return history;
}

bool Database::AddSearchHistory(const std::string& searchTerm) {
    if (searchTerm.empty()) {
        return false;
    }

    // Insert or update the search term
    const char* sql = "INSERT OR REPLACE INTO search_history (search_term, last_used) VALUES (?, CURRENT_TIMESTAMP)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, searchTerm.c_str(), -1, SQLITE_STATIC);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        
        if (success) {
            ClearOldSearchHistory(128);
        }
        return success;
    }
    return false;
}

bool Database::ClearOldSearchHistory(int keepCount) {
    const char* sql = "DELETE FROM search_history WHERE id NOT IN "
                      "(SELECT id FROM search_history ORDER BY last_used DESC LIMIT ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, keepCount);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<Database::Tag> Database::GetTags() {
    std::vector<Tag> tags;
    const char* sql = "SELECT id, name, tag_order FROM tags ORDER BY tag_order";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Tag tag;
            tag.id = sqlite3_column_int(stmt, 0);
            tag.name = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            tag.order = sqlite3_column_int(stmt, 2);
            tags.push_back(tag);
        }
        sqlite3_finalize(stmt);
    }
    return tags;
}

std::map<int, int> Database::GetTagUsageCounts() {
    std::map<int, int> counts;
    const char* sql = "SELECT tag_id, COUNT(*) FROM note_tags GROUP BY tag_id";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int tagId = sqlite3_column_int(stmt, 0);
            int count = sqlite3_column_int(stmt, 1);
            counts[tagId] = count;
        }
        sqlite3_finalize(stmt);
    }
    return counts;
}

bool Database::CreateTag(Tag& tag) {
    const char* sql = "INSERT INTO tags (name, tag_order) VALUES (?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, tag.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, tag.order);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            tag.id = (int)sqlite3_last_insert_rowid(m_db);
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
    }
    return false;
}

bool Database::UpdateTag(const Tag& tag) {
    const char* sql = "UPDATE tags SET name = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text16(stmt, 1, tag.name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, tag.id);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

bool Database::DeleteTag(int id) {
    // First, remove associations from note_tags
    const char* sql_note_tags = "DELETE FROM note_tags WHERE tag_id = ?";
    sqlite3_stmt* stmt_note_tags;

    if (sqlite3_prepare_v2(m_db, sql_note_tags, -1, &stmt_note_tags, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_note_tags, 1, id);
        sqlite3_step(stmt_note_tags); // Don't need to check result, just execute
        sqlite3_finalize(stmt_note_tags);
    } else {
        return false; // Could not prepare statement
    }

    // Then, delete the tag itself
    const char* sql_tags = "DELETE FROM tags WHERE id = ?";
    sqlite3_stmt* stmt_tags;

    if (sqlite3_prepare_v2(m_db, sql_tags, -1, &stmt_tags, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt_tags, 1, id);
        bool success = (sqlite3_step(stmt_tags) == SQLITE_DONE);
        sqlite3_finalize(stmt_tags);
        return success;
    }
    return false;
}

bool Database::ReorderTag(int tagId, int newOrder) {
    // This is a complex operation, for now we just update the order.
    // A full implementation would shift other tags.
    const char* sql = "UPDATE tags SET tag_order = ? WHERE id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, newOrder);
        sqlite3_bind_int(stmt, 2, tagId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::vector<Database::Tag> Database::GetNoteTags(int noteId) {
    std::vector<Tag> tags;
    const char* sql = "SELECT t.id, t.name, t.tag_order FROM tags t INNER JOIN note_tags nt ON t.id = nt.tag_id WHERE nt.note_id = ? ORDER BY t.tag_order";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, noteId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Tag tag;
            tag.id = sqlite3_column_int(stmt, 0);
            tag.name = reinterpret_cast<const wchar_t*>(sqlite3_column_text16(stmt, 1));
            tag.order = sqlite3_column_int(stmt, 2);
            tags.push_back(tag);
        }
        sqlite3_finalize(stmt);
    }
    return tags;
}

bool Database::AddTagToNote(int noteId, int tagId) {
    const char* sql = "INSERT OR IGNORE INTO note_tags (note_id, tag_id) VALUES (?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, noteId);
        sqlite3_bind_int(stmt, 2, tagId);
        int result = sqlite3_step(stmt);
        bool success = (result == SQLITE_DONE);
        if (!success) {
            fprintf(stderr, "AddTagToNote failed: noteId=%d, tagId=%d, result=%d, error=%s\n", 
                noteId, tagId, result, sqlite3_errmsg(m_db));
        }
        sqlite3_finalize(stmt);
        return success;
    } else {
        fprintf(stderr, "AddTagToNote prepare failed: %s\n", sqlite3_errmsg(m_db));
    }
    return false;
}

bool Database::RemoveTagFromNote(int noteId, int tagId) {
    const char* sql = "DELETE FROM note_tags WHERE note_id = ? AND tag_id = ?";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, noteId);
        sqlite3_bind_int(stmt, 2, tagId);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}

std::string Database::GetSetting(const std::string& key, const std::string& defaultValue) {
    const char* sql = "SELECT value FROM settings WHERE key = ?";
    sqlite3_stmt* stmt;
    std::string value = defaultValue;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (val) value = val;
        }
        sqlite3_finalize(stmt);
    }
    return value;
}

bool Database::SetSetting(const std::string& key, const std::string& value) {
    const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_STATIC);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return success;
    }
    return false;
}


void Database::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}
