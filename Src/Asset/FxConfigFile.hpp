#pragma once

#include <Core/FxHash.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Util/FxTokenizer.hpp>
#include <string>

template <typename TType>
concept C_ConfigSupportsType = std::is_integral_v<TType> || std::is_floating_point_v<TType> ||
                               std::is_same_v<TType, char*>;

class FxConfigEntry
{
public:
    enum class ValueType
    {
        eNone,
        eInt,
        eFloat,
        eString,
    };

public:
    FxConfigEntry() = default;

    template <typename TType>
    FxConfigEntry(const std::string& name, TType value) : Name(name)
    {
        NameHash = FxHashStr64(name.c_str());
        Set(value);
    }

    template <typename TType>
        requires C_ConfigSupportsType<TType> && (!std::is_same_v<std::string, TType>)
    void Set(TType value)
    {
        if constexpr (std::is_integral_v<TType>) {
            Type = ValueType::eInt;
            mIntValue = value;
        }
        else if constexpr (std::is_floating_point_v<TType>) {
            Type = ValueType::eFloat;
            mFloatValue = value;
        }
        else if constexpr (std::is_same_v<char*, std::remove_const_t<TType>>) {
            Type = ValueType::eString;
            mStringValue = strdup(value);
        }
    }

    FxConfigEntry(const FxConfigEntry& other) = delete;
    FxConfigEntry(FxConfigEntry&& other)
    {
        Type = other.Type;
        other.Type = ValueType::eNone;

        if (Type == ValueType::eString) {
            mStringValue = other.mStringValue;
            other.mStringValue = nullptr;
        }
        else if (Type == ValueType::eInt) {
            mIntValue = other.mIntValue;
        }
        else if (Type == ValueType::eFloat) {
            mFloatValue = other.mFloatValue;
        }

        Name = other.Name;
        NameHash = other.NameHash;

        other.NameHash = 0;
        other.Name.clear();
    }

    FxConfigEntry& operator=(const FxConfigEntry& other) = delete;


    void Set(const std::string& str)
    {
        Type = ValueType::eString;
        mStringValue = strdup(str.c_str());
    }

    void Print()
    {
        printf("Entry [Name=%s, Type=", Name.c_str());
        switch (Type) {
        case ValueType::eNone:
            printf("None]\n");
            break;
        case ValueType::eString:
            printf("Str, Value=\"%s\"]\n", mStringValue);
            break;
        case ValueType::eInt:
            printf("Int, Value=%lld]\n", mIntValue);
            break;
        case ValueType::eFloat:
            printf("Float, Value=%f\n", mFloatValue);
        default:
            break;
        }
    }

    std::string AsString() const
    {
        switch (Type) {
        case ValueType::eNone:
            return "";
        case ValueType::eInt:
            return std::to_string(mIntValue);
        case ValueType::eFloat:
            return std::to_string(mFloatValue);
        case ValueType::eString:
            return std::string(mStringValue);
        }

        return "";
    }

    template <typename T>
    T Get() const;

    template <typename TIntType>
        requires std::is_integral_v<TIntType>
    TIntType Get() const
    {
        FxAssert(Type == ValueType::eInt);

        return static_cast<TIntType>(mIntValue);
    }

    template <typename TFloatType>
        requires std::is_floating_point_v<TFloatType>
    TFloatType Get() const
    {
        FxAssert(Type == ValueType::eFloat);

        return static_cast<TFloatType>(mFloatValue);
    }

    template <>
    const char* Get() const
    {
        FxAssert(Type == ValueType::eString);

        return mStringValue;
    }

    ~FxConfigEntry()
    {
        if (Type == ValueType::eString && mStringValue != nullptr) {
            free(mStringValue);
        }

        Type = ValueType::eNone;

        mStringValue = nullptr;
    }

    FxHash64 NameHash = 0;
    std::string Name = "";

    ValueType Type = ValueType::eNone;

private:
    union
    {
        int64 mIntValue;
        float32 mFloatValue;
        char* mStringValue;
    };
};

class FxConfigFile
{
public:
    FxConfigFile() = default;

    void Load(const std::string& path);
    void Write(const std::string& path);

    const FxConfigEntry* GetEntry(FxHash64 requested_name_hash) const;
    FxPagedArray<FxConfigEntry>& GetEntries() { return mConfigEntries; }

    template <typename T>
    T GetValue(FxHash64 entry_name_hash) const
    {
        const FxConfigEntry* entry = GetEntry(entry_name_hash);
        if (!entry) {
            FxLogError("Cannot find config entry {}!", entry_name_hash);
            return 0;
        }
        return entry->Get<T>();
    }

    template <typename T>
    constexpr T GetValue(const char* entry_name) const
    {
        return GetValue<T>(FxHashStr64(entry_name));
    }

    template <typename TValue>
    void AddEntry(const std::string& name, TValue value)
    {
        FxConfigEntry entry;
        entry.Name = name;
        entry.NameHash = FxHashStr64(name.c_str());

        entry.Set<TValue>(value);

        mConfigEntries.Insert(std::move(entry));
    }

private:
    void ParseEntries(FxPagedArray<FxToken>& tokens);

private:
    FxPagedArray<FxConfigEntry> mConfigEntries;
};
