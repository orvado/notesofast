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
    
    return CreateSchema();
}

std::vector<Note> Database::GetAllNotes() {
    std::vector<Note> notes;
    const char* sql = "SELECT id, title, content, color_id, is_archived, is_pinned, created_at, modified_at FROM notes ORDER BY modified_at DESC";
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Note note;
            note.id = sqlite3_column_int(stmt, 0);
            note.title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            note.content = content ? content : "";
            note.color_id = sqlite3_column_int(stmt, 3);
            note.is_archived = sqlite3_column_int(stmt, 4) != 0;
            note.is_pinned = sqlite3_column_int(stmt, 5) != 0;
            note.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            note.modified_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
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

bool Database::CreateSchema() {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS notes ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    title TEXT NOT NULL,"
        "    content TEXT,"
        "    color_id INTEGER DEFAULT 0,"
        "    is_archived INTEGER DEFAULT 0,"
        "    is_pinned INTEGER DEFAULT 0,"
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

void Database::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}
