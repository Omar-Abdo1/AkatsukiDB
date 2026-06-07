//
// Created by omarabdo on 6/7/26.
//

#include "../../include/AkatsukiDB/Storage/StorageLayout.hpp"

void StorageLayout::CreateFolders() const {
    std::filesystem::create_directories(TablesFolder());
    std::filesystem::create_directories(IndexFolder());
    std::filesystem::create_directories(SchemaFolder());
    std::filesystem::create_directories(WalFolder());
}

StorageLayout::StorageLayout(const std::string &rootDirectory):_root(rootDirectory) {
CreateFolders();
}

std::string StorageLayout::TablesFolder() const {
 return (_root / "tables").string();
}

std::string StorageLayout::IndexFolder() const {

return (_root / "index").string();
}

std::string StorageLayout::SchemaFolder() const {

    return (_root / "schema").string();
}

std::string StorageLayout::WalFolder() const {

    return (_root / "wal").string();

}

std::string StorageLayout::TableFile(const std::string &tableName) const {
return (_root / "tables" / (tableName + ".tbl")).string();
}

std::string StorageLayout::IndexFile(const std::string &indexName) const {
    return (_root / "index" / (indexName + ".idx")).string();
}

std::string StorageLayout::SchemaFile(const std::string &tableName) const {
    return (_root / "schema" / (tableName + ".schema.json")).string();
}

std::string StorageLayout::WalFile() const {
    return (_root / "wal" / "wal_current.log").string();
}
