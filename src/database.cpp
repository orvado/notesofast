#include "database.h"
#include <iostream>

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

    return InitializeColors();
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

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            note.id = (int)sqlite3_last_insert_rowid(m_db);
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
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

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return true;
        }
        sqlite3_finalize(stmt);
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

void Database::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}
