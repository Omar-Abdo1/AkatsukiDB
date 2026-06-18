//
// Created by omarabdo on 6/11/26.
//

#include "AkatsukiDB/AQL/QueryValidatorAndBinder.hpp"

#include <algorithm>

QueryValidatorAndBinder::QueryValidatorAndBinder(TableRegistry& registry)
    : _registry(registry) {}

std::unordered_map<std::string, std::string>
QueryValidatorAndBinder::BuildAvailable(const std::string& mainTable,
                                        const std::optional<std::string>& mainAlias,
                                        const std::vector<JoinClause>& joins) {

    std::unordered_map<std::string, std::string> available;

    AddTable(mainTable, mainAlias, available);

    for (const auto& jc : joins) {
        AddTable(jc.TableName, jc.Alias, available);
    }

    return available;
}
// if we have name="omar" then we have -> employee.name=omar , name=omar , e.name=omar (if there an alias)
void QueryValidatorAndBinder::AddTable(const std::string& tableName,
                                       const std::optional<std::string>& alias,
                                       std::unordered_map<std::string, std::string>& available) {
    std::string name = tableName;

    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    if (!_registry.TableExists(name)) return;

    const auto& def = _registry.GetTable(name);

    for (const auto& col : def.Columns) {
        std::string colName = col.Name;
        std::transform(colName.begin(), colName.end(), colName.begin(), ::tolower);

        auto it = available.find(colName);
        if (it != available.end())
            available[colName] = "__ambiguous__";
        else
            available[colName] = name;

        //table.col
        available[name + "." + colName] = name;

        // alias.col
        if (alias.has_value()) {
            std::string aliasLower = alias.value();
            std::transform(aliasLower.begin(), aliasLower.end(), aliasLower.begin(), ::tolower);
            available[aliasLower + "." + colName] = name;
        }

    }
}

std::optional<std::string> QueryValidatorAndBinder::ValidateSelect(SelectStatement& stmt) {
    std::string tableLower = stmt.TableName;
    std::transform(tableLower.begin(), tableLower.end(), tableLower.begin(), ::tolower);
    if (!_registry.TableExists(tableLower))
        return "Table " + stmt.TableName + " doesn't exist";

    for (auto& jc : stmt.Joins) {
        std::string joinTable = jc.TableName;
        std::transform(joinTable.begin(), joinTable.end(), joinTable.begin(), ::tolower);
        if (!_registry.TableExists(joinTable))
            return "Table " + jc.TableName + " doesn't exist";
    }

    auto available = BuildAvailable(stmt.TableName, stmt.Alias, stmt.Joins);

    for (auto& jc : stmt.Joins) {
        if (auto err = ValidateExpression(*jc.On, available))
            return err;
    }

    for (auto& col : stmt.Columns) {
        if (col.IsStar) continue;
        if (auto err = ValidateExpression(*col.Column, available))
            return err;
    }

    if (stmt.Where) {
        if (auto err = ValidateExpression(*stmt.Where, available))
            return err;
    }

    for (auto& ob : stmt.OrderBy) {
        std::string col = ob.Column;
        std::transform(col.begin(), col.end(), col.begin(), ::tolower);
        if (available.find(col) == available.end()) // column with it is all combinations do not exist !!
            return "Column " + ob.Column + " in ORDER BY not found";
    }

    for (auto& col : stmt.GroupBy) {
        std::string colLower = col;
        std::transform(colLower.begin(), colLower.end(), colLower.begin(), ::tolower);
        if (available.find(colLower) == available.end())
            return "Column " + col + " in GROUP BY not found";
    }

    bool hasAggregate   = false;
    bool hasNonAggregate = false;

    for (const auto& sc : stmt.Columns) {
        if (sc.IsStar) { hasNonAggregate = true; continue; }
        if (sc.Column==nullptr || sc.IsWindow) continue;
        if (dynamic_cast<FunctionExpr*>(sc.Column.get()))
            hasAggregate = true;
        else
            hasNonAggregate = true;
    }

    if (hasAggregate && hasNonAggregate && stmt.GroupBy.empty()) {
        return "Cannot mix aggregate functions with non-aggregated columns "
               "without GROUP BY. Use GROUP BY or a window function.";
    }

    return std::nullopt;
}

