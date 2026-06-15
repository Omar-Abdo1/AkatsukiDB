//
// Created by omarabdo on 6/10/26.
//

#include "../../include/AkatsukiDB/Index/BPlusTree.hpp"

// has it own bufferpool ,  and the node has pointer to the page in the bufferpool
BPlusTree::BPlusTree(const std::string& idxFilePath)
    : _bufferPool(idxFilePath)
{
    if (_bufferPool.TotalPages() == 1) {
        // First page is the header (page 0). Allocate a root leaf.
        int rootId = _bufferPool.AllocatePage();
        auto page = _bufferPool.GetPage(rootId);
        auto root = BPlusTreeNode::CreateLeaf(page, rootId);
        _rootPageId = rootId;
        WriteRootPageId(_rootPageId);
    } else {
        _rootPageId = ReadRootPageId();
    }
}

void BPlusTree::Flush() {
    _bufferPool.FlushAll();
}


BPlusTreeNode BPlusTree::LoadNode(int pageId) {
    auto page = _bufferPool.GetPage(pageId);
    return BPlusTreeNode(std::move(page));  // move shared_ptr to avoid refcount increment
}

void BPlusTree::WriteRootPageId(int rootId) {
    auto headerPage = _bufferPool.GetPage(0);
    std::memcpy(headerPage->MutableData() + 10, &rootId, 4);
    headerPage->MarkDirty();
}

int BPlusTree::ReadRootPageId() {
    auto headerPage = _bufferPool.GetPage(0);
    int rootId;
    std::memcpy(&rootId, headerPage->MutableData() + 10, 4);
    return rootId;
}

BPlusTreeNode BPlusTree::FindLeaf(const IndexKey& key) {
    auto node = LoadNode(_rootPageId);
    while (node.GetType() == NodeType::Internal) {
        node = GetChildNodeForInternal(node, key);
    }
    return node;
}

BPlusTreeNode BPlusTree::GetChildNodeForInternal(const BPlusTreeNode& internal, const IndexKey& key) {
    int idx = internal.FindKeyIndex(key);
    int childId;
    if (idx == internal.GetKeyCount()) {
        childId = internal.GetChildPageId(idx);
    } else {
        auto foundKey = internal.GetKey(idx);
        if (foundKey==key)
            childId = internal.GetChildPageId(idx + 1);
        else
            childId = internal.GetChildPageId(idx);
    }
    return LoadNode(childId);
}


std::vector<std::pair<int, short>> BPlusTree::PointQuery(const IndexKey& key) {
    std::vector<std::pair<int, short>> result;
    auto leaf = FindLeaf(key);
    int idx = leaf.FindKeyIndex(key);
    while (true) {
        if (idx >= leaf.GetKeyCount()) {
            if (leaf.GetNextLeafPageId() == -1) break;
            leaf = LoadNode(leaf.GetNextLeafPageId());
            idx = 0;
        }
        if (leaf.GetKey(idx)!=key) break;
        result.push_back(leaf.GetRowLocation(idx));
        ++idx;
    }
    return result;
}

std::vector<std::pair<int, short>> BPlusTree::RangeQuery(const IndexKey& start, const IndexKey& end,bool startOpen,bool endOpen) {
    std::vector<std::pair<int, short>> result;
    auto leaf = FindLeaf(start);
    int startIdx = leaf.FindKeyIndex(start);
    while (true) {
        for (int i = startIdx; i < leaf.GetKeyCount(); ++i) {
            auto key = leaf.GetKey(i);
            if (key>end || (endOpen && key==end) ) return result;
            if (startOpen && key==start)continue;
            result.push_back(leaf.GetRowLocation(i));
        }
        startIdx = 0;
        if (leaf.GetNextLeafPageId() == -1) break;
        leaf = LoadNode(leaf.GetNextLeafPageId());
    }
    return result;
}

