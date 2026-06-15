//
// Created by omarabdo on 6/11/26.
//

#include "../../include/AkatsukiDB/AQL/Parser.hpp"

#include <algorithm>

#include "AkatsukiDB/Statments/CreateIndexStatement.hpp"
#include "AkatsukiDB/Statments/CreateTableStatement.hpp"
#include "AkatsukiDB/Statments/DeleteStatement.hpp"
#include "AkatsukiDB/Statments/DropTableStatement.hpp"
#include "AkatsukiDB/Statments/InsertStatement.hpp"
#include "AkatsukiDB/Statments/SelectStatement.hpp"
#include "AkatsukiDB/Statments/ShowStatement.hpp"
#include "AkatsukiDB/Statments/UpdateStatement.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"

bool Parser::Match(const std::string& value) {
    if (IsAtEnd()) return false;
    std::string cur = Current().GetValue();
    std::transform(cur.begin(), cur.end(), cur.begin(), ::tolower);
    std::string lowerVal = value;
    std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
    if (cur != lowerVal) return false;
    ++_pos;
    return true;
}

void Parser::Expect(const std::string& value) {
    if (IsAtEnd()) throw ParseException("Expected " + value + " but reached end");
    std::string cur = Current().GetValue();
    std::transform(cur.begin(), cur.end(), cur.begin(), ::tolower);
    std::string lowerVal = value;
    std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::tolower);
    if (cur != lowerVal) {
        throw ParseException("Expected " + value + " but got " + cur + " at line " + std::to_string(Current().GetLine()));
    }
    ++_pos;
}

Token Parser::Expect(TokenType type) {
    if (IsAtEnd()) throw ParseException("Expected token type but reached end");
    if (Current().GetType() != type) {
        throw ParseException("Expected " + std::to_string(static_cast<int>(type)) + " but got " +
                             Current().ToString() + " at line " + std::to_string(Current().GetLine()));
    }
    return _tokens[_pos++];
}

std::string Parser::ExpectName() {
    if (IsAtEnd()) throw ParseException("Expected name (identifier or keyword) at end");
    if (Current().GetType() != TokenType::Identifier && Current().GetType() != TokenType::Keyword) {
        throw ParseException("Expected name but got " + Current().ToString());
    }
    std::string val = Current().GetValue();
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    ++_pos;
    return val;
}

std::unique_ptr<IStatement> Parser::Parse(const std::vector<Token>& tokens) {
    _tokens = tokens;
    _pos = 0;
    if (IsAtEnd()) throw ParseException("Empty input");

    std::string first = Current().GetValue();
    std::transform(first.begin(), first.end(), first.begin(), ::tolower);

    if (first == "select") return ParseSelect();
    if (first == "insert") return ParseInsert();
    if (first == "update") return ParseUpdate();
    if (first == "delete") return ParseDelete();
    if (first == "create") return ParseCreate();
    if (first == "drop") return ParseDrop();
    if (first == "truncate") return ParseTruncate();
    if (first == "show") return ParseShow();
    throw ParseException("Unknown statement '" + Current().GetValue() + "' at line " + std::to_string(Current().GetLine()));
//todo commit , rollback , begin
}

// Create statements
std::unique_ptr<IStatement> Parser::ParseCreate() {
    Expect("create");
    if (Match("table")) return ParseCreateTable();
    bool unique = Match("unique");
    if (Match("index")) return ParseCreateIndex(unique);
    throw ParseException("Expected TABLE or INDEX after CREATE");
}

/*
     CREATE (unique) INDEX idx_name_city
       ON Customers (LastName, City);  sql
     */

std::unique_ptr<IStatement> Parser::ParseCreateIndex(bool unique) {
    std::string name = ExpectName();
    Expect("on");
    std::string table = ExpectName();
    Expect("(");
    std::vector<std::string> cols;
    do {
        cols.push_back(ExpectName());
    } while (Match(","));
    Expect(")");

    auto stmt = std::make_unique<CreateIndexStatement>();
    stmt->IndexName = name;
    stmt->TableName = table;
    stmt->Columns = cols;
    stmt->IsUnique = unique;
    return stmt;
}

/*
    AQL

    CREATE TABLE employees {
          int    id        PK AUTO,
          int    dept_id   FK departments.id CASCADE,
          str    name      NOT NULL,
          float  salary    DEFAULT 0.0,
          bool   is_active DEFAULT true
      }

    */

