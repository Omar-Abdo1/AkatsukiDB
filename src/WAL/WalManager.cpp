//
// Created by omarabdo on 6/19/26.
//

#include "../../include/AkatsukiDB/WAL/WalManager.hpp"

#include <cstring>
#include <cstdio>
#include <stdexcept>


// We need to flush OS buffers to stable storage , to make sure it wrote in disk not only in the ram of the OS

// Platform‑specific headers and macros for disk synchronisation.
#ifdef _WIN32
  #include <io.h>
  #define FSYNC_FD(fd) _commit(fd) // for windows
#else
  #include <unistd.h>
  #define FSYNC_FD(fd) fsync(fd) // for Unix
#endif


WalManager::WalManager(const std::string& walPath) : _path(walPath) {

    _file = std::fopen(walPath.c_str(), "r+b");
    // Try to open the file in binary mode for both reading / writing

    if (!_file) {      // if file do not exist
        _file = std::fopen(walPath.c_str(), "w+b"); // w -> create
    }

    if (!_file) {
        throw std::runtime_error("WalManager: cannot open or create WAL file: " + walPath);
    }

    auto existing = ReadAll();

    if (!existing.empty()) {
        _nextLsn = existing.back().Header.Lsn + 1;
    }
}

WalManager::~WalManager() {
    if (_file) {
        Flush();              // ensure everything is written to disk
        std::fclose(_file);   // release OS resources (process , memory , etc..)
    }
}

// data -> C++ internal buffer -> OS RAM -> DISK

void WalManager::Flush() {
    std::fflush(_file); // pushes data from the C library's internal buffer into the operating system's kernel page cache.
    // if we fail the data in the RAM not in the DISK

     // fileno() gives us the integer file descriptor from the FILE*.

    int fd = fileno(_file);
    FSYNC_FD(fd); // force the kernel to push the data from RAM into DISK

    //now data physically on the disk
}

uint32_t WalManager::Write(WalType type, uint32_t txnId, const std::string& table,
                           int pageId, int slotIndex, const std::span<const uint8_t>& data) {

    WalHeader hdr{};

    hdr.Magic      = WAL_MAGIC;
    hdr.Lsn        = _nextLsn++;
    hdr.TxnId      = txnId;
    hdr.Type       = type;
    hdr.PageId     = pageId;
    hdr.SlotIndex  = slotIndex;
    hdr.RowDataSize = static_cast<uint32_t>(data.size());

    std::memset(hdr.TableName, 0, sizeof(hdr.TableName));
    std::strncpy(hdr.TableName, table.c_str(), sizeof(hdr.TableName) - 1); // keep the last byte as null terminator


    std::fseek(_file, 0, SEEK_END);


    // write one element the size is WalHeader
    std::fwrite(&hdr, sizeof(WalHeader), 1, _file);

    if (!data.empty()) {
        // write N elements each is 1 byte
        std::fwrite(data.data(), 1, data.size(), _file);
    }

    return hdr.Lsn;
}


uint32_t WalManager::LogBegin(uint32_t txnId) {
    return Write(WalType::Begin, txnId, "", -1, -1, {});
}

uint32_t WalManager::LogInsert(uint32_t txnId, const std::string& tableName, int pageId, int slotIndex) {
    return Write(WalType::Insert, txnId, tableName, pageId, slotIndex, {});
}

uint32_t WalManager::LogUpdate(uint32_t txnId, const std::string& tableName, int pageId, int slotIndex,
                               const std::span<const uint8_t>& before) {
    // we need the before data so we can roll back
    return Write(WalType::Update, txnId, tableName, pageId, slotIndex, before);
}

uint32_t WalManager::LogDelete(uint32_t txnId, const std::string& tableName, int pageId, int slotIndex,
                               const std::span<const uint8_t>& before) {
    // we need the before data so we can roll back
    return Write(WalType::Delete, txnId, tableName, pageId, slotIndex, before);
}

uint32_t WalManager::LogCommit(uint32_t txnId) {
    // Commit marks the transaction as successful.
    uint32_t lsn = Write(WalType::Commit, txnId, "", -1, -1, {});
    //we must force the commit record to disk.
    Flush();
    return lsn;
}

uint32_t WalManager::LogRollback(uint32_t txnId) {
    uint32_t lsn = Write(WalType::Rollback, txnId, "", -1, -1, {});
    Flush();
    return lsn;
}


std::vector<WalRecord> WalManager::ReadAll() {
    std::vector<WalRecord> records;


    std::fseek(_file, 0, SEEK_SET);

    std::clearerr(_file);

    while (true) {
        WalHeader hdr{};

        // Attempt to read exactly one header.
        size_t itemsRead = std::fread(&hdr, sizeof(WalHeader), 1, _file);
        if (itemsRead <= 0) {
            break;
        }

        if (hdr.Magic != WAL_MAGIC) {
            break;
        }

        // Start building the in‑memory record.
        WalRecord rec;
        rec.Header = hdr;

        if (hdr.RowDataSize != 0) {
            rec.RowData.resize(hdr.RowDataSize);

            // fread reads raw bytes; we ask for exactly RowDataSize bytes.
            itemsRead = std::fread(rec.RowData.data(), 1, hdr.RowDataSize, _file); // then read one row of RowDataSize bytes
            if (itemsRead != hdr.RowDataSize) {
                break;
            }
        }
        records.push_back(std::move(rec));
    }
    std::clearerr(_file);
    return records;
}