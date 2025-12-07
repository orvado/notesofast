#pragma once
#include <string>
#include <vector>
#include "sqlite3.h"
#include "note.h"

class Database {
public:
    Database();
    ~Database();
    bool Initialize(const std::string& dbPath);
    void Close();

    std::vector<Note> GetAllNotes();
    bool CreateNote(Note& note);
    bool UpdateNote(const Note& note);
    bool DeleteNote(int id);

private:
    bool CreateSchema();
    sqlite3* m_db;
};
