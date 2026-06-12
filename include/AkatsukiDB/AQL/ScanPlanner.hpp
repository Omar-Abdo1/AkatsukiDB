#pragma once
#include "AkatsukiDB/Index/BPlusTree.hpp"
#include "AkatsukiDB/Index/IndexKey.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"
#include "AkatsukiDB/Expressions/Expression.hpp"
#include <unordered_map>
#include <vector>
#include <string>

enum class ScanType { Full, Point, Range };

struct ScanPlan {
    ScanType            Type            = ScanType::Full;
    BPlusTree*          Index           = nullptr;
    IndexKey            PointKey        = IndexKey::Min();
    IndexKey            RangeStart      = IndexKey::Min();
    IndexKey            RangeEnd        = IndexKey::Max();
    bool                RangeStartOpen  = false; // true = >, false = >=
    bool                RangeEndOpen    = false; // true = <, false = <=
    Expression*         FilterAfter     = nullptr; // conditions index cannot handle
};

class ScanPlanner {
public:
    using IndexMap = std::unordered_map<std::string,
        std::vector<std::pair<IndexDefinition, std::unique_ptr<BPlusTree>>>>;

    explicit ScanPlanner(IndexMap& indexes) : _indexes(indexes) {}

    ScanPlan Decide(const std::string& table, Expression* where);

private:
    IndexMap& _indexes;

    ScanPlan* TryIndex(const std::string& table, Expression* expr, ScanPlan& out);

    bool TryPoint  (Expression* expr, std::string& col, DbObject& val);
    bool TryBetween(Expression* expr, std::string& col, DbObject& lo, DbObject& hi);
    bool TryGreater(Expression* expr, std::string& col, DbObject& val, bool& open);
    bool TryLess   (Expression* expr, std::string& col, DbObject& val, bool& open);

    BPlusTree* FindIndex(const std::string& table, const std::string& col);
};