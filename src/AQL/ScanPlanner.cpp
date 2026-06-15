//
// Created by omarabdo on 6/13/26.
//

// AQL/ScanPlanner.cpp
#include "AkatsukiDB/AQL/ScanPlanner.hpp"

ScanPlan ScanPlanner::Decide(const std::string& table, Expression* where) {
    ScanPlan plan;
    if (!where) return plan; // full scan

    std::vector<Expression*>exprs;
    FlatAnd(exprs,where);

    int choose=-1;
    for (int i=0;i<exprs.size();++i) {
        // try to choose some condition to use index on
        if (TryIndex(table, exprs[i], plan)) {
             choose=i;
            break;
        }
    }
    if (choose==-1) {
        plan.FilterAfter=exprs;
        return plan;
    }

    std::vector<Expression*> tmp;
    for (int i=0;i<exprs.size();++i) if (i!=choose) tmp.push_back(exprs[i]);
    plan.FilterAfter=tmp;
    return plan;
}

void ScanPlanner::FlatAnd(std::vector<Expression*>& exprs,Expression* expr) {
    if (auto * bin = dynamic_cast<BinaryExpr*>(expr)) {
         if (bin->Op=="and") {
             FlatAnd(exprs,bin->Left.get());
             FlatAnd(exprs,bin->Right.get());
         }
        else exprs.push_back(expr);
    }
    else if (expr) {
        exprs.push_back(expr);
    }
}

bool ScanPlanner::TryIndex(const std::string& table,
    Expression* expr,ScanPlan & out)
{
    if (!expr) return false;

    std::string col;
    DbObject val, lo, hi;
    bool open;

    // col = val → point
    if (TryPoint(expr, col, val)) {
        auto* tree = FindIndex(table, col);
        if (tree) {
            out.Type     = ScanType::Point;
            out.Index    = tree;
            out.PointKey = IndexKey(std::span<const DbObject>({val}));
            return true;
        }
    }

    // BETWEEN → range
    if (TryBetween(expr, col, lo, hi)) {
        auto* tree = FindIndex(table, col);
        if (tree) {
            out.Type           = ScanType::Range;
            out.Index          = tree;
            out.RangeStart     = IndexKey(std::span<const DbObject>({lo}));
            out.RangeEnd       = IndexKey(std::span<const DbObject>({hi}));
            out.RangeStartOpen = false;
            out.RangeEndOpen   = false;
            return true;
        }
    }

    // col > val or col >= val → range with Max end
    if (TryGreater(expr, col, val, open)) {
        auto* tree = FindIndex(table, col);
        if (tree) {
            out.Type           = ScanType::Range;
            out.Index          = tree;
            out.RangeStart     = IndexKey(std::span<const DbObject>({val}));
            out.RangeEnd       = IndexKey::Max();
            out.RangeStartOpen = open;
            return true;
        }
    }

    // col < val or col <= val → range with Min start
    if (TryLess(expr, col, val, open)) {
        auto* tree = FindIndex(table, col);
        if (tree) {
            out.Type         = ScanType::Range;
            out.Index        = tree;
            out.RangeStart   = IndexKey::Min();
            out.RangeEnd     = IndexKey(std::span<const DbObject>({val}));
            out.RangeEndOpen = open;
            return true;
        }
    }

   return false;
}

bool ScanPlanner::TryPoint(Expression* expr,
    std::string& col, DbObject& val)
{
    auto* bin = dynamic_cast<BinaryExpr*>(expr);
    if (!bin || bin->Op != "=") return false;
    auto* cr  = dynamic_cast<ColumnRef*>(bin->Left.get());
    auto* lit = dynamic_cast<Literal*>(bin->Right.get());
    if (!cr || !lit) return false;
    col = cr->Column;
    val = lit->Value;
    return true;
}

bool ScanPlanner::TryBetween(Expression* expr,
    std::string& col, DbObject& lo, DbObject& hi)
{
    auto* b   = dynamic_cast<BetweenExpr*>(expr);
    if (!b) return false;
    auto* cr  = dynamic_cast<ColumnRef*>(b->Value.get());
    auto* lLit = dynamic_cast<Literal*>(b->Lower.get());
    auto* hLit = dynamic_cast<Literal*>(b->Upper.get());
    if (!cr || !lLit || !hLit) return false;
    col = cr->Column;
    lo  = lLit->Value;
    hi  = hLit->Value;
    return true;
}

bool ScanPlanner::TryGreater(Expression* expr,
    std::string& col, DbObject& val, bool& open)
{
    auto* bin = dynamic_cast<BinaryExpr*>(expr);
    if (!bin) return false;
    if (bin->Op != ">" && bin->Op != ">=") return false;
    auto* cr  = dynamic_cast<ColumnRef*>(bin->Left.get());
    auto* lit = dynamic_cast<Literal*>(bin->Right.get());
    if (!cr || !lit) return false;
    col  = cr->Column;
    val  = lit->Value;
    open = (bin->Op == ">");
    return true;
}

bool ScanPlanner::TryLess(Expression* expr,
    std::string& col, DbObject& val, bool& open)
{
    auto* bin = dynamic_cast<BinaryExpr*>(expr);
    if (!bin) return false;
    if (bin->Op != "<" && bin->Op != "<=") return false;
    auto* cr  = dynamic_cast<ColumnRef*>(bin->Left.get());
    auto* lit = dynamic_cast<Literal*>(bin->Right.get());
    if (!cr || !lit) return false;
    col  = cr->Column;
    val  = lit->Value;
    open = (bin->Op == "<");
    return true;
}

BPlusTree* ScanPlanner::FindIndex(const std::string& table,
    const std::string& col)
{
    auto it = _indexes.find(table);
    if (it == _indexes.end()) return nullptr;
    for (auto& [def, tree] : _indexes[table]) {
        if (def.Columns.size() == 1 &&
            def.Columns[0] == col)
            return tree.get();
    }
    return nullptr;
}