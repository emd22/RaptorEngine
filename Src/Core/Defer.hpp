#pragma once

#include <utility>

/**
 * @brief Defers calling a function until the end of a scope.
 */
template <typename FuncType>
class DeferObject
{
public:
    DeferObject(FuncType&& func) noexcept : mFunc(std::move(func)) {}

    DeferObject(const DeferObject& other) = delete;
    DeferObject& operator=(const DeferObject& other) = delete;

    ~DeferObject() noexcept { mFunc(); }

private:
    FuncType mFunc;
};

#define FX_CONCAT_INNER(a_, b_) a_##b_
#define FX_CONCAT(a_, b_)       FX_CONCAT_INNER(a_, b_)

#define Defer(fn_) DeferObject FX_CONCAT(_ds_, __LINE__)(fn_)
