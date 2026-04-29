#pragma once

#include <Color.hpp>
#include <Core/Hash.hpp>
#include <Core/Memory.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Types.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Util/Tokenizer.hpp>
#include <string>

namespace fx {

template <typename TType>
concept C_ConfigSupportsType = std::is_integral_v<TType> || std::is_floating_point_v<TType> ||
                               std::is_same_v<TType, char*>;


/////////////////////////////////////
// Config Value
/////////////////////////////////////

struct ConfigValue
{
public:
    ConfigValue() = default;

    template <typename T>
    ConfigValue(T value)
    {
        Set(value);
    }

    ConfigValue(const ConfigValue& other) { (*this) = other; }

    ConfigValue& operator=(const ConfigValue& other)
    {
        Type = other.Type;

        if (Type == eValueType::String) {
            mStringValue = strdup(other.mStringValue);
        }
        else if (Type == eValueType::Int) {
            mIntValue = other.mIntValue;
        }
        else if (Type == eValueType::Float) {
            mFloatValue = other.mFloatValue;
        }

        return *this;
    }

    void Set(const ConfigValue& other) { (*this) = other; }

    template <typename T>
    T Get() const;

    template <typename TIntType>
        requires std::is_integral_v<TIntType>
    TIntType Get() const
    {
        if (Type != eValueType::Int) {
            LogWarning("Attempting to retrieve int type from non-int!");
            return 0;
        }

        return static_cast<TIntType>(mIntValue);
    }

    template <typename TFloatType>
        requires std::is_floating_point_v<TFloatType>
    TFloatType Get() const
    {
        if (Type != eValueType::Float) {
            LogWarning("Attempting to retrieve float type from non-float!");
            return 0.0f;
        }

        return static_cast<TFloatType>(mFloatValue);
    }

    template <>
    const char* Get() const
    {
        Assert(Type == eValueType::String);

        return mStringValue;
    }


    template <typename TType>
        requires C_ConfigSupportsType<TType> && (!std::is_same_v<std::string, TType>)
    void Set(TType value)
    {
        if constexpr (std::is_integral_v<TType>) {
            Type = eValueType::Int;
            mIntValue = value;
        }
        else if constexpr (std::is_floating_point_v<TType>) {
            Type = eValueType::Float;
            mFloatValue = value;
        }
        else if constexpr (std::is_same_v<char*, std::remove_const_t<TType>>) {
            Type = eValueType::String;
            mStringValue = strdup(value);
        }
    }

    void Set(const std::string& str)
    {
        Type = eValueType::String;
        mStringValue = strdup(str.c_str());
    }

    std::string AsString() const;

public:
    enum class eValueType
    {
        None,
        Int,
        Float,
        String,
        Struct,
    };

    eValueType Type = eValueType::None;

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


class ConfigEntry : public ConfigValue
{
public:
    static ConfigEntry Array(const std::string& name, ConfigValue::eValueType type)
    {
        ConfigEntry entry(name);
        entry.Type = type;
        entry.bIsArray = true;
        return entry;
    }

    template <typename TType>
    static ConfigEntry Array(const std::string& name, ConfigValue::eValueType type, const Slice<TType>& data)
    {
        ConfigEntry entry = ConfigEntry::Array(name, type);

        for (const TType& value : data) {
            entry.AppendValue(value);
        }

        return entry;
    }

    static ConfigEntry Struct(const std::string& name)
    {
        ConfigEntry entry(name);
        entry.Type = ConfigValue::eValueType::Struct;
        return entry;
    }

    template <typename TType>
    static ConfigEntry Literal(const std::string& name, const TType& literal)
    {
        ConfigEntry entry(name, literal);
        return entry;
    }

public:
    ConfigEntry() = default;

    ConfigEntry(const std::string& name) : Name(name) {}

    template <typename TType>
    ConfigEntry(const std::string& name, TType value) : ConfigEntry(name)
    {
        Set(value);
    }

    ConfigEntry(const ConfigEntry& other) = delete;
    ConfigEntry(ConfigEntry&& other);

    ConfigEntry& operator=(ConfigEntry&& entry);

    void AddMember(ConfigEntry&& entry);

    void AppendValue(ConfigValue&& value);

    void AppendValue(const Vec3f& vec);
    void AppendValue(const Vec4f& vec);
    void AppendValue(const Quat& quat);

    /////////////////////////////////////
    // Value get functions
    /////////////////////////////////////


    template <typename T>
    T GetValue() const;

