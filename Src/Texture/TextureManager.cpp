#include "TextureManager.hpp"

#include <Engine.hpp>
#include <Math/Mat4.hpp>
#include <Object/Object.hpp>
#include <Renderer/Backend/DsLayoutBuilder.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/RenderBackend.hpp>

namespace fx {

const TextureID TextureID::Null = TextureID(UINT32_MAX);

void TextureManager::Create() { mTextureList.Init(scMaxTextures); }

TextureID TextureManager::NewTextureID()
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	Image* texture = mTextureList.NewItem(&index);
	texture->ID = TextureID(index);

	return texture->ID;
}

Image* TextureManager::NewTexture()
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 index;
	Image* image = mTextureList.NewItem(&index);

	// Temporary, this should remove the least used textures.
	Assert(image != nullptr);

	image->ID = TextureID { index };

	return image;
}

Image* TextureManager::GetTexture(TextureID id)
{
	if (id.IsInvalid()) {
		return nullptr;
	}

	return mTextureList.GetItem(id.GetID());
}


void TextureManager::DestroyTexture(TextureID id)
{
	std::lock_guard<std::mutex> guard(mInUse);

	if (id.IsInvalid()) {
		return;
	}

	// If the ID is passed in directly from an object (as it probably will be in some places), then we want to make sure
	// we dont invalidate our ID before freeing it.
	TextureID id_copy = id;

	// Delete the object id at the definition
	mTextureList.GetItem(id.GetID())->ID.Invalidate();

	// Free the object from the list
	mTextureList.FreeItem(id_copy.GetID());

	// Invalidate the passed ID
	id.Invalidate();
}

void TextureManager::ReleaseAllTextures()
{
	std::lock_guard<std::mutex> guard(mInUse);

	uint32 capacity = mTextureList.Capacity;

	for (uint32 i = 0; i < capacity; i++) {
		if (!mTextureList.SlotsInUse.Get(i)) {
			continue;
		}

		mTextureList.FreeItem(i);
	}
}

void TextureManager::Destroy() {}

} // namespace fx
