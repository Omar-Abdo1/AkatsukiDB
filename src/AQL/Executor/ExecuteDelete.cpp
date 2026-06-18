// ExecuteDelete.cpp
#include "AkatsukiDB/AQL/Executor.hpp"

QueryResult Executor::ExecuteDelete(DeleteStatement& stmt) {
    auto err = _validator.ValidateDelete(stmt);
    if (err) return QueryResult::Error(*err);

    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    const auto& def = _registry.GetTable(name);
    auto& tm = *_tables[name];

    auto plan = _scanPlanner.Decide(name, stmt.Where.get());
    auto scanned = GetScannedRows(tm, def, plan);

    for (const auto& rowInfo : scanned) {
        // can delete this row from the table ?
        auto checkErr = CanDelete(name, rowInfo.Row, def);
        if (checkErr)
            return QueryResult::Error(*checkErr);
    }

    int count = 0;
    for (const auto& rowInfo : scanned) {
        DoDelete(name, rowInfo.PageId, rowInfo.SlotIndex, rowInfo.Row, def);
        ++count;
    }
    return QueryResult::Affected(count);
}

std::optional<std::string> Executor::CanDelete(const std::string& tableName,
                                               const DbRow& row,
                                               const TableDefinition& def) {
    auto it = _referencedBy.find(tableName);
    if (it == _referencedBy.end())
        return std::nullopt;

    // see the tables that has relationship with it using the reverse graph
    for (auto& [fromTable, fk] : it->second) {
        // Get the Pk for the current Row i want to delete it
        DbObject pkVal;
        bool found = false;
        for (const auto& pk : def.PrimaryKey) {
            if (pk == fk.RefColumn) { // primary in me = foreign , id in dept , see dept_id in employee
                auto rowIt = row.find(pk);
                if (rowIt != row.end()) {
                    pkVal = rowIt->second;
                    found = true;
                }
                break;
            }
        }
        if (!found)
            continue;

        // Check if there are any row in refTable has this id
        // see employee with dept_id = 2
        if (!HasDependents(fromTable, fk.Column, pkVal))
            continue;

        if (fk.OnDeleteAction == OnDelete::RESTRICT) {
            return "FK violation: '" + tableName + "' row referenced by '"
                 + fromTable + "." + fk.Column + "'. Cannot delete.";
        }

        if (fk.OnDeleteAction == OnDelete::CASCADE) {
            // now i need to delete any thing from the RefTable that has Fk = my Pk
            const auto& fromDef = _registry.GetTable(fromTable);
            auto& fromTm = *_tables[fromTable];
            auto fromEntries = fromTm.FullScan();

            for (auto& e : fromEntries) {
                auto depRow = RowSerializer::Deserialize(e.Bytes, fromDef.Columns);
                auto colIt = depRow.find(fk.Column);
                if (colIt == depRow.end())
                    continue;
                if (!AreEqual(colIt->second, pkVal))
                    continue;

                auto err = CanDelete(fromTable, depRow, fromDef);
                if (err)
                    return err;
            }
        }
    }
    return std::nullopt;
}

bool Executor::HasDependents(const std::string& fromTable,
                             const std::string& fkCol,
                             const DbObject& pkVal) {
    // search in the table for Fk that equal the Pk

    // Try to use an index on the foreign key column
    auto idxIt = _indexes.find(fromTable);
    if (idxIt != _indexes.end()) {
        for (auto& [idxDef, tree] : idxIt->second) {
            if (idxDef.Columns.size() == 1 && idxDef.Columns[0] == fkCol) {
                std::vector<DbObject> vals = {pkVal};
                auto res = tree->PointQuery(IndexKey(std::span<const DbObject>(vals)));
                return !res.empty();
            }
        }
    }

    // full table scan
    auto tmIt = _tables.find(fromTable);
    if (tmIt == _tables.end())
        return false;
    const auto& fromDef = _registry.GetTable(fromTable);
    auto entries = tmIt->second->FullScan();
    for (auto& e : entries) {
        auto row = RowSerializer::Deserialize(e.Bytes, fromDef.Columns);
        auto it = row.find(fkCol); // see dept_id in employee
        if (it != row.end() && AreEqual(it->second, pkVal)) // dept_id = id in departments ?
            return true;
    }
    return false;
}

void Executor::DoDelete(const std::string& name,
                        int pageId,
                        int slotIndex,
                        const DbRow& row,
                        const TableDefinition& def) {
    // try to delete from table this row
    auto it = _referencedBy.find(name);
    // first delete from my Dependents
    if (it != _referencedBy.end()) {
        for (auto& [fromTable, fk] : it->second) {
            if (fk.OnDeleteAction != OnDelete::CASCADE)
                continue;

            DbObject pkVal;
            for (const auto& pk : def.PrimaryKey) {
                if (pk == fk.RefColumn) {
                    auto rowIt = row.find(pk);
                    if (rowIt != row.end()) {
                        pkVal = rowIt->second;
                        break;
                    }
                }
            }

            const auto& fromDef = _registry.GetTable(fromTable);
            auto& fromTm = *_tables[fromTable];
            auto fromEntries = fromTm.FullScan();

            for (auto& e : fromEntries) {
                auto depRow = RowSerializer::Deserialize(e.Bytes, fromDef.Columns);
                auto colIt = depRow.find(fk.Column);
                if (colIt == depRow.end())
                    continue;
                if (!AreEqual(colIt->second, pkVal))
                    continue;

                DoDelete(fromTable, e.PageId, e.SlotIndex, depRow, fromDef); // delete the row from table if it has also some edges
            }
        }
    }

    _tables[name]->DeleteRow(pageId, slotIndex);

    if (_indexes.find(name)!= _indexes.end()) {
        // from every index delete this row
        for (auto& [idxDef, tree] : _indexes[name]) {
            auto key = BuildKeyForTree(row, idxDef.Columns);
            tree->Delete(key);
        }
    }
}