# AkatsukiDB

A relational database engine built from scratch in C++20 — no database libraries, no ORMs, no shortcuts. Every byte written by hand: from raw file I/O to SQL parsing to B+ tree rebalancing to crash recovery.

```
SQL Query
    │
    ▼
┌─────────────┐
│  Tokenizer  │  splits text into tokens
└──────┬──────┘
       │
┌──────▼──────┐
│   Parser    │  recursive descent → typed AST
└──────┬──────┘
       │
┌──────▼──────────────┐
│ Validator & Binder  │  column existence, ambiguity, table binding
└──────┬──────────────┘
       │
┌──────▼──────┐
│  Planner    │  FullScan / PointQuery / RangeScan decision
└──────┬──────┘
       │
┌──────▼──────┐
│  Executor   │  runs the plan, enforces constraints
└──────┬──────┘
       │
┌──────▼──────────────────────────────────┐
│  Storage Engine                          │
│  BufferPool → PageManager → .tbl files  │
│  BPlusTree  → PageManager → .idx files  │
└─────────────────────────────────────────┘
```

---

## Storage Engine

### Page (`Storage/Page.hpp`)

Every unit of storage is a fixed 4,096-byte page matching OS page size.

```
Page layout (4096 bytes):
  Bytes  0– 1  │ SlotCount    (int16)  — how many rows in this page
  Bytes  2– 5  │ FreeOffset   (int32)  — next write position
  Bytes  6– 9  │ NextPageId   (int32)  — linked list to next data page
  Bytes 10–13  │ PageId       (int32)  — self-identification
  Bytes 14–15  │ Reserved
  Bytes 16–4095│ Row slots
```

- Template `Read<T>` / `Write<T>` for type-safe binary access via `std::memcpy`
- `MarkDirty()` / `ClearDirty()` — dirty tracking for buffer pool eviction
- `GetSlot(slotIndex, rowSize)` returns `std::span<uint8_t>` — zero-copy row access

### PageManager (`Storage/PageManager.hpp`)

Direct file I/O using `std::fstream` in binary mode.

- Magic bytes `AKTS` at file header for sanity checking on open
- `AllocatePage()` — seeks to end, writes 4096 zero bytes, returns new page ID
- `ReadPage(id)` — seeks to `id * 4096`, reads exactly 4096 bytes
- `WritePage(page)` — skips clean pages, writes dirty pages, calls `flush()`
- `fsync()` used in WAL to guarantee physical disk write beyond OS buffer

### LruCache (`Storage/LruCache.hpp`)

Doubly linked list + `std::unordered_map` — O(1) get and put.

- Sentinel head/tail nodes — no null checks in Add/Remove
- `OnEvict` callback — fires when capacity exceeded, writes dirty page via PageManager
- `unique_ptr<Node>` in map owns nodes; raw pointer in linked list for O(1) removal
- Capacity: 256 pages (1 MB per table)

### BufferPool (`Storage/BufferPool.hpp`)

Sits between executor and disk. All page access goes through here.

- `GetPage(id)` — checks LRU cache first, reads from disk on miss
- `FlushAll()` — iterates all cached pages, writes dirty ones
- RAII destructor calls `FlushAll()` — no data lost on clean shutdown
- Lambda captures `this` for `OnEvict` — avoids storing extra pointer

### StorageLayout (`Storage/StorageLayout.hpp`)

Centralises all file paths. One source of truth.

```
akatsuki_data/
├── tables/     employees.tbl
├── index/      pk_employees.idx   uq_employees_email.idx
├── schema/     employees.schema.json
└── wal/        wal_current.log
```

---

## Row Serialization (`Table/RowSerializer.hpp`)

Fixed-size binary layout. Each row occupies exactly `RowSizeBytes` bytes.

```
Row bytes:
  [col1 bytes][col2 bytes]...[colN bytes][9 null bitmap bytes][1 delete flag]
                                          └── 10 system bytes ──────────────┘
```

**Type sizes:**
| Type  | Bytes | Format                          |
|-------|-------|---------------------------------|
| int   | 4     | `memcpy` little-endian int32    |
| float | 8     | `memcpy` IEEE 754 double        |
| bool  | 1     | 0x00 or 0x01                    |
| str   | 104   | 4-byte length prefix + 100-byte buffer |

