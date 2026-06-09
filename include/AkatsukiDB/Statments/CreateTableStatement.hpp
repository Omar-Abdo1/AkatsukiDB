
#include <string>
#include <vector>

class CreateTableStatement : public IStatement {
public:
    std::string TableName;
    std::vector<AkatsukiDB::Table::ColumnDefinition> Columns;
    std::vector<std::string> PrimaryKey;
    bool AutoIncrement = false;
    std::vector<AkatsukiDB::Table::ForeignKeyDef> ForeignKeys;
};
