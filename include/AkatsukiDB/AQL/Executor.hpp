//
// Created by omarabdo on 6/11/26.
//

#ifndef AKATSUKIDB_CPP_EXECUTOR_HPP
#define AKATSUKIDB_CPP_EXECUTOR_HPP


#pragma once

#include "AkatsukiDB/Storage/StorageLayout.hpp"
#include "AkatsukiDB/Table/TableRegistry.hpp"
#include "AkatsukiDB/Table/TableManager.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"
#include "AkatsukiDB/Index/BPlusTree.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <optional>

#include "QueryValidatorAndBinder.hpp"
#include "ScanPlanner.hpp"
#include "AkatsukiDB/Engine/QueryResult.hpp"
#include "AkatsukiDB/Statments/CreateIndexStatement.hpp"
#include "AkatsukiDB/Statments/CreateTableStatement.hpp"
#include "AkatsukiDB/Statments/DeleteStatement.hpp"
#include "AkatsukiDB/Statments/DropTableStatement.hpp"
#include "AkatsukiDB/Statments/InsertStatement.hpp"
#include "AkatsukiDB/Statments/IStatement.hpp"
#include "AkatsukiDB/Statments/SelectStatement.hpp"
#include "AkatsukiDB/Statments/ShowStatement.hpp"
#include "AkatsukiDB/Statments/UpdateStatement.hpp"


static std::string DbObjectToString(const DbObject& obj) {
 if (std::holds_alternative<std::monostate>(obj)) return "NULL";
 if (std::holds_alternative<int>(obj)) return std::to_string(std::get<int>(obj));
 if (std::holds_alternative<double>(obj)) return std::to_string(std::get<double>(obj));
 if (std::holds_alternative<std::string>(obj)) return std::get<std::string>(obj);
 if (std::holds_alternative<bool>(obj)) return std::get<bool>(obj) ? "true" : "false";
 return "???";
}

class Executor {
public:
    // Type aliases for maps passed from AkatsukiEngine
    using TableManagerMap = std::unordered_map<std::string, std::unique_ptr<TableManager>>;
    using IndexMap = std::unordered_map<std::string, std::vector<std::pair<IndexDefinition, std::unique_ptr<BPlusTree>>>>;
    using ReferencedByMap = std::unordered_map<std::string, std::vector<std::pair<std::string, ForeignKeyDef>>>;

    explicit Executor(TableRegistry& registry,
             StorageLayout& layout,
             TableManagerMap& tables,
             IndexMap& indexes,
             ReferencedByMap& referencedBy);

    QueryResult Execute(IStatement& stmt);

private:
    // Statement executors
    QueryResult ExecuteSelect(SelectStatement& stmt);
    QueryResult ExecuteInsert(InsertStatement& stmt);
    QueryResult ExecuteUpdate(UpdateStatement& stmt);
    QueryResult ExecuteDelete(DeleteStatement& stmt);
    QueryResult ExecuteCreateTable(CreateTableStatement& stmt);
    QueryResult ExecuteCreateIndex(CreateIndexStatement& stmt);
    QueryResult ExecuteDropTable(DropTableStatement& stmt);
    QueryResult ExecuteTruncate(TruncateStatement& stmt);
    QueryResult ExecuteShow(ShowStatement& stmt);

    // for show
   QueryResult ShowSchema(const std::string& tableName);
    QueryResult ShowIndexes(const std::string& tableName);
     QueryResult ShowTables();


     // Helpers
     IndexKey BuildKeyForTree(const std::unordered_map<std::string, DbObject>& row,
                              const std::vector<std::string>& columns);

      void OpenTable(const std::string& name);

    // for delete/updata/select to get the rows from where expression
 std::vector<RowEntry> GetRowEntries(TableManager& tm,
    const TableDefinition& def, const ScanPlan& plan);

 bool PassesFilter(const DbRow& row, const ScanPlan& plan);

    // for delete
 std::optional<std::string> CheckDependents(const std::string& tableName,
    const DbRow& row, const TableDefinition& def);

 bool HasDependents(const std::string& fromTable,
     const std::string& fkCol, const DbObject& pkVal);


    // for select
 void PrefixRow(DbRow& target, const DbRow& source,
                   const std::string& table, const std::string& alias);

 std::vector<DbRow> HashJoin(
     const std::vector<DbRow>& left,
     const std::vector<DbRow>& right,
     const std::string& leftCol,
     const std::string& rightCol,
     bool isLeft);

 void GetJoinColumns(Expression& on,
     std::string& leftCol, std::string& rightCol);

 std::vector<DbRow> ApplyGroupBy(
     const std::vector<DbRow>& rows,
     const std::vector<std::string>& groupCols,
     const std::vector<SelectColumn>& selectCols);

 DbRow ComputeGroup(
     const std::vector<const DbRow*>& group,
     const std::vector<std::string>& groupCols,
     const std::vector<SelectColumn>& selectCols);

 void ApplyOrderBy(std::vector<DbRow>& rows,
     const std::vector<OrderByClause>& order);

 std::vector<std::string> GetOutputColumns(
     const std::vector<SelectColumn>& cols,
     const TableDefinition& def,
     const std::vector<JoinClause>& joins);

 std::vector<DbRow> ProjectRows(
     const std::vector<DbRow>& rows,
     const std::vector<SelectColumn>& cols);

    std::vector<DbRow> ApplyWindowFunctions(
    std::vector<DbRow>& rows,
    const std::vector<SelectColumn>& selectCols);

    std::vector<DbRow>ApplyDistinct(const std::vector<DbRow>& rows) ;


    // For Expressions
     bool EvaluateBool(Expression& expr, const std::unordered_map<std::string, DbObject>& row);

     DbObject GetValue(Expression& expr, const std::unordered_map<std::string, DbObject>& row);

     bool Compare(const DbObject& left, const std::string& op, const DbObject& right);

    int CompareAny(const DbObject& a, const DbObject& b);

     bool AreEqual(const DbObject& a, const DbObject& b);

     DbObject Calculate(const DbObject& a, const std::string& op, const DbObject& b);

     bool TryDouble(const DbObject& v, double& result);

    bool LikeMatch(const std::string& s, const std::string& pattern);

    bool RecLike(int i, int j, const std::string& s, const std::string& pattern,
                 std::unordered_map<std::string, bool>& dp);


     // Reference members (non‑owning)
     TableRegistry& _registry;
     StorageLayout& _layout;
     TableManagerMap& _tables;
     IndexMap& _indexes;
     ReferencedByMap& _referencedBy;

    // Helper objects
     QueryValidatorAndBinder _validator;
    ScanPlanner _scanPlanner;
};


#endif //AKATSUKIDB_CPP_EXECUTOR_HPP
