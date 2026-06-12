//
// Created by omarabdo on 6/13/26.
//

// AQL/ScanPlanner.cpp
#include "AkatsukiDB/AQL/ScanPlanner.hpp"

ScanPlan ScanPlanner::Decide(const std::string& table, Expression* where) {
    ScanPlan plan;
    if (!where) return plan; // full scan

    ScanPlan candidate;
    if (TryIndex(table, where, candidate))
        return candidate;

    // no index found — full scan, filter everything
    plan.Type        = ScanType::Full;
    plan.FilterAfter = where;
    return plan;
}

ScanPlan* ScanPlanner::TryIndex(const std::string& table,
    Expression* expr, ScanPlan& out)
{
    if (!expr) return nullptr;

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
            return &out;
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
            return &out;
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
            return &out;
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
            return &out;
        }
    }

    // AND — try each side, use first that has index
    if (auto* bin = dynamic_cast<BinaryExpr*>(expr);
        bin && bin->Op == "and")
    {
        ScanPlan left, right;
        bool hasLeft  = TryIndex(table, bin->Left.get(),  left)  != nullptr;
        bool hasRight = TryIndex(table, bin->Right.get(), right) != nullptr;

        if (hasLeft) {
            out = left;
            // the other side becomes a filter
            out.FilterAfter = bin->Right.get();
            return &out;
        }
        if (hasRight) {
            out = right;
            out.FilterAfter = bin->Left.get();
            return &out;
        }
    }

    return nullptr;
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
    for (auto& [def, tree] : it->second) {
        if (def.Columns.size() == 1 &&
            def.Columns[0] == col)
            return tree.get();
    }
    return nullptr;
}