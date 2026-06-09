//
// Created by omarabdo on 6/9/26.
//

#include "../../include/AkatsukiDB/Table/TableManager.hpp"

#include <cstring>

std::shared_ptr<Page> TableManager::GetPageWithSpace() {

    if (_bufferPool->TotalPages()==1) {
        int firstDataPageId = _bufferPool -> AllocatePage();

        auto FirstPage =  _bufferPool->GetPage(firstDataPageId);
        FirstPage->SetFreeOffset(16);
        FirstPage->SetPageId(  firstDataPageId);
    }

    int lastPageId = _bufferPool->TotalPages() - 1;
    auto page = _bufferPool->GetPage(lastPageId);

    int usedSpace = 16 + (page->GetSlotCount() * _rowSizeBytes);

    int remainingSpace = 4096 - usedSpace;

    if (remainingSpace >= _rowSizeBytes)
        return page;

    int newId = _bufferPool->AllocatePage();

    page->SetNextPageId(newId); // Linking
    page->MarkDirty();

    auto NewPage =  _bufferPool->GetPage(newId);

    NewPage->SetFreeOffset(16);
    NewPage->SetPageId(newId);

    return NewPage;
}

bool TableManager::IsDeleted(std::span<const uint8_t> rowBytes) const {
    return rowBytes[_rowSizeBytes-10] == 1;
}


TableManager::TableManager(std::unique_ptr<BufferPool> bufferPool, int rowSizeBytes)
    : _bufferPool(std::move(bufferPool)), _rowSizeBytes(rowSizeBytes){}

std::pair<int, int> TableManager::InsertRow(std::span<const uint8_t> rowBytes) {

    auto page = GetPageWithSpace();

    int slotIdx = page->GetSlotCount();


    auto slotSpan = page->GetSlot(slotIdx, _rowSizeBytes);

    std::memcpy(slotSpan.data(), rowBytes.data(), _rowSizeBytes);

    page->SetSlotCount(slotIdx + 1);
    page->SetFreeOffset(page->GetFreeOffset() + _rowSizeBytes);
    page->MarkDirty();

    _bufferPool->FlushAll();

    return {page->GetPageId(), slotIdx};
}

std::vector<RowEntry> TableManager::FullScan() {
    std::vector<RowEntry> result;

    for (int pageId = 1; pageId < _bufferPool->TotalPages(); ++pageId) {
        auto page = _bufferPool->GetPage(pageId);

        for (int slotIndex = 0; slotIndex < page->GetSlotCount(); ++slotIndex) {
            auto slotSpan = page->GetSlot(slotIndex, _rowSizeBytes);

            if (IsDeleted(slotSpan)) continue;

            // on heap
            std::vector<uint8_t> copiedData(slotSpan.begin(), slotSpan.end());
            // just move the pointer
            result.push_back({pageId, slotIndex, std::move(copiedData)});
        }
    }
    return result;
}

std::vector<uint8_t> TableManager::ReadRow(int pageId, int slotIndex) {
    auto page = _bufferPool->GetPage(pageId);
    auto slotSpan = page->GetSlot(slotIndex, _rowSizeBytes);

    // perform deep copy , copy all bytes span looks at at new heap memory then return it
    return std::vector<uint8_t>(slotSpan.begin(), slotSpan.end());
}

void TableManager::UpdateRow(int pageId, int slotIndex, std::span<const uint8_t> newRowBytes) {
    auto page = _bufferPool->GetPage(pageId);
    auto slotSpan = page->GetSlot(slotIndex, _rowSizeBytes);

    std::memcpy(slotSpan.data(), newRowBytes.data(), _rowSizeBytes);

    page->MarkDirty();
    _bufferPool->FlushAll();
}

void TableManager::DeleteRow(int pageId, int slotIndex) {
    auto page = _bufferPool->GetPage(pageId);
    auto slotSpan = page->GetSlot(slotIndex, _rowSizeBytes);

    slotSpan[_rowSizeBytes - 10] = 1;

    page->MarkDirty();
    _bufferPool->FlushAll();
}