    template <>
    Vec3f GetValue<Vec3f>() const
    {
        Assert(bIsArray == true && ArrayData.Size() >= 3);
        return Vec3f(ArrayData[0].Get<float32>(), ArrayData[1].Get<float32>(), ArrayData[2].Get<float32>());
    }

    template <>
    Quat GetValue<Quat>() const
    {
        Assert(bIsArray == true && ArrayData.Size() >= 4);
        return Quat(ArrayData[0].Get<float32>(), ArrayData[1].Get<float32>(), ArrayData[2].Get<float32>(),
                    ArrayData[3].Get<float32>());
    }

    template <>
    Color GetValue<Color>() const
    {
        Assert(bIsArray == true && ArrayData.Size() >= 4);
        return Color::FromRGBA(ArrayData[0].Get<int32>(), ArrayData[1].Get<int32>(), ArrayData[2].Get<int32>(),
                               ArrayData[3].Get<int32>());
    }


    template <typename TIntType>
        requires std::is_integral_v<TIntType>
    TIntType GetValue() const
    {
        if (Type != eValueType::Int) {
            LogWarning("Attempting to retrieve int type from non-int!");
            return 0;
        }

        return static_cast<TIntType>(mIntValue);
    }

    template <typename TFloatType>
        requires std::is_floating_point_v<TFloatType>
    TFloatType GetValue() const
    {
        if (Type != eValueType::Float) {
            LogWarning("Attempting to retrieve float type from non-float!");
            return 0.0f;
        }

        return static_cast<TFloatType>(mFloatValue);
    }

    template <>
    const char* GetValue() const
    {
        Assert(Type == eValueType::String);

        return mStringValue;
    }


    ConfigEntry* GetMember(const Hash32 name) const;

    template <typename T>
    T GetMemberValue(const Hash32 name, const T& fallback) const
    {
        ConfigEntry* entry = GetMember(name);
        if (!entry) {
            return fallback;
        }

        return entry->GetValue<T>();
    }


    ConfigEntry& operator=(const ConfigEntry& other) = delete;


    std::string AsString(uint32 indent = 0) const;

    ~ConfigEntry();

public:
    Name Name;

    bool bIsArray = false;

    PagedArray<ConfigEntry> Members;
    PagedArray<ConfigValue> ArrayData;
};

/////////////////////////////////////
// Config File
/////////////////////////////////////

class ConfigFile
{
public:
    ConfigFile() = default;

    void Load(const std::string& path);
    void Write(const std::string& path);

    ConfigEntry* GetEntry(Hash32 requested_name_hash) const;
    PagedArray<ConfigEntry>& GetEntries() { return mConfigEntries; }

    void PrintEntries();

    template <typename T>
    T GetEntryValue(const Hash32 entry_name_hash, const T& fallback) const
    {
        const ConfigEntry* entry = GetEntry(entry_name_hash);
        if (!entry) {
            LogError("Cannot find config entry {}!", entry_name_hash);
            return fallback;
        }

        return entry->GetValue<T>();
    }

    template <typename TValue>
    void AddEntry(const std::string& name, TValue value)
    {
        ConfigEntry entry;
        entry.Name = name;

        entry.Set<TValue>(value);

        mConfigEntries.Insert(std::move(entry));
    }

    ConfigEntry& AddEntry(ConfigEntry&& entry)
    {
        if (!mConfigEntries.IsInited()) {
            mConfigEntries.Create();
        }
        return mConfigEntries.Insert(std::move(entry));
    }

    ConfigEntry* AddEntry(const std::string& name)
    {
        if (!mConfigEntries.IsInited()) {
            mConfigEntries.Create();
        }

        ConfigEntry* entry = mConfigEntries.Insert();
        entry->Name = name;

        return entry;
    }

private:
    void Parse(PagedArray<Token>& tokens);

    bool EatToken(eTokenType type);
    bool EatToken(const Slice<eTokenType>& expected_types);

    ConfigEntry ParseEntry();
    void ParseValue(ConfigValue& value);

    FX_FORCE_INLINE Token* GetToken() const
    {
        Assert(mTokenIndex <= mpTokens->Size());
        return &mpTokens->Get(mTokenIndex);
    }

    FX_FORCE_INLINE void NextToken() { ++mTokenIndex; }

    void InitConstants();


private:
    PagedArray<ConfigEntry> mConfigEntries;
    SizedArray<ConfigEntry> mConstants;

    PagedArray<Token>* mpTokens = nullptr;
    uint32 mTokenIndex = 0;
};

} // namespace fx
