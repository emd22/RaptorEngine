#include "Serialize2.hpp"

#include <Asset/ConfigFile.hpp>
#include <Core/Log.hpp>
#include <algorithm>
#include <limits>


namespace fx {

SerializerIO::SerializerIO(uint32 reserve_size)
{
	mTape.Create(reserve_size);
	mFields.Create(16);
}

void SerializerIO::Clear()
{
	mFields.Clear();
	mTape.Clear();
}

void SerializerIO::WriteField(const char* name, const void* value, uint32 size, const SerializedFieldLayout& layout)
{
	ReserveTape(size);

	mFields.Insert(SerializedField { Name(name), mTape.GetSize(), size, layout });
	mTape.InsertRaw(value, size);
}

void SerializerIO::WriteStringField(const char* name, const char* data, uint32 length)
{
	Assert(length <= UINT32_MAX - sizeof(uint32));

	const uint32 size = sizeof(uint32) + length;

	ReserveTape(size);

	SerializedFieldLayout layout {};
	layout.ValueType = SerializedFieldLayout::eValueType::String;
	layout.ElementSize = sizeof(char);
	layout.ElementCount = length;

	mFields.Insert(SerializedField { Name(name), mTape.GetSize(), size, layout });
	mTape.Insert(length);
	mTape.InsertRaw(data, length);
}

bool SerializerIO::ReadStringField(const char*& out_data, uint32& out_length)
{
	if (mTape.GetRemainingSize() < sizeof(uint32)) {
		return false;
	}

	const uint32 length = mTape.Get<uint32>(mTape.ReadOffset);

	if (length > mTape.GetRemainingSize() - sizeof(uint32)) {
		return false;
	}

	out_data = mTape.GetPtr<const char>(mTape.ReadOffset + sizeof(uint32));
	out_length = length;

	mTape.ReadOffset += sizeof(uint32) + length;

	return true;
}

SerializedField* SerializerIO::BeginObjectField(const char* name)
{
	SerializedFieldLayout layout {};
	layout.ValueType = SerializedFieldLayout::eValueType::Object;

	return &mFields.Insert(SerializedField { Name(name), mTape.GetSize(), 0, layout });
}

void SerializerIO::EndObjectField(SerializedField* record, uint32 first_child_index)
{
	record->Size = mTape.GetSize() - record->TapeOffset;
	record->Layout.ElementCount = GetFieldCount() - first_child_index;
}

bool SerializerIO::ReadField(void* value, uint32 size)
{
	if (size > mTape.GetRemainingSize()) {
		return false;
	}

	memcpy(value, mTape.GetRaw(mTape.ReadOffset), size);
	mTape.ReadOffset += size;

	return true;
}

void SerializerIO::ReserveTape(uint32 size)
{
	if (size <= mTape.GetRemainingCapacity()) {
		return;
	}

	Assert(size <= UINT32_MAX - mTape.GetSize());

	const uint64 required = static_cast<uint64>(mTape.GetSize()) + size;
	uint64 new_capacity = static_cast<uint64>(mTape.Capacity) * 2;

	if (new_capacity < required) {
		new_capacity = ((required + scTapeBlockSize - 1) / scTapeBlockSize) * scTapeBlockSize;
	}

	if (new_capacity > UINT32_MAX) {
		new_capacity = UINT32_MAX;
	}

	ByteBuffer new_tape(static_cast<uint32>(new_capacity));
	new_tape.InsertRaw(mTape.pData, mTape.GetSize());

	mTape = std::move(new_tape);
}

void SerializerIO::Save(ByteBuffer& out) const
{
	out.Create(mTape.GetSize());
	out.InsertRaw(mTape.pData, mTape.GetSize());
}

void SerializerIO::Load(const void* data, uint32 size)
{
	ByteBuffer tape(size);
	tape.InsertRaw(data, size);

	mFields.Clear();
	mTape = std::move(tape);

	SetMode(eMode::Read);
}

static ConfigPrimitive ReadConfigPrimitive(const ByteBuffer& tape, uint32 offset, const SerializedFieldLayout& layout)
{
	using eValueType = SerializedFieldLayout::eValueType;

	if (layout.ValueType == eValueType::Float) {
		if (layout.ElementSize == sizeof(float64)) {
			return ConfigPrimitive::FromValue(tape.Get<float64>(offset));
		}

		return ConfigPrimitive::FromValue(tape.Get<float32>(offset));
	}

	if (layout.ValueType == eValueType::Bool) {
		return ConfigPrimitive::FromValue(static_cast<int32>(tape.Get<uint8>(offset) != 0));
	}

	if (layout.ValueType == eValueType::Int) {
		switch (layout.ElementSize) {
		case 1:
			return ConfigPrimitive::FromValue(tape.Get<int8>(offset));
		case 2:
			return ConfigPrimitive::FromValue(tape.Get<int16>(offset));
		case 4:
			return ConfigPrimitive::FromValue(tape.Get<int32>(offset));
		default:
			return ConfigPrimitive::FromValue(tape.Get<int64>(offset));
		}
	}

	switch (layout.ElementSize) {
	case 1:
		return ConfigPrimitive::FromValue(tape.Get<uint8>(offset));
	case 2:
		return ConfigPrimitive::FromValue(tape.Get<uint16>(offset));
	case 4:
		return ConfigPrimitive::FromValue(tape.Get<uint32>(offset));
	default:
		return ConfigPrimitive::FromValue(tape.Get<uint64>(offset));
	}
}

static void ExportValueField(const ByteBuffer& tape, const SerializedField& field, ConfigEntry& entry)
{
	using eValueType = SerializedFieldLayout::eValueType;

	const SerializedFieldLayout& layout = field.Layout;

	if (layout.ValueType == eValueType::String) {
		ConfigEntry member(field.EntryName.Get());
		member.Set(String(tape.GetPtr<char>(field.TapeOffset + sizeof(uint32)), layout.ElementCount));
		entry.AddMember(std::move(member));
		return;
	}

	if (layout.ValueType == eValueType::Raw) {
		LogWarning("Serialized field '{}' has no config representation, skipping", field.EntryName.Get());
		return;
	}

	if (!layout.bIsArray) {
		ConfigEntry member(field.EntryName.Get());
		member.Set(ReadConfigPrimitive(tape, field.TapeOffset, layout));
		entry.AddMember(std::move(member));
		return;
	}

	ConfigPrimitive::ePrimitiveType array_type = ConfigPrimitive::ePrimitiveType::Int;

	if (layout.ValueType == eValueType::Float) {
		array_type = ConfigPrimitive::ePrimitiveType::Float;
	}

	ConfigEntry member = ConfigEntry::Array(field.EntryName.Get(), array_type);

	for (uint32 i = 0; i < layout.ElementCount; i++) {
		member.AppendValue(ReadConfigPrimitive(tape, field.TapeOffset + i * layout.ElementSize, layout));
	}

	entry.AddMember(std::move(member));
}

using SerializedFieldIterator = PagedArray<SerializedField>::ConstIterator;

static SerializedFieldIterator ExportFields(const ByteBuffer& tape, SerializedFieldIterator it, uint32 field_count,
											ConfigEntry& entry)
{
	uint32 consumed_count = 0;

	while (consumed_count < field_count) {
		const SerializedField& field = *it;

		++it;
		++consumed_count;

		if (field.Layout.ValueType != SerializedFieldLayout::eValueType::Object) {
			ExportValueField(tape, field, entry);
			continue;
		}

		ConfigEntry object_entry = ConfigEntry::Struct(field.EntryName.Get());

		it = ExportFields(tape, it, field.Layout.ElementCount, object_entry);
		consumed_count += field.Layout.ElementCount;

		entry.AddMember(std::move(object_entry));
	}

	return it;
}

void SerializerIO::ExportTo(ConfigEntry& entry) const { ExportFields(mTape, mFields.begin(), GetFieldCount(), entry); }

template <typename T>
static void StoreConfigValue(uint8* dst, T value)
{
	memcpy(dst, &value, sizeof(T));
}

template <typename T>
static T GetClampedConfigInt(const ConfigPrimitive& primitive)
{
	constexpr T min_value = std::numeric_limits<T>::min();
	constexpr T max_value = std::numeric_limits<T>::max();

	if (primitive.Type == ConfigPrimitive::ePrimitiveType::Float) {
		const float64 value = primitive.Get<float64>();

		if (!(value > static_cast<float64>(min_value))) {
			return min_value;
		}

		if (value >= static_cast<float64>(max_value)) {
			return max_value;
		}

		return static_cast<T>(value);
	}

	const int64 value = primitive.Get<int64>();

	if constexpr (std::is_signed_v<T>) {
		if (value < static_cast<int64>(min_value)) {
			return min_value;
		}

		if (value > static_cast<int64>(max_value)) {
			return max_value;
		}
	}
	else {
		if (value < 0) {
			return 0;
		}

		if (static_cast<uint64>(value) > static_cast<uint64>(max_value)) {
			return max_value;
		}
	}

	return static_cast<T>(value);
}

static void WriteConfigPrimitive(const ConfigPrimitive& primitive, uint8* dst, const SerializedFieldLayout& layout)
{
	using eValueType = SerializedFieldLayout::eValueType;

	if (layout.ValueType == eValueType::Float) {
		if (layout.ElementSize == sizeof(float64)) {
			StoreConfigValue(dst, primitive.Get<float64>());
		}
		else {
			StoreConfigValue(dst, primitive.Get<float32>());
		}
		return;
	}

	if (layout.ValueType == eValueType::Bool) {
		StoreConfigValue(dst, primitive.Get<int64>() != 0);
		return;
	}

	if (layout.ValueType == eValueType::Int) {
		switch (layout.ElementSize) {
		case 1:
			StoreConfigValue(dst, GetClampedConfigInt<int8>(primitive));
			break;
		case 2:
			StoreConfigValue(dst, GetClampedConfigInt<int16>(primitive));
			break;
		case 4:
			StoreConfigValue(dst, GetClampedConfigInt<int32>(primitive));
			break;
		default:
			StoreConfigValue(dst, GetClampedConfigInt<int64>(primitive));
			break;
		}
		return;
	}

	switch (layout.ElementSize) {
	case 1:
		StoreConfigValue(dst, GetClampedConfigInt<uint8>(primitive));
		break;
	case 2:
		StoreConfigValue(dst, GetClampedConfigInt<uint16>(primitive));
		break;
	case 4:
		StoreConfigValue(dst, GetClampedConfigInt<uint32>(primitive));
		break;
	default:
		StoreConfigValue(dst, GetClampedConfigInt<uint64>(primitive));
		break;
	}
}

static bool IsNumericPrimitive(const ConfigPrimitive& primitive)
{
	using ePrimitiveType = ConfigPrimitive::ePrimitiveType;

	return primitive.Type == ePrimitiveType::Int || primitive.Type == ePrimitiveType::Float;
}

void SerializerIO::ImportFrom(const ConfigEntry& entry)
{
	mpConfigSource = &entry;
	mpConfigFile = nullptr;
	SetMode(eMode::ReadConfig);
}

void SerializerIO::ImportFrom(const ConfigFile& file)
{
	mpConfigSource = nullptr;
	mpConfigFile = &file;
	SetMode(eMode::ReadConfig);
}

const ConfigEntry* SerializerIO::FindConfigMember(const char* name) const
{
	if (mpConfigSource != nullptr) {
		return mpConfigSource->GetMember(HashStr32(name));
	}

	if (mpConfigFile != nullptr) {
		return mpConfigFile->GetEntry(HashStr32(name));
	}

	return nullptr;
}

const ConfigEntry* SerializerIO::FindConfigObject(const char* name) const
{
	const ConfigEntry* member = FindConfigMember(name);

	if (member == nullptr) {
		return nullptr;
	}

	if (member->Type != ConfigPrimitive::ePrimitiveType::Struct || member->bIsArray) {
		LogWarning("Config member '{}' is not a struct", name);
		return nullptr;
	}

	return member;
}

bool SerializerIO::ReadConfigStringField(const char* name, const char*& out_data, uint32& out_length) const
{
	const ConfigEntry* member = FindConfigMember(name);

	if (member == nullptr) {
		return false;
	}

	if (member->bIsArray || member->Type != ConfigPrimitive::ePrimitiveType::String || member->mStringValue == nullptr) {
		LogWarning("Config member '{}' is not a string", name);
		return false;
	}

	out_data = member->mStringValue;
	out_length = static_cast<uint32>(strlen(member->mStringValue));

	return true;
}

bool SerializerIO::ReadConfigField(const char* name, void* value, const SerializedFieldLayout& layout) const
{
	if (layout.ValueType == SerializedFieldLayout::eValueType::Raw) {
		return false;
	}

	const ConfigEntry* member = FindConfigMember(name);

	if (member == nullptr) {
		return false;
	}

	uint8* dst = static_cast<uint8*>(value);

	if (!layout.bIsArray) {
		if (member->bIsArray || !IsNumericPrimitive(*member)) {
			LogWarning("Config member '{}' is not a number", name);
			return false;
		}

		WriteConfigPrimitive(*member, dst, layout);
		return true;
	}

	const PagedArray<ConfigPrimitive>& array_data = member->GetArrayData();

	uint32 min_count = layout.ElementCount;

	if (layout.MinElementCount > 0) {
		min_count = layout.MinElementCount;
	}

	if (!member->bIsArray || array_data.Size() < min_count) {
		LogWarning("Config member '{}' is not an array of at least {} values", name, min_count);
		return false;
	}

	const uint32 read_count = std::min(static_cast<uint32>(array_data.Size()), layout.ElementCount);

	for (uint32 i = 0; i < read_count; i++) {
		if (!IsNumericPrimitive(array_data[i])) {
			LogWarning("Config member '{}' has a non-number at index {}", name, i);
			return false;
		}
	}

	for (uint32 i = 0; i < read_count; i++) {
		WriteConfigPrimitive(array_data[i], dst + i * layout.ElementSize, layout);
	}

	const ConfigPrimitive fill = ConfigPrimitive::FromValue(layout.FillValue);

	for (uint32 i = read_count; i < layout.ElementCount; i++) {
		WriteConfigPrimitive(fill, dst + i * layout.ElementSize, layout);
	}

	return true;
}

static SerializedFieldIterator PrintFields(SerializedFieldIterator it, uint32 field_count, uint32 depth)
{
	uint32 consumed_count = 0;

	while (consumed_count < field_count) {
		const SerializedField& field = *it;

		++it;
		++consumed_count;

		LogInfo("{:{}}Field: {}, Offset: {}, Size: {}", "", depth * 4, field.EntryName.Get(), field.TapeOffset,
				field.Size);

		if (field.Layout.ValueType == SerializedFieldLayout::eValueType::Object) {
			it = PrintFields(it, field.Layout.ElementCount, depth + 1);
			consumed_count += field.Layout.ElementCount;
		}
	}

	return it;
}

void SerializerIO::PrintDebug() const { PrintFields(mFields.begin(), GetFieldCount(), 0); }


} // namespace fx
