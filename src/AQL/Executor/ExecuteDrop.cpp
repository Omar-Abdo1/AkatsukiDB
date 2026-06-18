//
// Created by omarabdo on 6/12/26.
//

#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Engine/QueryResult.hpp"

QueryResult Executor::ExecuteDropTable(DropTableStatement& stmt) { //  delete all files
    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (!_registry.TableExists(name))
        return QueryResult::Error("Table Name " + name + " not found");

    auto tblIt = _tables.find(name);
    if (tblIt != _tables.end()) {
        tblIt->second.reset();  // destructor flushes and closes
        _tables.erase(tblIt);
    }

    auto idxIt = _indexes.find(name);
    if (idxIt != _indexes.end()) {
        for (auto& [def, tree] : idxIt->second) {
            std::string idxPath = _layout.IndexFile(def.Name);
            if (std::filesystem::exists(idxPath)) std::filesystem::remove(idxPath);
            tree.reset();  // BPlusTree destructor closes files
        }
        _indexes.erase(idxIt);
    }

    // Delete physical files
    std::string tblPath = _layout.TableFile(name);
    std::string schPath = _layout.SchemaFile(name);
    if (std::filesystem::exists(tblPath)) std::filesystem::remove(tblPath);
    if (std::filesystem::exists(schPath)) std::filesystem::remove(schPath);

    _registry.DropTable(name);
    return QueryResult::Affected(0, "Table " + name + " dropped.");
}

QueryResult Executor::ExecuteTruncate(TruncateStatement& stmt) { // delete all but keep the table,schema,index just clear
    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (!_registry.TableExists(name))
        return QueryResult::Error("Table Name " + name + " not found");

TableDefinition& def = _registry.GetTable(name);

    auto tblIt = _tables.find(name);
    if (tblIt != _tables.end()) {
        tblIt->second.reset();
        _tables.erase(tblIt);
    }

    auto idxIt = _indexes.find(name);
    if (idxIt != _indexes.end()) {
        for (auto& [idxDef, tree] : idxIt->second) {
            std::string idxPath = _layout.IndexFile(idxDef.Name);
            if (std::filesystem::exists(idxPath)) std::filesystem::remove(idxPath);
            tree.reset();
        }
        _indexes.erase(idxIt);
    }

    // Delete .tbl file
    std::string tblPath = _layout.TableFile(name);
    if (std::filesystem::exists(tblPath)) std::filesystem::remove(tblPath);
    // Keep the .schema.json file

    // Reopen the table (creates fresh .tbl and indexes from schema)
    OpenTable(name);

    // Reset auto-increment counter
    def.NextAutoValue = 1;
    _registry.SaveTable(def);

    return QueryResult::Affected(0, "Table " + name + " truncated.");
}