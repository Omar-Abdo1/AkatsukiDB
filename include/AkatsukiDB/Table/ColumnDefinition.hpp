//
// Created by omarabdo on 6/9/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_COLUMNDEFINITION_HPP
#define AKATSUKIDB_CPP_COLUMNDEFINITION_HPP
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

using DbObject = std::variant<std::monostate,int,double,std::string,bool>; // like Object in C#
using DbRow = std::unordered_map<std::string, DbObject>;

#endif //AKATSUKIDB_CPP_COLUMNDEFINITION_HPP
struct  ColumnDefinition {
 std::string Name;
 std::string Type ;// int float str bool
 int Offset=0 ;// where it start in the row -> id , name , age ...
 int Size=0;
 bool Nullable = true;
 bool IsUnique  = false;
 std::optional<DbObject> Default = std::nullopt;
};