//
// Created by omarabdo on 6/12/26.
//

#include <iostream>

#include "AkatsukiDB/AQL/Executor.hpp"

QueryResult Executor::ExecuteSelect(SelectStatement& stmt) {

    // ── 1. VALIDATE ───────────────────────────────────────────────
    auto err = _validator.ValidateSelect(stmt);
    if (err) return QueryResult::Error(*err);

    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    const auto& def = _registry.GetTable(name);
    auto& tm        = *_tables[name];

    // ── 2. SCAN MAIN TABLE ────────────────────────────────────────
    // planner decides: FullScan / PointQuery / RangeQuery
    auto plan    = _scanPlanner.Decide(name, stmt.Where.get());
    auto entries = GetRowEntries(tm, def, plan);

    // ── 3. DESERIALIZE + PREFIX ───────────────────────────────────
    // every column gets: employees.name, e.name (if alias), name
    std::vector<DbRow> rows;
    for (auto& entry : entries) {
        auto raw = RowSerializer::Deserialize(entry.Bytes, def.Columns);
        DbRow prefixed;
        PrefixRow(prefixed, raw, name, stmt.Alias);
        rows.push_back(std::move(prefixed));
    }

    // ── 4. JOIN ───────────────────────────────────────────────────
    // for each join: scan right table, prefix, hash join
    for (const auto& join : stmt.Joins) {
        std::string jName = join.TableName;
        std::transform(jName.begin(), jName.end(), jName.begin(), ::tolower);
        const auto& jDef = _registry.GetTable(jName);
        auto& jTm        = *_tables[jName];

        // scan join table (always full scan — no pushdown yet)
        auto jEntries = jTm.FullScan();
        std::vector<DbRow> right;
        for (auto& e : jEntries) {
            auto raw = RowSerializer::Deserialize(e.Bytes, jDef.Columns);
            DbRow prefixed;
            PrefixRow(prefixed, raw, jName, join.Alias);
            right.push_back(std::move(prefixed));
        }

        // get join columns from ON clause
        std::string leftCol, rightCol;
        GetJoinColumns(*join.On, leftCol, rightCol);

        rows = HashJoin(rows, right, leftCol, rightCol,
            join.Type == JoinType::Left);
    }

    // ── 5. WHERE FILTER ───────────────────────────────────────────
    // index scan already filtered main condition
    // now apply remaining conditions
    if (plan.Type == ScanType::Full && stmt.Where) {
        // full scan: apply entire WHERE
        std::vector<DbRow> filtered;
        for (auto& row : rows)
            if (EvaluateBool(*stmt.Where, row))
                filtered.push_back(std::move(row));
        rows = std::move(filtered);
    } else if (plan.FilterAfter) {
        // index scan handled part of WHERE, apply the rest
        std::vector<DbRow> filtered;
        for (auto& row : rows)
            if (EvaluateBool(*plan.FilterAfter, row))
                filtered.push_back(std::move(row));
        rows = std::move(filtered);
    }
    // if WHERE had cross-table conditions (JOIN ON with extra filters)
    // those are already applied above since rows are merged

    // ── 6. GROUP BY + AGGREGATES ──────────────────────────────────
    // collapses rows into groups, computes COUNT/SUM/AVG/MIN/MAX
    if (!stmt.GroupBy.empty())
        rows = ApplyGroupBy(rows, stmt.GroupBy, stmt.Columns);

    // ── 7. HAVING ─────────────────────────────────────────────────
    // filter on aggregate results (like WHERE but after GROUP BY)
    if (stmt.Having) {
        std::vector<DbRow> filtered;
        for (auto& row : rows)
            if (EvaluateBool(*stmt.Having, row))
                filtered.push_back(std::move(row));
        rows = std::move(filtered);
    }

    // ── 8. WINDOW FUNCTIONS ───────────────────────────────────────
    // runs after GROUP BY, assigns values per row without collapsing
    bool hasWindow = false;
    for (const auto& sc : stmt.Columns)
        if (sc.IsWindow) { hasWindow = true; break; }
    if (hasWindow)
        rows = ApplyWindowFunctions(rows, stmt.Columns);

    // ── 9. ORDER BY ───────────────────────────────────────────────
    // after window so you can ORDER BY rn, rank etc
    if (!stmt.OrderBy.empty())
        ApplyOrderBy(rows, stmt.OrderBy);

    // ── 10. OFFSET ────────────────────────────────────────────────
    if (stmt.Offset > 0) {
        if (stmt.Offset >= (int)rows.size())
            rows.clear();
        else
            rows.erase(rows.begin(), rows.begin() + stmt.Offset);
    }

    // ── 11. LIMIT ─────────────────────────────────────────────────
    if (stmt.Limit > 0 && stmt.Limit < (int)rows.size())
        rows.resize(stmt.Limit);

    // ── 12. PROJECT ───────────────────────────────────────────────
    // keep only requested columns, evaluate expressions, apply aliases
    // SELECT *, SELECT id name, SELECT sal*1.1 AS bonus, SELECT COUNT(*) AS cnt
    auto outCols   = GetOutputColumns(stmt.Columns, def, stmt.Joins);
    auto projected = ProjectRows(rows, stmt.Columns);

    // ── 13. DISTINCT ──────────────────────────────────────────────
    // MUST be after projection — compare only selected columns
    if (stmt.IsDistinct)
        projected = ApplyDistinct(projected);

    return QueryResult::Success(outCols, std::move(projected));
}