std::unique_ptr<IStatement> Parser::ParseCreateTable() {
    std::string name = ExpectName();
    auto stmt = std::make_unique<CreateTableStatement>();
    stmt->TableName = name;
    Expect("{");

    do {
        ColumnDefinition col;
        col.Type = ExpectName();
        col.Name = ExpectName();

        while (!IsAtEnd() && Current().GetValue() != "," && Current().GetValue() != "}") {
            std::string kw = Current().GetValue();
            std::transform(kw.begin(), kw.end(), kw.begin(), ::tolower);

            if (kw == "pk") {
                ++_pos;
                stmt->PrimaryKey.push_back(col.Name);
                col.Nullable = false;
                col.IsUnique=true;
            } else if (kw == "auto") {
                ++_pos;
                stmt->AutoIncrement = true;
            } else if (kw == "not") {
                ++_pos;
                Expect("null");
                col.Nullable = false;
            } else if (kw == "default") {
                ++_pos;
                col.Default = ParseLiteralValue();
            } else if (kw == "unique") {
                ++_pos;
                col.IsUnique = true;
            } else if (kw == "fk") {
                ++_pos;
                std::string refTable = ExpectName();
                Expect(".");
                std::string refColumn = ExpectName();
                OnDelete onDelete;

                if (Match("cascade"))
                   onDelete= OnDelete::CASCADE;
                else onDelete = OnDelete::RESTRICT ,++_pos ;

                ForeignKeyDef fk;
                fk.Column = col.Name;
                fk.RefColumn = refColumn;
                fk.RefTable = refTable;
                fk.OnDeleteAction = onDelete;
                stmt->ForeignKeys.push_back(fk);
            } else {
                break;
            }
        }
        stmt->Columns.push_back(col);
    } while (Match(","));

    Expect("}");
    return stmt;
}

// Delete
std::unique_ptr<IStatement> Parser::ParseDelete() {
    Expect("delete");
    Expect("from");
    std::string table = ExpectName();
    auto stmt = std::make_unique<DeleteStatement>();
    stmt->TableName = table;
    if (Match("where")) {
        stmt->Where = ParseExpression();
    }
    return stmt;
}

// Drop & Truncate
std::unique_ptr<IStatement> Parser::ParseDrop() {
    Expect("drop");
    if (Match("table")) {
        auto stmt = std::make_unique<DropTableStatement>();
        stmt->TableName = ExpectName();
        return stmt;
    }
    if (Match("index")) {
        auto stmt = std::make_unique<DropIndexStatement>();
        stmt->IndexName = ExpectName();
        return stmt;
    }
    throw ParseException("Expected TABLE or INDEX after DROP");
}

std::unique_ptr<IStatement> Parser::ParseTruncate() {
    Expect("truncate");
    Match("table"); // optional
    auto stmt = std::make_unique<TruncateStatement>();
    stmt->TableName = ExpectName();
    return stmt;
}

// Insert
std::unique_ptr<IStatement> Parser::ParseInsert() {
    Expect("insert");
    Expect("into");
    auto stmt = std::make_unique<InsertStatement>();
    stmt->TableName = ExpectName();
    stmt->Rows = ParseInsertRows();
    return stmt;
}
/*
     insert into emp [{name:omar,sal:1000},{name:ali,sal:1000}]
     */
std::vector<DbRow> Parser::ParseInsertRows() {
    std::vector<DbRow> rows;
    if (Current().GetType() == TokenType::LBrace) { // {
        rows.push_back(ParseRow());
    } else if (Current().GetType() == TokenType::LBracket) { // [
        ++_pos;
        do {
            rows.push_back(ParseRow());
        } while (Match(","));
        Expect("]");
    }
    return rows;
}

std::unordered_map<std::string, DbObject> Parser::ParseRow() {
    DbRow row;
    Expect("{");
    do {
        std::string column = ExpectName();
        Expect(":");
        row[column] = ParseLiteralValue();
    } while (Match(","));
    Expect("}");
    return row;
}

// Update

/*
     update emp set {name=10,sal=sal*0,1 , age=age+1}  where exp
     */
