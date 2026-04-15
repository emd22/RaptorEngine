#pragma once

#include <Core/FxHash.hpp>
#include <Core/FxMemory.hpp>
#include <Core/FxName.hpp>
#include <Core/FxPagedArray.hpp>
#include <Core/FxTypes.hpp>
#include <FxColor.hpp>
#include <Math/FxQuat.hpp>
#include <Math/FxVec3.hpp>
#include <Util/FxTokenizer.hpp>
#include <string>

template <typename TType>
concept C_ConfigSupportsType = std::is_integral_v<TType> || std::is_floating_point_v<TType> ||
                               std::is_same_v<TType, char*>;


/////////////////////////////////////
// Config Value
/////////////////////////////////////

struct FxConfigValue
{
public:
    FxConfigValue() = default;

    template <typename T>
    FxConfigValue(T value)
    {
        Set(value);
    }

    FxConfigValue(const FxConfigValue& other) { (*this) = other; }

    FxConfigValue& operator=(const FxConfigValue& other)
    {
        Type = other.Type;

        if (Type == ValueType::eString) {
            mStringValue = strdup(other.mStringValue);
        }
        else if (Type == ValueType::eInt) {
            mIntValue = other.mIntValue;
        }
        else if (Type == ValueType::eFloat) {
            mFloatValue = other.mFloatValue;
        }

        return *this;
    }

    void Set(const FxConfigValue& other) { (*this) = other; }

    template <typename T>
    T Get() const;

    template <typename TIntType>
        requires std::is_integral_v<TIntType>
    TIntType Get() const
    {
        if (Type != ValueType::eInt) {
            FxLogWarning("Attempting to retrieve int type from non-int!");
            return 0;
        }

        return static_cast<TIntType>(mIntValue);
    }

    template <typename TFloatType>
        requires std::is_floating_point_v<TFloatType>
    TFloatType Get() const
    {
        if (Type != ValueType::eFloat) {
            FxLogWarning("Attempting to retrieve float type from non-float!");
            return 0.0f;
        }

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


/////////////////////////////////////
// Config Entry
/////////////////////////////////////


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

    template <typename TType>
    static FxConfigEntry Array(const std::string& name, FxConfigValue::ValueType type, const FxSlice<TType>& data)
    {
        FxConfigEntry entry = FxConfigEntry::Array(name, type);

        for (const TType& value : data) {
            entry.AppendValue(value);
        }

        return entry;
    }

    static FxConfigEntry Struct(const std::string& name)
    {
        FxConfigEntry entry(name);
        entry.Type = FxConfigValue::ValueType::eStruct;
        return entry;
    }

    template <typename TType>
    static FxConfigEntry Literal(const std::string& name, const TType& literal)
    {
        FxConfigEntry entry(name, literal);
        return entry;
    }

public:
    FxConfigEntry() = default;

    FxConfigEntry(const std::string& name) : Name(name) {}

    template <typename TType>
    FxConfigEntry(const std::string& name, TType value) : FxConfigEntry(name)
    {
        Set(value);
    }

    FxConfigEntry(const FxConfigEntry& other) = delete;
    FxConfigEntry(FxConfigEntry&& other);

    FxConfigEntry& operator=(FxConfigEntry&& entry);

    void AddMember(FxConfigEntry&& entry);

    void AppendValue(FxConfigValue&& value);

    void AppendValue(const FxVec3f& vec);
    void AppendValue(const FxVec4f& vec);
    void AppendValue(const FxQuat& quat);

    /////////////////////////////////////
    // Value get functions
    /////////////////////////////////////


    template <typename T>
    T GetValue() const;

    template <>
    FxVec3f GetValue<FxVec3f>() const
    {
        FxAssert(bIsArray == true && ArrayData.Size() >= 3);
        return FxVec3f(ArrayData[0].Get<float32>(), ArrayData[1].Get<float32>(), ArrayData[2].Get<float32>());
    }

    template <>
    FxQuat GetValue<FxQuat>() const
    {
        FxAssert(bIsArray == true && ArrayData.Size() >= 4);
        return FxQuat(ArrayData[0].Get<float32>(), ArrayData[1].Get<float32>(), ArrayData[2].Get<float32>(),
                      ArrayData[3].Get<float32>());
    }

    template <>
    FxColor GetValue<FxColor>() const
    {
        FxAssert(bIsArray == true && ArrayData.Size() >= 4);
        return FxColor::FromRGBA(ArrayData[0].Get<int32>(), ArrayData[1].Get<int32>(), ArrayData[2].Get<int32>(),
                                 ArrayData[3].Get<int32>());
    }


    template <typename TIntType>
        requires std::is_integral_v<TIntType>
    TIntType GetValue() const
    {
        if (Type != ValueType::eInt) {
            FxLogWarning("Attempting to retrieve int type from non-int!");
            return 0;
        }

        return static_cast<TIntType>(mIntValue);
    }

    template <typename TFloatType>
        requires std::is_floating_point_v<TFloatType>
    TFloatType GetValue() const
    {
        if (Type != ValueType::eFloat) {
            FxLogWarning("Attempting to retrieve float type from non-float!");
            return 0.0f;
        }

        return static_cast<TFloatType>(mFloatValue);
    }

    template <>
    const char* GetValue() const
    {
        FxAssert(Type == ValueType::eString);

        return mStringValue;
    }


    FxConfigEntry* GetMember(const FxHash64 name) const;

    template <typename T>
    T GetMemberValue(const FxHash64 name, const T& fallback) const
    {
        FxConfigEntry* entry = GetMember(name);
        if (!entry) {
            return fallback;
        }

        return entry->GetValue<T>();
    }


    FxConfigEntry& operator=(const FxConfigEntry& other) = delete;


    std::string AsString(uint32 indent = 0) const;

    ~FxConfigEntry();

public:
    FxName Name;

    bool bIsArray = false;

    FxPagedArray<FxConfigEntry> Members;
    FxPagedArray<FxConfigValue> ArrayData;
};

/////////////////////////////////////
// Config File
/////////////////////////////////////

class FxConfigFile
{
public:
    FxConfigFile() = default;

    void Load(const std::string& path);
    void Write(const std::string& path);

    FxConfigEntry* GetEntry(FxHash64 requested_name_hash) const;
    FxPagedArray<FxConfigEntry>& GetEntries() { return mConfigEntries; }

    void PrintEntries();

    template <typename T>
    T GetEntryValue(const FxHash64 entry_name_hash, const T& fallback) const
    {
        const FxConfigEntry* entry = GetEntry(entry_name_hash);
        if (!entry) {
            FxLogError("Cannot find config entry {}!", entry_name_hash);
            return fallback;
        }

        return entry->GetValue<T>();
    }

    template <typename TValue>
    void AddEntry(const std::string& name, TValue value)
    {
        FxConfigEntry entry;
        entry.Name = name;

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

    void InitConstants();


private:
    FxPagedArray<FxConfigEntry> mConfigEntries;
    FxSizedArray<FxConfigEntry> mConstants;

    FxPagedArray<FxToken>* mpTokens = nullptr;
    uint32 mTokenIndex = 0;
};