void BPlusTree::Insert(const InsertEntry& entry) {
    auto root = LoadNode(_rootPageId);
    auto result = InsertRec(root, entry);
    if (result) {
        // root must split as it have no space

        int newRootId = _bufferPool.AllocatePage();
        auto newRootPage = _bufferPool.GetPage(newRootId);
        auto newRoot = BPlusTreeNode::CreateInternal(newRootPage, newRootId);
        newRoot.SetChildPageId(0, _rootPageId); // leftchild is the old root
        newRoot.SetKey(0, result->key);// promoted key
        newRoot.SetChildPageId(1, result->pageId); // right child is the new node
        newRoot.SetKeyCount(1);
        _rootPageId = newRootId;
        WriteRootPageId(_rootPageId);
        /*
              c0 k0 c1  this is the new root
            */
    }
}

std::optional<InsertEntry> BPlusTree::InsertRec(BPlusTreeNode& cur, const InsertEntry& entry) {

    if (cur.GetType() == NodeType::Leaf) {
        if (!cur.IsLeafFull()) {
            InsertIntoLeaf(cur, entry);
            return std::nullopt;
        }
        return SplitLeaf(cur, entry);
    }

        auto child = GetChildNodeForInternal(cur, entry.key);
        auto result = InsertRec(child, entry);
        if (result) { // then something gonna up must we insert it
            if (!cur.IsInternalFull()) {
                InsertIntoInternal(cur, result->key, result->pageId);
                return std::nullopt;
            }
            return SplitInternal(cur, result->key, result->pageId);
        }
        return std::nullopt;
}

void BPlusTree::InsertIntoLeaf(BPlusTreeNode& leaf, const InsertEntry& entry) {
    int idx = leaf.FindKeyIndex(entry.key);
    // 30 40 50 60 70  i want to insert 45  lowerbound,45   -> 50  so we need to shift all after 50 one position
    // 30 40 x 50 60 70  then x is 45
    // Shift keys and row locations right
    for (int pos = leaf.GetKeyCount() - 1; pos >= idx; --pos) {
        leaf.SetKey(pos + 1, leaf.GetKey(pos));
        auto [pid, slot] = leaf.GetRowLocation(pos);
        leaf.SetRowLocation(pos + 1, pid, slot);
    }
    leaf.SetKey(idx, entry.key);
    leaf.SetRowLocation(idx, entry.pageId, entry.slotIndex);
    leaf.SetKeyCount(leaf.GetKeyCount() + 1);
    leaf.MarkDirty();
}

InsertEntry BPlusTree::SplitLeaf(BPlusTreeNode& leaf, const InsertEntry& newEntry) {
    std::vector<InsertEntry> total;
    for (int i = 0; i < leaf.GetKeyCount(); ++i) {
        total.push_back({leaf.GetKey(i), leaf.GetRowLocation(i).first, leaf.GetRowLocation(i).second});
    }
    total.push_back(newEntry);
    std::sort(total.begin(), total.end(),
              [](const InsertEntry& a, const InsertEntry& b) { return a.key < b.key; });

    int mid = total.size() / 2;
    // Update left (current) leaf
    for (int i = 0; i < mid; ++i) {
        leaf.SetKey(i, total[i].key);
        leaf.SetRowLocation(i, total[i].pageId, total[i].slotIndex);
    }
    leaf.SetKeyCount(mid);

    // Create new right leaf
    int newPageId = _bufferPool.AllocatePage();
    auto newPage = _bufferPool.GetPage(newPageId);
    auto newLeaf = BPlusTreeNode::CreateLeaf(newPage, newPageId);
    for (int i = mid; i < total.size(); ++i) {
        int idx = i - mid;
        newLeaf.SetKey(idx, total[i].key);
        newLeaf.SetRowLocation(idx, total[i].pageId, total[i].slotIndex);
        newLeaf.SetKeyCount(idx + 1);
    }

    // Update leaf links
    newLeaf.SetNextLeafPageId(leaf.GetNextLeafPageId());
    newLeaf.SetPrevLeafPageId(leaf.GetPageId());
    leaf.SetNextLeafPageId(newLeaf.GetPageId());
    if (newLeaf.GetNextLeafPageId() != -1) {
        auto next = LoadNode(newLeaf.GetNextLeafPageId());
        next.SetPrevLeafPageId(newLeaf.GetPageId());
    }
    return {total[mid].key, newLeaf.GetPageId(), total[mid].slotIndex};
    // middle element will be promoted + pageid because it will be the child
}

