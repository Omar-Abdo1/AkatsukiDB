//
// Created by omarabdo on 6/19/26.
//

#ifndef AKATSUKIDB_CPP_TRANSACTIONMANAGER_HPP
#define AKATSUKIDB_CPP_TRANSACTIONMANAGER_HPP
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "WalManager.hpp"


struct TxnChange {
    WalType              Type;
    std::string          TableName;
    int                  PageId, SlotIndex;
    std::vector<std::uint8_t> BeforeData;
};

struct TxnEntry {
    uint32_t TxnId;

    bool Active = true;
    std::vector<TxnChange> Changes;
};

class TransactionManager {
public:
    explicit TransactionManager(WalManager& wal) : _wal(wal) {}

    uint32_t Begin() {
        if (_activeTxnId != 0)
            throw std::runtime_error("Nested transactions not supported. COMMIT or ROLLBACK first."); //we can not begin and the we already have one transaction yet :)
        uint32_t id = _nextTxnId++;
        _wal.LogBegin(id);
        _txns[id] = {id, true, {}};
        _activeTxnId = id;
        return id;
    }

    void Commit(uint32_t txnId) { _wal.LogCommit(txnId); End(txnId); }

    void RecordInsert(uint32_t txnId, const std::string& t, int p, int s) {
        _wal.LogInsert(txnId, t, p, s);
        _txns[txnId].Changes.push_back({WalType::Insert, t, p, s, {}});
    }
    void RecordUpdate(uint32_t txnId, const std::string& t, int p, int s,
        std::span<const uint8_t> before) {
        _wal.LogUpdate(txnId, t, p, s, before);
        TxnChange c{WalType::Update, t, p, s, {}};
        c.BeforeData.assign(before.begin(), before.end());
        _txns[txnId].Changes.push_back(std::move(c));
    }
    void RecordDelete(uint32_t txnId, const std::string& t, int p, int s,
        std::span<const uint8_t> before) {
        _wal.LogDelete(txnId, t, p, s, before);
        TxnChange c{WalType::Delete, t, p, s, {}};
        c.BeforeData.assign(before.begin(), before.end());
        _txns[txnId].Changes.push_back(std::move(c));
    }

    void End(uint32_t txnId) {
        if (_txns.count(txnId)) _txns[txnId].Active = false;
        _activeTxnId = 0;
    }

    TxnEntry* Get(uint32_t txnId) {
        auto it = _txns.find(txnId);
        return it != _txns.end() ? &it->second : nullptr;
    }
    uint32_t CurrentTxnId() const { return _activeTxnId; }

private:
    WalManager& _wal;
    uint32_t _nextTxnId = 1;
    uint32_t _activeTxnId = 0; // 0 means there are no active
    std::unordered_map<uint32_t, TxnEntry> _txns;
};


#endif //AKATSUKIDB_CPP_TRANSACTIONMANAGER_HPP
