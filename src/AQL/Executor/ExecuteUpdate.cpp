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

    // ── PHASE 1: validate ALL rows first ─────────────────────────
    // build list of (oldRow, newRow, entry) — touch nothing yet
    struct RowChange {
        const RowEntry*      Entry;
        DbRow                OldRow;
        DbRow                NewRow;
        std::vector<IndexKey>OldKeys;
        std::vector<IndexKey>NewKeys;
    };
    std::vector<RowChange> changes;

    for (auto& entry : entries) {
        auto row = RowSerializer::Deserialize(entry.Bytes, def.Columns);

        if (!PassesFilter(row, plan)) continue;
        if (plan.Type == ScanType::Full && stmt.Where)
            if (!EvaluateBool(*stmt.Where, row)) continue;

        // save old row + old keys
        RowChange change;
        change.Entry  = &entry;
        change.OldRow = row;

        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name])
                change.OldKeys.push_back(BuildKeyForTree(row, idxDef.Columns));

        // apply SET to get new row
        DbRow newRow = row;
        for (auto& [col, expr] : stmt.Assignments)
            newRow[col] = GetValue(*expr, row);

        // NOT NULL check — before any write
        for (const auto& col : def.Columns) {
            if (!col.Nullable) {
                auto it = newRow.find(col.Name);
                if (it == newRow.end()
                    || std::holds_alternative<std::monostate>(it->second))
                    return QueryResult::Error(
                        "NOT NULL violation: '" + col.Name + "'.");
            }
        }

        // compute new keys
        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name])
                change.NewKeys.push_back(BuildKeyForTree(newRow, idxDef.Columns));

        // PK uniqueness check — before any write
        if (_indexes.count(name)) {
            auto& idxList = _indexes[name];
            for (size_t i = 0; i < idxList.size(); i++) {
                auto& [idxDef, tree] = idxList[i];
                if (change.OldKeys[i].CompareTo(change.NewKeys[i]) == 0)
                    continue; // key unchanged — no conflict possible
                if (!idxDef.IsUnique) continue;
                auto res = tree->PointQuery(change.NewKeys[i]);
                if (!res.empty())
                    return QueryResult::Error(
                        idxDef.IsPrimary
                        ? "PRIMARY KEY violation on UPDATE."
                        : "UNIQUE violation on UPDATE: index '" + idxDef.Name + "'.");
            }
        }

        change.NewRow = std::move(newRow);
        changes.push_back(std::move(change));
    }

    // ── PHASE 2: all valid — now write ───────────────────────────
    for (auto& c : changes) {
        // write new bytes to .tbl
        auto newBytes = RowSerializer::Serialize(
            c.NewRow, def.Columns, def.RowSizeBytes);
        tm.UpdateRow(c.Entry->PageId, c.Entry->SlotIndex, newBytes);

        // update indexes
        if (_indexes.count(name)) {
            auto& idxList = _indexes[name];
            for (size_t i = 0; i < idxList.size(); i++) {
                auto& [idxDef, tree] = idxList[i];
                if (c.OldKeys[i].CompareTo(c.NewKeys[i]) == 0) continue;
                tree->Delete(c.OldKeys[i]);
                tree->Insert({c.NewKeys[i], c.Entry->PageId,
                    static_cast<short>(c.Entry->SlotIndex)});
            }
        }
        ++count;
    }

    return QueryResult::Affected(count);
}