void BPlusTree::InsertIntoInternal(BPlusTreeNode& internal, const IndexKey& key, int childPageId) {
    int idx = internal.FindKeyIndex(key);
    for (int pos = internal.GetKeyCount() - 1; pos >= idx; --pos)
        internal.SetKey(pos + 1, internal.GetKey(pos));
    for (int pos = internal.GetKeyCount(); pos >= idx + 1; --pos)
        internal.SetChildPageId(pos + 1, internal.GetChildPageId(pos));
    internal.SetKey(idx, key);
    internal.SetChildPageId(idx + 1, childPageId);
    internal.SetKeyCount(internal.GetKeyCount() + 1);
    internal.MarkDirty();
}

InsertEntry BPlusTree::SplitInternal(BPlusTreeNode& internal, const IndexKey& key, int childPageId) {
    std::vector<IndexKey> allKeys;
    std::vector<int> allChildren;
    for (int i = 0; i <= internal.GetKeyCount(); ++i)
        allChildren.push_back(internal.GetChildPageId(i));

    for (int i = 0; i < internal.GetKeyCount(); ++i)
        allKeys.push_back(internal.GetKey(i));

    int insertPos = internal.FindKeyIndex(key);
    allKeys.insert(allKeys.begin() + insertPos, key);
    allChildren.insert(allChildren.begin() + insertPos + 1, childPageId);
    // should do this so the order of keys and pages do not lost


    int mid = allKeys.size() / 2;
    IndexKey promotedKey = allKeys[mid];

    // Update current internal node (left half)
    for (int i = 0; i < mid; ++i)
        internal.SetKey(i, allKeys[i]);
    for (int i = 0; i <= mid; ++i)
        internal.SetChildPageId(i, allChildren[i]);
    internal.SetKeyCount(mid);

    // Create new internal node (right half)
    int newPageId = _bufferPool.AllocatePage();
    auto newPage = _bufferPool.GetPage(newPageId);
    auto newInternal = BPlusTreeNode::CreateInternal(newPage, newPageId);
    int rightKeyCount = allKeys.size() - mid - 1;
    for (int i = 0; i < rightKeyCount; ++i)
        newInternal.SetKey(i, allKeys[mid + 1 + i]);
    for (int i = 0; i <= rightKeyCount; ++i)
        newInternal.SetChildPageId(i, allChildren[mid + 1 + i]);
    newInternal.SetKeyCount(rightKeyCount);

    return {promotedKey, newInternal.GetPageId(), 0};
}

bool BPlusTree::Delete(const IndexKey& key) {
    auto root = LoadNode(_rootPageId);
    bool deleted = DeleteRec(root, key);
    if (deleted && root.GetType() == NodeType::Internal && root.GetKeyCount() == 0) {
        // Empty internal node – promote its only child
        _rootPageId = root.GetChildPageId(0);
        WriteRootPageId(_rootPageId);
    }
    return deleted;
}

