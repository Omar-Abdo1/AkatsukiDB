//
// Created by omarabdo on 6/12/26.
//

#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Expressions/Expression.hpp"




bool Executor::EvaluateBool(Expression& expr, const std::unordered_map<std::string, DbObject>& row) {
    if (auto* bin = dynamic_cast<BinaryExpr*>(&expr)) {
        if (bin->Op == "and")
            return EvaluateBool(*bin->Left, row) && EvaluateBool(*bin->Right, row);
        if (bin->Op == "or")
            return EvaluateBool(*bin->Left, row) || EvaluateBool(*bin->Right, row);
        // Comparison
        auto left = GetValue(*bin->Left, row);
        auto right = GetValue(*bin->Right, row);
        return Compare(left, bin->Op, right);
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(&expr)) {
        if (unary->Op == "not")
            return !EvaluateBool(*unary->Operand, row);
    }
    if (auto* isNull = dynamic_cast<IsNullExpr*>(&expr)) {
        auto val = GetValue(*isNull->Value, row);
        bool isnull = std::holds_alternative<std::monostate>(val);
        return isnull ? !isNull->Not : isNull->Not;
    }
    if (auto* between = dynamic_cast<BetweenExpr*>(&expr)) {
        auto v = GetValue(*between->Value, row);
        auto lo = GetValue(*between->Lower, row);
        auto hi = GetValue(*between->Upper, row);
        return Compare(v, ">=", lo) && Compare(v, "<=", hi);
    }
    if (auto* in = dynamic_cast<InExpr*>(&expr)) {
        auto v = GetValue(*in->Value, row);
        for (auto& valExpr : in->Values) {
            if (AreEqual(v, GetValue(*valExpr, row)))
                return true;
        }
        return false;
    }
    if (auto* like = dynamic_cast<LikeExpr*>(&expr)) {
        auto v = GetValue(*like->Value, row);
        auto pat = GetValue(*like->Pattern, row);
        std::string s = DbObjectToString(v);
        std::string p = DbObjectToString(pat);
        return LikeMatch(s, p);
    }
    // Literal? Evaluate a literal as bool?
    if (auto* lit = dynamic_cast<Literal*>(&expr)) {
        const auto& val = lit->Value;
        if (std::holds_alternative<bool>(val)) return std::get<bool>(val);
        if (std::holds_alternative<int>(val)) return std::get<int>(val) != 0;
        if (std::holds_alternative<double>(val)) return std::get<double>(val) != 0.0;
        if (std::holds_alternative<std::string>(val)) return !std::get<std::string>(val).empty();
        return false;
    }
    // ColumnRef: treat as boolean? Not typical, but for simplicity return true if not null.
    if (auto* cr = dynamic_cast<ColumnRef*>(&expr)) {
        auto val = GetValue(expr, row);
        return !std::holds_alternative<std::monostate>(val);
    }
    return false;
}

// ----------------------------------------------------------------------------
// Helper: Get value of an expression from a row
// ----------------------------------------------------------------------------
DbObject Executor::GetValue(Expression& expr, const std::unordered_map<std::string, DbObject>& row) {
    if (auto* lit = dynamic_cast<Literal*>(&expr)) {
        return lit->Value;
    }
    if (auto* cr = dynamic_cast<ColumnRef*>(&expr)) {
        std::string fullName = cr->Column;
        if (cr->TableName.has_value()) {
            fullName = cr->TableName.value() + "." + cr->Column;
            auto it = row.find(fullName);
            if (it != row.end()) return it->second;
        }
        // Try unqualified
        auto it = row.find(cr->Column);
        if (it != row.end()) return it->second;
        // Try table.column (if table is known from binding)
        if (cr->TableName.has_value()) {
            std::string withTable = cr->TableName.value() + "." + cr->Column;
            it = row.find(withTable);
            if (it != row.end()) return it->second;
        }
        return std::monostate{};
    }
    if (auto* bin = dynamic_cast<BinaryExpr*>(&expr)) {
        if (bin->Op == "+" || bin->Op == "-" || bin->Op == "*" || bin->Op == "/") {
            auto left = GetValue(*bin->Left, row);
            auto right = GetValue(*bin->Right, row);
            return Calculate(left, bin->Op, right);
        }
        // For boolean operators, we don't return a value (they are used in EvaluateBool)
        return std::monostate{};
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(&expr)) {
        // Not supported for value extraction
        return std::monostate{};
    }
    if (auto* fn = dynamic_cast<FunctionExpr*>(&expr)) {
        // Not implemented
        return std::monostate{};
    }
    return std::monostate{};
}

