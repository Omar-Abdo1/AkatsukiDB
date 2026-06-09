//
// Created by omarabdo on 6/9/26.
//
#pragma once // so compiler when include file.hpp which include file2.hpp then include file2.hpp , just include it once

#ifndef AKATSUKIDB_CPP_TABLEDEFINITION_HPP
#define AKATSUKIDB_CPP_TABLEDEFINITION_HPP
#include <string>
#include <vector>

#include "ColumnDefinition.hpp"

#endif //AKATSUKIDB_CPP_TABLEDEFINITION_HPP

enum class OnDelete {
    RESTRICT,
    CASCADE
};

struct ForeignKeyDef {
    std::string Column;
    std::string RefTable;
    std::string RefColumn;

    OnDelete OnDeleteAction = OnDelete::RESTRICT;
};


struct IndexDefinition {
    std::string Name;
    std::vector<std::string> Columns;
    bool IsUnique = false;
    bool IsPrimary = false;
};


struct TableDefinition {
    std::string Name;
    int RowSizeBytes = 0;
    std::vector<ColumnDefinition> Columns;
    std::vector<std::string> PrimaryKey;
    bool AutoIncrement = false;
    int NextAutoValue = 1;
    std::vector<ForeignKeyDef> ForeignKeys;
    std::vector<IndexDefinition> Indexes;
};