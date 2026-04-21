#pragma once

#include "PagedArray.hpp"
#include "Types.hpp"

namespace fx {

template <typename T>
concept C_IsIntType = C_IsAnyOf<T, int16, uint16, int32, uint32, int64, uint64>;

template <typename T>
concept C_IsFloatType = std::is_same_v<T, float32>;

template <typename T>
concept C_IsSerializable = C_IsIntType<T> || C_IsFloatType<T>;


struct SerializerValue
{
public:
    enum eType
    {
        Int,
        Float,
    };

public:
    SerializerValue() = delete;

    template <typename T>
        requires C_IsIntType<T>
    explicit SerializerValue(T value) : Type(eType::Int), IntValue(value)
    {
    }

    template <typename T>
        requires C_IsFloatType<T>
    explicit SerializerValue(T value) : Type(eType::Float), FloatValue(value)
    {
    }

public:
    eType Type;

    union
    {
        int64 IntValue;
        float32 FloatValue;
    };
};

class Serializer
{
public:
    Serializer() { Values.Create(16); }

    Serializer(const Serializer& other) = delete;


    template <typename T>
        requires C_IsSerializable<T>
    Serializer& operator<<(T value)
    {
        Values.Insert(SerializerValue { value });
        return *this;
    }

    template <typename T>
        requires C_IsSerializable<T>
    Serializer& Add(T& member)
    {
        if (bInWriteMode) {
            (*this) << member;
        }
        else {
            (*this) >> member;
        }

        return *this;
    }

    void SetWriteMode() { bInWriteMode = true; }
    void SetReadMode() { bInWriteMode = false; }

    template <typename T>
        requires C_IsSerializable<T>
    Serializer& operator>>(T& value)
    {
        if (WriteIndex > Values.Size()) {
            return *this;
        }

        SerializerValue& stored_value = Values[WriteIndex++];

        if constexpr (C_IsIntType<T>) {
            value = stored_value.IntValue;
        }
        else if constexpr (C_IsFloatType<T>) {
            value = stored_value.FloatValue;
        }

        return *this;
    }


    void Rewind() { WriteIndex = 0; }

public:
    int WriteIndex = 0;
    PagedArray<SerializerValue> Values;

    bool bInWriteMode = true;
};

} // namespace fx

#define FX_SERIALIZABLE_MEMBERS(...)                                                                                   \
    template <typename... TMemberTypes>                                                                                \
    void SerializerDefineRecursive(Serializer& s, TMemberTypes&... types)                                              \
    {                                                                                                                  \
        (s.Add(types), ...);                                                                                           \
    }                                                                                                                  \
    void SerializeNow(Serializer& s) { SerializerDefineRecursive(s, __VA_ARGS__); }                                    \
    void Serialize(Serializer& s)                                                                                      \
    {                                                                                                                  \
        s.SetWriteMode();                                                                                              \
        SerializeNow(s);                                                                                               \
    }                                                                                                                  \
    void Deserialize(Serializer& s)                                                                                    \
    {                                                                                                                  \
        s.SetReadMode();                                                                                               \
        SerializeNow(s);                                                                                               \
    }
