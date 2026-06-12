//
// Created by omarabdo on 6/12/26.
//

#include <unordered_set>

#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Engine/QueryResult.hpp"

QueryResult Executor::ExecuteCreateTable(CreateTableStatement& stmt) {
    std::string tableName = stmt.TableName;
    std::transform(tableName.begin(), tableName.end(), tableName.begin(), ::tolower);
    if (_registry.TableExists(tableName))
        return QueryResult::Error("Table Name " + stmt.TableName + " already exists");

    if (stmt.AutoIncrement) {
        if (stmt.PrimaryKey.size() != 1)
            return QueryResult::Error("AUTO can only be used with a single-column PRIMARY KEY.");
        const std::string& pkCol = stmt.PrimaryKey[0];
        const ColumnDefinition* pkDef = nullptr;
        for (const auto& col : stmt.Columns) {
            if (col.Name == pkCol) {
                pkDef = &col;
                break;
            }
        }
        if (!pkDef || pkDef->Type != "int")
            return QueryResult::Error("AUTO PRIMARY KEY must be an int column.");
    }

    // Check foreign keys
    for (const auto& fk : stmt.ForeignKeys) {
        std::string refTable = fk.RefTable;
        std::transform(refTable.begin(), refTable.end(), refTable.begin(), ::tolower);
        if (!_registry.TableExists(refTable))
            return QueryResult::Error("FK reference Table " + fk.RefTable + " not found");
        const auto& fkDef = _registry.GetTable(refTable);
        bool colFound = false;
        for (const auto& col : fkDef.Columns) {
            if (col.Name == fk.RefColumn) {
                colFound = true;
                break;
            }
        }
        if (!colFound)
            return QueryResult::Error("FK Column Name " + fk.RefColumn + " not found in " + fk.RefTable);
        bool isPK = false;
        for (const auto& pk : fkDef.PrimaryKey) {
            if (pk == fk.RefColumn) {
                isPK = true;
                break;
            }
        }
        if (!isPK)
            return QueryResult::Error("FK reference " + fk.RefColumn + "." + fk.RefTable + " is not PRIMARY KEY");
    }

    // Create the table definition (writes JSON schema)
    auto def = _registry.CreateTable(stmt.TableName, stmt.Columns, stmt.PrimaryKey,
                                     stmt.AutoIncrement, stmt.ForeignKeys);
    OpenTable(def.Name);  // creates .tbl file and loads indexes (none initially)
    return QueryResult::Affected(0, "Table '" + def.Name + "' created.");
}

QueryResult Executor::ExecuteCreateIndex(CreateIndexStatement& stmt) {
    std::string tableName = stmt.TableName;
    std::transform(tableName.begin(), tableName.end(), tableName.begin(), ::tolower);
    std::string indexName = stmt.IndexName;
    std::transform(indexName.begin(), indexName.end(), indexName.begin(), ::tolower);

    if (!_registry.TableExists(tableName))
        return QueryResult::Error("Table Name " + stmt.TableName + " not found");

    const auto& def = _registry.GetTable(tableName);
    std::unordered_set<std::string> colNames;
    for (const auto& col : def.Columns) {
        std::string lower = col.Name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        colNames.insert(lower);
    }
    for (const auto& col : stmt.Columns) {
        std::string lower = col;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (!colNames.count(lower))
            return QueryResult::Error("Column " + col + " Not Found");
    }

    auto& tm = *_tables[tableName];

    // Check if index already exists
    auto it = _indexes.find(tableName);
    if (it != _indexes.end()) {
        for (const auto& [idxDef, _] : it->second) {
            if (idxDef.Name == indexName)
                return QueryResult::Error("Index " + indexName + " already exists");
        }
    }

    IndexDefinition idxDef;
    idxDef.Name = indexName;
    idxDef.Columns = stmt.Columns;
    idxDef.IsPrimary = false;
    idxDef.IsUnique = stmt.IsUnique;

    std::string idxPath = _layout.IndexFile(indexName);
    auto tree = std::make_unique<BPlusTree>(idxPath);

    // Populate index from existing rows
    auto entries = tm.FullScan();
    for (const auto& entry : entries) {
        auto row = RowSerializer::Deserialize(entry.Bytes, def.Columns);
        auto key = BuildKeyForTree(row, stmt.Columns);
        if (stmt.IsUnique) {
            auto res = tree->PointQuery(key);
            if (!res.empty()) {
                tree.reset(); // destructor will close/delete?
                if (std::filesystem::exists(idxPath)) std::filesystem::remove(idxPath);
                return QueryResult::Error("Cannot Create UNIQUE Index - duplicate values exist in " + stmt.Columns[0]);
            }
        }
        tree->Insert({key, entry.PageId, static_cast<short>(entry.SlotIndex)});
    }


    // Save index definition to table schema
    TableDefinition defCopy = _registry.GetTable(tableName);

    defCopy.Indexes.push_back(idxDef);

    _registry.SaveTable(defCopy);

    // Add to in-memory map
    _indexes[tableName].emplace_back(idxDef, std::move(tree));

    return QueryResult::Affected(0, "Index " + indexName + " created on " + tableName + ".");
}