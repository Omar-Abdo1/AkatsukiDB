#include <memory>
#include <optional>
#include <vector>

#include "AkatsukiDB/Expressions/Expression.hpp"

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
    std::unique_ptr<Expression> On;
};

struct   SelectColumn
{
 std::unique_ptr<Expression> Column ;
std:: string Alias="";
 bool IsStar= false;
 bool IsWindow =false;
 std::string WindowFunc="" ;  // "row_number", "sum"
 std::vector<std::string> PartitionBy ; // like group by a,b,c
 std::vector<OrderByClause> WindowOrder ;
};

class SelectStatement : public IStatement {
public:
    std::vector<SelectColumn> Columns;
    bool IsDistinct = false;
    std::string TableName;
     std::string Alias=""; // can be empty
    std::unique_ptr<Expression> Where;

    int Offset =-1 ; // -1 means is not set
    int Limit =-1 ;

    std::vector<JoinClause> Joins;
    std::vector<std::string> GroupBy;
    std::unique_ptr<Expression> Having;
    std::vector<OrderByClause> OrderBy;
};