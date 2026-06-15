//
// Created by omarabdo on 6/11/26.
//

#pragma once


#ifndef AKATSUKIDB_CPP_TOKENIZER_HPP
#define AKATSUKIDB_CPP_TOKENIZER_HPP
#include <string>
#include <unordered_set>
#include <vector>

enum class TokenType {
    Keyword, Identifier, Operator, IntLiteral, FloatLiteral, StringLiteral, BooleanLiteral,
    NullLiteral, Comma, Colon, Dot, Star, LParen, RParen, LBrace, RBrace, LBracket, RBracket, EOF_
};

class Token {
public:
    Token(TokenType type, std::string value, int line);

    TokenType GetType() const { return _type; }
    const std::string& GetValue() const { return _value; }
    int GetLine() const { return _line; }

    std::string ToString() const;

private:
    TokenType _type;
    std::string _value;
    int _line;
};

class Tokenizer {
public:

    explicit Tokenizer(const std::string& input);

    std::vector<Token> Tokenize();
        static const std::unordered_set<std::string>& GetKeywords();


private:
    std::string _input;
    size_t _pos;
    int _line;


    void SkipWhitespaceAndComments();
    Token ReadIdentifierOrKeyword();
    Token ReadString(char quote);
    Token ReadNumber();
};


#endif //AKATSUKIDB_CPP_TOKENIZER_HPP