std::unique_ptr<IStatement> Parser::ParseUpdate() {
    Expect("update");
    auto stmt = std::make_unique<UpdateStatement>();
    stmt->TableName = ExpectName();
    Expect("set");
    Expect("{");
    do {
        std::string column = ExpectName();
        Expect("=");
        stmt->Assignments[column] = ParseExpression();
    } while (Match(","));
    Expect("}");
    if (Match("where")) {
        stmt->Where = ParseExpression();
    }
    return stmt;
}

// Select
std::unique_ptr<IStatement> Parser::ParseSelect() {
    Expect("select");
    auto stmt = std::make_unique<SelectStatement>();
    if (Match("distinct")) stmt->IsDistinct = true;
    stmt->Columns = ParseSelectColumn();
    Expect("from");
    stmt->TableName = ExpectName();
    if (!IsAtEnd() && Current().GetType() == TokenType::Identifier) {
        stmt->Alias = Expect(TokenType::Identifier).GetValue();
        std::transform(stmt->Alias.value().begin(), stmt->Alias.value().end(), stmt->Alias.value().begin(), ::tolower);
    }
    while (IsJoinStart()) {
        stmt->Joins.push_back(ParseJoin());
    }
    if (Match("where")) {
        if (IsAtEnd()) throw ParseException("Expected expression after WHERE");
        stmt->Where = ParseExpression();
    }
    if (Match("group")) {
        Expect("by");
        stmt->GroupBy = ParseGroupBy();
    }
    if (Match("having")) {
        stmt->Having = ParseExpression();
    }
    if (Match("order")) {
        Expect("by");
        stmt->OrderBy = ParseOrderBy();
    }
    if (Match("limit")) {
        stmt->Limit = std::stoi(Expect(TokenType::IntLiteral).GetValue());
    }
    if (Match("offset")) {
        stmt->Offset = std::stoi(Expect(TokenType::IntLiteral).GetValue());
    }
    return stmt;
}

bool Parser::IsJoinStart() const {
    if (IsAtEnd()) return false;
    std::string val = Current().GetValue();
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return val == "join" || val == "inner" || val == "left" || val == "right";
}

JoinClause Parser::ParseJoin() {
    JoinClause jc;
    if (Match("left")) jc.Type = JoinType::Left;
    else if (Match("right")) jc.Type = JoinType::Right;
    else if (Match("inner")) jc.Type = JoinType::Inner;
    else jc.Type = JoinType::Inner; // default "join"
    Expect("join");
    jc.TableName = ExpectName();
    if (!IsAtEnd() && Current().GetType() == TokenType::Identifier) {
        jc.Alias = Expect(TokenType::Identifier).GetValue();
        std::transform(jc.Alias.begin(), jc.Alias.end(), jc.Alias.begin(), ::tolower);
    }
    Expect("on");
    jc.On = ParseExpression();
    return jc;
}

std::vector<std::string> Parser::ParseGroupBy() {
    std::vector<std::string> result;
    do {
        result.push_back(Expect(TokenType::Identifier).GetValue());
        std::transform(result.back().begin(), result.back().end(), result.back().begin(), ::tolower);
    } while (Match(","));
    return result;
}

std::vector<OrderByClause> Parser::ParseOrderBy() {
    std::vector<OrderByClause> result;
    do {
        OrderByClause ob;
        ob.Column = Expect(TokenType::Identifier).GetValue();
        std::transform(ob.Column.begin(), ob.Column.end(), ob.Column.begin(), ::tolower);
        ob.Descending = Match("desc");
        result.push_back(ob);
    } while (Match(","));
    return result;
}

std::vector<SelectColumn> Parser::ParseSelectColumn() {
    std::vector<SelectColumn> cols;
    do {
        if (Current().GetType() == TokenType::Star) {
            SelectColumn sc;
            sc.IsStar = true;
            ++_pos;
            cols.push_back(std::move(sc));
        } else {
            /*
                select a+b as S , ROW_NUMBER() OVER(PARTITION BY customer_id ORDER BY order_date DESC) as rn
                */
            SelectColumn sc;
            sc.Column = ParseExpression();
            bool isWindow = false;
            std::string windowFunc;
            std::vector<std::string> partitionBy;
            std::vector<OrderByClause> windowOrder;

            // Check for OVER clause
            if (auto* fn = dynamic_cast<FunctionExpr*>(sc.Column.get()); fn && Match("over")) {
                isWindow = true;
                windowFunc = fn->Name;
                Expect("(");
                if (Match("partition")) {
                    Expect("by");
                    do { partitionBy.push_back(ExpectName()); } while (Match(","));
                }
                if (Match("order")) {
                    Expect("by");
                    windowOrder = ParseOrderBy();
                }
                Expect(")");
            }

            if (Match("as")) {
                sc.Alias = ExpectName();
            }

            sc.IsWindow = isWindow;
            sc.WindowFunc = windowFunc;
            sc.PartitionBy = partitionBy;
            sc.WindowOrder = windowOrder;
            cols.push_back(std::move(sc));
        }
    } while (Match(","));
    return cols;
}

