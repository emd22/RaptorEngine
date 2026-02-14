#pragma once

//
#include <Core/FxHash.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/FxTypes.hpp>
#include <Util/FxTokenizer.hpp>
#include <string>

template <typename TType>
concept C_ConfigSupportsType = std::is_integral_v<TType> || std::is_floating_point_v<TType> ||
                               std::is_same_v<TType, char*>;


struct FxConfigValue
{
public:
    FxConfigValue() = default;

    template <typename T>
    FxConfigValue(T value)
    {
        Set(value);
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

    void Set(const std::string& str)
    {
        Type = ValueType::eString;
        mStringValue = strdup(str.c_str());
    }

    std::string AsString() const;

public:
    enum class ValueType
    {
        eNone,
        eInt,
        eFloat,
        eString,
        eStruct,
    };

    ValueType Type = ValueType::eNone;

    union
    {
        int64 mIntValue;
        float32 mFloatValue;
        char* mStringValue;
    };
};

class FxConfigEntry : public FxConfigValue
{
public:
    static FxConfigEntry Array(const std::string& name, FxConfigValue::ValueType type)
    {
        FxConfigEntry entry(name);
        entry.Type = type;
        entry.bIsArray = true;
        return entry;
    }

    static FxConfigEntry Struct(const std::string& name)
    {
        FxConfigEntry entry(name);
        entry.Type = FxConfigValue::ValueType::eStruct;
        return entry;
    }

public:
    FxConfigEntry() = default;

    FxConfigEntry(const std::string& name) : Name(name) { NameHash = FxHashStr64(name.c_str()); }

    template <typename TType>
    FxConfigEntry(const std::string& name, TType value) : FxConfigEntry(name)
    {
        Set(value);
    }

    FxConfigEntry(const FxConfigEntry& other) = delete;
    FxConfigEntry(FxConfigEntry&& other);

    FxConfigEntry& operator=(FxConfigEntry&& entry);

    void AddMember(FxConfigEntry&& entry);

    void AppendValue(FxConfigValue&& value)
    {
        if (!ArrayData.IsInited()) {
            ArrayData.Create();
        }

        ArrayData.Insert(std::move(value));
    }

    FxConfigEntry& operator=(const FxConfigEntry& other) = delete;


    std::string AsString(uint32 indent = 0) const;

    ~FxConfigEntry();

public:
    FxHash64 NameHash = 0;
    std::string Name = "";

    bool bIsArray = false;

    FxPagedArray<FxConfigEntry> Members;
    FxPagedArray<FxConfigValue> ArrayData;
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

    FxConfigEntry& AddEntry(FxConfigEntry&& entry)
    {
        if (!mConfigEntries.IsInited()) {
            mConfigEntries.Create();
        }
        return mConfigEntries.Insert(std::move(entry));
    }

private:
    void Parse(FxPagedArray<FxToken>& tokens);
    bool EatToken(FxTokenType type);
    FxConfigEntry ParseEntry();
    void ParseValue(FxConfigValue& value);

    FX_FORCE_INLINE FxToken* GetToken() const
    {
        FxAssert(mTokenIndex <= mpTokens->Size());
        return &mpTokens->Get(mTokenIndex);
    }

    FX_FORCE_INLINE void NextToken() { ++mTokenIndex; }


private:
    FxPagedArray<FxConfigEntry> mConfigEntries;

    FxPagedArray<FxToken>* mpTokens = nullptr;
    uint32 mTokenIndex = 0;
};
