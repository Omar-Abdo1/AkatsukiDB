//
// Created by omarabdo on 6/19/26.
//

#include "AkatsukiDB/AQL/Executor.hpp"

uint32_t Executor::GetOrBeginTxn() {
    if (_txnManager.CurrentTxnId() != 0) return _txnManager.CurrentTxnId();
    return _txnManager.Begin();
}

void Executor::AutoCommit(uint32_t txnId, bool wasAuto) {
    if (wasAuto) _txnManager.Commit(txnId);
}

QueryResult Executor::ExecuteBegin() {
    try { _txnManager.Begin(); return QueryResult::Affected(0); }
    catch (const std::exception& e) { return QueryResult::Error(e.what()); }
}
QueryResult Executor::ExecuteCommit() {
    uint32_t id = _txnManager.CurrentTxnId();
    if (id == 0) return QueryResult::Error("No active transaction.");
    _txnManager.Commit(id);
    return QueryResult::Affected(0);
}
QueryResult Executor::ExecuteRollback() {
    uint32_t id = _txnManager.CurrentTxnId();
    if (id == 0) return QueryResult::Error("No active transaction.");
    auto* txn = _txnManager.Get(id);
    if (!txn) return QueryResult::Error("Transaction not found.");

    for (int i = (int)txn->Changes.size() - 1; i >= 0; --i) {
        auto& c  = txn->Changes[i];
        auto& tm = *_tables[c.TableName];
        const auto& def = _registry.GetTable(c.TableName);

        if (c.Type == WalType::Insert) { // we delete
            auto bytes = tm.ReadRow(c.PageId, c.SlotIndex);
            auto row   = RowSerializer::Deserialize(bytes, def.Columns);
            if (_indexes.count(c.TableName))
                for (auto& [idxDef, tree] : _indexes[c.TableName])
                    tree->Delete(BuildKeyForTree(row, idxDef.Columns));
            tm.DeleteRow(c.PageId, c.SlotIndex);
        }
        else if (c.Type == WalType::Update) { // just put the before
            auto curBytes  = tm.ReadRow(c.PageId, c.SlotIndex);
            auto curRow    = RowSerializer::Deserialize(curBytes, def.Columns);
            auto beforeRow = RowSerializer::Deserialize(c.BeforeData, def.Columns);
            tm.UpdateRow(c.PageId, c.SlotIndex, c.BeforeData);
            if (_indexes.count(c.TableName))
                for (auto& [idxDef, tree] : _indexes[c.TableName]) {
                    auto curKey = BuildKeyForTree(curRow, idxDef.Columns);
                    auto oldKey = BuildKeyForTree(beforeRow, idxDef.Columns);
                    if (curKey != oldKey) {
                        tree->Delete(curKey);
                        tree->Insert({oldKey, c.PageId, (short)c.SlotIndex});
                    }
                }
        }
        else if (c.Type == WalType::Delete) {
            tm.UndeleteRow(c.PageId, c.SlotIndex, c.BeforeData);
            auto row = RowSerializer::Deserialize(c.BeforeData, def.Columns);
            if (_indexes.count(c.TableName))
                for (auto& [idxDef, tree] : _indexes[c.TableName])
                    tree->Insert({BuildKeyForTree(row, idxDef.Columns), c.PageId, (short)c.SlotIndex});
        }
    }
    _wal.LogRollback(id);
    _txnManager.End(id);
    return QueryResult::Affected(0);
}

