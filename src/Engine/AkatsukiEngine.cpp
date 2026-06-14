//
// Created by omarabdo on 6/11/26.
//

#include "../../include/AkatsukiDB/Engine/AkatsukiEngine.hpp"



#include "AkatsukiDB/Storage/StorageLayout.hpp"
#include "AkatsukiDB/Table/TableRegistry.hpp"
#include "AkatsukiDB/Table/TableManager.hpp"
#include "AkatsukiDB/Table/TableDefinition.hpp"
#include "AkatsukiDB/Index/BPlusTree.hpp"
#include "AkatsukiDB/AQL/Tokenizer.hpp"
#include "AkatsukiDB/AQL/Parser.hpp"
#include "AkatsukiDB/AQL/Executor.hpp"
#include <filesystem>


AkatsukiEngine::AkatsukiEngine(const std::string& dataDirectory) {
    _layout = std::make_unique<StorageLayout>(dataDirectory);
    _registry = std::make_unique<TableRegistry>(*_layout);

    for (const auto& name : _registry->GetAllTableNames())
        OpenTable(name);

    BuildReferencedByMap();

    _executor = std::make_unique<Executor>(
        *_registry, *_layout, _tables, _indexes, _referencedBy);
}

AkatsukiEngine::~AkatsukiEngine() {
    Dispose();
}

QueryResult AkatsukiEngine::Execute(const std::string& aql) {
    if (aql.empty())
        return QueryResult::Error("Empty query.");
    try {
        Tokenizer tokenizer(aql);
        auto tokens = tokenizer.Tokenize();
        Parser parser;
        auto ast = parser.Parse(tokens);
        return _executor->Execute(*ast);
    } catch (const std::exception& ex) {
        return QueryResult::Error(ex.what());
    }
}

void AkatsukiEngine::OpenTable(const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    auto& def = _registry->GetTable(lowerName);

    auto bp = std::make_unique<BufferPool>(_layout->TableFile(lowerName));

    _tables[lowerName] = std::make_unique<TableManager>(std::move(bp), def.RowSizeBytes);

    for (auto& idxDef : def.Indexes) {
        auto idxPath = _layout->IndexFile(idxDef.Name);
        auto tree = std::make_unique<BPlusTree>(idxPath);
        _indexes[lowerName].emplace_back(idxDef, std::move(tree));
    }
}

void AkatsukiEngine::BuildReferencedByMap() {
    auto names = _registry->GetAllTableNames();
    for (const auto& name : names) {
        auto& def = _registry->GetTable(name);
        for (auto& fk : def.ForeignKeys) {
            std::string refTable = fk.RefTable;
            std::transform(refTable.begin(), refTable.end(), refTable.begin(), ::tolower);
            _referencedBy[refTable].push_back({name, fk});
        }
    }
}

void AkatsukiEngine::CloseTable(const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    auto it = _tables.find(lowerName);
    if (it != _tables.end()) {
        it->second.reset(); // destructor will flush and close
        _tables.erase(it);
    }
    auto idxIt = _indexes.find(lowerName);
    if (idxIt != _indexes.end()) {
        for (auto& [def, tree] : idxIt->second)
            tree.reset(); // BPlusTree destructor flushes
        _indexes.erase(idxIt);
    }
}

void AkatsukiEngine::Dispose() {
    std::vector<std::string> names;
    for (auto& p : _tables) names.push_back(p.first);
    for (const auto& name : names)
        CloseTable(name);
}

