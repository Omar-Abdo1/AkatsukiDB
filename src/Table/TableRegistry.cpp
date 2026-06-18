//
// Created by omarabdo on 6/9/26.
//

#include "AkatsukiDB/Table/TableRegistry.hpp"

#include "../../include/AkatsukiDB/Table/TableRegistry.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>
//

void to_json(nlohmann::json& j, const DbObject& obj) {
    if (std::holds_alternative<int>(obj)) {
        j = std::get<int>(obj);
    }
    else if (std::holds_alternative<double>(obj)) {
        j = std::get<double>(obj);
    }
    else if (std::holds_alternative<bool>(obj)) {
        j = std::get<bool>(obj);
    }
    else if (std::holds_alternative<std::string>(obj)) {
        j = std::get<std::string>(obj);
    }
}

void from_json(const nlohmann::json& j, DbObject& obj) {
    if (j.is_boolean()) obj = j.get<bool>();
    else if (j.is_number_integer()) obj = j.get<int>();
    else if (j.is_number_float()) obj = j.get<double>();
    else if (j.is_string()) obj = j.get<std::string>();
    else throw std::runtime_error("Invalid DbObject type");
}

namespace nlohmann {

    template <>
    struct adl_serializer<std::optional<DbObject>> {

        static void to_json(json& j, const std::optional<DbObject>& opt) {
            if (!opt.has_value()) {
                j = nullptr;
                return;
            }

            json tmp;
            ::to_json(tmp, opt.value());
            j = tmp;
        }

        static void from_json(const json& j, std::optional<DbObject>& opt) {
            if (j.is_null()) {
                opt = std::nullopt;
                return;
            }

            DbObject obj;
            ::from_json(j, obj);
            opt = obj;
        }
    };

}


NLOHMANN_JSON_SERIALIZE_ENUM(OnDelete, {
    {OnDelete::RESTRICT, "RESTRICT"},
    {OnDelete::CASCADE, "CASCADE"}
})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ForeignKeyDef,Column,RefTable,RefColumn,OnDeleteAction)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(IndexDefinition,Name,Columns,IsUnique,IsPrimary)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ColumnDefinition,Name,Type,Offset,Size,Nullable,IsUnique,Default)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TableDefinition,Name,RowSizeBytes,Columns,PrimaryKey,AutoIncrement,NextAutoValue,ForeignKeys,Indexes)
/*
the library literally copy-pasted a hidden to_json and from_json function into your code during the build step */

TableRegistry::TableRegistry(StorageLayout& layout)
    : _layout(layout)
{
    LoadAllSchemas();
}

void TableRegistry::LoadAllSchemas() {
    if (!std::filesystem::exists(_layout.SchemaFolder())) return;

    for (const auto& entry : std::filesystem::directory_iterator(_layout.SchemaFolder())) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());

            nlohmann::json j;
            file >> j; // read the json file

            TableDefinition schema = j.get<TableDefinition>(); // Deserialize
            _schemas[schema.Name] = std::move(schema);
        }
    }
}

void TableRegistry::SaveTable(const TableDefinition& definition) {
    std::string path = _layout.SchemaFile(definition.Name);

    nlohmann::json j = definition; // Serialize convert the cpp object into json

    std::ofstream file(path);
    file << j.dump(4); // 4 spaces for every indentation level

    _schemas[definition.Name] = definition;
}

 TableDefinition& TableRegistry::CreateTable(
    const std::string& tableName,
    std::vector<ColumnDefinition> columns,
    const std::vector<std::string>& primaryKey,
    bool autoIncrement,
    const std::vector<ForeignKeyDef>& foreignKeys)
{
    std::string unifiedName = Unify(tableName);
    int currentOffset = 0;

    std::unordered_set<std::string> colSet;
    for (const auto& col : columns) {
        if (colSet.count(col.Name))
            throw std::runtime_error("Duplicate column name: " + col.Name);
        colSet.insert(col.Name);
    }


    std::vector<ColumnDefinition> processedColumns = columns;
    for (auto& col : processedColumns) {
        col.Offset = currentOffset;
        col.Size = GetTypeSize(col.Type);
        currentOffset += col.Size;
    }

    std::vector<IndexDefinition> indexes;
    if (!primaryKey.empty()) {
        indexes.push_back({
            "pk_" + unifiedName,
            primaryKey,
            true,
            true
        });
    }

    for (const auto& col : processedColumns) {
        bool inPk = std::find(primaryKey.begin(), primaryKey.end(), col.Name) != primaryKey.end();

        if (col.IsUnique && !inPk) { // if the column is unique constrain we make for it an index
            indexes.push_back({
                "uq_" + unifiedName + "_" + col.Name,
                {col.Name},
                true,
                false
            });
        }
    }

    TableDefinition schema;
    schema.Name = unifiedName;
    schema.Columns = std::move(processedColumns);
    schema.PrimaryKey = primaryKey;
    schema.AutoIncrement = autoIncrement;
    schema.ForeignKeys = foreignKeys;
    schema.Indexes = std::move(indexes);
    schema.NextAutoValue = 1;
    schema.RowSizeBytes = currentOffset + 10;

    SaveTable(schema);
    return _schemas[unifiedName];
}

TableDefinition& TableRegistry::GetTable(const std::string& tableName) {
    std::string unified = Unify(tableName);
    auto it = _schemas.find(unified);
    if (it == _schemas.end())
        throw std::runtime_error("Table '" + tableName + "' does not exist.");
    return it->second;
}

const TableDefinition& TableRegistry::GetTable(const std::string& tableName) const {
    std::string unified = Unify(tableName);
    auto it = _schemas.find(unified);
    if (it == _schemas.end())
        throw std::runtime_error("Table '" + tableName + "' does not exist.");
    return it->second;
}

std::vector<std::string> TableRegistry::GetAllTableNames() const {
    std::vector<std::string> names;
    names.reserve(_schemas.size());

    for (const auto& [name,tableDef] : _schemas) {
        names.push_back(name);
    }
    return names;
}

bool TableRegistry::TableExists(const std::string& name) const {
    return _schemas.find(Unify(name)) != _schemas.end();
}

void TableRegistry::DropTable(const std::string& name) {
    // executor remove the physical files
    _schemas.erase(Unify(name)); // remove from in-memory map
}

int TableRegistry::GetTypeSize(const std::string& type) const {
    std::string t = Unify(type);
    if (t == "int") return 4;
    if (t == "float") return 8;
    if (t == "bool") return 1;
    if (t == "str") return 104; // 4 for length + 100 byte for the string itself

    throw std::invalid_argument("Unsupported type: " + type);
}

std::string TableRegistry::Unify(const std::string& s) const {
    std::string result = s;
    for (auto &c : result) c=tolower(c);
    return result;
}
