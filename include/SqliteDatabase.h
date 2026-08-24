#pragma once

#include "DatabaseError.h"

#include <mutex>
#include <string>

struct sqlite3;

namespace dnf
{
constexpr int SQLITE_DATABASE_SCHEMA_VERSION = 4;

class SqliteDatabase
{
public:
    explicit SqliteDatabase(const std::string& databasePath);
    ~SqliteDatabase();

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    sqlite3* Handle() const;
    std::mutex& ConnectionMutex();

private:
    void InitializeSchema();

    sqlite3* database_ = nullptr;
    std::mutex connectionMutex_;
};
} // namespace dnf
