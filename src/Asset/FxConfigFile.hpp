#pragma once

#include <string>


#include <Core/FxMemPool.hpp>
#include <Core/Types.hpp>
#include <Core/FxHash.hpp>

#include <Util/FxTokenizer.hpp>

#include <vector>

struct FxConfigEntry
{
	FxConfigEntry() = default;

	FxHash NameHash;
	std::string Name = "";

	void Print()
	{
		printf("Entry [name=%s, type=", Name.c_str());
		switch (Type) {
		case ValueType::None:
			printf("None]\n");
			break;
		case ValueType::Str:
			printf("Str, value=\"%s\"]\n", ValueStr);
			break;
		case ValueType::Identifier:
			printf("Ident, value=%s]\n", ValueStr);
			break;
		case ValueType::Int:
			printf("Int, value=%lld]\n", ValueInt);
			break;
		default:
			break;
		}
	}

	template <typename T>
	T GetValue() const;

	template <>
	int64 GetValue<int64>() const
	{
		if (Type != ValueType::Int) {
			return 0;
		}
		return ValueInt;
	}

	template <>
	uint32 GetValue<uint32>() const
	{
	    return static_cast<uint32>(GetValue<int64>());
	}

	template <>
	char* GetValue<char*>() const
	{
		if (Type != ValueType::Str) {
			return nullptr;
		}
		return ValueStr;
	}

	template <>
	const char* GetValue<const char*>() const
	{
		if (Type != ValueType::Str) {
			return nullptr;
		}
		return ValueStr;
	}

	enum class ValueType {
		None,
		Int,
		Float,
		Str,
		Identifier,
	};

	ValueType Type;

	union
	{
		int64 ValueInt;
		float32 ValueFloat;
		char* ValueStr;
	};
};

class FxConfigFile
{
public:
	FxConfigFile() = default;

	void Load(const std::string& path);
	std::vector<FxConfigEntry>& GetEntries()
	{
		return mConfigEntries;
	}

	const FxConfigEntry* GetEntry(uint32 requested_name_hash) const
	{
		for (const FxConfigEntry& entry : mConfigEntries) {
			if (entry.NameHash == requested_name_hash) {
				return &entry;
			}
		}
		return nullptr;
	}

	template <typename T>
	T GetValue(uint32 entry_name_hash) const
	{
		const FxConfigEntry* entry = GetEntry(entry_name_hash);
		if (!entry) {
			return 0;
		}
		return entry->GetValue<T>();
	}

	template <typename T>
	constexpr T GetValue(const char* entry_name) const
	{
	    return GetValue<T>(FxHashStr(entry_name));
	}



private:
	void ParseEntries(std::vector<FxTokenizer::Token>& tokens);

private:

	std::vector<FxConfigEntry> mConfigEntries;
};
