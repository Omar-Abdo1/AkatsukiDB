//
// Created by omarabdo on 6/7/26.
//

#include "../../include/AkatsukiDB/Storage/Page.hpp"

#include <cstring>
#include <iostream>

template<typename T>
T Page::Read(size_t offset) const {
     T value;
    std::memcpy(&value,_data.data()+offset,sizeof(T));
   //function copies the memory in a byte-by-byte , memcpy(target,source,NumberOfBytes)
 return value;
}

template<typename T>
void Page::Write(size_t offset, T value) {
    std::memcpy(_data.data() + offset, &value, sizeof(T));
    MarkDirty();
}

Page::Page(int pageId, std::vector<uint8_t> data)
    : _data(std::move(data)), _pageId(pageId) {}
// It only copies the 24-byte vector pointers on the stack so _data looks at the
// existing Heap memory, completely avoiding a slow 4096-byte deep copy!

// when we create page we also do move , new Page(10,move(data))

 bool Page::isDirty() const {
 return _isDirty;
}

 const std::vector<uint8_t> & Page::Data() const {
return _data;
 //returns a read-only reference to the 24-byte (the std::vector object itself)  3 pointers
}

 int Page::GetPageId() const {
  return _pageId;
}

 void Page::SetPageId(int pageId) {
  Write<int>(10,pageId);
 _pageId = pageId;
}

 short Page::GetSlotCount() const {
  return Read<short>(0);
}

 void Page::SetSlotCount(short slotCount) {
 Write<short>(0,slotCount);
}

 int Page::GetFreeOffset() const {
return Read<int>(2);
}

 void Page::SetFreeOffset(int freeOffset) {
  Write<int>(2,freeOffset);
}

 int Page::GetNextPageId() const {
   return Read<int>(6);
}

 void Page::SetNextPageId(int nextPageId) {

 Write<int>(6, nextPageId);
}

 std::span<uint8_t> Page::GetSlot(int slotIndex, int rowSize) {
return std::span<uint8_t>(_data.data() + 16 + slotIndex*rowSize,rowSize);
}

 void Page::MarkDirty() {
  _isDirty=true;
}

 void Page::ClearDirty() {
  _isDirty=false;
}

uint8_t* Page:: MutableData() { return _data.data(); } // return pointer for the first byte , and he can edit/write in the data

/*
Page Format :
PAGE = 4096 byte
First 16 bytes = page header:
  bytes 0–1   : SlotCount     (how many rows are in this page)   2^16
  bytes 2–5   : FreeOffset    (where the next row will be written) 2^32
  bytes 6–9   : NextPageId    (0 if this is the last page) 2^32
  bytes 10–13 : PageId        (which page number is this) 2^32
  bytes 14–15 : reserved
Bytes 16 to 4095 = row slots.
*/