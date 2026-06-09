#include <string>
#include <vector>

class CreateIndexStatement : public IStatement {
public:
 std::string IndexName;
 std::string TableName;
 std::vector<std::string> Columns;
 bool IsUnique = false;
};
