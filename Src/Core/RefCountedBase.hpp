#pragma once

#include "Ref.hpp"

namespace fx {

/**
 * @brief Base class used for types to have ownership sharing and reference counting.
 */
class RefCountedBase
{
public:
    RefCountedBase();

    void InheritRef(const RefCountedBase& other);

    virtual void ReleaseRef();

    /**
     * @brief Destroys and deinitializes the object.
     */
    virtual void DestroyObject() {};

    virtual ~RefCountedBase();

private:
protected:
    RefCount* mpRefCnt = nullptr;
};

} // namespace fx
