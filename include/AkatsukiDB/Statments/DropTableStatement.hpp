#pragma once

#include <string>

class DropTableStatement : public IStatement {
public:
    std::string TableName;
};

class DropIndexStatement : public IStatement {
public:
    std::string IndexName;
};

class TruncateStatement : public IStatement {
public:
    std::string TableName;
};