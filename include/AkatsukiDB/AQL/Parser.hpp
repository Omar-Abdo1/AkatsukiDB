//
// Created by omarabdo on 6/11/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_PARSER_HPP
#define AKATSUKIDB_CPP_PARSER_HPP
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "AkatsukiDB/AQL/Tokenizer.hpp"
#include "AkatsukiDB/Expressions/Expression.hpp"
#include "AkatsukiDB/Statments/IStatement.hpp"
#include "AkatsukiDB/Statments/SelectStatement.hpp"
#include "AkatsukiDB/Table/RowSerializer.hpp"


class Parser {
public:
    class ParseException : public std::runtime_error {
    public:
        explicit ParseException(const std::string& msg) : std::runtime_error(msg) {}
    };

    std::unique_ptr<IStatement> Parse(const std::vector<Token>& tokens);

private:
    std::vector<Token> _tokens;
    size_t _pos = 0;

    const Token& Current() const { return _tokens[_pos]; }

    const Token& Peek(int offset = 1) const {
        size_t idx = _pos + offset;
        return idx < _tokens.size() ? _tokens[idx] : _tokens.back();
    }
    bool IsAtEnd() const { return _pos >= _tokens.size() || Current().GetType() == TokenType::EOF_; }

    bool Match(const std::string& value);
    void Expect(const std::string& value);
    Token Expect(TokenType type);
    std::string ExpectName();

    // Statement parsers
    std::unique_ptr<IStatement> ParseCreate();
    std::unique_ptr<IStatement> ParseCreateIndex(bool unique);
    std::unique_ptr<IStatement> ParseCreateTable();
    std::unique_ptr<IStatement> ParseDelete();
    std::unique_ptr<IStatement> ParseDrop();
    std::unique_ptr<IStatement> ParseTruncate();
    std::unique_ptr<IStatement> ParseInsert();
    std::unique_ptr<IStatement> ParseSelect();
    std::unique_ptr<IStatement> ParseShow();
    std::unique_ptr<IStatement> ParseUpdate();

    // Helpers for Insert
    std::vector<DbRow> ParseInsertRows();
    DbRow ParseRow();

    // Expression parsing
    std::unique_ptr<Expression> ParseExpression();
    std::unique_ptr<Expression> ParseOr();
    std::unique_ptr<Expression> ParseAnd();
    std::unique_ptr<Expression> ParseNot();
    std::unique_ptr<Expression> ParseComparison();
    std::unique_ptr<Expression> ParseAddSubExpression();
    std::unique_ptr<Expression> ParseMulDivExpression();
    std::unique_ptr<Expression> ParseTerm();
    DbObject ParseLiteralValue();
    std::unique_ptr<Expression> ParseFunction();

    // Select helpers
    std::vector<SelectColumn> ParseSelectColumn();
    bool IsJoinStart() const;
    JoinClause ParseJoin();
    std::vector<std::string> ParseGroupBy();
    std::vector<OrderByClause> ParseOrderBy();
};


#endif //AKATSUKIDB_CPP_PARSER_HPP
