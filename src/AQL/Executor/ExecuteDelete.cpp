//
// Created by omarabdo on 6/12/26.
//

#include "AkatsukiDB/AQL/Executor.hpp"

// ExecuteDelete.cpp
#include "AkatsukiDB/AQL/Executor.hpp"

QueryResult Executor::ExecuteDelete(DeleteStatement& stmt) {
    auto err = _validator.ValidateDelete(stmt);
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

        if (!PassesFilter(row, plan)) continue;
        if (plan.Type == ScanType::Full && stmt.Where)
            if (!EvaluateBool(*stmt.Where, row)) continue;

        // FK dependent check
        auto fkErr = CheckDependents(name, row, def);
        if (fkErr) return QueryResult::Error(*fkErr);

        // soft delete in .tbl
        tm.DeleteRow(entry.PageId, entry.SlotIndex);

        // physical delete from all indexes
        if (_indexes.count(name))
            for (auto& [idxDef, tree] : _indexes[name]) {
                auto key = BuildKeyForTree(row, idxDef.Columns);
                tree->Delete(key);
            }
        ++count;
    }
    return QueryResult::Affected(count);
}

std::optional<std::string> Executor::CheckDependents(
    const std::string& tableName,
    const DbRow& row,
    const TableDefinition& def)
{
    auto it = _referencedBy.find(tableName);
    if (it == _referencedBy.end()) return std::nullopt;

    for (auto& [fromTable, fk] : it->second) {
        // find PK value being deleted
        DbObject pkVal;
        bool found = false;
        for (const auto& pk : def.PrimaryKey) {
            if (pk == fk.RefColumn) {
                auto rowIt = row.find(pk);
                if (rowIt != row.end()) {
                    pkVal = rowIt->second;
                    found = true;
                }
                break;
            }
        }
        if (!found) continue;

        // check if any row in fromTable references this value
        bool hasDeps = HasDependents(fromTable, fk.Column, pkVal);
        if (!hasDeps) continue;

        if (fk.OnDeleteAction == OnDelete::CASCADE) {
            // build a fake DELETE and recurse
            auto cascadeStmt = std::make_unique<DeleteStatement>();
            cascadeStmt->TableName = fromTable;
            auto bin  = std::make_unique<BinaryExpr>();
            auto cr   = std::make_unique<ColumnRef>();
            auto lit  = std::make_unique<Literal>();
            cr->Column  = fk.Column;
            lit->Value  = pkVal;
            bin->Left   = std::move(cr);
            bin->Op     = "=";
            bin->Right  = std::move(lit);
            cascadeStmt->Where = std::move(bin);
            auto res = ExecuteDelete(*cascadeStmt);
            if (res.IsError) return res.ErrorMessage;
        } else {
            return "FK violation: '" + tableName + "' row referenced by '"
                 + fromTable + "." + fk.Column + "'. Cannot delete.";
        }
    }
    return std::nullopt;
}

bool Executor::HasDependents(const std::string& fromTable,
    const std::string& fkCol, const DbObject& pkVal)
{
    // try index first
    auto idxIt = _indexes.find(fromTable);
    if (idxIt != _indexes.end())
        for (auto& [idxDef, tree] : idxIt->second)
            if (idxDef.Columns.size() == 1 && idxDef.Columns[0] == fkCol) {
                std::vector<DbObject> vals = {pkVal};
                auto res = tree->PointQuery(
                    IndexKey(std::span<const DbObject>(vals)));
                return !res.empty();
            }

    // full scan fallback
    auto tmIt = _tables.find(fromTable);
    if (tmIt == _tables.end()) return false;
    const auto& fromDef = _registry.GetTable(fromTable);
    auto entries = tmIt->second->FullScan();
    for (auto& e : entries) {
        auto row = RowSerializer::Deserialize(e.Bytes, fromDef.Columns);
        auto it  = row.find(fkCol);
        if (it != row.end() && AreEqual(it->second, pkVal))
            return true;
    }
    return false;
}