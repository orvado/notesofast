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
    return true;
}

void Database::Close() {
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}
