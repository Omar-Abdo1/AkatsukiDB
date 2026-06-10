//
// Created by omarabdo on 6/10/26.
//

#ifndef AKATSUKIDB_CPP_BPLUSTREENODE_HPP
#define AKATSUKIDB_CPP_BPLUSTREENODE_HPP
#include <memory>

#include "IndexKey.hpp"
#include "AkatsukiDB/Storage/Page.hpp"


enum class NodeType : uint8_t {Internal = 0,Leaf = 1}; // 1 byte

struct InsertEntry {IndexKey key;int pageId;short slotIndex;};

// wrapper for the 4096 byte page , every node is page !!
class BPlusTreeNode {
public:
    static constexpr int PageSize = 4096;
    static constexpr int KeySize = IndexKey::Size;
    static constexpr int PageIdSize = 4;
    static constexpr int SlotIndexSize = 2;
    static constexpr int ChildSize = 4;
    static constexpr int DataStart = 20;                  // header size

    static constexpr int InternalEntrySize = KeySize + ChildSize;
    static constexpr int LeafEntrySize = KeySize + PageIdSize + SlotIndexSize;

    static int InternalOrder() { return (PageSize - DataStart) / InternalEntrySize; } //  M which has M children + M-1 keys
    static int LeafOrder()     { return (PageSize - DataStart) / LeafEntrySize; }

    explicit BPlusTreeNode(std::shared_ptr<Page> page);

        static BPlusTreeNode CreateLeaf(std::shared_ptr<Page> page, int pageId);
    static BPlusTreeNode CreateInternal(std::shared_ptr<Page> page, int pageId);

    NodeType GetType() const;
    void SetType(NodeType type);

    short GetKeyCount() const;
    void SetKeyCount(short count);

    int GetParentPageId() const;
    void SetParentPageId(int pid);

    int GetNextLeafPageId() const;
    void SetNextLeafPageId(int pid);

    int GetPrevLeafPageId() const;
    void SetPrevLeafPageId(int pid);

    int GetPageId() const;
    void SetPageId(int pid);

    // Key access
    IndexKey GetKey(int index) const;
    void SetKey(int index, const IndexKey& key);

    // Lower bound search
    int FindKeyIndex(const IndexKey& key) const;

    bool IsLeafFull() const;
    std::pair<int, short> GetRowLocation(int index) const;
    void SetRowLocation(int index, int pageId, short slotIndex);

    bool IsInternalFull() const;
    int GetChildPageId(int index) const;
    void SetChildPageId(int index, int childPageId);

    void MarkDirty();
    void ClearDirty();

    std::shared_ptr<Page> GetPage() const { return _page; }

private:
    std::shared_ptr<Page> _page; // instead of saving the bytes again in this class we just have a pointer where to it its location in the heap
};


#endif //AKATSUKIDB_CPP_BPLUSTREENODE_HPP
