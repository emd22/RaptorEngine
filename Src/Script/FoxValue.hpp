#pragma once

#include <Core/Types.hpp>
#include <cstdio>
#include <format>

namespace fx {

class Token;

enum class eFoxType : uint16
{
    NONETYPE,
    INT,
    FLOAT,
    STRING,
    REF,
};

eFoxType FoxStringToType(Token* token);

namespace script {
struct FoxAstVarRef;

struct FoxValue
{
public:
    static const FoxValue scNone;

public:
    FoxValue() {}

    explicit FoxValue(int value) : Type(eFoxType::INT), ValueInt(value) {}
    explicit FoxValue(float value) : Type(eFoxType::FLOAT), ValueFloat(value) {}
    explicit FoxValue(const char* value) : Type(eFoxType::STRING), ValueString(value) {}

    static FoxValue ValueFromRaw(eFoxType type, uint32 raw_value)
    {
        FoxValue value = FoxValue::scNone;

        switch (type) {
        case eFoxType::INT:
            value.ValueInt = std::bit_cast<int32>(raw_value);
            value.Type = type;
            break;

        case eFoxType::FLOAT:
            value.ValueFloat = std::bit_cast<float32>(raw_value);
            value.Type = type;
            break;

        case eFoxType::STRING:
            value.ValueInt = std::bit_cast<int32>(raw_value);
            value.Type = type;

        default:
            break;
        }

        return value;
    }

    FoxValue(const FoxValue& other) { (*this) = other; }

    FoxValue& operator=(const FoxValue& other)
    {
        Type = other.Type;

        if (other.Type == eFoxType::INT) {
            ValueInt = other.ValueInt;
        }
        else if (other.Type == eFoxType::FLOAT) {
            ValueFloat = other.ValueFloat;
        }
        else if (other.Type == eFoxType::STRING) {
            ValueString = other.ValueString;
        }
        else if (other.Type == eFoxType::REF) {
            pValueRef = other.pValueRef;
        }

        return *this;
    }


    void Print() const
    {
        printf("[Value: ");
        if (Type == eFoxType::NONETYPE) {
            printf("Null]\n");
        }
        else if (Type == eFoxType::INT) {
            printf("Int, %d]\n", ValueInt);
        }
        else if (Type == eFoxType::FLOAT) {
            printf("Float, %f]\n", ValueFloat);
        }
        else if (Type == eFoxType::STRING) {
            printf("String, %s]\n", ValueString);
        }
        else if (Type == eFoxType::REF) {
            printf("Ref, %p]\n", pValueRef);
        }
    }

    /////////////////////////////////////
    // Value set functions
    /////////////////////////////////////

    template <typename T>
    void Set(T value);

    template <>
    void Set<int32>(int32 value)
    {
        Type = eFoxType::INT;
        ValueInt = value;
    }

    template <>
    void Set<float32>(float32 value)
    {
        Type = eFoxType::FLOAT;
        ValueFloat = value;
    }

    /////////////////////////////////////
    // Value get functions
    /////////////////////////////////////

    template <typename T>
    T Get() const;

    template <>
    int32 Get<int32>() const
    {
        return ValueInt;
    }

    template <>
    float32 Get<float32>() const
    {
        return ValueFloat;
    }

    template <>
    const char* Get<const char*>() const
    {
        return ValueString;
    }


    uint32 AsUInt() const { return std::bit_cast<uint32>(ValueInt); }

    inline bool IsNumber() { return (Type == eFoxType::INT || Type == eFoxType::FLOAT); }

    inline bool IsRef() { return (Type == eFoxType::REF); }


public:
    eFoxType Type = eFoxType::NONETYPE;

    union
    {
        int32 ValueInt = 0;
        float32 ValueFloat;
        const char* ValueString;

        FoxAstVarRef* pValueRef;
    };
};
} // namespace script


} // namespace fx


template <>
struct std::formatter<fx::script::FoxValue>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::script::FoxValue& obj, std::format_context& ctx) const
    {
        using VT = fx::eFoxType;

        if (obj.Type == VT::NONETYPE) {
            return std::format_to(ctx.out(), "Null");
        }
        else if (obj.Type == VT::INT) {
            return std::format_to(ctx.out(), "{}", obj.ValueInt);
        }
        else if (obj.Type == VT::FLOAT) {
            return std::format_to(ctx.out(), "{}", obj.ValueFloat);
        }
        else if (obj.Type == VT::STRING) {
            return std::format_to(ctx.out(), "{}", obj.ValueString);
        }
        else if (obj.Type == VT::REF) {
            return std::format_to(ctx.out(), "{:p}", reinterpret_cast<void*>(obj.pValueRef));
        }

        return std::format_to(ctx.out(), "Unknown");
    }
};


template <>
struct std::formatter<fx::eFoxType>
{
    auto parse(format_parse_context& ctx) { return ctx.begin(); }

    auto format(const fx::eFoxType& type, std::format_context& ctx) const
    {
        using VT = fx::eFoxType;

        switch (type) {
        case VT::NONETYPE:
            return std::format_to(ctx.out(), "none");
        case VT::INT:
            return std::format_to(ctx.out(), "int");
        case VT::FLOAT:
            return std::format_to(ctx.out(), "float");
        case VT::REF:
            return std::format_to(ctx.out(), "ref");
        case VT::STRING:
            return std::format_to(ctx.out(), "str");
        }

        return std::format_to(ctx.out(), "Unknown");
    }
};
