#pragma once

#include "FoxVM.hpp"

namespace fx {
class String;


namespace script {

class FoxScript
{
public:
    FoxScript() = default;
    FoxScript(const String& path);

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

    FoxValue CallProc(FoxSymbol* sym, const SizedArray<FoxValue>& args);

    FoxValue Update();
    FoxValue Resume();

    void RegisterProc(Hash32 name_hash, eFoxProcFlags flags, const SizedArray<eFoxType> arg_types,
                      VMExternalFunction function);

    void SetGlobal(const Hash32 name_hash, const FoxValue& value);
    FoxValue GetGlobal(const Hash32 name_hash) const;

    FX_FORCE_INLINE bool IsPaused() const { return Vm.bIsPaused; }

    FX_FORCE_INLINE FoxSymbol* GetSymbol(const String& name) const { return Vm.GetSymbol(HashStr32(name.CStr())); }
    FX_FORCE_INLINE FoxSymbol* GetSymbol(const Hash32 name_hash) const { return Vm.GetSymbol(name_hash); };

    FX_FORCE_INLINE uint32 GetProcAddr(Hash32 name_hash) const { return Vm.GetProcAddr(name_hash); };

public:
    FoxVM Vm;

    uint64 ResumeTime = 0;
};

} // namespace script
} // namespace fx