bool BPlusTree::DeleteRec(BPlusTreeNode& node, const IndexKey& key) {
    if (node.GetType() == NodeType::Leaf)
        return DeleteFromLeaf(node, key);

    auto child = GetChildNodeForInternal(node, key);
    bool deleted = DeleteRec(child, key);
    if (!deleted) return false;

    int minKeys = (child.GetType() == NodeType::Leaf)
                  ? BPlusTreeNode::LeafOrder() / 2
                  : BPlusTreeNode::InternalOrder() / 2;

    if (child.GetKeyCount() >= minKeys)
        return true;

    int idx = node.FindKeyIndex(key);
    // Try borrow from left sibling
    if (idx > 0) {
        auto leftNeighbour = LoadNode(node.GetChildPageId(idx - 1));
        if (leftNeighbour.GetKeyCount() > minKeys) {
            BorrowFromLeft(node, idx, child, leftNeighbour);
            return true;
        }
    }
    // Try borrow from right sibling
    if (idx < node.GetKeyCount()) {
        auto rightNeighbour = LoadNode(node.GetChildPageId(idx + 1));
        if (rightNeighbour.GetKeyCount() > minKeys) {
            BorrowFromRight(node, idx, child, rightNeighbour);
            return true;
        }
    }
    // Merge
    if (idx > 0)
        MergeChildren(node, idx - 1);
    else
        MergeChildren(node, idx);
    return true;
}

bool BPlusTree::DeleteFromLeaf(BPlusTreeNode& leaf, const IndexKey& key) {
    int idx = leaf.FindKeyIndex(key);
    if (idx >= leaf.GetKeyCount() || leaf.GetKey(idx).CompareTo(key) != 0)
        return false;

    for (int pos = idx; pos < leaf.GetKeyCount() - 1; ++pos) {
        leaf.SetKey(pos, leaf.GetKey(pos + 1));
        auto [pid, slot] = leaf.GetRowLocation(pos + 1);
        leaf.SetRowLocation(pos, pid, slot);
    }
    leaf.SetKeyCount(leaf.GetKeyCount() - 1);
    return true;
}

void BPlusTree::BorrowFromLeft(BPlusTreeNode& parent, int childIdx, BPlusTreeNode& child, BPlusTreeNode& left) {
    if (child.GetType() == NodeType::Leaf) {
        int lastIdx = left.GetKeyCount() - 1;
        auto borrowKey = left.GetKey(lastIdx);
        auto [pid, slot] = left.GetRowLocation(lastIdx);
        // Shift child keys right
        for (int pos = child.GetKeyCount() - 1; pos >= 0; --pos) {
            child.SetKey(pos + 1, child.GetKey(pos));
            auto [p, s] = child.GetRowLocation(pos);
            child.SetRowLocation(pos + 1, p, s);
        }
        child.SetKey(0, borrowKey);
        child.SetRowLocation(0, pid, slot);
        child.SetKeyCount(child.GetKeyCount() + 1);
        left.SetKeyCount(left.GetKeyCount() - 1);
        parent.SetKey(childIdx - 1, borrowKey); // the key separates left,child is key with idx-1
    } else {
            //1-child gets parent key separator the last child from left node and put it in first
        auto borrowKey = parent.GetKey(childIdx - 1);
        int lastChild = left.GetChildPageId(left.GetKeyCount());
        // Shift child right
        for (int pos = child.GetKeyCount(); pos > 0; --pos)
            child.SetKey(pos, child.GetKey(pos - 1));
        for (int pos = child.GetKeyCount() + 1; pos > 0; --pos)
            child.SetChildPageId(pos, child.GetChildPageId(pos - 1));
        child.SetKey(0, borrowKey);
        child.SetChildPageId(0, lastChild);
        child.SetKeyCount(child.GetKeyCount() + 1);
        // 2- parent takes the last key from left

        parent.SetKey(childIdx - 1, left.GetKey(left.GetKeyCount() - 1));
        left.SetKeyCount(left.GetKeyCount() - 1);
    }
}

