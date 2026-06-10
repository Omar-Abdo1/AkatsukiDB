//
// Created by omarabdo on 6/10/26.
//

#include "../../include/AkatsukiDB/Index/BPlusTreeNode.hpp"

BPlusTreeNode::BPlusTreeNode(std::shared_ptr<Page> page) : _page(std::move(page)) {
    if (!_page) throw std::runtime_error("Null page in BPlusTreeNode");
}

BPlusTreeNode BPlusTreeNode::CreateLeaf(std::shared_ptr<Page> page, int pageId) {
    BPlusTreeNode node(std::move(page));
    node.SetNextLeafPageId(-1);
    node.SetPrevLeafPageId(-1);
    node.SetPageId(pageId);
    node.SetType(NodeType::Leaf);
    return node;
}

BPlusTreeNode BPlusTreeNode::CreateInternal(std::shared_ptr<Page> page, int pageId) {
    BPlusTreeNode node(std::move(page));
    node.SetPageId(pageId);
    node.SetType(NodeType::Internal);
    return node;
}

NodeType BPlusTreeNode::GetType() const {
    return static_cast<NodeType>(_page->MutableData()[0]);
}

void BPlusTreeNode::SetType(NodeType type) {
    _page->MutableData()[0] = static_cast<uint8_t>(type);
    MarkDirty();
}

short BPlusTreeNode::GetKeyCount() const {
    short val;
    std::memcpy(&val, _page->MutableData() + 1, 2);
    return val;
}

void BPlusTreeNode::SetKeyCount(short count) {
    std::memcpy(_page->MutableData() + 1, &count, 2);
    MarkDirty();
}

int BPlusTreeNode::GetParentPageId() const {
    int val;
    std::memcpy(&val, _page->MutableData() + 3, 4);
    return val;
}

void BPlusTreeNode::SetParentPageId(int pid) {
    std::memcpy(_page->MutableData() + 3, &pid, 4);
    MarkDirty();
}

int BPlusTreeNode::GetNextLeafPageId() const {
    int val;
    std::memcpy(&val, _page->MutableData() + 7, 4);
    return val;
}

void BPlusTreeNode::SetNextLeafPageId(int pid) {
    std::memcpy(_page->MutableData() + 7, &pid, 4);
    MarkDirty();
}

int BPlusTreeNode::GetPrevLeafPageId() const {
    int val;
    std::memcpy(&val, _page->MutableData() + 11, 4);
    return val;
}

void BPlusTreeNode::SetPrevLeafPageId(int pid) {
    std::memcpy(_page->MutableData() + 11, &pid, 4);
    MarkDirty();
}

int BPlusTreeNode::GetPageId() const {
    int val;
    std::memcpy(&val, _page->MutableData() + 15, 4);
    return val;
}

void BPlusTreeNode::SetPageId(int pid) {
    std::memcpy(_page->MutableData() + 15, &pid, 4);
    MarkDirty();
}

IndexKey BPlusTreeNode::GetKey(int index) const {
    int offset = DataStart + (GetType() == NodeType::Leaf
                              ? index * LeafEntrySize
                              : index * InternalEntrySize + ChildSize);
    return IndexKey(std::span<const uint8_t>(_page->MutableData() + offset, KeySize));
}

void BPlusTreeNode::SetKey(int index, const IndexKey& key) {
    int offset = DataStart + (GetType() == NodeType::Leaf
                              ? index * LeafEntrySize
                              : index * InternalEntrySize + ChildSize);
    std::memcpy(_page->MutableData() + offset, key.Data().data(), KeySize);
    MarkDirty();
}

int BPlusTreeNode::FindKeyIndex(const IndexKey& key) const {
    int l = 0, r = GetKeyCount() - 1, ans = GetKeyCount();
    while (l <= r) {
        int mid = (l + r) >> 1;
        auto midKey = GetKey(mid);
        if (midKey>=key) {
            ans = mid;
            r = mid - 1;
        } else {
            l = mid + 1;
        }
    }
    return ans;
}   

bool BPlusTreeNode::IsLeafFull() const {
    return GetKeyCount() >= LeafOrder();
}

std::pair<int, short> BPlusTreeNode::GetRowLocation(int index) const {
    int offset = DataStart + index * LeafEntrySize + KeySize;
    int pageId;
    short slot;
    std::memcpy(&pageId, _page->MutableData() + offset, 4);
    std::memcpy(&slot, _page->MutableData() + offset + 4, 2);
    return {pageId, slot};
}

void BPlusTreeNode::SetRowLocation(int index, int pageId, short slotIndex) {
    int offset = DataStart + index * LeafEntrySize + KeySize;
    std::memcpy(_page->MutableData() + offset, &pageId, 4);
    std::memcpy(_page->MutableData() + offset + 4, &slotIndex, 2);
    MarkDirty();
}

bool BPlusTreeNode::IsInternalFull() const {
    return GetKeyCount() >= InternalOrder();
}

int BPlusTreeNode::GetChildPageId(int index) const {
    int offset = DataStart + index * InternalEntrySize;
    int child;
    std::memcpy(&child, _page->MutableData() + offset, 4);
    return child;
}

void BPlusTreeNode::SetChildPageId(int index, int childPageId) {
    int offset = DataStart + index * InternalEntrySize;
    std::memcpy(_page->MutableData() + offset, &childPageId, 4);
    MarkDirty();
}

void BPlusTreeNode::MarkDirty() { _page->MarkDirty(); }
void BPlusTreeNode::ClearDirty() { _page->ClearDirty(); }


/*
 Bytes	Field	Header
   0	NodeType	  1	    0 = Internal, 1 = Leaf
   1–2	KeyCount	2	Number of keys currently in node
   3–6	ParentId	4	The PageId of this node's parent
   7–10	NextId	4	Next leaf in the linked list (-1 for internal)
   11–14	PrevId	4	Previous leaf in the linked list (-1 for internal)
   15–18	PageId	4	Self-Identification
   19	Reserved	1	Extra padding for alignment


 LEAF NODE — 4096 bytes:
   entries start at byte 20
    Entry 0: [128 bytes key][4 bytes tblPageId][2 bytes slotIndex] = 134 bytes
    Entry 1: [128 bytes key][4 bytes tblPageId][2 bytes slotIndex]
    Entry 2: [128 bytes key][4 bytes tblPageId][2 bytes slotIndex]
   --------------------------------
   INTERNAL NODE — 4096 bytes:
   data starts at byte 20
   child[0]:  [4 bytes idxPageId]
   key[0]:    [128 bytes]
    child[1]:  [4 bytes idxPageId]
    key[1]:    [128 bytes]
   child[2]:  [4 bytes idxPageId]
    ...
   Pattern: child, key, child, key, child
    Always one more child than keys
    c0 k0 c1 k1 c2 k2 c3 k3 c4

     x<k0          c0
     k0<=x<k1      c1
     k1<=x<k2      c2
     k2<=x<k3      c3
      x>=k3        c4
 */