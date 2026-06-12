//
// Created by omarabdo on 6/12/26.
//


#include "AkatsukiDB/AQL/Executor.hpp"

// ExecuteUpdate.cpp
#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Table/RowSerializer.hpp"

QueryResult Executor::ExecuteUpdate(UpdateStatement& stmt) {
    auto err = _validator.ValidateUpdate(stmt);
    if (err) return QueryResult::Error(*err);

    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    const auto& def = _registry.GetTable(name);
    auto& tm        = *_tables[name];
    int count       = 0;

    auto plan    = _scanPlanner.Decide(name, stmt.Where.get());
    auto entries = GetRowEntries(tm, def, plan);

    for (auto& entry : entries) {
        auto row = RowSerializer::Deserialize(entry.Bytes, def.Columns);

        // apply scan filter
        if (!PassesFilter(row, plan)) continue;

        // apply full WHERE if full scan
        if (plan.Type == ScanType::Full && stmt.Where)
            if (!EvaluateBool(*stmt.Where, row)) continue;

        // save old keys
        std::vector<IndexKey> oldKeys;
        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name])
                oldKeys.push_back(BuildKeyForTree(row, idxDef.Columns));

        // apply SET
        for (auto& [col, expr] : stmt.Assignments)
            row[col] = GetValue(*expr, row);

        // NOT NULL check
        for (const auto& col : def.Columns) {
            if (!col.Nullable) {
                auto it = row.find(col.Name);
                if (it == row.end() ||
                    std::holds_alternative<std::monostate>(it->second))
                    return QueryResult::Error(
                        "NOT NULL violation: '" + col.Name + "'.");
            }
        }

        // write updated bytes
        auto newBytes = RowSerializer::Serialize(row, def.Columns, def.RowSizeBytes);
        tm.UpdateRow(entry.PageId, entry.SlotIndex, newBytes);

        // update all indexes
        if (_indexes.count(name)) {
            auto& idxList = _indexes[name];
            for (size_t i = 0; i < idxList.size(); i++) {
                auto& [idxDef, tree] = idxList[i];
                auto newKey = BuildKeyForTree(row, idxDef.Columns);
                if (oldKeys[i].CompareTo(newKey) != 0) {
                    if (idxDef.IsPrimary) {
                        auto res = tree->PointQuery(newKey);
                        if (!res.empty())
                            return QueryResult::Error(
                                "PRIMARY KEY violation on UPDATE.");
                    }
                    tree->Delete(oldKeys[i]);
                    tree->Insert({newKey, entry.PageId,
                        static_cast<short>(entry.SlotIndex)});
                }
            }
        }
        ++count;
    }
    return QueryResult::Affected(count);
}