std::optional<std::string> QueryValidatorAndBinder::ValidateUpdate(UpdateStatement& stmt) {
    std::string tableLower = stmt.TableName;
    std::transform(tableLower.begin(), tableLower.end(), tableLower.begin(), ::tolower);
    if (!_registry.TableExists(tableLower))
        return "Table " + stmt.TableName + " doesn't exist";

    auto available = BuildAvailable(stmt.TableName, std::nullopt, {});

    for (auto& [col, expr] : stmt.Assignments) {
        std::string colLower = col;
        std::transform(colLower.begin(), colLower.end(), colLower.begin(), ::tolower);
        if (available.find(colLower) == available.end())
            return "Column '" + col + "' in SET not found in '" + stmt.TableName + "'.";

        if (auto err = ValidateExpression(*expr, available))
            return err;
    }

    if (stmt.Where) {
        if (auto err = ValidateExpression(*stmt.Where, available))
            return err;
    }

    return std::nullopt;
}

std::optional<std::string> QueryValidatorAndBinder::ValidateDelete(DeleteStatement& stmt) {
    std::string tableLower = stmt.TableName;
    std::transform(tableLower.begin(), tableLower.end(), tableLower.begin(), ::tolower);
    if (!_registry.TableExists(tableLower))
        return "Table " + stmt.TableName + " doesn't exist";

    if (stmt.Where) {
        auto available = BuildAvailable(stmt.TableName, std::nullopt, {});
        if (auto err = ValidateExpression(*stmt.Where, available))
            return err;
    }
    return std::nullopt;
}

std::optional<std::string> QueryValidatorAndBinder::ValidateExpression(
    Expression& expr,
    const std::unordered_map<std::string, std::string>& available) {
    // Literal always valid
    if (dynamic_cast<Literal*>(&expr)) return std::nullopt;

    if (auto* cr = dynamic_cast<ColumnRef*>(&expr)) {
        std::string lookup;
        if (cr->TableName.has_value())
            lookup = cr->TableName.value() + "." + cr->Column;
        else
            lookup = cr->Column;
        std::transform(lookup.begin(), lookup.end(), lookup.begin(), ::tolower);

        auto it = available.find(lookup);
        if (it == available.end())
            return "Column '" + lookup + "' not found in any table.";
        if (it->second == "__ambiguous__")
            return "Column '" + cr->Column + "' is ambiguous. Use tableName.column to specify.";

        // Bind table name
        cr->TableName = it->second; // changes will reflect on the main object in heap
        return std::nullopt;
    }

    if (auto* bin = dynamic_cast<BinaryExpr*>(&expr)) {
        if (auto err = ValidateExpression(*bin->Left, available)) return err;
        return ValidateExpression(*bin->Right, available);
    }

    if (auto* unary = dynamic_cast<UnaryExpr*>(&expr))
        return ValidateExpression(*unary->Operand, available);

    if (auto* isNull = dynamic_cast<IsNullExpr*>(&expr))
        return ValidateExpression(*isNull->Value, available);

    if (auto* between = dynamic_cast<BetweenExpr*>(&expr)) {
        if (auto err = ValidateExpression(*between->Value, available)) return err;
        if (auto err = ValidateExpression(*between->Lower, available)) return err;
        return ValidateExpression(*between->Upper, available);
    }

    if (auto* in = dynamic_cast<InExpr*>(&expr)) {
        if (auto err = ValidateExpression(*in->Value, available)) return err;
        for (auto& v : in->Values) {
            if (auto err = ValidateExpression(*v, available)) return err;
        }
        return std::nullopt;
    }

    if (auto* like = dynamic_cast<LikeExpr*>(&expr))
        return ValidateExpression(*like->Value, available);

    if (auto* fn = dynamic_cast<FunctionExpr*>(&expr)) {
         auto& args =   fn->Arguments;
            // Skip "*" column
              
              for(auto &arg : args){  
            if (auto* cr = dynamic_cast<ColumnRef*>(arg.get()))
                if (cr->Column == "*")
                    continue;
            if (auto err = ValidateExpression(*arg, available)) return err;
        }
    }
        return std::nullopt;

}