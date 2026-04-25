#pragma once

#include "FoxVM.hpp"

namespace fx {
class String;


namespace script {

class FoxScript
{
public:
    FoxScript() = default;

    void Load(const String& path);

    template <typename T>
        requires C_IsAnyOf<T, uint32, int32, float32>
    void PushValue(T value)
    {
        eFoxType value_type = eFoxType::NONETYPE;

        if constexpr (C_IsAnyOf<T, uint32, int32>) {
            value_type = eFoxType::INT;
        }
        else if constexpr (std::is_same_v<T, float32>) {
            value_type = eFoxType::FLOAT;
        }

        Push32(value_type, std::bit_cast<uint32>(value));
    }

    void PushValue(const FoxValue& value);

    FoxValue Call(FoxSymbol* sym, const SizedArray<FoxValue>& args);
    FoxValue CallIfExists(const Hash32 name_hash, const SizedArray<FoxValue>& args);

    void RegisterFunction(Hash32 name_hash, bool returns_value, uint32 parameter_count, VMExternalFunction function);

    FX_FORCE_INLINE FoxSymbol* GetSymbol(const String& name) const { return Vm.GetSymbol(HashStr32(name.CStr())); }
    FX_FORCE_INLINE FoxSymbol* GetSymbol(const Hash32 name_hash) const { return Vm.GetSymbol(name_hash); };

    FX_FORCE_INLINE uint32 GetFunctionAddr(Hash32 name_hash) const { return Vm.GetFunctionAddr(name_hash); };

public:
    FoxVM Vm;
};

} // namespace script
} // namespace fx
