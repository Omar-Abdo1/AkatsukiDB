#include <memory>

class DeleteStatement : public IStatement {
public:
    std::string TableName;
    std::unique_ptr<Expressions::Expression> Where;
};