void Executor::GetJoinColumns(Expression& on,
    std::string& leftCol, std::string& rightCol)
{
    auto* bin = dynamic_cast<BinaryExpr*>(&on);
    if (!bin || bin->Op != "=")
        throw std::runtime_error("JOIN ON must be: col = col");

    auto* lcr = dynamic_cast<ColumnRef*>(bin->Left.get());
    auto* rcr = dynamic_cast<ColumnRef*>(bin->Right.get());
    if (!lcr || !rcr)
        throw std::runtime_error("JOIN ON must reference columns");

    leftCol  = lcr->TableName.has_value()
        ? *lcr->TableName + "." + lcr->Column : lcr->Column;
    rightCol = rcr->TableName.has_value()
        ? *rcr->TableName + "." + rcr->Column : rcr->Column;
}

// get output column names for QueryResult.Columns
std::vector<std::string> Executor::GetOutputColumns(
    const std::vector<SelectColumn>& cols,
    const TableDefinition& def,
    const std::vector<JoinClause>& joins)
{
    for (const auto& sc : cols)
        if (sc.IsStar) {
            std::vector<std::string> result;
            for (const auto& col : def.Columns)
                result.push_back(col.Name);
            for (const auto& join : joins) {
                const auto& jDef = _registry.GetTable(join.TableName);
                for (const auto& col : jDef.Columns)
                    result.push_back(join.TableName + "." + col.Name);
            }
            return result;
        }

    std::vector<std::string> result;
    for (const auto& sc : cols) {
        if (!sc.Alias.empty()) {
            result.push_back(sc.Alias);
        } else if (auto* cr = dynamic_cast<ColumnRef*>(sc.Column.get())) {
            if (cr->WasQualified && cr->TableName.has_value())
                result.push_back(*cr->TableName + "." + cr->Column); // user wrote e.name
            else
                result.push_back(cr->Column);  // user wrote just name → show name
        } else if (auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get())) {
            result.push_back(fn->Name);
        } else {
            result.push_back("expr");
        }
    }
    return result;
}

std::vector<DbRow> Executor::ApplyDistinct(const std::vector<DbRow>& rows) {
    std::vector<DbRow> result;
    std::vector<std::string> seen;

    for (const auto& row : rows) {
        std::string fp;
        for (const auto& [k, v] : row)
            fp += k + "=" + DbObjectToString(v) + "|";

        bool found = false;
        for (const auto& s : seen)
            if (s == fp) { found = true; break; }

        if (!found) {
            seen.push_back(fp);
            result.push_back(row);
        }
    }
    return result;
}

