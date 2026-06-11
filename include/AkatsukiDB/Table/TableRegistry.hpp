//
// Created by omarabdo on 6/9/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_TABLEREGISTRY_HPP
#define AKATSUKIDB_CPP_TABLEREGISTRY_HPP
#include <unordered_map>
#include <vector>

#include "AkatsukiDB/Storage/StorageLayout.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"


class TableRegistry {

    StorageLayout _layout;
    std::unordered_map<std::string, TableDefinition> _schemas;

    void LoadAllSchemas();
    std::string Unify(const std::string& s) const;
    int GetTypeSize(const std::string& type) const;

public:

    explicit TableRegistry(StorageLayout layout);

    std::vector<std::string> GetAllTableNames() const;

    bool TableExists(const std::string& name) const;

    void DropTable(const std::string& name);

    // Return a const reference! Look, but don't touch, and don't copy!
    const TableDefinition& GetTable(const std::string& tableName) const;

    const TableDefinition& CreateTable(
        const std::string& tableName,
        std::vector<ColumnDefinition> columns,
        const std::vector<std::string>& primaryKey,
        bool autoIncrement,
        const std::vector<ForeignKeyDef>& foreignKeys
    );

    void SaveTable(const TableDefinition& definition);

};


#endif //AKATSUKIDB_CPP_TABLEREGISTRY_HPP
