//
// Created by omarabdo on 6/12/26.
//

#include <iostream>
#include <unordered_set>

#include "AkatsukiDB/AQL/Executor.hpp"

QueryResult Executor::ExecuteSelect(SelectStatement& stmt) {

    // 1 validate the select columns , tables
    auto err = _validator.ValidateSelect(stmt);
    if (err) return QueryResult::Error(*err);

    std::string name = stmt.TableName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    const auto& def = _registry.GetTable(name);
    auto& tm        = *_tables[name];

    // 2 take the plan and decide fullscan/index
    auto plan    = _scanPlanner.Decide(name, stmt.Where.get());
    auto scanned = GetScannedRows(tm, def, plan);

    // 3 employees.name, e.name, name   make the prefix for each name
    std::vector<DbRow> rows;
    rows.reserve(scanned.size());
    if (stmt.Joins.empty()) {
        for (auto& sr : scanned)
            rows.push_back(std::move(sr.Row));
    } else {
        for (auto& sr : scanned) {
            DbRow prefixed;
            PrefixRow(prefixed, sr.Row, name, stmt.Alias);
            rows.push_back(std::move(prefixed));
        }
    }

    // 4 join
    for (const auto& join : stmt.Joins) { // current join is hash using full scan for each table !!
        std::string jName = join.TableName;
        std::transform(jName.begin(), jName.end(), jName.begin(), ::tolower);
        const auto& jDef = _registry.GetTable(jName);
        auto& jTm        = *_tables[jName];

        auto jEntries = jTm.FullScan();
        std::vector<DbRow> right;
        for (auto& e : jEntries) {
            auto raw = RowSerializer::Deserialize(e.Bytes, jDef.Columns);
            DbRow prefixed;
            PrefixRow(prefixed, raw, jName, join.Alias);
            right.push_back(std::move(prefixed));
        }

        std::string leftCol, rightCol;
        GetJoinColumns(*join.On, leftCol, rightCol);
        rows = HashJoin(rows, right, leftCol, rightCol,
            join.Type == JoinType::Left);
    }

    //5 where
    if (stmt.Where && !stmt.Joins.empty()) {
        std::vector<DbRow> filtered;
        for (auto& row : rows)
            if (EvaluateBool(*stmt.Where, row))
                filtered.push_back(std::move(row));
        rows = std::move(filtered);
    }

    bool hasAggregate = false;
    for (const auto& sc : stmt.Columns) {
        if (!sc.Column || sc.IsStar || sc.IsWindow) continue;
        if (dynamic_cast<FunctionExpr*>(sc.Column.get()))
        { hasAggregate = true; break; }
    }

    // group by
    if (!stmt.GroupBy.empty()) {
        rows = ApplyGroupBy(rows, stmt.GroupBy, stmt.Columns);
    } else if (hasAggregate) {
        std::vector<const DbRow*> singleGroup;
        for (const auto& row : rows)
            singleGroup.push_back(&row);
        rows = { ComputeGroup(singleGroup, {}, stmt.Columns) }; // only one ROW
    }

    auto outCols   = GetOutputColumns(stmt.Columns, def, stmt.Joins);
    auto projected = ProjectRows(rows, stmt.Columns);

    // 7. having is like where but we do it after we make the rows into groups
    if (stmt.Having) {
        std::vector<DbRow> filtered;
        for (auto& row : projected)
            if (EvaluateBool(*stmt.Having, row))
                filtered.push_back(std::move(row));
        projected = std::move(filtered);
    }

    // 8. window function
    bool hasWindow = false;
    for (const auto& sc : stmt.Columns)
        if (sc.IsWindow) { hasWindow = true; break; }
    if (hasWindow)
        projected = ApplyWindowFunctions(projected, stmt.Columns);

    // 9 order by
    if (!stmt.OrderBy.empty())
        ApplyOrderBy(projected, stmt.OrderBy);

    // 10 offest
    if (stmt.Offset > 0) { // remove {offset} from the result
        if (stmt.Offset >= (int)projected.size()) projected.clear();
        else projected.erase(projected.begin(), projected.begin() + stmt.Offset);
    }

    // 11 limit
    if (stmt.Limit > 0 && stmt.Limit < (int)projected.size())
        projected.resize(stmt.Limit);

    // 12
    if (stmt.IsDistinct)
        projected = ApplyDistinct(projected);

std::string planLabel = plan.Type == ScanType::Point ? "Index Scan (Point)"
                       : plan.Type == ScanType::Range ? "Index Scan (Range)"
                       : "Full Scan";
auto result = QueryResult::Success(outCols, std::move(projected));
result.PlanUsed = planLabel;
return result;
}