// ----------------------------------------------------------------------------
// Helper: Compare two DbObject values with given operator
// ----------------------------------------------------------------------------
bool Executor::Compare(const DbObject& left, const std::string& op, const DbObject& right) {
    // Null handling
    if (std::holds_alternative<std::monostate>(left) && std::holds_alternative<std::monostate>(right))
        return op == "=";
    if (std::holds_alternative<std::monostate>(left) || std::holds_alternative<std::monostate>(right))
        return op == "!=";

    int cmp = CompareAny(left, right);
    if (op == "=") return cmp == 0;
    if (op == "!=") return cmp != 0;
    if (op == ">") return cmp == 1;
    if (op == "<") return cmp == -1;
    if (op == ">=") return cmp >= 0;
    if (op == "<=") return cmp <= 0;
    return false;
}

bool Executor::AreEqual(const DbObject& a, const DbObject& b) {
    return Compare(a, "=", b);
}

// ----------------------------------------------------------------------------
// Helper: CompareAny returns -1, 0, 1
// ----------------------------------------------------------------------------
int Executor::CompareAny(const DbObject& a, const DbObject& b) {
    // Try numeric conversion
    double da, db;
    bool aNumeric = TryDouble(a, da);
    bool bNumeric = TryDouble(b, db);
    if (aNumeric && bNumeric) {
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    // Fallback to string comparison
    std::string sa = DbObjectToString(a);
    std::string sb = DbObjectToString(b);
    int len = std::min(sa.size(), sb.size());
    for (int i = 0; i < len; ++i) {
        if (sa[i] < sb[i]) return -1;
        if (sa[i] > sb[i]) return 1;
    }
    if (sa.size() < sb.size()) return -1;
    if (sa.size() > sb.size()) return 1;
    return 0;
}

// ----------------------------------------------------------------------------
// Helper: Try to convert DbObject to double
// ----------------------------------------------------------------------------
bool Executor::TryDouble(const DbObject& v, double& result) {
    if (std::holds_alternative<int>(v)) {
        result = static_cast<double>(std::get<int>(v));
        return true;
    }
    if (std::holds_alternative<double>(v)) {
        result = std::get<double>(v);
        return true;
    }
    if (std::holds_alternative<bool>(v)) {
        result = std::get<bool>(v) ? 1.0 : 0.0;
        return true;
    }
    if (std::holds_alternative<std::string>(v)) {
        try {
            result = std::stod(std::get<std::string>(v));
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Helper: Calculate arithmetic
// ----------------------------------------------------------------------------
DbObject Executor::Calculate(const DbObject& a, const std::string& op, const DbObject& b) {
    double da,db;
    if (!TryDouble(a,  da) || !TryDouble(b,  db))
        return std::monostate{};
    double res;
    if (op == "*") res = da * db;
    else if (op == "+") res = da + db;
    else if (op == "-") res = da - db;
    else if (op == "/") {
        if (db == 0.0) throw std::runtime_error("Division by zero");
        res = da / db;
    }
    else return std::monostate{};
    // Preserve integer type if both operands were ints
    if (std::holds_alternative<int>(a) && std::holds_alternative<int>(b)) {
        // Check if result fits in int
        if (res == static_cast<double>(static_cast<int>(res)))
            return static_cast<int>(res);
    }
    return res;
}

// ----------------------------------------------------------------------------
// Like pattern matching (recursive with memoization)
// ----------------------------------------------------------------------------
bool Executor::LikeMatch(const std::string& s, const std::string& pattern) {
    std::vector<std::vector<int>>dp;
    dp=std::vector<std::vector<int>>(s.size()+1,std::vector<int>(pattern.size()+1,-1));
    return RecLike(0, 0, s, pattern, dp);
}

bool Executor::RecLike(int i, int j, const std::string& s, const std::string& pattern,
                       std::vector<std::vector<int>>& dp) {

    if (i == (int)s.size()) {
        while (j < (int)pattern.size() && pattern[j] == '%') ++j;
        return  (j == (int)pattern.size());
    }

    if (j == (int)pattern.size()) return false;

    int &ret = dp[i][j];
    if (~ret)return ret;

    if (pattern[j] == '%') {
        for (int k = i; k <= (int)s.size(); ++k) {
            if (RecLike(k, j+1, s, pattern, dp))
                return ret = true;
        }
    }

    if (pattern[j] == '?' || pattern[j] == s[i]) {
        return ret = RecLike(i+1, j+1, s, pattern, dp);
    }

    return ret =  false;
}