//
// Created by omarabdo on 6/10/26.
//
#pragma once

#ifndef AKATSUKIDB_CPP_BPLUSTREE_HPP
#define AKATSUKIDB_CPP_BPLUSTREE_HPP
#include <optional>
#include <string>
#include <vector>

#include "BPlusTreeNode.hpp"
#include "IndexKey.hpp"
#include "AkatsukiDB/Storage/BufferPool.hpp"


class BPlusTree{
public:
    explicit BPlusTree(const std::string& idxFilePath);
    ~BPlusTree() = default;

    // Queries
    std::vector<std::pair<int, short>> PointQuery(const IndexKey& key);
    std::vector<std::pair<int, short>> RangeQuery(const IndexKey& start, const IndexKey& end);

    void Insert(const InsertEntry& entry);
    bool Delete(const IndexKey& key);

    // Flush all dirty pages
    void Flush();

private:
    BufferPool _bufferPool;
    int _rootPageId;

    void SaveNode(BPlusTreeNode& node);
    BPlusTreeNode LoadNode(int pageId);
    void WriteRootPageId(int rootId);
    int ReadRootPageId();

    BPlusTreeNode FindLeaf(const IndexKey& key); // find the leaf contains this key
    BPlusTreeNode GetChildNodeForInternal(const BPlusTreeNode& internal, const IndexKey& key);
    //from internal node get the child contains this key

    // for Insertion
    std::optional<InsertEntry> InsertRec(BPlusTreeNode& cur, const InsertEntry& entry);
    void InsertIntoLeaf(BPlusTreeNode& leaf, const InsertEntry& entry);
    void InsertIntoInternal(BPlusTreeNode& internal, const IndexKey& key, int childPageId);
    InsertEntry SplitLeaf(BPlusTreeNode& leaf, const InsertEntry& newEntry);
    InsertEntry SplitInternal(BPlusTreeNode& internal, const IndexKey& key, int childPageId);

    // for Deletion
    bool DeleteRec(BPlusTreeNode& node, const IndexKey& key);
    bool DeleteFromLeaf(BPlusTreeNode& leaf, const IndexKey& key);
    void BorrowFromLeft(BPlusTreeNode& parent, int childIdx, BPlusTreeNode& child, BPlusTreeNode& left);
    void BorrowFromRight(BPlusTreeNode& parent, int childIdx, BPlusTreeNode& child, BPlusTreeNode& right);
    void MergeChildren(BPlusTreeNode& parent, int leftIdx);
};


#endif //AKATSUKIDB_CPP_BPLUSTREE_HPP
