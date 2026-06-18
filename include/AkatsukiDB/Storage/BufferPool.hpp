//
// Created by omarabdo on 6/7/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_BUFFERPOOL_HPP
#define AKATSUKIDB_CPP_BUFFERPOOL_HPP
#include <memory>

#include "LruCache.hpp"
#include "Page.hpp"
#include "PageManager.hpp"
#include "PageManager.hpp"

constexpr size_t MaxCapacity = 1024;

class BufferPool {
    PageManager _pageManager;
    LruCache _cache;

public:

    explicit BufferPool(const std::string& dbFilePath);

    ~BufferPool();

    int TotalPages() const;

    int AllocatePage();

    std::shared_ptr<Page> GetPage(int pageId);

    void FlushAll();
};


#endif //AKATSUKIDB_CPP_BUFFERPOOL_HPP