void Executor::PrefixRow(DbRow& target,
               const DbRow& source,
               const std::string& table,
               const std::optional<std::string>& alias)
{
    for (const auto& [col, val] : source) {
        target[table + "." + col] = val;        // employees.name
        if (alias.has_value())
            target[alias.value() + "." + col] = val;    // e.name
        if (!target.count(col))
            target[col] = val;                  // name (first table wins)
    }
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
        ? lcr->TableName.value() + "." + lcr->Column : lcr->Column;
    rightCol = rcr->TableName.has_value()
        ? *rcr->TableName + "." + rcr->Column : rcr->Column;
}

std::vector<DbRow> Executor::HashJoin(
    const std::vector<DbRow>& left,
    const std::vector<DbRow>& right,
    const std::string& leftCol,
    const std::string& rightCol,
    bool isLeft)
{
    // build map from right — SKIP NULL keys
    std::unordered_map<std::string, std::vector<const DbRow*>> map; // dept_id = id
    for (const auto& row : right) {
        auto it = row.find(rightCol); // see the id value
        if (it == row.end()
            || std::holds_alternative<std::monostate>(it->second))
            continue;
        std::string key = DbObjectToString(it->second);
        map[key].push_back(&row); // id -> row
    }

    std::vector<DbRow> result;
    for (const auto& lRow : left) {

        auto it = lRow.find(leftCol);

        if (it == lRow.end()
            || std::holds_alternative<std::monostate>(it->second)) {
            if (isLeft) result.push_back(lRow);
            continue;
            }

        std::string key = DbObjectToString(it->second);
        auto mapIt = map.find(key);

        if (mapIt != map.end()) { // found
            for (const DbRow* rRow : mapIt->second) {
                DbRow merged = lRow;
                for (const auto& [Rcol, Robject] : *rRow)
                    if (merged.find(Rcol)== merged.end())
                        merged[Rcol] = Robject;
                result.push_back(std::move(merged));
            }
        } else if (isLeft) {
            result.push_back(lRow);
        }
    }
    return result;
}

// transform the rows into groups
std::vector<DbRow> Executor::ApplyGroupBy(
    const std::vector<DbRow>& rows,
    const std::vector<std::string>& groupCols,
    const std::vector<SelectColumn>& selectCols)
{
    std::vector<std::string> groupKeys;
    std::vector<std::vector<const DbRow*>> groups;

    for (const auto& row : rows) {
        // build group key
        std::string key;
        for (int i=0;i<(int)groupCols.size();i++) {
            const auto &col = groupCols[i];
            if (i>0) key += "|";
            auto it = row.find(col);
            key += (it != row.end() ? DbObjectToString(it->second) : "NULL");
        } // key = dept_id | age | name  but values -> 1 | 20 | "omar"

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

// compute the result of one group
DbRow Executor::ComputeGroup(
    const std::vector<const DbRow*>& group,
    const std::vector<std::string>& groupCols,
    const std::vector<SelectColumn>& selectCols)
{
    DbRow result;

    // copy group-by values
    for (const auto& col : groupCols) {
        if (group.empty()) break;
        auto it = group[0]->find(col);
        if (it != group[0]->end()) result[col] = it->second; // because all rows have the same GroupCols so we can only use the first row
    }

    /*
    The Cardinal Rule of GROUP BYWhen utilizing this clause,
    every column in your SELECT statement must either be included in the GROUP BY clause
    or be wrapped inside an aggregate function
     */

    for (const auto& sc : selectCols) {
        if (sc.IsStar || !sc.Column) continue;

        auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get());

        if (!fn) continue;
        std::string fname = fn->Name;
        std::string alias = sc.Alias.has_value() ? sc.Alias.value() : fname;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);


        if (fname == "count") {
            if (fn->Arguments.empty()) {
                // count() -> count all rows
                result[alias] = DbObject((int)group.size());
            } else {
                // only one argument either star ,  column
                auto* cr = dynamic_cast<ColumnRef*>(fn->Arguments[0].get());
                if (cr && cr->Column == "*") {
                    result[alias] =DbObject((int)group.size());
                } else {
                    // count(expr) -> count non-null
                    int count = 0;
                    for (auto* row : group) {
                        auto val = GetValue(*fn->Arguments[0], *row);
                        count += !std::holds_alternative<std::monostate>(val);
                    }
                    result[alias] = DbObject(count);
                }
            }
            continue;
        }

        if (fn->Arguments.empty()) continue;

        Expression* argExpr = fn->Arguments[0].get();
        if (!argExpr) continue;

        if (fname == "sum") {
            double sum = 0;
            for (auto* row : group) {
                auto val = GetValue(*argExpr, *row);
                double d;
                if (TryDouble(val, d)) sum += d;
            }
            result[alias] = DbObject(sum);
        }
        else if (fname == "avg") {
            double sum = 0; int count = 0;
            for (auto* row : group) {
                auto val = GetValue(*argExpr, *row);
                double d;
                if (TryDouble(val, d)) { sum += d; count++; }
            }
            result[alias] = count == 0
                ? DbObject{std::monostate{}} : DbObject{sum / count};
        }
        else if (fname == "min") {
            DbObject min{std::monostate{}};
            for (auto* row : group) {
                auto val = GetValue(*argExpr, *row);

                if (std::holds_alternative<std::monostate>(val)) continue;

                if (std::holds_alternative<std::monostate>(min)
                    || CompareAny(val, min) < 0)
                    min = val;
            }
            result[alias] = min;
        }
        else if (fname == "max") {
            DbObject max{std::monostate{}};
            for (auto* row : group) {
                auto val = GetValue(*argExpr, *row);

                if (std::holds_alternative<std::monostate>(val)) continue;

                if (std::holds_alternative<std::monostate>(max)
                    || CompareAny(val, max) > 0)
                    max = val;
            }
            result[alias] = max;
        }
    }
    return result;
}

