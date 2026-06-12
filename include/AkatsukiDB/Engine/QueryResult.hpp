//
// Created by omarabdo on 6/11/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_QUERYRESULT_HPP
#define AKATSUKIDB_CPP_QUERYRESULT_HPP
#include <optional>
#include <string>

#include "AkatsukiDB/Table/RowSerializer.hpp"

class QueryResult {
public:
    bool IsError = false;
    std::optional<std::string> ErrorMessage;
    std::vector<std::string> Columns;
    std::vector<DbRow> Rows;
    int RowsAffected = 0;
    std::optional<std::string> PlanUsed;

    static QueryResult Error(const std::string& message);
    static QueryResult Success(const std::vector<std::string>& cols,
                               std::vector<DbRow> rows);
    static QueryResult Affected(int count, const std::optional<std::string>& plan = std::nullopt);
};



#endif //AKATSUKIDB_CPP_QUERYRESULT_HPP
