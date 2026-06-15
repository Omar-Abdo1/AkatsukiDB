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

    const auto& def = _registry.GetTable(tableName);
    auto tmIt = _tables.find(tableName);
    if (tmIt == _tables.end())
        return QueryResult::Error("Table manager not loaded for " + tableName);
    auto& tm = *tmIt->second;

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
                rowDict[col.Name] = *col.Default;
                }
        }

        // Auto-increment
        TableDefinition& mutableDef = const_cast<TableDefinition&>(def);
        if (mutableDef.AutoIncrement) {
            const std::string& pk = mutableDef.PrimaryKey[0];
            if (!rowDict.count(pk) || std::holds_alternative<std::monostate>(rowDict[pk])) {
                rowDict[pk] = mutableDef.NextAutoValue;
                mutableDef.NextAutoValue++;
                _registry.SaveTable(mutableDef);
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
        auto& pkTree = *pkIndexIt->second[0].second;
        auto pkRes = pkTree.PointQuery(pkKey);
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
            auto& refTree = *refIt->second[0].second;
            // Build key from single value (assuming FK is single column)
            std::vector<DbObject> keyVals = {val};
            IndexKey fkKey((std::span<const DbObject>(keyVals)));
            auto res = refTree.PointQuery(fkKey);
            if (res.empty())
                return QueryResult::Error("FOREIGN KEY violation: '" + fk.Column + "' = " + DbObjectToString(val) + " not found in " + fk.RefTable + ".");
        }

        // Check secondary unique indexes
        const auto& idxList = _indexes[tableName];
        for (size_t i = 0; i < def.Indexes.size(); ++i) {
            const auto& idxDef = def.Indexes[i];
            if (idxDef.IsUnique && !idxDef.IsPrimary) {
                auto key = BuildKeyForTree(rowDict, idxDef.Columns);
                auto& idxTree = *idxList[i].second;
                auto res = idxTree.PointQuery(key);
                if (!res.empty())
                    return QueryResult::Error("UNIQUE violation on index '" + idxDef.Name + "'.");
            }
        }

        // Write row to storage
        std::vector<std::uint8_t> bytes = RowSerializer::Serialize(rowDict, def.Columns, def.RowSizeBytes);
        auto [pageId, slotIndex] = tm.InsertRow(bytes);

        // Update all indexes
        for (size_t i = 0; i < def.Indexes.size(); ++i) {
            auto& idxTree = *idxList[i].second;
            auto key = BuildKeyForTree(rowDict, def.Indexes[i].Columns);
            idxTree.Insert({key, pageId, static_cast<short>(slotIndex)});
        }

        if (mutableDef.AutoIncrement) {
            const std::string& pk = mutableDef.PrimaryKey[0];
            auto it = rowDict.find(pk);
            if (it != rowDict.end() && !std::holds_alternative<std::monostate>(it->second)) {
                // Explicit primary key value provided
                int providedId = std::get<int>(it->second);
                if (providedId >= mutableDef.NextAutoValue) {
                    mutableDef.NextAutoValue = providedId + 1;
                    _registry.SaveTable(mutableDef);
                }
            }
        }

        ++count;
    }
    return QueryResult::Affected(count);
}
