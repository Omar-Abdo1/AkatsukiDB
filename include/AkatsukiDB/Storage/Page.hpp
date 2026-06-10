//
// Created by omarabdo on 6/7/26.
//

#ifndef AKATSUKIDB_CPP_PAGE_HPP
#define AKATSUKIDB_CPP_PAGE_HPP
#include <cstdint>
#include <span>
#include <vector>

constexpr size_t PAGE_SIZE = 4096;

class Page {

    std::vector<uint8_t> _data;
    bool _isDirty =false;
    int _pageId;
    template<typename T> T Read(size_t offset) const;
    template<typename T> void Write(size_t offset, T value);

    public:
    Page(int pageId, std::vector<uint8_t> data);

    bool isDirty() const ;

    const std::vector<uint8_t>& Data() const;

    int GetPageId() const;
    void SetPageId(int pageId);

    short GetSlotCount() const;
    void SetSlotCount(short slotCount);

    int GetFreeOffset() const;
    void SetFreeOffset(int freeOffset);

    int GetNextPageId() const;
    void SetNextPageId(int nextPageId);

    std::span<uint8_t> GetSlot(int slotIndex, int rowSize);

    void MarkDirty();
    void ClearDirty();

   uint8_t* MutableData();
};




#endif //AKATSUKIDB_CPP_PAGE_HPP