**Null bitmap (9 bytes = 72 bits):**
- Bit `colIndex` set → column is NULL
- Deserialization checks bit before reading bytes → returns `std::monostate`
- Supports up to 72 columns with NULL tracking

**Delete flag:**
- `rowBytes[rowSizeBytes - 1] = 1` — soft delete, PostgreSQL-style
- Row stays on disk; `FullScan()` skips deleted rows
- Physical space reclaimed by VACUUM (planned)

**Type coercion in Serialize:**
- `int` column receives `double` → truncates (`5.9 → 5`)
- `float` column receives `int` → promotes (`9000 → 9000.0`)
- `str` field tries `std::stoi` / `std::stod` as fallback

---

## Table Metadata

### ColumnDefinition
```cpp
struct ColumnDefinition {
    std::string Name, Type;
    int Offset, Size;
    bool Nullable, IsUnique;
    std::optional<DbObject> Default;
};
```

### TableDefinition
```cpp
struct TableDefinition {
    std::string Name;
    int RowSizeBytes, NextAutoValue;
    bool AutoIncrement;
    std::vector<ColumnDefinition> Columns;
    std::vector<std::string>      PrimaryKey;
    std::vector<ForeignKeyDef>    ForeignKeys;
    std::vector<IndexDefinition>  Indexes;
};
```

### TableRegistry (`Table/TableRegistry.hpp`)

- Loads all `.schema.json` files on startup via `nlohmann/json`
- `CreateTable` computes offsets, creates PK `IndexDefinition`, serialises JSON
- `SaveTable` used after auto-increment increments `NextAutoValue`
- In-memory `unordered_map<string, TableDefinition>` — O(1) schema lookup

---

## B+ Tree Index

Non-clustered. Data rows live in `.tbl`; index stores `(key → pageId, slotIndex)`.

### IndexKey (`Index/IndexKey.hpp`)

Fixed 128-byte key enabling composite keys and byte-by-byte comparison.

- `WriteValue(int)` — flips sign bit, converts to big-endian → correct numeric sort order for negative numbers
- `WriteValue(double)` — IEEE 754 bit manipulation, flips all bits if negative → correct sort for floats
- `WriteValue(string)` — raw UTF-8 bytes, left-aligned
- `Min()` — all zeros; `Max()` — all `0xFF`
- Operators `<`, `<=`, `>`, `>=`, `==` via `CompareTo` (byte-by-byte)
- Variadic template constructor for compile-time known types
- `span<const DbObject>` constructor for runtime value lists

### BPlusTreeNode (`Index/BPlusTreeNode.hpp`)

Wraps a `shared_ptr<Page>`. Node IS a page — no separate allocation.

```
Header (20 bytes):
  [1] NodeType  — 0=Internal, 1=Leaf
  [2] KeyCount
  [4] ParentPageId
  [4] NextLeafPageId   (-1 if none)
  [4] PrevLeafPageId   (-1 if none)
  [4] PageId
  [1] Reserved

Leaf entry (134 bytes):  [128 key][4 pageId][2 slotIndex]
Internal entry (132 bytes): [4 childPageId][128 key]
Internal layout: c0 k0 c1 k1 c2 ... — always one more child than keys

Capacity:
  Leaf:     (4096 - 20) / 134 = 30 entries
  Internal: (4096 - 20) / 132 = 30 children
```

- `FindKeyIndex(key)` — binary search lower bound → O(log m) per node
- Setters call `MarkDirty()` → buffer pool tracks changes automatically

### BPlusTree (`Index/BPlusTree.hpp`)

**Insert:**
1. `FindLeaf(key)` — walk internal nodes via `GetChildNodeForInternal`
2. If leaf not full → `InsertIntoLeaf` (shift right, place)
3. If full → `SplitLeaf`: sort all+new, split at midpoint, update linked list (`prev/next` pointers), promote mid key to parent
4. If internal full → `SplitInternal`: extract all keys+children, find insert position, split, promote median (not copied to children, unlike leaf)
5. If root splits → new root with two children, height increases

**Delete:**
1. `FindLeaf(key)` → `DeleteFromLeaf` (shift left)
2. If underflow (count < order/2):
   - Try `BorrowFromLeft` — rotate through parent separator
   - Try `BorrowFromRight` — rotate through parent separator
   - Merge — pull parent separator down, merge siblings, remove from parent
