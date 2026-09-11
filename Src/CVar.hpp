/*
 * File:        CVar.hpp
 * Author:      emd22
 * Created:     11/09/2026
 * Description: Console vars and global values to be shared between script, game and engine.
 */

#pragma once

#include <Core/String.hpp>
#include <unordered_map>

namespace fx {

enum class eCVarType
{
	Int,
	Float,
	Boolean,
	String,
};

class CVarValue
{
public:
	CVarValue() = delete;

	CVarValue(const String& name, const eCVarType cv_type);
	CVarValue(CVarValue&& other);

	String AsString() const;
	const String& GetName() { return mName; };

	CVarValue& operator=(const CVarValue& other);
	CVarValue& operator=(CVarValue&& other);

	void SetName(const String& name);

	~CVarValue();

public:
	eCVarType Type = eCVarType::Int;

	union
	{
		String StringValue;
		int64 IntValue;
		float32 FloatValue;
	};

private:
	bool bIsUndefined = true;
	String mName;
};

class CVarManager
{
public:
	CVarManager() = default;

	/////////////////////////////////////
	// Set functions
	/////////////////////////////////////

	template <typename T>
	CVarValue* Set(const String& name, T value) = delete;

	template <typename T>
		requires std::is_integral_v<T>
	CVarValue* Set(const String& name, const T value)
	{
		auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::Int);

		if (!inserted && it->second.Type != eCVarType::Int) {
			InvalidTypeError(it->second, name, eCVarType::Int);
			return nullptr;
		}

		it->second.IntValue = static_cast<int64>(value);
		return &it->second;
	}

	template <typename T>
		requires std::is_floating_point_v<T>
	CVarValue* Set(const String& name, const T value)
	{
		auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::Float);

		if (!inserted && it->second.Type != eCVarType::Float) {
			InvalidTypeError(it->second, name, eCVarType::Float);
			return nullptr;
		}

		it->second.FloatValue = static_cast<float32>(value);
		return &it->second;
	}

	template <>
	CVarValue* Set(const String& name, const char* value)
	{
		auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::String);

		if (!inserted && it->second.Type != eCVarType::String) {
			InvalidTypeError(it->second, name, eCVarType::String);
			return nullptr;
		}

		it->second.StringValue = String(value);
		return &it->second;
	}


	/////////////////////////////////////
	// Get functions
	/////////////////////////////////////

	const CVarValue* GetCVar(const String& name);

	template <typename T>
	T Get(const String& name, T fallback) = delete;

	template <typename T>
		requires std::is_integral_v<T>
	T Get(const String& name, T fallback)
	{
		const CVarValue* cv = GetCVar(name);

		if (cv == nullptr) {
			return fallback;
		}

		return cv->IntValue;
	}

	template <typename T>
		requires std::is_floating_point_v<T>
	T Get(const String& name, T fallback)
	{
		const CVarValue* cv = GetCVar(name);

		if (cv == nullptr) {
			return fallback;
		}

		return cv->FloatValue;
	}

	template <typename T>
	const char* Get(const String& name, const char* fallback)
	{
		const CVarValue* cv = GetCVar(name);

		if (cv == nullptr) {
			return fallback;
		}

		return cv->StringValue.CStr();
	}


private:
	void InvalidTypeError(const CVarValue& found, const String& name, eCVarType expected);

private:
	std::unordered_map<String, CVarValue> mCVars;
};


} // namespace fx
