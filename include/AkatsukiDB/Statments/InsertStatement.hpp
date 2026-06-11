#pragma once

#include "AkatsukiDB/Table/RowSerializer.hpp"
class InsertStatement : public IStatement {
public:
    std::string TableName;
    std::vector<DbRow> Rows;
};