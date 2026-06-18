#include "AkatsukiDB/Engine/AkatsukiEngine.hpp"
#include <sqlite3.h>
#include <chrono>
#include <iostream>
#include <filesystem>

using Clock = std::chrono::high_resolution_clock;

double ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

const int ROWS = 1e4;
const int WARMUP_RUNS = 4;

void BenchAkatsuki() {
    std::filesystem::remove_all("./bench_akatsuki_data");
    AkatsukiEngine db("./bench_akatsuki_data");

    db.Execute("CREATE TABLE bench { int id PK AUTO, str name NOT NULL, float val }");

    std::cout << "\n=== AkatsukiDB ===\n";

    auto t1 = Clock::now();
    for (int i = 0; i < ROWS; i++) {
        db.Execute("INSERT INTO bench { name: \"row" + std::to_string(i)
                   + "\", val: " + std::to_string(i * 1.5) + " }");
    }
    auto t2 = Clock::now();
    std::cout << "10k INSERTs (no flush):    " << ms(t1, t2) << " ms\n";

    for (int i = 0; i < WARMUP_RUNS; i++) {
        for (int j = 0; j < ROWS; j += 2)
            db.Execute("SELECT * FROM bench WHERE id = " + std::to_string(j + 1));
    }
    t1 = Clock::now();
    for (int i = 0; i < ROWS; i += 2) {
        db.Execute("SELECT * FROM bench WHERE id = " + std::to_string(i + 1));
    }
    t2 = Clock::now();
    std::cout << "PK point queries (warm):   " << ms(t1, t2) << " ms\n";

    const int SCAN_REPEATS = 50;
    std::string scanQuery = "SELECT * FROM bench WHERE val > 14850.0"; // fixed: real 1% selectivity

    for (int i = 0; i < WARMUP_RUNS; i++) db.Execute(scanQuery);
    t1 = Clock::now();
    for (int i = 0; i < SCAN_REPEATS; i++) db.Execute(scanQuery);
    t2 = Clock::now();
    std::cout << "Full scan (avg of 50):     " << ms(t1, t2) / SCAN_REPEATS << " ms\n";

    db.Execute("CREATE INDEX idx_val ON bench (val)");

    for (int i = 0; i < WARMUP_RUNS; i++) db.Execute(scanQuery);
    t1 = Clock::now();
    for (int i = 0; i < SCAN_REPEATS; i++) db.Execute(scanQuery);
    t2 = Clock::now();
    std::cout << "Indexed range scan (avg of 50): " << ms(t1, t2) / SCAN_REPEATS << " ms\n";


    db.Execute("CREATE TABLE bench2 { int id PK AUTO, int category, float val }");
    for (int i = 0; i < ROWS; i++) {
        db.Execute("INSERT INTO bench2 { category: " + std::to_string(i % 10)
                   + ", val: " + std::to_string(i * 1.5) + " }");
    }
    for (int i = 0; i < WARMUP_RUNS; i++)
        db.Execute("SELECT category, COUNT(*), AVG(val) FROM bench2 GROUP BY category");
    t1 = Clock::now();
    db.Execute("SELECT category, COUNT(*), AVG(val) FROM bench2 GROUP BY category");
    t2 = Clock::now();
    std::cout << "GROUP BY 10k rows (warm):  " << ms(t1, t2) << " ms\n";
}

void BenchSQLite() {
    std::filesystem::remove("./bench_sqlite.db");
    sqlite3* db;
    sqlite3_open("./bench_sqlite.db", &db);
    sqlite3_exec(db, "CREATE TABLE bench (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT NOT NULL, val REAL)", nullptr, nullptr, nullptr);

    std::cout << "\n=== SQLite ===\n";

    auto t1 = Clock::now();
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO bench (name, val) VALUES (?, ?)", -1, &stmt, nullptr);
    for (int i = 0; i < ROWS; i++) {
        std::string name = "row" + std::to_string(i);
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, i * 1.5);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    auto t2 = Clock::now();
    std::cout << "10k INSERTs:               " << ms(t1, t2) << " ms\n";

    t1 = Clock::now();
    for (int i = 0; i < ROWS; i += 10) {
        sqlite3_prepare_v2(db, "SELECT * FROM bench WHERE id = ?", -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, i + 1);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    t2 = Clock::now();
    std::cout << "PK point queries:          " << ms(t1, t2) << " ms\n";

    t1 = Clock::now();
    sqlite3_exec(db, "SELECT * FROM bench WHERE val > 14850.0", nullptr, nullptr, nullptr);
    t2 = Clock::now();
    std::cout << "Full scan:                 " << ms(t1, t2) << " ms\n";

    sqlite3_exec(db, "CREATE INDEX idx_val ON bench(val)", nullptr, nullptr, nullptr);

    t1 = Clock::now();
    sqlite3_exec(db, "SELECT * FROM bench WHERE val > 14850.0", nullptr, nullptr, nullptr);
    t2 = Clock::now();
    std::cout << "Indexed range scan:        " << ms(t1, t2) << " ms\n";

    sqlite3_exec(db, "CREATE TABLE bench2 (id INTEGER PRIMARY KEY, category INTEGER, val REAL)",
                 nullptr, nullptr, nullptr);
    sqlite3_prepare_v2(db, "INSERT INTO bench2 (category, val) VALUES (?, ?)", -1, &stmt, nullptr);
    for (int i = 0; i < ROWS; i++) {
        sqlite3_bind_int(stmt, 1, i % 10);
        sqlite3_bind_double(stmt, 2, i * 1.5);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

    t1 = Clock::now();
    sqlite3_exec(db, "SELECT category, COUNT(*), AVG(val) FROM bench2 GROUP BY category",
                 nullptr, nullptr, nullptr);
    t2 = Clock::now();
    std::cout << "GROUP BY 10k rows:         " << ms(t1, t2) << " ms\n";

    sqlite3_close(db);
}

int main() {
    BenchAkatsuki();
    BenchSQLite();
    return 0;
}