// Show
/*
     show tables
     show schema [table_name]
     show indexes [table_name]
     show triggers [table_name]
     */

std::unique_ptr<IStatement> Parser::ParseShow() {
    Expect("show");
    if (Current().GetType() != TokenType::Keyword)
        throw ParseException("Expected keyword after SHOW");
    std::string what = Current().GetValue();
    std::transform(what.begin(), what.end(), what.begin(), ::tolower);
    ++_pos;
    std::optional<std::string> target;
    if (what != "tables") {
        target = ExpectName();
    }
    auto stmt = std::make_unique<ShowStatement>();
    stmt->What = what;
    stmt->Target = target;
    return stmt;
}

// Expression parsing (recursive descent)
std::unique_ptr<Expression> Parser::ParseExpression() {
    // should do from the higher priority
    // term/litreal/paren ()  -> Mul/div -> Add/Sub -> Comparison -> Not -> And -> Or
    return ParseOr();
}

std::unique_ptr<Expression> Parser::ParseOr() {
    auto left = ParseAnd();
    while (Match("or")) {
        auto right = ParseAnd();
        auto bin = std::make_unique<BinaryExpr>();
        bin->Left = std::move(left);
        bin->Op = "or";
        bin->Right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expression> Parser::ParseAnd() {
    auto left = ParseNot();
    while (Match("and")) {
        auto right = ParseNot();
        auto bin = std::make_unique<BinaryExpr>();
        bin->Left = std::move(left);
        bin->Op = "and";
        bin->Right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expression> Parser::ParseNot() {
    if (Match("not")) {
        auto operand = ParseComparison();
        auto unary = std::make_unique<UnaryExpr>();
        unary->Op = "not";
        unary->Operand = std::move(operand);
        return unary;
    }
    return ParseComparison();
}

std::unique_ptr<Expression> Parser::ParseComparison() {
    auto left = ParseAddSubExpression();

    if (Match("is")) {
        bool notFlag = Match("not");
        Expect("null");
        auto isNull = std::make_unique<IsNullExpr>();
        isNull->Value = std::move(left);
        isNull->Not = notFlag;
        return isNull;
    }

    if (Match("between")) {
        auto lower = ParseAddSubExpression();
        Expect("and");
        auto upper = ParseAddSubExpression();
        auto between = std::make_unique<BetweenExpr>();
        between->Value = std::move(left);
        between->Lower = std::move(lower);
        between->Upper = std::move(upper);
        return between;
    }

    if (Match("in")) {
        Expect("(");
        std::vector<std::unique_ptr<Expression>> values;
        if (!Match(")")) {
            do {
                values.push_back(ParseExpression());
            } while (Match(","));
            Expect(")");
        }
        auto inExpr = std::make_unique<InExpr>();
        inExpr->Value = std::move(left);
        inExpr->Values = std::move(values);
        return inExpr;
    }

    if (Match("like")) {
        auto pattern = ParseAddSubExpression();
        auto like = std::make_unique<LikeExpr>();
        like->Value = std::move(left);
        like->Pattern = std::move(pattern);
        return like;
    }

    // Comparison operators
    std::vector<std::string> ops = {"!=", ">=", "<=", "=", ">", "<"};
    for (const auto& op : ops) {
        if (Match(op)) {
            auto right = ParseAddSubExpression();
            auto bin = std::make_unique<BinaryExpr>();
            bin->Left = std::move(left);
            bin->Op = op;
            bin->Right = std::move(right);
            left = std::move(bin);
            break;
        }
    }
    return left;
}

std::unique_ptr<Expression> Parser::ParseAddSubExpression() {
    auto left = ParseMulDivExpression();
    while (Match("+") || Match("-")) {
        std::string op = _tokens[_pos - 1].GetValue();
        std::transform(op.begin(), op.end(), op.begin(), ::tolower);
        auto right = ParseMulDivExpression();
        auto bin = std::make_unique<BinaryExpr>();
        bin->Left = std::move(left);
        bin->Op = op;
        bin->Right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expression> Parser::ParseMulDivExpression() {
    auto left = ParseTerm();
    while (Match("*") || Match("/")) {
        std::string op = _tokens[_pos - 1].GetValue();
        std::transform(op.begin(), op.end(), op.begin(), ::tolower);
        auto right = ParseTerm();
        auto bin = std::make_unique<BinaryExpr>();
        bin->Left = std::move(left);
        bin->Op = op;
        bin->Right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<Expression> Parser::ParseTerm() {
    const Token& t = Current();

    // Function call
    if ((t.GetType() == TokenType::Identifier || t.GetType() == TokenType::Keyword) &&
        Peek().GetType() == TokenType::LParen) {
        return ParseFunction();
    }

    // Column reference
    if (t.GetType() == TokenType::Identifier || t.GetType() == TokenType::Keyword) {
        std::string name = t.GetValue();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        ++_pos;
        if (!IsAtEnd() && Current().GetType() == TokenType::Dot) {
            ++_pos;
            std::string col = Expect(TokenType::Identifier).GetValue();
            std::transform(col.begin(), col.end(), col.begin(), ::tolower);
            auto colRef = std::make_unique<ColumnRef>();
            colRef->TableName = name;
            colRef->Column = col;
            colRef->WasQualified = true;   // ← user wrote table.col explicitly
            return colRef;
        }
        auto colRef = std::make_unique<ColumnRef>();
        colRef->Column = name;
        return colRef;
    }
    // Literals
     if (t.GetType() == TokenType::IntLiteral) {
        ++_pos;
        auto lit = std::make_unique<Literal>();
        lit->Value = std::stoi(t.GetValue());
        return lit;
    }
     if (t.GetType() == TokenType::FloatLiteral) {
        ++_pos;
        auto lit = std::make_unique<Literal>();
        lit->Value = std::stod(t.GetValue());
        return lit;
    }
     if (t.GetType() == TokenType::BooleanLiteral) {
        ++_pos;
        auto lit = std::make_unique<Literal>();
        lit->Value = (t.GetValue() == "true");
        return lit;
    }
     if (t.GetType() == TokenType::StringLiteral) {
        ++_pos;
        auto lit = std::make_unique<Literal>();
        lit->Value = t.GetValue();
        return lit;
    }
     if (t.GetType() == TokenType::NullLiteral) {
        ++_pos;
        auto lit = std::make_unique<Literal>();
        lit->Value = std::monostate();
        return lit;
    }
     if (t.GetType() == TokenType::LParen) {
        ++_pos;
        auto expr = ParseExpression();
        Expect(")");
        return expr;
    }
    throw ParseException("Line " + std::to_string(t.GetLine()) + ": unexpected token '" + t.GetValue() + "'");
}

DbObject Parser::ParseLiteralValue() {
    const Token& t = Current();
    ++_pos;
    if (t.GetType() == TokenType::IntLiteral) return std::stoi(t.GetValue());
    if (t.GetType() == TokenType::FloatLiteral) return std::stod(t.GetValue());
    if (t.GetType() == TokenType::StringLiteral) return t.GetValue();
    if (t.GetType() == TokenType::BooleanLiteral) return (t.GetValue() == "true");
    if (t.GetType() == TokenType::NullLiteral) return std::monostate();
    throw ParseException("Expected literal value but got '" + t.GetValue() + "'");
}

std::unique_ptr<Expression> Parser::ParseFunction() {
    std::string name = Current().GetValue();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    ++_pos;
    Expect("(");
    std::unique_ptr<Expression> args;
    if (!Match(")")) {
            if (Current().GetType() == TokenType::Star) {
                auto star = std::make_unique<ColumnRef>();
                star->Column = "*";
                args = std::move(star);
                ++_pos;
            } else {
                args= ParseExpression();
            }
        Expect(")");
    }
    auto func = std::make_unique<FunctionExpr>();
    func->Name = name;
    func->Arguments = std::move(args);
    return func;
}