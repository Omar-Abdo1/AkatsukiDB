#include "AkatsukiDB/Table/RowSerializer.hpp"
class InsertStatement : public IStatement {
public:
    std::string TableName;
    DbRow Rows;
};