// project each row to only the requested columns
std::vector<DbRow> Executor::ProjectRows(
    const std::vector<DbRow>& rows,
    const std::vector<SelectColumn>& cols)
{
    for (const auto& sc : cols)
        if (sc.IsStar) return rows;

    std::vector<DbRow> result;
    for (const auto& row : rows) {
        DbRow projected;
        for (const auto& sc : cols) {
            // build output key
            std::string outKey;
            if (!sc.Alias.empty())
                outKey = sc.Alias;
            else if (auto* cr = dynamic_cast<ColumnRef*>(sc.Column.get()))
                outKey = (cr->WasQualified && cr->TableName.has_value())
                    ? *cr->TableName + "." + cr->Column
                    : cr->Column;
            else if (auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get()))
                outKey = fn->Name;
            else
                outKey = "expr";

            // try to find value — multiple fallbacks
            DbObject val{std::monostate{}};

            // 1. exact outKey match (handles GROUP BY precomputed + prefixed cols)
            auto it = row.find(outKey);
            if (it != row.end()) {
                val = it->second;
            }
            // 2. evaluate expression (handles arithmetic, plain columns, etc.)
            else {
                val = GetValue(*sc.Column, row);
            }
            // 3. if still null and it's a ColumnRef, try all variants
            if (std::holds_alternative<std::monostate>(val)) {
                if (auto* cr = dynamic_cast<ColumnRef*>(sc.Column.get())) {
                    // try table.column
                    if (cr->TableName.has_value()) {
                        auto q = row.find(*cr->TableName + "." + cr->Column);
                        if (q != row.end()) val = q->second;
                    }
                    // try just column
                    if (std::holds_alternative<std::monostate>(val)) {
                        auto u = row.find(cr->Column);
                        if (u != row.end()) val = u->second;
                    }
                }
            }

            projected[outKey] = val;
        }
        result.push_back(std::move(projected));
    }
    return result;
}

void Executor::ApplyOrderBy(std::vector<DbRow>& rows,
    const std::vector<OrderByClause>& order)
{
    for (int i = 0; i < (int)rows.size() - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < (int)rows.size(); ++j) {
            // compare rows[j] vs rows[best]
            int cmp = 0;
            for (const auto& clause : order) {
                auto aIt = rows[j].find(clause.Column);
                auto bIt = rows[best].find(clause.Column);
                DbObject a = aIt != rows[j].end()
                    ? aIt->second : DbObject{std::monostate{}};
                DbObject b = bIt != rows[best].end()
                    ? bIt->second : DbObject{std::monostate{}};
                cmp = CompareAny(a, b);
                if (clause.Descending) cmp = -cmp;
                if (cmp != 0) break;
            }
            if (cmp < 0) best = j;
        }
        if (best != i) std::swap(rows[i], rows[best]);
    }
}

// group rows by GROUP BY columns
std::vector<DbRow> Executor::ApplyGroupBy(
    const std::vector<DbRow>& rows,
    const std::vector<std::string>& groupCols,
    const std::vector<SelectColumn>& selectCols)
{
    // build groups — key is concatenated group values
    std::vector<std::string> groupKeys;
    std::vector<std::vector<const DbRow*>> groups;

    for (const auto& row : rows) {
        // build group key
        std::string key;
        for (const auto& col : groupCols) {
            auto it = row.find(col);
            key += (it != row.end() ? DbObjectToString(it->second) : "NULL");
            key += "|";
        }

        // find existing group or create new one
        int idx = -1;
        for (int i = 0; i < (int)groupKeys.size(); ++i)
            if (groupKeys[i] == key) { idx = i; break; }

        if (idx == -1) {
            groupKeys.push_back(key);
            groups.push_back({});
            idx = groups.size() - 1;
        }
        groups[idx].push_back(&row);
    }

    // compute one result row per group
    std::vector<DbRow> result;
    for (auto& group : groups)
        result.push_back(ComputeGroup(group, groupCols, selectCols));
    return result;
}

