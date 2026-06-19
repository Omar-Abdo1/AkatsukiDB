//
// Created by omarabdo on 6/19/26.
//

#ifndef AKATSUKIDB_WALMANAGER_HPP
#define AKATSUKIDB_WALMANAGER_HPP
#include <cstdint>
#include <span>
#include <string>
#include <vector>
/*
 4F 4D 41 52

 O -> 79  0100 1111  -> 4F
 M ->     0100 1101  -> 4D
 A -> 65  0100 0001  -> 41
 R ->     0101 0010 -> 52
  */
constexpr std::uint32_t WAL_MAGIC = 0x4F4D4152; // OMAR :)

enum class WalType : uint8_t {
    Begin    = 1,
    Insert   = 2,
    Update   = 3,
    Delete   = 4,
    Commit   = 5,
    Rollback = 6
};

#pragma pack(push, 1) // enforce the compiler to not make padding in the struct
struct WalHeader {
    uint32_t Magic;
    uint32_t Lsn;  // log sequence number
    uint32_t TxnId; // transaction identifier
    WalType  Type;
    uint8_t  Pad[3]; // to make it 4bytes
    int32_t  PageId;
    int32_t  SlotIndex;
    uint32_t RowDataSize=0;
    char     TableName[32];
};
#pragma pack(pop) // restore the original settings


struct WalRecord {
    WalHeader Header;
    std::vector<uint8_t> RowData;
};

class WalManager {

    public:
    explicit WalManager(const std::string& walPath);

    ~WalManager();

    // Each returns the assigned log sequence number (LSN).
    uint32_t LogBegin   (uint32_t txnId);
    uint32_t LogInsert  (uint32_t txnId, const std::string& table, int pageId, int slotIndex);

    uint32_t LogUpdate  (uint32_t txnId, const std::string& table, int pageId, int slotIndex,
                         const std::span<const uint8_t>& beforeRowData);

    uint32_t LogDelete  (uint32_t txnId, const std::string& table, int pageId, int slotIndex,
                         const std::span<const uint8_t>& beforeRowData);

    uint32_t LogCommit  (uint32_t txnId);

    uint32_t LogRollback(uint32_t txnId);

    std::vector<WalRecord> ReadAll();

    // Flushes the C library buffer and forces the OS to write all
    void Flush();

private:
    FILE*       _file;      // C file handle
    uint32_t    _nextLsn = 1;
    std::string _path;

    uint32_t Write(WalType type, uint32_t txnId, const std::string& table,
                   int pageId, int slotIndex, const std::span<const uint8_t>& data);
};


#endif //AKATSUKIDB_WALMANAGER_HPP
