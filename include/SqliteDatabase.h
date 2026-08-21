#pragma once

#include <mutex>
#include <stdexcept>
#include <string>

struct sqlite3;

namespace dnf
{
constexpr int SQLITE_DATABASE_SCHEMA_VERSION = 3;

class DatabaseError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

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
