#pragma once

#include <string>
#include <vector>

#include "AkatsukiDB/Table/ColumnDefinition.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"

class CreateTableStatement : public IStatement {
public:
    std::string TableName;
    std::vector<ColumnDefinition> Columns;
    std::vector<std::string> PrimaryKey;
    bool AutoIncrement = false;
    std::vector<ForeignKeyDef> ForeignKeys;
};
