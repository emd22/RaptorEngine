#include "RenderList.hpp"

#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>

namespace fx::renderer {

static constexpr uint32 scMaxRenderable = 512;

uint32 RenderList::Add(ePipelineName pl_name, const ObjectID id)
{
	if (!mSections.IsInited()) {
		mSections.InitSize(scNumPipelines);
	}

	AssertLess(static_cast<uint32>(pl_name), mSections.Capacity);

	RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

	if (!section.InUse.IsInited()) {
		section.InUse.InitZero(scMaxRenderable);
	}


	uint32 index = section.InUse.FindNextFreeBit();

	if (index >= section.Objects.Size) {
		section.Objects.Insert(id);
	}
	else {
		section.Objects[index] = id;
	}

	section.InUse.Set(index);

	return index;
}

void RenderList::Remove(ePipelineName pl_name, const ObjectID id)
{
	Assert(static_cast<uint32>(pl_name) < mSections.Capacity);
	RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

	for (uint32 object_index = 0; object_index < section.Objects.Size; object_index++) {
		if (section.Objects[object_index] == id) {
			section.InUse.Unset(object_index);
		}
	}
}

uint32 RenderList::GetObjectIndex(ePipelineName pl_name, const ObjectID id) const
{
	if (static_cast<uint32>(pl_name) >= mSections.Capacity) {
		// Sections have not been inited yet, so it doesn't exist
		return scNotFound;
	}

	const RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

	for (uint32 object_index = 0; object_index < section.Objects.Size; object_index++) {
		if (section.Objects[object_index] == id) {
			return object_index;
		}
	}

	return scNotFound;
}

void RenderList::RemoveAllOfObject(const ObjectID id)
{
	for (uint32 si = 0; si < scNumPipelines; si++) {
		Remove(static_cast<ePipelineName>(si), id);
	}
}

int32 RenderList::CheckForObjectDuplicates(const ObjectID id) const
{
	int32 count = 0;

	Object* object = gObjectManager->GetObject(id);
	if (object == nullptr) {
		return 0;
	}

	for (uint32 section_index = 0; section_index < mSections.Size; section_index++) {
		const RenderListSection& section = mSections[section_index];

		for (uint32 object_index = 0; object_index < section.Objects.Size; object_index++) {
			if (section.Objects[object_index] == id) {
				++count;
				LogInfo(LC_RENDER, "Object '{}' found in pipeline '{}'", object->Name.Get(),
						PipelineNameUtil::GetName(static_cast<ePipelineName>(section_index)));
			}
		}
	}

	return count;
}

RenderListSection& RenderList::GetSection(ePipelineName pl_name)
{
	if (!mSections.IsInited()) {
		mSections.InitSize(scNumPipelines);
	}

	return mSections[static_cast<uint32>(pl_name)];
}


} // namespace fx::renderer