3. Root collapse — if root internal node has 0 keys, promote its only child
4. Leaf merge updates `prev/next` linked list pointers

**PointQuery:** finds leaf, walks right via `NextLeafPageId` while key matches — handles duplicates.

**RangeQuery:** finds start leaf via `FindLeaf`, walks linked list until key > end. Accepts `startOpen`/`endOpen` flags to exclude boundary values (`>` vs `>=`).

---

## SQL Layer

### AQL Syntax

AkatsukiDB uses AQL — a dialect of SQL with simplified CREATE syntax:

```sql
CREATE TABLE employees {
    int   id        PK AUTO,
    str   name      NOT NULL,
    float salary    DEFAULT 0.0,
    int   dept_id   FK departments.id CASCADE
}

INSERT INTO employees { name: "Omar", salary: 9000, dept_id: 1 }
INSERT INTO employees [{ name: "Sara" }, { name: "Ali" }]

UPDATE employees SET { salary = salary * 1.1 } WHERE dept_id = 1
DELETE FROM employees WHERE id = 5

SELECT e.name, d.name, AVG(e.salary) AS avg_sal
FROM employees e
JOIN departments d ON e.dept_id = d.id
WHERE e.salary > 5000
GROUP BY d.name
HAVING avg_sal > 7000
ORDER BY avg_sal DESC
LIMIT 10 OFFSET 0
```

### Tokenizer (`AQL/Tokenizer.hpp`)

Hand-written lexer. No regex. No parser generator.

- Line comments `//` and block comments `/* */`
- String literals with `"` or `'`
- Numbers: integer (`IntLiteral`) or float (`FloatLiteral`) detection by `.`
- Negative numbers: `-` followed immediately by digit
- Keywords: `select from where insert update delete create drop join...` (60+ keywords)
- Operators: `= != >= <= > <` — two-character operators detected with lookahead
- Line tracking for error messages
- `GetKeywords()` returns `static const unordered_set` — initialised once

### Parser (`AQL/Parser.hpp`)

Recursive descent. Operator precedence encoded in call chain:

```
ParseOr → ParseAnd → ParseNot → ParseComparison
       → ParseAddSub → ParseMulDiv → ParseTerm
```

Higher in the chain = lower precedence. `ParseTerm` handles atoms.

**Expressions:**
- `BinaryExpr` — arithmetic (`+`, `-`, `*`, `/`) and comparison (`=`, `!=`, `>`, `<`, `>=`, `<=`) and logical (`AND`, `OR`)
- `UnaryExpr` — `NOT`
- `Literal` — `DbObject` value (int, double, string, bool, null via `std::monostate`)
- `ColumnRef` — `Column` + optional `TableName` + `WasQualified` flag
- `FunctionExpr` — `Name` + `vector<unique_ptr<Expression>>` Arguments
- `IsNullExpr`, `BetweenExpr`, `InExpr`, `LikeExpr`

**Window functions parsed inline:**
```sql
ROW_NUMBER() OVER(PARTITION BY dept_id ORDER BY salary DESC) AS rn
```
`SelectColumn` carries `IsWindow`, `WindowFunc`, `PartitionBy`, `WindowOrder`.

**Memory:** all AST nodes are `unique_ptr` — automatic cleanup, no manual delete.

---

## Query Validator & Binder (`AQL/QueryValidatorAndBinder.hpp`)

Runs before execution. Catches errors without touching disk.

1. `BuildAvailable(mainTable, alias, joins)` → `map<key, tableName>`
   - Registers `col`, `table.col`, `alias.col` for every column in every table
   - Marks `"__ambiguous__"` if same unqualified name in multiple tables

2. `ValidateExpression` walks entire AST:
   - `ColumnRef` → looks up in available map → error if missing or ambiguous
   - **Binds** `cr->TableName = tableName` — downstream code knows exact table
   - `WasQualified` = true if user wrote `table.col` explicitly

3. Validates: table existence, JOIN table existence, WHERE columns, ORDER BY, GROUP BY columns

4. Detects illegal aggregate mixing: `SELECT count(*), id FROM t` without GROUP BY → error

---

## Scan Planner (`AQL/ScanPlanner.hpp`)

Rule-based optimizer. Decides how to fetch rows before execution.

