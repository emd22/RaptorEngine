#include "RenderList.hpp"

#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>

namespace fx::renderer {


void RenderList::AddObject(ePipelineName pl_name, const ObjectID id)
{
	if (!mSections.IsInited()) {
		mSections.InitSize(scNumPipelines);
	}

	AssertLess(static_cast<uint32>(pl_name), mSections.Capacity);

	RenderListSection& section = mSections[static_cast<uint32>(pl_name)];

	section.Objects.Insert(id);
}

void RenderList::InvalidateObject(const ObjectID id)
{
	for (uint32 section_index = 0; section_index < mSections.Size; section_index++) {
		RenderListSection& section = mSections[section_index];

		for (ObjectID& found_id : section.Objects) {
			if (found_id == id) {
				found_id.Invalidate();
				break;
			}
		}
	}
}

void RenderList::ClearSection(ePipelineName section_name)
{
	DebugAssert(static_cast<uint32>(pl_name) < mSections.Capacity);
	RenderListSection& section = mSections[static_cast<uint32>(section_name)];

	section.Objects.Clear();
}

uint32 RenderList::GetItemCount() const
{
	uint32 count = 0;

	for (uint32 section_index = 0; section_index < mSections.Size; section_index++) {
		const RenderListSection& section = mSections[section_index];
		count += section.Objects.Size;
	}

	return count;
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
