// #pragma once

// #include "Types.hpp"

// #include "FxStaticArray.hpp"

// template <typename ElementType>
// class FxLinkedList
// {
// public:

//     struct Node
//     {
//     public:
//         Node* Next = nullptr;
//         Node* Prev = nullptr;
//         ElementType Data;

//         uint32 PoolIndex = 0;

//     private:
//         friend class FxLinkedList;
//         ElementType& InsertAfterNode(Node* other)
//         {
//             Prev = other;
//             Next = other->Next;

//             other->Next = this;

//             return Data;
//         }

//         ElementType& InsertBeforeNode(Node* other)
//         {
//             Prev = other->Prev;
//             Next = other;

//             other->Prev = this;
//             return Data;
//         }
//     };

// public:
//     FxLinkedList() = default;

//     FxLinkedList(uint32 initial_pool_size)
//     {
//         Create(initial_pool_size);
//     }

//     void Create(uint32 initial_pool_size = 32)
//     {
//         mNodePool.InitCapacity(initial_pool_size);
//         mFreedNodes.InitCapacity(initial_pool_size / 2);
//     }

//     Node* NewNode(const ElementType& data)
//     {
//         Node* node = nullptr;

//         // Check if there are available spaces that have been freed
//         if (mFreedNodes.Size > 0) {
//             uint32 pool_index = mFreedNodes[mFreedNodes.Size - 1];
//             Log::Debug("Reusing pool index %u", pool_index);

//             // 'remove' the node from the freed nodes list, treating it like queue
//             mFreedNodes.Size--;

//             // Get the ptr for the available node
//             node = &mNodePool[pool_index];
//             node->PoolIndex = pool_index;

//             Log::Debug("Reusing node!");
//         }

//         if (mNodePool.Size >= mNodePool.Capacity) {
//             mNodePool.Resize(mNodePool.Capacity * 2);
//         }

//         // No nodes can be recycled, insert a new one
//         if (node == nullptr) {
//             node = mNodePool.Insert();
//             node->PoolIndex = static_cast<uint32>(mNodePool.Size) - 1;
//         }

//         Log::Debug("New node at %u | %p)", node->PoolIndex, node);

//         node->Data = data;
//         node->Prev = nullptr;
//         node->Next = nullptr;

//         return node;
//     }

//     void DeleteNode(Node* node)
//     {
//         if (mFreedNodes.Size >= mFreedNodes.Capacity) {
//             mFreedNodes.Resize(mFreedNodes.Capacity * 2);
//             Log::Debug("Resizing linked list FreedNodes pool to %llu", mFreedNodes.Capacity);
//         }

//         Log::Debug("Freeing node at %u | (%p - %p - %p)", node->PoolIndex, node->Prev, node, node->Next);

//         mFreedNodes.Insert(node->PoolIndex);

//         // Since the node will be removed, make the previous and next nodes point to each other
//         if (node->Prev != nullptr) {
//             node->Prev->Next = node->Next;
//         }
//         if (node->Next != nullptr) {
//             node->Next->Prev = node->Prev;
//         }

//         node->Next = nullptr;
//         node->Prev = nullptr;
//     }

//     Node* InsertLast(const ElementType& element)
//     {
//         Node* node = NewNode(element);

//         // If there is a tail, insert the element after
//         if (Tail != nullptr) {
//             node->InsertAfterNode(Tail);
//             Tail = node;
//         }
//         // If there is no tail, insert this element as the tail.
//         else {
//             Tail = node;
//         }

//         // This the first node in the list, set the head and tail to be equal
//         if (Head == nullptr) {
//             Head = Tail;
//         }

//         return node;
//     }

//     Node* InsertFirst(const ElementType& element)
//     {
//         Node* node = NewNode(element);

//         if (Head != nullptr) {
//             node->InsertBeforeNode(Head);
//             Head = node;
//         }
//         else {
//             Head = node;
//         }

//         if (Tail == nullptr) {
//             Tail = Head;
//         }
//         return node;
//     }

//     Node* InsertAfterNode(const ElementType& element, Node* other)
//     {
//         // If there is no tail or the node to insert after is the tail, insert last
//         if (Tail == nullptr || other == Tail) {
//             return InsertLast(element);
//         }

//         Node* node = NewNode(element);
//         node->InsertAfterNode(other);

//         return node;
//     }

//     Node* InsertBeforeNode(const ElementType& element, Node* other)
//     {
//         // If there is no head or the node to insert before is the head, insert first
//         if (Head == nullptr || other == Head) {
//             return InsertFirst(element);
//         }

//         Node* node = NewNode(element);
//         node->InsertBeforeNode(other);

//         return node;
//     }


//     ~FxLinkedList()
//     {
//     }

// public:
//     Node* Head = nullptr;
//     Node* Tail = nullptr;

// private:
//     FxStaticArray<Node> mNodePool;
//     FxStaticArray<uint32> mFreedNodes;
// };