```
Decide(tableName, WHERE expression):

1. FlattenAnd(WHERE) → [cond1, cond2, cond3]
   WHERE a=1 AND b>5 AND c<10 → [a=1, b>5, c<10]

2. For each condition:
   col = val  AND col has index → ScanType::Point
   col BETWEEN x AND y         → ScanType::Range (startOpen=false, endOpen=false)
   col > val                   → ScanType::Range (startOpen=true)
   col >= val                  → ScanType::Range (startOpen=false)
   col < val                   → ScanType::Range (endOpen=true)
   col <= val                  → ScanType::Range (endOpen=false)
   anything else               → stays in FilterAfter

3. First indexable condition → drives the scan
   Rest → vector<Expression*> FilterAfter, applied after scan
```

`GetScannedRows` applies `FilterAfter` after index scan, returns `vector<ScannedRow>` with `{PageId, SlotIndex, Row}`.

---

## Executor

### Constraints Enforced

| Constraint   | Where enforced                                       |
|-------------|------------------------------------------------------|
| NOT NULL     | INSERT, UPDATE — before writing                     |
| PRIMARY KEY  | INSERT — PointQuery before write; UPDATE — on key change |
| FOREIGN KEY  | INSERT — ref PointQuery; UPDATE — FK column change; DELETE — CanDelete chain |
| UNIQUE       | INSERT — secondary index PointQuery; UPDATE — on key change |
| FK CASCADE   | DELETE — DoDelete recurses with index-accelerated lookup |
| FK RESTRICT  | DELETE — CanDelete walks full chain before touching any row |
| AUTO         | Single int PK only — validated at CREATE TABLE time |

### ExecuteInsert — Steps
1. Column name validation
2. Apply DEFAULT values
3. AUTO INCREMENT (single-int PK only)
4. NOT NULL check
5. PK uniqueness via index PointQuery
6. FK existence check for each FK column
7. UNIQUE check on secondary indexes
8. `tm.InsertRow(bytes)` → get `(pageId, slotIndex)`
9. Insert into all indexes

### ExecuteSelect — Steps
1. Validate (QueryValidatorAndBinder)
2. Planner decides scan type
3. `GetScannedRows` → applies FilterAfter
4. Prefix rows: `employees.name`, `e.name`, `name`
5. JOIN: full scan each join table, HashJoin
6. WHERE filter on merged rows (handles cross-table conditions)
7. GROUP BY → `ComputeGroup` per group (uses `GetValue` on arg expressions)
8. Implicit single group if aggregates present with no GROUP BY
9. HAVING filter
10. Window functions (ROW_NUMBER, RANK, DENSE_RANK, aggregates OVER PARTITION)
11. ORDER BY (`std::sort` with multi-column comparator)
12. OFFSET + LIMIT
13. Projection (`ProjectRows` with three fallbacks)
14. DISTINCT (after projection — compares only selected columns)

### ExecuteUpdate — Two-Phase
**Phase 1 (validate all):** NOT NULL, FK column check, uniqueness on new keys — touch nothing  
**Phase 2 (write all):** `tm.UpdateRow`, delete old index keys, insert new index keys

### ExecuteDelete — Two-Phase
**Phase 1 (`CanDelete`):** walks entire FK cascade chain recursively — if any RESTRICT blocks, return error before touching anything  
**Phase 2 (`DoDelete`):** cascade delete dependents first (uses FK column index if exists, otherwise full scan), then soft-delete row, remove from all indexes

### FK Reverse Map
Built at startup. `_referencedBy["departments"] = [("employees", fkDef)]`  
Delete from `departments` → O(1) lookup of all tables that reference it.  
No looping all tables on every delete.

---

## Write-Ahead Log & Transactions

### WAL (`WAL/WalManager.hpp`)

Append-only log file. Every modification logged before disk write.

```
Record layout:
  [4]  Magic    = 0xABCD1234
  [4]  LSN      (monotonically increasing)
  [4]  TxnId
  [1]  Type     (Begin/Insert/Update/Delete/Commit/Rollback)
  [3]  Padding
  [4]  PageId
  [4]  SlotIndex
  [4]  RowDataSize
  [32] TableName
  [N]  Row before-image (N = RowDataSize bytes)
```

- `LogCommit` calls `Flush()` → `_file.flush()` + `fsync()` — actual physical disk guarantee
- `fsync` in C++ is required; `fflush` only flushes to OS buffer
- `ReadAll()` used for crash recovery — reads until Magic mismatch

