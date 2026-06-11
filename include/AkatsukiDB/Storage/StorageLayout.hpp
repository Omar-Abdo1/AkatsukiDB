//
// Created by omarabdo on 6/7/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_STORAGELAYOUT_HPP
#define AKATSUKIDB_CPP_STORAGELAYOUT_HPP
#include <filesystem>
#include <string>


class StorageLayout {

    std::filesystem::path _root;

    void CreateFolders() const;

public:
    explicit StorageLayout(const std::string& rootDirectory);

    std::string TablesFolder() const;
    std::string IndexFolder() const;
    std::string SchemaFolder() const;
    std::string WalFolder() const;

    std::string TableFile(const std::string& tableName) const;
    std::string IndexFile(const std::string& indexName) const;
    std::string SchemaFile(const std::string& tableName) const;
    std::string WalFile() const;

};


#endif //AKATSUKIDB_CPP_STORAGELAYOUT_HPP
