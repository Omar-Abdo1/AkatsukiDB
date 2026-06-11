//
// Created by omarabdo on 6/9/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_TABLEMANAGER_HPP
#define AKATSUKIDB_CPP_TABLEMANAGER_HPP
#include "AkatsukiDB/Storage/BufferPool.hpp"

struct RowEntry {
    int PageId, SlotIndex;
    std::vector<uint8_t> Bytes;
};

class TableManager {
    std::unique_ptr<BufferPool> _bufferPool;
    int _rowSizeBytes;

    std::shared_ptr<Page> GetPageWithSpace();

    // span avoid heap copies . just look at the bytes
    bool IsDeleted(std::span<const uint8_t> rowBytes)const;

public:
    TableManager(std::unique_ptr<BufferPool> bufferPool,int rowSizeBytes);

    // The unique_ptr automatically cleans up the BufferPool so we do not need a destructor

    std::pair<int,int>InsertRow(std::span<const uint8_t> rowBytes);

    std::vector<RowEntry>FullScan();

    std::vector<uint8_t>ReadRow(int pageId,int slotIndex);

    void UpdateRow(int pageId, int slotIndex, std::span<const uint8_t >newRowBytes);

    void DeleteRow(int pageId, int slotIndex);
};


#endif //AKATSUKIDB_CPP_TABLEMANAGER_HPP
