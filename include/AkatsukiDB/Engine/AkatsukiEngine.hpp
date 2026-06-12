//
// Created by omarabdo on 6/11/26.
//

#pragma once

#ifndef AKATSUKIDB_CPP_AKATSUKIENGINE_HPP
#define AKATSUKIDB_CPP_AKATSUKIENGINE_HPP
#include <memory>
#include <string>

#include "QueryResult.hpp"
#include "AkatsukiDB/AQL/Executor.hpp"
#include "AkatsukiDB/Index/BPlusTree.hpp"
#include "AkatsukiDB/Table/TableManager.hpp"
#include "AkatsukiDB/Table/TableRegistry.hpp"


class AkatsukiEngine {
public:
    explicit AkatsukiEngine(const std::string& dataDirectory);
    ~AkatsukiEngine();

    QueryResult Execute(const std::string& aql);

    void Dispose();

private:
    std::unique_ptr<StorageLayout> _layout;
    std::unique_ptr<TableRegistry> _registry;
    std::unique_ptr<Executor> _executor;

    // table name -> TableManager
    std::unordered_map<std::string, std::unique_ptr<TableManager>> _tables;

    // table name -> list of (index def, BPlusTree)
    std::unordered_map<std::string,std::vector<std::pair<IndexDefinition,std::unique_ptr<BPlusTree>>>> _indexes;

    // reverse FK: referenced table -> list of (fromTable, fkDef)
   std::unordered_map<std::string, std::vector<std::pair<std::string, ForeignKeyDef>>>_referencedBy;

    void OpenTable(const std::string& name);
    void BuildReferencedByMap();
    void CloseTable(const std::string& name);
};

#endif //AKATSUKIDB_CPP_AKATSUKIENGINE_HPP