std::vector<DbRow> Executor::ApplyWindowFunctions(
    std::vector<DbRow>& rows,
    const std::vector<SelectColumn>& selectCols)
{
    for (const auto& sc : selectCols) {
        if (!sc.IsWindow) continue;

        std::string fname = sc.WindowFunc;
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

        std::string outKey = sc.Alias.has_value() ? sc.Alias.value() : sc.WindowFunc ;


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


        for (auto& partition : partitions) {

            if (!sc.WindowOrder.empty()) {
                // same logic as ApplyOrderBy
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
            if (fname == "row_number") { // 1 2 3 4 5
                for (int rank = 0; rank < (int)partition.size(); ++rank) {
                    auto &curRow = rows[partition[rank]];
                    curRow[outKey] = rank + 1;
                }
            }
            else if (fname == "rank") { // 1 2 2 4
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
                    auto &curRow = rows[partition[i]];
                    curRow[outKey] = rank;
                }
            }
            else if (fname == "dense_rank") { // 1 2 2 3 3 4    same number for equal
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
                    auto &curRow = rows[partition[i]];
                    curRow[outKey] = rank;
                }
            }
            else if (fname == "sum" || fname == "avg" ||
                     fname == "min" || fname == "max" || fname == "count")
            {
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
                for (int i : partition) {
                    auto &curRow = rows[partition[i]];
                    curRow[outKey] = partResult;
                }
            }
        }
    }
    return rows;
}

void Executor::ApplyOrderBy(std::vector<DbRow>& rows,
    const std::vector<OrderByClause>& order)
{
    if (order.empty())return;
    std::sort(rows.begin(), rows.end(),[&](const DbRow& a, const DbRow& b)->bool {
        for (auto &clause : order) {
                auto aIt = a.find(clause.Column);
                auto bIt = b.find(clause.Column);
                DbObject av = (aIt != a.end()) ? aIt->second : DbObject{std::monostate{}};
                DbObject bv = (bIt != b.end()) ? bIt->second : DbObject{std::monostate{}};
                int cmp = CompareAny(av, bv);
                if (clause.Descending) cmp = -cmp;
                if (cmp != 0) return cmp < 0;
        }
        return false;
    });
}

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
                    result.push_back(
                        ( join.Alias.empty() ? join.TableName : join.Alias) + "." + col.Name);
            }
            return result;
        }

    std::vector<std::string> result;
    for (const auto& sc : cols) {
        if (sc.Alias.has_value()) {
            result.push_back(sc.Alias.value());
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
            if (!sc.Column) continue;

            std::string outKey;
            if (sc.Alias.has_value())
                outKey = sc.Alias.value();
            else if (auto* cr = dynamic_cast<ColumnRef*>(sc.Column.get()))
                outKey = (cr->WasQualified && cr->TableName.has_value())
                    ? *cr->TableName + "." + cr->Column
                    : cr->Column;
            else if (auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get()))
                outKey = fn->Name;
            else
                outKey = "expr";

            DbObject val{std::monostate{}};

            auto it = row.find(outKey);
            if (it != row.end()) {
                val = it->second; // id = 1 find it
            }
            else { // like select a+b as M we do not have M in Row we should compute it
                val = GetValue(*sc.Column, row); // evaluate the expression
            }

            if (std::holds_alternative<std::monostate>(val)) { // val is NULL
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


std::vector<DbRow> Executor::ApplyDistinct(const std::vector<DbRow>& rows) {
    std::vector<DbRow> result;
    std::unordered_set<std::string> seen;

    for (const auto& row : rows) {
        std::string fp;
        for (const auto& [k, v] : row)
            fp += k + "=" + DbObjectToString(v) + "|";

        if (!seen.contains(fp)) {
            seen.insert(fp);
            result.push_back(row);
        }
    }
    return result;
}