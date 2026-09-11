#include "CVar.hpp"

#include <Core/Defines.hpp>
#include <Core/Log.hpp>

namespace fx {

#define ENUM_TYPE eCVarType

static const char* GetCVarTypeName(const eCVarType cv_type)
{
	switch (cv_type) {
		FX_ENUM_CASE_NAME(Int);
		FX_ENUM_CASE_NAME(Float);
		FX_ENUM_CASE_NAME(Boolean);
		FX_ENUM_CASE_NAME(String);
	default:;
	}

	return "Unknown";
}

CVarValue::CVarValue(const String& name, const eCVarType cv_type) : Type(cv_type), StringValue(""), mName(name) {}
CVarValue::CVarValue(CVarValue&& other)
{
	Type = other.Type;
	mName = std::move(other.mName);

	switch (Type) {
	case eCVarType::String:
		StringValue = std::move(other.StringValue);
		break;
	case fx::eCVarType::Boolean:
	case fx::eCVarType::Int:
		IntValue = other.IntValue;
		other.IntValue = 0;
		break;
	case fx::eCVarType::Float:
		FloatValue = other.FloatValue;
		other.FloatValue = 0.0f;
		break;
	}

	other.bIsUndefined = true;
}

String CVarValue::AsString() const
{
	switch (Type) {
	case eCVarType::String:
		return StringValue;
	case fx::eCVarType::Int:
		return String::Fmt("{}", IntValue);
	case fx::eCVarType::Float:
		return String::Fmt("{}", FloatValue);
	case fx::eCVarType::Boolean:
		return String::Fmt("{}", IntValue ? "true" : "false");
	}
}


void CVarValue::SetName(const String& name) { mName = name; }


CVarValue& CVarValue::operator=(const CVarValue& other)
{
	Type = other.Type;
	mName = other.mName;

	switch (Type) {
	case eCVarType::String:
		StringValue = other.StringValue;
		break;

	case fx::eCVarType::Boolean:
	case fx::eCVarType::Int:
		IntValue = other.IntValue;
		break;

	case fx::eCVarType::Float:
		FloatValue = other.FloatValue;
		break;
	}

	return *this;
}

CVarValue& CVarValue::operator=(CVarValue&& other)
{
	Type = other.Type;
	mName = std::move(other.mName);

	switch (Type) {
	case eCVarType::String:
		StringValue = std::move(other.StringValue);
		break;
	case fx::eCVarType::Boolean:
	case fx::eCVarType::Int:
		IntValue = other.IntValue;
		other.IntValue = 0;
		break;
	case fx::eCVarType::Float:
		FloatValue = other.FloatValue;
		other.FloatValue = 0.0f;
		break;
	}

	other.bIsUndefined = true;

	return *this;
}


CVarValue* CVarManager::Set(const String& name, const String& value)
{
	auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::String);

	if (!inserted && it->second.Type != eCVarType::String) {
		LogError(LC_CORE, "Cannot assign type String to CVar '{}' of type {}", name, GetCVarTypeName(it->second.Type));
		return nullptr;
	}

	it->second.StringValue = value;
	return &it->second;
}


CVarValue* CVarManager::Set(const String& name, const int64 value)
{
	auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::Int);

	if (!inserted && it->second.Type != eCVarType::Int) {
		LogError(LC_CORE, "Cannot assign type Int to CVar '{}' of type {}", name, GetCVarTypeName(it->second.Type));
		return nullptr;
	}

	it->second.IntValue = value;
	return &it->second;
}

CVarValue* CVarManager::Set(const String& name, const float32 value)
{
	auto [it, inserted] = mCVars.try_emplace(name, name, eCVarType::Float);

	if (!inserted && it->second.Type != eCVarType::Float) {
		LogError(LC_CORE, "Cannot assign type Float to CVar '{}' of type {}", name, GetCVarTypeName(it->second.Type));
		return nullptr;
	}

	it->second.FloatValue = value;
	return &it->second;
}

const CVarValue* CVarManager::Get(const String& name)
{
	auto it = mCVars.find(name);

	const bool does_cvar_exist = (it != mCVars.end());

	if (does_cvar_exist == false) {
		return nullptr;
	}

	return &it->second;
}


CVarValue::~CVarValue()
{
	if (Type == eCVarType::String) {
		StringValue.~String();
	}
}


} // namespace fx
