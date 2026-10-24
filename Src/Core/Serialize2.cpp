#include "Serialize2.hpp"

#include <Core/Log.hpp>


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

void SerializerIO::WriteField(const char* name, const void* value, uint32 size)
{
	ReserveTape(size);

	mFields.Insert(SerializedField { Name(name), mTape.GetSize(), size });
	mTape.InsertRaw(value, size);
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

void SerializerIO::PrintDebug() const
{
	for (const SerializedField& field : mFields) {
		LogInfo("Field: {}, Offset: {}, Size: {}", field.EntryName.Get(), field.TapeOffset, field.Size);
	}
}


} // namespace fx
