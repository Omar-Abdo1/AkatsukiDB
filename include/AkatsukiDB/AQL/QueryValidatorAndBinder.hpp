//
// Created by omarabdo on 6/11/26.
//
#pragma  once

#ifndef AKATSUKIDB_CPP_QUERYVALIDATORANDBINDER_HPP
#define AKATSUKIDB_CPP_QUERYVALIDATORANDBINDER_HPP
#include <optional>
#include <string>

#include "AkatsukiDB/Statments/DeleteStatement.hpp"
#include "AkatsukiDB/Statments/SelectStatement.hpp"
#include "AkatsukiDB/Statments/UpdateStatement.hpp"
#include "AkatsukiDB/Table/TableRegistry.hpp"


class QueryValidatorAndBinder {
public:
    explicit QueryValidatorAndBinder(TableRegistry& registry);

    // Returns error message or nullopt on success
    std::optional<std::string> ValidateSelect(SelectStatement& stmt);
    std::optional<std::string> ValidateUpdate(UpdateStatement& stmt);
    std::optional<std::string> ValidateDelete(DeleteStatement& stmt);

private:
    TableRegistry& _registry;

    // Builds a map: column name -> table name (or "ambiguous")
    std::unordered_map<std::string, std::string> BuildAvailable(
        const std::string& mainTable,
        const std::optional<std::string>& mainAlias,
        const std::vector<JoinClause>& joins);

    void AddTable(const std::string& tableName,
                  const std::optional<std::string>& alias,
                  std::unordered_map<std::string, std::string>& available);

    std::optional<std::string> ValidateExpression(Expression& expr,
        const std::unordered_map<std::string, std::string>& available);
};

#endif //AKATSUKIDB_CPP_QUERYVALIDATORANDBINDER_HPP
