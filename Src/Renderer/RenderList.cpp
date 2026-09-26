#include "RenderList.hpp"

#include <Object/ObjectID.hpp>
#include <Object/ObjectManager.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/PipelineCache.hpp>

namespace fx::renderer {


void RenderList::AddObject(PipelineHandle pipeline, const ObjectID id) { GetSection(pipeline).Objects.Insert(id); }

void RenderList::InvalidateObject(const ObjectID id)
{
	for (std::unique_ptr<RenderListSection>& section : mSections) {
		if (section == nullptr) {
			continue;
		}

		for (ObjectID& found_id : section->Objects) {
			if (found_id == id) {
				found_id.Invalidate();
				break;
			}
		}
	}
}

void RenderList::ClearSection(PipelineHandle pipeline) { GetSection(pipeline).Objects.Clear(); }

uint32 RenderList::GetItemCount() const
{
	uint32 count = 0;

	for (const std::unique_ptr<RenderListSection>& section : mSections) {
		if (section != nullptr) {
			count += section->Objects.Size;
		}
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

	for (uint32 section_index = 0; section_index < mSections.size(); section_index++) {
		const RenderListSection* section = mSections[section_index].get();

		if (section == nullptr) {
			continue;
		}

		for (uint32 object_index = 0; object_index < section->Objects.Size; object_index++) {
			if (section->Objects[object_index] == id) {
				++count;
				LogInfo(LC_RENDER, "Object '{}' found in pipeline '{}'", object->Name.Get(),
						gPipelineCache->GetDebugName(PipelineHandle { section_index }));
			}
		}
	}

	return count;
}

RenderListSection& RenderList::GetSection(PipelineHandle pipeline)
{
	Assert(pipeline.IsValid());

	if (pipeline.Index >= mSections.size()) {
		mSections.resize(pipeline.Index + 1);
	}

	std::unique_ptr<RenderListSection>& section = mSections[pipeline.Index];

	if (section == nullptr) {
		section = std::make_unique<RenderListSection>();
	}

	return *section;
}


} // namespace fx::renderer