### TransactionManager (`WAL/TransactionManager.hpp`)

- `Begin()` → assigns `TxnId`, writes WAL Begin record, sets `_activeTxnId`
- Single-threaded: one active transaction at a time
- Each modifying operation records a `TxnChange {Type, TableName, PageId, SlotIndex, BeforeData}`
- `Rollback()` iterates changes in **reverse** order:
  - INSERT undo → `tm.DeleteRow` + remove from indexes
  - UPDATE undo → `tm.UpdateRow(beforeData)` + restore index keys
  - DELETE undo → `tm.UndeleteRow(beforeData)` + re-insert to indexes

### Crash Recovery

On `AkatsukiEngine` startup, after opening all tables:
1. `WalManager::ReadAll()` reads every record
2. Find all TxnIds that have a Commit record
3. For every TxnId WITHOUT a Commit → undo all its changes in reverse
4. Uncommitted transactions evaporate — database is consistent

### Auto-Commit Mode

If no `BEGIN` is issued, each statement auto-starts and auto-commits its own transaction. `GetOrBeginTxn()` / `AutoCommit(txnId, wasAuto)` handle this transparently.

---

## Interactive REPL (`main.cpp`)

Built with **replxx** library.

- Syntax highlighting: keywords → magenta, strings → green, numbers → cyan
- Command history — saved to `.akatsuki_history` across sessions
- Query timing: `std::chrono::steady_clock` — displayed in milliseconds
- Formatted table output with dynamic column widths and box-drawing characters
- ANSI color codes: errors in red, success in green, metadata in dark gray
- `clear` / `cls` command clears terminal
- `exit` or `Ctrl+D` for clean shutdown

---

## Building

**Requirements:** C++20, CMake 3.20+, nlohmann/json, replxx

```bash
git clone https://github.com/yourname/AkatsukiDB
cd AkatsukiDB
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./AkatsukiDB_Cpp
```

---

## AQL Quick Reference

```sql
-- DDL
CREATE TABLE t { int id PK AUTO, str name NOT NULL, float salary DEFAULT 0.0 }
CREATE UNIQUE INDEX idx_name ON t (name)
DROP TABLE t
TRUNCATE TABLE t

-- DML
INSERT INTO t { name: "Omar", salary: 9000 }
INSERT INTO t [{ name: "Sara" }, { name: "Ali" }]
UPDATE t SET { salary = salary * 1.1 } WHERE id = 1
DELETE FROM t WHERE salary < 3000

-- Transactions
BEGIN
INSERT INTO t { name: "test" }
ROLLBACK

-- Query
SELECT * FROM t WHERE salary BETWEEN 5000 AND 10000
SELECT name, salary * 1.1 AS bonus FROM t ORDER BY bonus DESC LIMIT 10
SELECT dept, COUNT(*), AVG(salary) FROM t GROUP BY dept HAVING avg > 5000
SELECT name, ROW_NUMBER() OVER(PARTITION BY dept ORDER BY salary DESC) AS rn FROM t
SELECT e.name, d.name FROM employees e JOIN departments d ON e.dept_id = d.id

-- Utility
SHOW TABLES
SHOW SCHEMA employees
SHOW INDEXES employees
```

---

## Project Stats

| Component         | Lines of C++ | Description                    |
|------------------|--------------|--------------------------------|
| Storage Engine    | ~800         | Page, PageManager, BufferPool, LruCache |
| Row Serialization | ~200         | Serialize/Deserialize + null bitmap |
| B+ Tree           | ~900         | Full insert/delete/query with rebalancing |
| SQL Parser        | ~700         | Tokenizer + recursive descent parser |
| Executor          | ~1200        | All statements + constraints   |
| Scan Planner      | ~250         | Rule-based optimizer           |
| WAL + Transactions| ~500         | Crash recovery + ACID transactions |
| REPL              | ~250         | replxx + formatted output      |
| **Total**         | **~4800**    |                                |

---

## Planned

- [ ] MVCC snapshot isolation
- [ ] REST API (cpp-httplib)
- [ ] Benchmark vs SQLite
- [ ] Lazy table loading
- [ ] VACUUM (reclaim soft-deleted space)
- [ ] Concurrent reader-writer lock (`std::shared_mutex`)
