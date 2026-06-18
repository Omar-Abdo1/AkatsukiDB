#include <cstdint>
#include <unordered_set>
#include <vector>

#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Engine/QueryResult.hpp"
#include "AkatsukiDB/Table/RowSerializer.hpp"

QueryResult Executor::ExecuteInsert(InsertStatement& stmt) {
    std::string tableName = stmt.TableName;
    std::transform(tableName.begin(), tableName.end(), tableName.begin(), ::tolower);
    if (!_registry.TableExists(tableName))
        return QueryResult::Error("Table Name " + stmt.TableName + " does not exist");

     auto& def = _registry.GetTable(tableName);
    auto tmIt = _tables.find(tableName);
    if (tmIt == _tables.end())
        return QueryResult::Error("Table manager not loaded for " + tableName);

    TableManager* tm =tmIt->second.get();

    // Set of column names (lowercase)
    std::unordered_set<std::string> colNames;
    for (const auto& col : def.Columns) {
        std::string lower = col.Name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        colNames.insert(lower);
    }

    int count = 0;
    for (auto& rowDict : stmt.Rows) {
        // Validate columns exist
        for (const auto& [k, v] : rowDict) {
            std::string kLower = k;
            std::transform(kLower.begin(), kLower.end(), kLower.begin(), ::tolower);
            if (!colNames.count(kLower))
                return QueryResult::Error("Column " + k + " Not Found");
        }

        // Apply defaults
        for (const auto& col : def.Columns) {
            if (col.Default.has_value() &&
                (!rowDict.count(col.Name) || std::holds_alternative<std::monostate>(rowDict[col.Name]))) {
                rowDict[col.Name] = col.Default.value();
                }
        }

        // Auto-increment
        if (def.AutoIncrement) {
            const std::string& pk = def.PrimaryKey[0];
            if (!rowDict.count(pk) || std::holds_alternative<std::monostate>(rowDict[pk])) { // id=null
                rowDict[pk] = def.NextAutoValue;
                def.NextAutoValue++;
                _registry.SaveTable(def);
            }
        }

        // NOT NULL
        for (const auto& col : def.Columns) {
            if (!col.Nullable) {
                auto it = rowDict.find(col.Name);
                if (it == rowDict.end() || std::holds_alternative<std::monostate>(it->second))
                    return QueryResult::Error("NOT NULL violation: '" + col.Name + "' cannot be null.");
            }
        }

        // Primary key uniqueness
        auto pkKey = BuildKeyForTree(rowDict, def.PrimaryKey);
        auto pkIndexIt = _indexes.find(tableName);
        if (pkIndexIt == _indexes.end() || pkIndexIt->second.empty())
            return QueryResult::Error("Primary key index not found");
        BPlusTree * pkTree = pkIndexIt->second[0].second.get();
        auto pkRes = pkTree->PointQuery(pkKey);
        if (!pkRes.empty())
            return QueryResult::Error("PRIMARY KEY violation: key already exists.");

        // Foreign key checks
        for (const auto& fk : def.ForeignKeys) {
            auto it = rowDict.find(fk.Column);

            if (it == rowDict.end() || std::holds_alternative<std::monostate>(it->second))
                continue;

            const DbObject& val = it->second;
            auto refIt = _indexes.find(fk.RefTable);
            if (refIt == _indexes.end() || refIt->second.empty())
                return QueryResult::Error("Referenced table '" + fk.RefTable + "' has no index.");
            BPlusTree * refTree = refIt->second[0].second.get();
            std::vector<DbObject> keyVals = {val};
            IndexKey fkKey((std::span<const DbObject>(keyVals)));
            auto res = refTree->PointQuery(fkKey);
            if (res.empty())
                return QueryResult::Error("FOREIGN KEY violation: '" + fk.Column + "' = " + DbObjectToString(val) + " not found in " + fk.RefTable + ".");
        }

        // Check secondary unique indexes
        const auto& idxList = _indexes[tableName];
        for (size_t i = 0; i < def.Indexes.size(); ++i) {
            const auto& idxDef = def.Indexes[i];
            if (idxDef.IsUnique && !idxDef.IsPrimary) {
                auto key = BuildKeyForTree(rowDict, idxDef.Columns);
                BPlusTree* idxTree = idxList[i].second.get();
                auto res = idxTree->PointQuery(key);
                if (res.empty()==true)
                    return QueryResult::Error("UNIQUE violation on index '" + idxDef.Name + "'.");
            }
        }

        // Write row to storage
        std::vector<std::uint8_t> bytes = RowSerializer::Serialize(rowDict, def.Columns, def.RowSizeBytes);
        auto [pageId, slotIndex] = tm->InsertRow(bytes);

        // Update all indexes
        for (size_t i = 0; i < def.Indexes.size(); ++i) {
            BPlusTree* idxTree = idxList[i].second.get();
            auto key = BuildKeyForTree(rowDict, def.Indexes[i].Columns);
            idxTree->Insert({key, pageId, static_cast<short>(slotIndex)});
        }

        if (def.AutoIncrement) {
            const std::string& pk = def.PrimaryKey[0];
            auto it = rowDict.find(pk);
            if (it != rowDict.end() && !std::holds_alternative<std::monostate>(it->second) ) {
                int providedId = std::get<int>(it->second);
                if (providedId >= def.NextAutoValue) {
                    def.NextAutoValue = providedId + 1;
                    _registry.SaveTable(def);
                }
            }
        }
        ++count;
    }
    return QueryResult::Affected(count);
}
