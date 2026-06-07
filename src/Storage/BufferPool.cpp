//
// Created by omarabdo on 6/7/26.
//

#include "../../include/AkatsukiDB/Storage/BufferPool.hpp"

BufferPool::BufferPool(const std::string &dbFilePath):_pageManager(dbFilePath),_cache(MaxCapacity) {
    _cache.OnEvict = [this](std::shared_ptr<Page> page) {
        // Dereference the pointer (*) to pass the actual Page reference to WritePage
        this->_pageManager.WritePage(*page);
        // we expect by reference so we need the physical object
        // if we on stack we will pass it directly , but if we have pointer we want the object itself so *p
    };
}

int BufferPool::TotalPages() const {
    return _pageManager.TotalPages();
}

int BufferPool::AllocatePage() {
 return _pageManager.AllocatePage();
}

std::shared_ptr<Page> BufferPool::GetPage(int pageId) {

   auto page = _cache.Get(pageId);
    if (page==nullptr) {

        //read from disk then
        std::unique_ptr<Page> uniquePage = _pageManager.ReadPage(pageId);

        page = std::move(uniquePage);

        _cache.Put(pageId,page);
    }
    return page;
}

void BufferPool::FlushAll() {
  for (auto &page : _cache.Pages()) {
      if (page->isDirty()) {
          _pageManager.WritePage(*page);
      }
  }
}

BufferPool::~BufferPool() {
    FlushAll();
}
