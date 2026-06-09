#include <memory>
#include <unordered_map>

class UpdateStatement : public IStatement {
public:
    std::string TableName;
    std::unordered_map<std::string, std::unique_ptr<Expressions::Expression>> Assignments;
    std::unique_ptr<Expressions::Expression> Where;
};