void BPlusTree::BorrowFromRight(BPlusTreeNode& parent, int childIdx, BPlusTreeNode& child, BPlusTreeNode& right) {
    if (child.GetType() == NodeType::Leaf) {
        auto borrowKey = right.GetKey(0);
        auto [pid, slot] = right.GetRowLocation(0);
        // Shift right left
        for (int pos = 0; pos < right.GetKeyCount() - 1; ++pos) {
            right.SetKey(pos, right.GetKey(pos + 1));
            auto [p, s] = right.GetRowLocation(pos + 1);
            right.SetRowLocation(pos, p, s);
        }
        child.SetKey(child.GetKeyCount(), borrowKey);
        child.SetRowLocation(child.GetKeyCount(), pid, slot);
        child.SetKeyCount(child.GetKeyCount() + 1);
        right.SetKeyCount(right.GetKeyCount() - 1);
        parent.SetKey(childIdx, right.GetKey(0));// separator will be the first key in right node
    } else {
        // 1- child gets the separator key from parent  and get the first children from right node  and put it in last

        auto borrowKey = parent.GetKey(childIdx);  // key between child and right nodes
        int firstChild = right.GetChildPageId(0);
        child.SetKey(child.GetKeyCount(), borrowKey);
        child.SetChildPageId(child.GetKeyCount() + 1, firstChild);
        child.SetKeyCount(child.GetKeyCount() + 1);
            // 2- now the right node delete it first key and first children
        for (int pos = 0; pos < right.GetKeyCount() - 1; ++pos)
            right.SetKey(pos, right.GetKey(pos + 1));
        for (int pos = 0; pos <= right.GetKeyCount() - 1; ++pos)
            right.SetChildPageId(pos, right.GetChildPageId(pos + 1));
        right.SetKeyCount(right.GetKeyCount() - 1);
        //3- parent separator key will be first from the right node

        parent.SetKey(childIdx, right.GetKey(0));
    }
}

void BPlusTree::MergeChildren(BPlusTreeNode& parent, int leftIdx) {
    auto left = LoadNode(parent.GetChildPageId(leftIdx));
    auto right = LoadNode(parent.GetChildPageId(leftIdx + 1));
    // we will merge right into left , remove right

    if (left.GetType() == NodeType::Leaf) {
        // Merge right leaf into left leaf
        for (int i = 0; i < right.GetKeyCount(); ++i) {
            left.SetKey(left.GetKeyCount() + i, right.GetKey(i));
            auto [pid, slot] = right.GetRowLocation(i);
            left.SetRowLocation(left.GetKeyCount() + i, pid, slot);
        }
        left.SetKeyCount(left.GetKeyCount() + right.GetKeyCount());
        left.SetNextLeafPageId(right.GetNextLeafPageId());
        if (right.GetNextLeafPageId() != -1) {
            auto next = LoadNode(right.GetNextLeafPageId());
            next.SetPrevLeafPageId(left.GetPageId());
        }
    } else {
        // Internal node merge: insert parent separator key

        // last children in left is bounded by last key in left and the separator key parent
        // last children in left let it be Cl [Kl,Kp] Kl last key in left , Kp separator key parent
        // so before inserting right child we need to put the separtor key parent to maintain the proper of B+ Tree

        auto separatorKey = parent.GetKey(leftIdx);
        left.SetKey(left.GetKeyCount(), separatorKey);
        left.SetChildPageId(left.GetKeyCount() + 1, right.GetChildPageId(0));
        left.SetKeyCount(left.GetKeyCount() + 1);
        for (int i = 0; i < right.GetKeyCount(); ++i) {
            left.SetKey(left.GetKeyCount(), right.GetKey(i));
            left.SetChildPageId(left.GetKeyCount() + 1, right.GetChildPageId(i + 1));
            left.SetKeyCount(left.GetKeyCount() + 1);
        }
    }

    // Remove separator key and right child from parent
    for (int i = leftIdx; i < parent.GetKeyCount() - 1; ++i)
        parent.SetKey(i, parent.GetKey(i + 1));
    for (int i = leftIdx + 1; i < parent.GetKeyCount(); ++i)
        parent.SetChildPageId(i, parent.GetChildPageId(i + 1));
    parent.SetKeyCount(parent.GetKeyCount() - 1);

}