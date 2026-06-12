#pragma once

#include <memory>

#include "IStatement.hpp"
#include "AkatsukiDB/Expressions/Expression.hpp"

class DeleteStatement : public IStatement {
public:
    std::string TableName;
    std::unique_ptr<Expression> Where;
};
