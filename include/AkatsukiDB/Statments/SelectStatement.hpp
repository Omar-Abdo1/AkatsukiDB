#include <memory>
#include <optional>
#include <vector>

struct OrderByClause {
    std::string Column;
    bool Descending = false;
};


enum JoinType {
    Inner,Left,Right
};


struct JoinClause {
    std::string TableName;
     std::string Alias=""; // can be empty
    JoinType Type=Inner;
    std::unique_ptr<Expressions::Expression> On;
};

class SelectStatement : public IStatement {
public:
    std::vector<std::unique_ptr<Expressions::Expression>> Columns;
    bool IsDistinct = false;
    std::string TableName;
     std::string Alias=""; // can be empty
    std::unique_ptr<Expressions::Expression> Where;

    int Offset =-1 ; // -1 means is not set
    int Limit =-1 ;

    std::vector<JoinClause> Joins;
    std::vector<std::string> GroupBy;
    std::unique_ptr<Expressions::Expression> Having;
    std::vector<OrderByClause> OrderBy;
};