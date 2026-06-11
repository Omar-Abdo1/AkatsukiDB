//
// Created by omarabdo on 6/7/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_LRUCACHE_HPP
#define AKATSUKIDB_CPP_LRUCACHE_HPP
#include <functional>
#include <memory>
#include <unordered_map>

#include "Page.hpp"


class LruCache {

    struct Node {
      int pageId;
        Node* next;
        Node* prev;
        std::shared_ptr<Page> page;
        Node();
        Node(int pageId,std::shared_ptr<Page>p):
        page(std::move(p)), next(nullptr), prev(nullptr),pageId(pageId) {}
    };

    int _capacity;

    std::unordered_map<int,std::unique_ptr<Node> > cache;

    Node* head;
    Node* tail;

    void AddFront(Node * node);
    void Remove(Node* node);

public:
    std::function<void(std::shared_ptr<Page>)> OnEvict;

    explicit LruCache(int capacity);
    ~LruCache();

    std::shared_ptr<Page> Get(int pageId);

    void Put(int pageId, std::shared_ptr<Page> page);

    std::vector<std::shared_ptr<Page>> Pages() const;

};


#endif //AKATSUKIDB_CPP_LRUCACHE_HPP
