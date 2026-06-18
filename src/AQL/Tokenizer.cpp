//
// Created by omarabdo on 6/11/26.
//

#include "../../include/AkatsukiDB/AQL/Tokenizer.hpp"

#include <cctype>
#include <algorithm>
#include <stdexcept>

Token::Token(TokenType type, std::string value, int line)
    : _type(type), _value(std::move(value)), _line(line) {}

std::string Token::ToString() const {
    std::string typeStr;
    if (_type == TokenType::Keyword) typeStr = "Keyword";
    else if (_type == TokenType::Identifier) typeStr = "Identifier";
    else if (_type == TokenType::Operator) typeStr = "Operator";
    else if (_type == TokenType::IntLiteral) typeStr = "IntLiteral";
    else if (_type == TokenType::FloatLiteral) typeStr = "FloatLiteral";
    else if (_type == TokenType::StringLiteral) typeStr = "StringLiteral";
    else if (_type == TokenType::BooleanLiteral) typeStr = "BooleanLiteral";
    else if (_type == TokenType::NullLiteral) typeStr = "NullLiteral";
    else if (_type == TokenType::Comma) typeStr = "Comma";
    else if (_type == TokenType::Colon) typeStr = "Colon";
    else if (_type == TokenType::Dot) typeStr = "Dot";
    else if (_type == TokenType::Star) typeStr = "Star";
    else if (_type == TokenType::LParen) typeStr = "LParen";
    else if (_type == TokenType::RParen) typeStr = "RParen";
    else if (_type == TokenType::LBrace) typeStr = "LBrace";
    else if (_type == TokenType::RBrace) typeStr = "RBrace";
    else if (_type == TokenType::LBracket) typeStr = "LBracket";
    else if (_type == TokenType::RBracket) typeStr = "RBracket";
    else if (_type == TokenType::EOF_) typeStr = "EOF";
    return "[ " + typeStr + " : " + _value + " ]  Line " + std::to_string(_line);
}

const std::unordered_set<std::string>& Tokenizer::GetKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "select", "from", "where", "insert", "into", "update", "show", "schema", "tables", "indexes", "triggers","truncate",
        "delete", "create", "table", "drop", "alter", "pk", "fk","default",
        "on", "index", "auto", "not", "true", "false", "and", "or",
        "cascade", "restrict", "join", "left", "right", "inner", "order",
        "by", "asc", "desc", "group", "having", "limit", "in", "between", "like", "distinct",
        "over", "partition", "row_number", "sum", "count", "avg", "min", "max", "begin", "commit", "rollback","int","float","str"
    };
    return keywords;
}

Tokenizer::Tokenizer(const std::string& input) : _input(input), _pos(0), _line(1) {}

std::vector<Token> Tokenizer::Tokenize() {
    std::vector<Token> tokens;
    _pos = 0;
    _line = 1;

    while (_pos < _input.size()) {

        SkipWhitespaceAndComments();

        if (_pos >= _input.size()) break;

        char c = _input[_pos];
        Token tk(TokenType::EOF_, "", _line); // dummy

        if (c == '.') {
            tk = Token(TokenType::Dot, ".", _line);
            ++_pos;
        } else if (c == ',') {
            tk = Token(TokenType::Comma, ",", _line);
            ++_pos;
        } else if (c == '*') {
            tk = Token(TokenType::Star, "*", _line);
            ++_pos;
        } else if (c == '(') {
            tk = Token(TokenType::LParen, "(", _line);
            ++_pos;
        } else if (c == ')') {
            tk = Token(TokenType::RParen, ")", _line);
            ++_pos;
        } else if (c == '{') {
            tk = Token(TokenType::LBrace, "{", _line);
            ++_pos;
        } else if (c == '}') {
            tk = Token(TokenType::RBrace, "}", _line);
            ++_pos;
        } else if (c == '[') {
            tk = Token(TokenType::LBracket, "[", _line);
            ++_pos;
        } else if (c == ']') {
            tk = Token(TokenType::RBracket, "]", _line);
            ++_pos;
        } else if (c == ':') {
            tk = Token(TokenType::Colon, ":", _line);
            ++_pos;
        } else if (c == '\'' || c == '"') {
            tk = ReadString(c);
        } else if (std::isdigit(c) ||
                   (c == '-' && _pos + 1 < _input.size() && std::isdigit(_input[_pos + 1]))) {
            tk = ReadNumber();
        } else if (std::isalpha(c) || c == '_') {
            tk = ReadIdentifierOrKeyword();
        } else if (std::string("><=!+-*/").find(c) != std::string::npos) {
            char next = (_pos + 1 < _input.size()) ? _input[_pos + 1] : '\0';
            if (c == '!' && next != '=')
                throw std::runtime_error("Unexpected character '!' at line " + std::to_string(_line));

            std::string op;
            if (c == '!' && next == '=') {
                op = "!=";
                _pos += 2;
            } else if (c == '<' && next == '=') {
                op = "<=";
                _pos += 2;
            } else if (c == '>' && next == '=') {
                op = ">=";
                _pos += 2;
            }else {
                op = std::string(1, c);
                ++_pos;
            }
            tk = Token(TokenType::Operator, op, _line);
        } else {
            throw std::runtime_error("Unexpected character: " + std::string(1, c) + " at line " + std::to_string(_line));
        }

        tokens.push_back(tk);
    }

    tokens.emplace_back(TokenType::EOF_, "", _line);
    return tokens;
}

