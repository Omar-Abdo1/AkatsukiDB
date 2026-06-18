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
    auto scanned = GetScannedRows(tm, def, plan);

    struct RowChange {
        int PageId, SlotIndex;
        DbRow OldRow, NewRow;
        std::vector<IndexKey> OldKeys, NewKeys;
    };

    std::vector<RowChange> changes;

    for (auto& [PageId, SlotIndex, Row] : scanned) {
        RowChange change;
        change.PageId    = PageId;
        change.SlotIndex = SlotIndex;
        change.OldRow    = Row;

        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name])
                change.OldKeys.push_back(BuildKeyForTree(Row, idxDef.Columns));

        DbRow newRow = Row;
        for (auto& [col, expr] : stmt.Assignments)
            newRow[col] = GetValue(*expr, Row);

        // NOT NULL check
        for (const auto& col : def.Columns) {
            if (!col.Nullable) {
                auto it = newRow.find(col.Name);
                if (it == newRow.end()
                    || std::holds_alternative<std::monostate>(it->second))
                    return QueryResult::Error(
                        "NOT NULL violation: '" + col.Name + "'.");
            }
        }

        for (const auto& fk : def.ForeignKeys) {
            // is this FK column being changed?
            if (!stmt.Assignments.count(fk.Column)) continue;

            auto newVal = newRow.find(fk.Column);
            if (newVal == newRow.end()
                || std::holds_alternative<std::monostate>(newVal->second))
                continue;

            auto refIdxIt = _indexes.find(fk.RefTable);
            if (refIdxIt == _indexes.end() || refIdxIt->second.empty())
                return QueryResult::Error(
                    "Referenced table '" + fk.RefTable + "' has no index.");

            std::vector<DbObject> vals = { newVal->second };
            auto res = refIdxIt->second[0].second->PointQuery(
                IndexKey(std::span<const DbObject>(vals)));
            if (res.empty())
                return QueryResult::Error(
                    "FK violation: '" + fk.Column + "' = "
                    + DbObjectToString(newVal->second)
                    + " not found in '" + fk.RefTable + "'.");
        }

        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name])
                change.NewKeys.push_back(BuildKeyForTree(newRow, idxDef.Columns));

        if (_indexes.count(name)) {
            auto& idxList = _indexes[name];
            for (size_t i = 0; i < idxList.size(); i++) {
                auto& [idxDef, tree] = idxList[i];
                if (change.OldKeys[i] == change.NewKeys[i]) continue;
                if (!idxDef.IsUnique) continue;
                auto res = tree->PointQuery(change.NewKeys[i]);
                if (!res.empty())
                    return QueryResult::Error(
                        idxDef.IsPrimary
                        ? "PRIMARY KEY violation on UPDATE."
                        : "UNIQUE violation: index '" + idxDef.Name + "'.");
            }
        }

        change.NewRow = std::move(newRow);
        changes.push_back(std::move(change));
    }

    for (auto& c : changes) {
        auto newBytes = RowSerializer::Serialize(
            c.NewRow, def.Columns, def.RowSizeBytes);

        tm.UpdateRow(c.PageId, c.SlotIndex, newBytes);

        if (_indexes.count(name)) {
            auto& idxList = _indexes[name];
            for (size_t i = 0; i < idxList.size(); i++) {
                auto& [idxDef, tree] = idxList[i];
                if (c.OldKeys[i] == c.NewKeys[i]) continue;
                tree->Delete(c.OldKeys[i]);
                tree->Insert({c.NewKeys[i], c.PageId,
                    static_cast<short>(c.SlotIndex)});
            }
        }
        ++count;
    }

    return QueryResult::Affected(count);
}