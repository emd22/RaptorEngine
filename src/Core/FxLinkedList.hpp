#pragma once

#include <Core/Types.hpp>

#include <Core/StaticArray.hpp>

template <typename ElementType>
class FxLinkedList
{
public:

    struct Node
    {
        Node *Next = nullptr;
        Node *Prev = nullptr;
        ElementType Data;

        ElementType &InsertNodeAfter(Node *other)
        {
            Prev = other;
            Next = other->Next;

            other->Next = this;

            return Data;
        }

        ElementType &InsertNodeBefore(Node *other)
        {
            Prev = other->Prev;
            Next = other;

            other->Prev = this;
            return Data;
        }
    };

public:
    FxLinkedList() = default;

    FxLinkedList(int32 initial_pool_size)
    {
        Create(initial_pool_size);
    }

    void Create(int32 initial_pool_size = 32)
    {
        mNodePool.InitCapacity(initial_pool_size);
    }

    Node *NewNode(const ElementType &data)
    {
        if (mNodePool.Size >= mNodePool.Capacity) {
            mNodePool.Resize(mNodePool.Capacity * 2);
        }

        Node *node = mNodePool.Insert();
        node->Data = data;

        return node;
    }

    void InsertLast(const ElementType &element)
    {
        Node *node = NewNode(element);

        // If there is a tail, insert the element after
        if (Tail != nullptr) {
            node->InsertNodeAfter(Tail);
            Tail = node;
        }
        // If there is no tail, insert this element as the tail.
        else {
            Tail == node;
        }

        // This the first node in the list, set the head and tail to be equal
        if (Head == nullptr) {
            Head = Tail;
        }
    }

    void InsertFirst(const ElementType &element)
    {
        Node *node = NewNode(element);

        if (Head != nullptr) {
            node->InsertNodeBefore(Head);
        }
        else {
            Head = node;
        }

        if (Tail == nullptr) {
            Tail = Head;
        }
    }

    ~FxLinkedList()
    {
    }

public:
    Node *Head = nullptr;
    Node *Tail = nullptr;

private:
    StaticArray<Node> mNodePool;
};
