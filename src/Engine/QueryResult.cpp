//
// Created by omarabdo on 6/11/26.
//

#include "../../include/AkatsukiDB/Engine/QueryResult.hpp"



    QueryResult QueryResult::Error(const std::string& message) {
        QueryResult res;
        res.IsError = true;
        res.ErrorMessage = message;
        return res;
    }

    QueryResult QueryResult::Success(const std::vector<std::string>& cols,
                                     std::vector<DbRow> rows) {
        QueryResult res;
        res.Columns = cols;
        res.Rows = std::move(rows);
        res.RowsAffected = static_cast<int>(res.Rows.size());
        return res;
    }

    QueryResult QueryResult::Affected(int count, const std::optional<std::string>& plan) {
        QueryResult res;
        res.RowsAffected = count;
        res.PlanUsed = plan;
        return res;
    }

