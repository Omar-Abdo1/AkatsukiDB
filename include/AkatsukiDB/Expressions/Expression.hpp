//
// Created by omarabdo on 6/10/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_EXPRESSION_HPP
#define AKATSUKIDB_CPP_EXPRESSION_HPP
#include <memory>
#include <string>
#include <vector>

#include "AkatsukiDB/Table/ColumnDefinition.hpp"

#endif //AKATSUKIDB_CPP_EXPRESSION_HPP

class Expression {
public:
   virtual  ~Expression()=default;
};


 class BinaryExpr : public Expression
{
 public:
 std::unique_ptr<Expression> Left;
 std::string Op; // = != > < >= <= AND OR
std::unique_ptr<Expression>Right ;
};


class Literal : public Expression
{
 public:
  DbObject Value;
};



 class ColumnRef : public Expression
{
 public:
  std::string Column ;
  std::optional<std::string> TableName; // can be empty (null)
};

 class UnaryExpr : public Expression
{
 public:
  std::unique_ptr<Expression> Operand;
std::string Op ; // NOT

};

 class FunctionExpr : public Expression
{
 public:
std:: string Name ; // avg , min , func , etc..
 std::vector<std::unique_ptr<Expression>> Arguments ; // avg(a+b) , min(a*b+c)
};

 class IsNullExpr : public Expression
{
 public:
  std::unique_ptr<Expression> Value ;
 bool Not ;
};

 class BetweenExpr : public Expression
{
 public:
  std::unique_ptr<Expression> Value ;
std::unique_ptr<Expression>  Lower ;
std::unique_ptr<Expression>  Upper ;
};

 class InExpr : public Expression
{
 public :
  std::unique_ptr<Expression> Value  ;
std::vector<std::unique_ptr<Expression>>Values ;
};

 class LikeExpr : public Expression
{
 public:
   std::unique_ptr<Expression>  Value  ;
std::unique_ptr<Expression>     Pattern;
  // i can make both string because this the most cases but for more flexibility making both as expressing it also good
};