DbRow Executor::ComputeGroup(
    const std::vector<const DbRow*>& group,
    const std::vector<std::string>& groupCols,
    const std::vector<SelectColumn>& selectCols)
{
    DbRow result;

    // copy GROUP BY values from first row
    for (const auto& col : groupCols) {
        auto it = group[0]->find(col);
        if (it != group[0]->end()) result[col] = it->second;
    }

    // compute each aggregate in SELECT list
    for (const auto& sc : selectCols) {
        if (sc.IsStar) continue;
        auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get());
        if (!fn) continue;

        std::string alias = sc.Alias.empty() ? fn->Name : sc.Alias;
        std::string fname = fn->Name;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

        // get argument column name
        std::string argCol;
        if (!fn->Arguments.empty())
            if (auto* cr = dynamic_cast<ColumnRef*>(fn->Arguments[0].get()))
                argCol = cr->Column;

        if (fname == "count") {
            result[alias] = (int)group.size();
        }
        else if (!argCol.empty()) {
            if (fname == "sum") {
                double sum = 0;
                for (auto* row : group) {
                    auto it = row->find(argCol);
                    if (it != row->end()) {
                        double d; if (TryDouble(it->second, d)) sum += d;
                    }
                }
                result[alias] = sum;
            }
            else if (fname == "avg") {
                double sum = 0; int cnt = 0;
                for (auto* row : group) {
                    auto it = row->find(argCol);
                    if (it != row->end()) {
                        double d; if (TryDouble(it->second, d)) { sum += d; cnt++; }
                    }
                }
                result[alias] = cnt == 0
                    ? DbObject(std::monostate{}) : DbObject(sum / cnt);
            }
            else if (fname == "min") {
                DbObject min{std::monostate{}};
                for (auto* row : group) {
                    auto it = row->find(argCol);
                    if (it == row->end()) continue;
                    if (std::holds_alternative<std::monostate>(min)
                        || CompareAny(it->second, min) < 0)
                        min = it->second;
                }
                result[alias] = min;
            }
            else if (fname == "max") {
                DbObject max{std::monostate{}};
                for (auto* row : group) {
                    auto it = row->find(argCol);
                    if (it == row->end()) continue;
                    if (std::holds_alternative<std::monostate>(max)
                        || CompareAny(it->second, max) > 0)
                        max = it->second;
                }
                result[alias] = max;
            }
        }
    }
    return result;
}

std::vector<DbRow> Executor::HashJoin(
    const std::vector<DbRow>& left,
    const std::vector<DbRow>& right,
    const std::string& leftCol,
    const std::string& rightCol,
    bool isLeft)
{
    // build map from right
    std::unordered_map<std::string, std::vector<const DbRow*>> map;
    for (const auto& row : right) {
        auto it = row.find(rightCol);
        std::string key = it != row.end()
            ? DbObjectToString(it->second) : "__null__";
        map[key].push_back(&row);
    }

    std::vector<DbRow> result;
    for (const auto& lRow : left) {
        auto it = lRow.find(leftCol);
        std::string key = it != lRow.end()
            ? DbObjectToString(it->second) : "__null__";

        auto mapIt = map.find(key);
        if (mapIt != map.end()) {
            for (const DbRow* rRow : mapIt->second) {
                DbRow merged = lRow;
                for (const auto& [k, v] : *rRow)
                    if (!merged.count(k))
                        merged[k] = v;
                result.push_back(std::move(merged));
            }
        } else if (isLeft) {
            result.push_back(lRow); // LEFT JOIN — keep left row
        }
    }
    return result;
}

// Write this helper first:
void Executor::PrefixRow(DbRow& target,
               const DbRow& source,
               const std::string& table,
               const std::string& alias)
{
    for (const auto& [col, val] : source) {
        target[table + "." + col] = val;        // employees.name
        if (!alias.empty())
            target[alias + "." + col] = val;    // e.name
        if (!target.count(col))
            target[col] = val;                  // name (first table wins)
    }
}

