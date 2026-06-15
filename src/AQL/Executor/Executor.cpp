//
// Created by omarabdo on 6/11/26.
//

#include "AkatsukiDB/AQL/Executor.hpp"

#include "AkatsukiDB/Storage/StorageLayout.hpp"

Executor::Executor(TableRegistry& registry,
                   StorageLayout& layout,
                   TableManagerMap& tables,
                   IndexMap& indexes,
                   ReferencedByMap& referencedBy)
    : _registry(registry)
    , _layout(layout)
    , _tables(tables)
    , _indexes(indexes)
    , _referencedBy(referencedBy)
    , _validator(registry),
   _scanPlanner(indexes)
{}

QueryResult Executor::Execute(IStatement& stmt) {
    try {
        // Using dynamic_cast to determine statement type virtual table / vptr
        if (auto* s = dynamic_cast<SelectStatement*>(&stmt))
            return ExecuteSelect(*s);
        if (auto* s = dynamic_cast<InsertStatement*>(&stmt))
            return ExecuteInsert(*s);
        if (auto* s = dynamic_cast<UpdateStatement*>(&stmt))
            return ExecuteUpdate(*s);
        if (auto* s = dynamic_cast<DeleteStatement*>(&stmt))
            return ExecuteDelete(*s);
        if (auto* s = dynamic_cast<CreateTableStatement*>(&stmt))
            return ExecuteCreateTable(*s);
        if (auto* s = dynamic_cast<CreateIndexStatement*>(&stmt))
            return ExecuteCreateIndex(*s);
        if (auto* s = dynamic_cast<DropTableStatement*>(&stmt))
            return ExecuteDropTable(*s);
        if (auto* s = dynamic_cast<TruncateStatement*>(&stmt))
            return ExecuteTruncate(*s);
        if (auto* s = dynamic_cast<ShowStatement*>(&stmt))
            return ExecuteShow(*s);
        return QueryResult::Error("Statement not supported yet.");
    } catch (const std::exception& ex) {
        return QueryResult::Error(ex.what());
    }
}
// Open a table (create .tbl file and load indexes)
void Executor::OpenTable(const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    const auto& def = _registry.GetTable(lowerName);

    std::unique_ptr<BufferPool> bp = std::make_unique<BufferPool>(_layout.TableFile(lowerName));

    _tables[lowerName] = std::make_unique<TableManager>(std::move(bp), def.RowSizeBytes);

    for (const auto& idxDef : def.Indexes) {
        std::string idxPath = _layout.IndexFile(idxDef.Name);
         std::unique_ptr<BPlusTree> tree = std::make_unique<BPlusTree>(idxPath);
        _indexes[lowerName].emplace_back(idxDef, std::move(tree));
    }
}


// Helper: Build IndexKey from row dictionary and column list
IndexKey Executor::BuildKeyForTree(const std::unordered_map<std::string, DbObject>& row,
                                   const std::vector<std::string>& columns) {
    std::vector<DbObject> values;
    values.reserve(columns.size());
    for (const auto& col : columns) {
        auto it = row.find(col);
        values.push_back(it != row.end() ? it->second : DbObject{});
    }
    return IndexKey(std::span<const DbObject>(values));
}

std::vector<DbRow> Executor::GetRowEntries(TableManager& tm,
    const TableDefinition& def, const ScanPlan& plan)
{
    std::vector<RowEntry> entries;

    if (plan.Type == ScanType::Point && plan.Index) {
        auto locs = plan.Index->PointQuery(plan.PointKey);
        for (auto& [pid, slot] : locs) {
            auto bytes = tm.ReadRow(pid, slot);
            entries.push_back({pid, slot, std::move(bytes)});
        }
    } else if (plan.Type == ScanType::Range && plan.Index) {
        auto locs = plan.Index->RangeQuery(plan.RangeStart, plan.RangeEnd,
                                           plan.RangeStartOpen, plan.RangeEndOpen);
        for (auto& [pid, slot] : locs) {
            auto bytes = tm.ReadRow(pid, slot);
            entries.push_back({pid, slot, std::move(bytes)});
        }
    } else {
        entries = tm.FullScan();
    }

    // Deserialize and apply post‑scan filters
    std::vector<DbRow> filtered;
    for (auto& entry : entries) {
        auto row = RowSerializer::Deserialize(entry.Bytes, def.Columns);
        if (PassesFilter(row, plan))
            filtered.push_back(std::move(row));
    }
    return filtered;
}

bool Executor::PassesFilter(const DbRow& row, const ScanPlan& plan) {
    for (auto* expr : plan.FilterAfter) {
        if (!EvaluateBool(*expr, row))
            return false;
    }
    return true;
}

// Executor for SHOW statements
QueryResult Executor::ExecuteShow(ShowStatement& stmt) {
    std::string what = stmt.What;
    std::transform(what.begin(), what.end(), what.begin(), ::tolower);
    if (what == "tables")
        return ShowTables();
    if (what == "schema")
        return ShowSchema(stmt.Target.value_or(""));
    if (what == "indexes")
        return ShowIndexes(stmt.Target.value_or(""));
    return QueryResult::Error("Unknown SHOW target: " + stmt.What);
}

QueryResult Executor::ShowTables() {
    auto names = _registry.GetAllTableNames();
    std::vector<std::unordered_map<std::string, DbObject>> rows;
    rows.reserve(names.size());
    for (const auto& n : names) {
        rows.push_back({{"table_name", n}});
    }
    return QueryResult::Success({"table_name"}, std::move(rows));
}

QueryResult Executor::ShowSchema(const std::string& tableName) {
    std::string lower = tableName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (!_registry.TableExists(lower))
        return QueryResult::Error("Table '" + tableName + "' not found.");
    const auto& def = _registry.GetTable(lower);
    std::vector<std::unordered_map<std::string, DbObject>> rows;
    rows.reserve(def.Columns.size());
    for (const auto& col : def.Columns) {
        rows.push_back({
            {"column", col.Name},
            {"type", col.Type},
            {"nullable", col.Nullable},
            {"default", col.Default ? DbObjectToString(*col.Default) : "none"}
        });
    }
    return QueryResult::Success({"column", "type", "nullable", "default"}, std::move(rows));
}

QueryResult Executor::ShowIndexes(const std::string& tableName) {
    std::string lower = tableName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (!_registry.TableExists(lower))
        return QueryResult::Error("Table '" + tableName + "' not found.");
    const auto& def = _registry.GetTable(lower);
    std::vector<std::unordered_map<std::string, DbObject>> rows;
    rows.reserve(def.Indexes.size());
    for (const auto& idx : def.Indexes) {
        std::string colsStr;
        for (size_t i = 0; i < idx.Columns.size(); ++i) {
            if (i > 0) colsStr += ",";
            colsStr += idx.Columns[i];
        }
        rows.push_back({
            {"name", idx.Name},
            {"columns", colsStr},
            {"IsPrimary", idx.IsPrimary},
            {"IsUnique", idx.IsUnique}
        });
    }
    return QueryResult::Success({"name", "columns", "IsPrimary", "IsUnique"}, std::move(rows));
}
