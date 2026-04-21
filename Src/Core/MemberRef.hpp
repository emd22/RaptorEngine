#pragma once

#include <utility>

template <typename TPtrType>
struct MemberRef
{
    MemberRef() : pPtr(nullptr) {};

    MemberRef(nullptr_t np) : pPtr(nullptr) {}

    MemberRef(const MemberRef& other) = delete;
    MemberRef(MemberRef&& other) = delete;

    template <typename... TArgTypes>
    inline void InitRef(TArgTypes... args)
    {
        pPtr = ::new TPtrType(std::forward<TArgTypes>(args)...);
    }

    inline void Destroy()
    {
        if (pPtr == nullptr) {
            return;
        }

        ::delete pPtr;

        pPtr = nullptr;
    }

    void operator=(MemberRef& other) = delete;

    TPtrType* operator->() { return *pPtr; }
    TPtrType& operator*() { return *pPtr; }

    ~MemberRef() { Destroy(); }

public:
    TPtrType* pPtr;
};
