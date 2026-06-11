#pragma once

#include <memory>
#include <unordered_map>

class UpdateStatement : public IStatement {
public:
    std::string TableName;
    std::unordered_map<std::string, std::unique_ptr<Expression>> Assignments;
    std::unique_ptr<Expression> Where;
};
