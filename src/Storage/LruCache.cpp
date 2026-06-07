//
// Created by omarabdo on 6/7/26.
//

#include "../../include/AkatsukiDB/Storage/LruCache.hpp"

void LruCache::AddFront(Node* node) {
   node->prev = head;
    node->next=head->next;
    head->next->prev = node;
    head->next=node;
}

void LruCache::Remove(Node * node) {
    node->prev->next=node->next;
    node->next->prev=node->prev;
}


LruCache::LruCache(int capacity):_capacity(capacity) {

  head = new Node(-1,nullptr);
  tail = new Node(-1,nullptr);

    head->next = tail;
    tail->prev = head;

}

LruCache::~LruCache() {
    delete head;
    delete tail;
}

std::shared_ptr<Page> LruCache::Get(int pageId) {
  if (cache.find(pageId) == cache.end())
      return nullptr;

    Node* node = cache[pageId].get();

    Remove(node);
    AddFront(node);

    return node->page;
}

void LruCache::Put(int pageId, std::shared_ptr<Page> page) {

   if (cache.find(pageId) != cache.end()) {
       Node *node = cache[pageId].get();
       node->page = page;
       Remove(node);
       AddFront(node);
       return;
   }

    if (cache.size()>=_capacity) {
        Node* LruNode = tail->prev;
        if (LruNode->page!=nullptr && LruNode->page->isDirty() && OnEvict) {
            OnEvict(LruNode->page);
        }
        Remove(LruNode);
        cache.erase(LruNode->pageId);
    }
    auto newNode = std::make_unique<Node>(pageId, page);
    Node* rawPointer = newNode.get();
    cache[pageId] = std::move(newNode);
    AddFront(rawPointer);
}

std::vector<std::shared_ptr<Page>> LruCache::Pages() const {
    std::vector<std::shared_ptr<Page>> pages;
    Node* root = head->next;
    while (root!=tail) {
        if (root->page!=nullptr)
       pages.push_back(root->page);
        root = root->next;
    }
    return pages;
}