std::vector<DbRow> Executor::ApplyWindowFunctions(
    std::vector<DbRow>& rows,
    const std::vector<SelectColumn>& selectCols)
{
    for (const auto& sc : selectCols) {
        if (!sc.IsWindow) continue;

        std::string fname = sc.WindowFunc;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

        std::string outKey = sc.Alias.empty() ? sc.WindowFunc : sc.Alias;

        // ── PARTITION ─────────────────────────────────────────────
        // group row indexes by partition key
        std::vector<std::string>              partKeys;
        std::vector<std::vector<int>>         partitions;

        for (int i = 0; i < (int)rows.size(); ++i) {
            // build partition key from PARTITION BY columns
            std::string key;
            for (const auto& col : sc.PartitionBy) {
                auto it = rows[i].find(col);
                key += (it != rows[i].end()
                    ? DbObjectToString(it->second) : "NULL") + "|";
            }

            int idx = -1;
            for (int k = 0; k < (int)partKeys.size(); ++k)
                if (partKeys[k] == key) { idx = k; break; }

            if (idx == -1) {
                partKeys.push_back(key);
                partitions.push_back({});
                idx = partitions.size() - 1;
            }
            partitions[idx].push_back(i);
        }

        // ── APPLY FUNCTION PER PARTITION ─────────────────────────
        for (auto& partition : partitions) {
            // sort partition by window ORDER BY
            if (!sc.WindowOrder.empty()) {
                std::sort(partition.begin(), partition.end(),
                    [&](int a, int b) {
                        for (const auto& ob : sc.WindowOrder) {
                            auto aIt = rows[a].find(ob.Column);
                            auto bIt = rows[b].find(ob.Column);
                            DbObject av = aIt != rows[a].end()
                                ? aIt->second : DbObject{std::monostate{}};
                            DbObject bv = bIt != rows[b].end()
                                ? bIt->second : DbObject{std::monostate{}};
                            int cmp = CompareAny(av, bv);
                            if (ob.Descending) cmp = -cmp;
                            if (cmp != 0) return cmp < 0;
                        }
                        return false;
                    });
            }

            // assign value to each row in this partition
            if (fname == "row_number") {
                for (int rank = 0; rank < (int)partition.size(); ++rank)
                    rows[partition[rank]][outKey] = rank + 1;
            }
            else if (fname == "rank") {
                // same value = same rank, next rank skips
                int rank = 1;
                for (int i = 0; i < (int)partition.size(); ++i) {
                    if (i > 0) {
                        // compare current row with previous
                        bool same = true;
                        for (const auto& ob : sc.WindowOrder) {
                            auto prev = rows[partition[i-1]].find(ob.Column);
                            auto curr = rows[partition[i]].find(ob.Column);
                            if (prev == rows[partition[i-1]].end() ||
                                curr == rows[partition[i]].end()   ||
                                CompareAny(prev->second, curr->second) != 0) {
                                same = false; break;
                            }
                        }
                        if (!same) rank = i + 1;
                    }
                    rows[partition[i]][outKey] = rank;
                }
            }
            else if (fname == "dense_rank") {
                int rank = 1;
                for (int i = 0; i < (int)partition.size(); ++i) {
                    if (i > 0) {
                        bool same = true;
                        for (const auto& ob : sc.WindowOrder) {
                            auto prev = rows[partition[i-1]].find(ob.Column);
                            auto curr = rows[partition[i]].find(ob.Column);
                            if (prev == rows[partition[i-1]].end() ||
                                curr == rows[partition[i]].end()   ||
                                CompareAny(prev->second, curr->second) != 0) {
                                same = false; break;
                            }
                        }
                        if (!same) rank++;
                    }
                    rows[partition[i]][outKey] = rank;
                }
            }
            else if (fname == "sum" || fname == "avg" ||
                     fname == "min" || fname == "max" || fname == "count")
            {
                // get argument column
                std::string argCol;
                if (auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get()))
                    if (!fn->Arguments.empty())
                        if (auto* cr = dynamic_cast<ColumnRef*>(
                                fn->Arguments[0].get()))
                            argCol = cr->Column;

                // compute aggregate over whole partition
                double sum = 0; int cnt = 0;
                DbObject minVal{std::monostate{}}, maxVal{std::monostate{}};

                for (int i : partition) {
                    if (fname == "count") { cnt++; continue; }
                    auto it = rows[i].find(argCol);
                    if (it == rows[i].end()) continue;
                    double d;
                    if (TryDouble(it->second, d)) { sum += d; cnt++; }
                    if (std::holds_alternative<std::monostate>(minVal) ||
                        CompareAny(it->second, minVal) < 0)
                        minVal = it->second;
                    if (std::holds_alternative<std::monostate>(maxVal) ||
                        CompareAny(it->second, maxVal) > 0)
                        maxVal = it->second;
                }

                DbObject partResult;
                if (fname == "count") partResult = cnt;
                else if (fname == "sum") partResult = sum;
                else if (fname == "avg") partResult = cnt == 0 ? DbObject{std::monostate{}} : DbObject{sum/cnt};
                else if (fname == "min") partResult = minVal;
                else if (fname == "max") partResult = maxVal;

                // assign SAME value to every row in partition
                for (int i : partition)
                    rows[i][outKey] = partResult;
            }
        }
    }
    return rows;
}