void Tokenizer::SkipWhitespaceAndComments() {
    while (_pos < _input.size()) {
        if (_input[_pos] == '\n') {
            ++_line;
            ++_pos;
            continue;
        }
        if (std::isspace(_input[_pos])) {
            ++_pos;
            continue;
        }

        // Line comment //
        if (_pos + 1 < _input.size() && _input[_pos] == '/' && _input[_pos + 1] == '/') {
            while (_pos < _input.size() && _input[_pos] != '\n') ++_pos;
            continue;
        }

        // Block comment /*
        if (_pos + 1 < _input.size() && _input[_pos] == '/' && _input[_pos + 1] == '*') {
            _pos += 2;
            while (_pos + 1 < _input.size() && !(_input[_pos] == '*' && _input[_pos + 1] == '/')) {
                if (_input[_pos] == '\n') ++_line;
                ++_pos;
            }
            _pos += 2;
            continue;
        }
        break;
    }
}

Token Tokenizer::ReadIdentifierOrKeyword() {
    size_t start = _pos;
    while (_pos < _input.size() && ((isdigit(_input[_pos]) || isalpha(_input[_pos]) )
        || _input[_pos] == '_')   ) {
        ++_pos;
    }
    std::string value = _input.substr(start, _pos - start);
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    const auto& keywords = GetKeywords();
    if (keywords.find(lower) != keywords.end()) {
        if (lower == "true" || lower == "false")
            return Token(TokenType::BooleanLiteral, lower, _line);
        if (lower == "null")
            return Token(TokenType::NullLiteral, lower, _line);
        return Token(TokenType::Keyword, lower, _line);
    }
    return Token(TokenType::Identifier, value, _line);
}

Token Tokenizer::ReadString(char quote) {
    ++_pos; // skip opening quote " '
    size_t start = _pos;
    while (_pos < _input.size() && _input[_pos] != quote) {
        ++_pos;
    }
    if (_pos >= _input.size())
        throw std::runtime_error("Unterminated string literal at line " + std::to_string(_line));
    std::string value = _input.substr(start, _pos - start);
    ++_pos; // skip closing quote " '
    return Token(TokenType::StringLiteral, value, _line);
}

Token Tokenizer::ReadNumber() {
    size_t start = _pos;
    if (_input[_pos] == '-') ++_pos;
    while (_pos < _input.size() && std::isdigit(_input[_pos])  ) ++_pos;

    if (_pos < _input.size() && _input[_pos] == '.') {
        ++_pos;
        while (_pos < _input.size() && std::isdigit(_input[_pos])) ++_pos;
        std::string value = _input.substr(start, _pos - start);
        return Token(TokenType::FloatLiteral, value, _line);
    }
    std::string value = _input.substr(start, _pos - start);
    return Token(TokenType::IntLiteral, value, _line);
}

