#pragma once
#include <string>
#include <vector>
#include "sqlite3.h"

class Database {
public:
    Database();
    ~Database();
    bool Initialize(const std::string& dbPath);
    void Close();

private:
    sqlite3* m_db;
};
