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

	CVarValue* Set(const String& name, const String& value);
	CVarValue* Set(const String& name, const int64 value);
	CVarValue* Set(const String& name, const float32 value);

	const CVarValue* Get(const String& name);

private:
	std::unordered_map<String, CVarValue> mCVars;
};


} // namespace fx
