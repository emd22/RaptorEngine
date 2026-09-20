#pragma once

#include "Physics/Body.hpp"

// #include <ThirdParty/Jolt/Jolt.h>
// #include <ThirdParty/Jolt/Physics/Body/Body.h>
// #include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Asset/Animation.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/TSRef.hpp>
#include <Entity.hpp>
#include <FoxScript/FoxScript.hpp>
#include <Material/MaterialID.hpp>
#include <Math/BoundingBox.hpp>
#include <WorldGrid.hpp>


namespace fx {

namespace renderer {
class Pipeline;
};

enum class eObjectTag : uint32
{
	None = 0,
	Blockout = (1 << 0),
	/// Locks rotation and transformation for scripts
	LockTransform = (1 << 1),
};

FxEnumFlags(eObjectTag);

enum class eObjectFlags : uint16
{
	None = 0,
	ReadyToRender = (1 << 0),
	PhysicsEnabled = (1 << 1),
	IsInstance = (1 << 2),
	ShadowCaster = (1 << 3),
	Unlit = (1 << 4),
	DisableCulling = (1 << 5),
	/// Left out of light probe bakes (capture, capture shadows and probe placement). See Object::IsProbeVisible().
	NotProbeVisible = (1 << 6),
};

FxEnumFlags(eObjectFlags);


class PrimitiveMesh;

class Object final : public Entity
{
	friend class AssetManager;

public:
	static constexpr eEntityType scEntityType = eEntityType::Object;

public:
	Object() = default;
	Object(const ObjectID id);
	Object(const ObjectID id, const MaterialID material);

	void MakeInstanceOf(const ObjectID& source);

	void Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material);

	/**
	 * @brief Render only the primitive(s) for the objects. Does not bind material or other object data.
	 */
	void RenderPrimitive(const renderer::CommandBuffer& cmd);
	void RenderShallow(const Camera& camera, renderer::Pipeline* alt_pipeline = nullptr);

	bool CheckIfReady(bool require_material);
	void AttachObject(const ObjectID& object);

	void Update();

	void SetPosition(const Vec3f& position) override;
	void SetRotation(const Quat& rotation) override;

	void OnAttached(World* scene) override;

	void PrintDebug() const;

	float32 GetDirectionScale(const Vec3f& direction);

	// XXX: TEMP
	void UpdateAnimation();

	/**
	 * @brief Reserve `num_instances` amount of future instances in the object manager.
	 * @note This may update the object id if there are not enough free slots following this object.
	 */
	void ReserveInstances(uint32 num_instances);

	/////////////////////////////////////
	// Script
	/////////////////////////////////////


	/////////////////////////////////////
	// Material
	/////////////////////////////////////

	/**
	 * @brief Returns the ID of the material assigned to this object.
	 */
	FX_FORCE_INLINE const MaterialID& GetMaterialID() const { return mMaterialID; };

	void SetMaterial(const MaterialID& id);

	/////////////////////////////////////
	// Physics
	/////////////////////////////////////

	/**
	 * @brief Links a collider to this object. Allows a collider to recognize the object it was connected to, and vice
	 * versa.
	 */
	void AttachCollider(physics::Body* body);

	FX_FORCE_INLINE void SetPhysicsID(physics::BodyID phys_id) { PhysicsID = phys_id; }
	FX_FORCE_INLINE physics::BodyID GetPhysicsID() const { return PhysicsID; }
	void SetPhysicsEnabled(bool enabled);
	FX_FORCE_INLINE bool GetPhysicsEnabled() { return (Flags & eObjectFlags::PhysicsEnabled) != 0; }

	void SetObjectLayer(eObjectLayer layer);
	FX_FORCE_INLINE eObjectLayer GetObjectLayer() const { return mObjectLayer; }

	/////////////////////////////////////
	// Render options
	/////////////////////////////////////

	FX_FORCE_INLINE void SetShadowCaster(const bool value)
	{
		if (value) {
			Flags |= eObjectFlags::ShadowCaster;
		}
		else {
			Flags &= ~(eObjectFlags::ShadowCaster);
		}
	}

	FX_FORCE_INLINE bool HasTags(eObjectTag tag) const { return HasFlag(Tags, tag); }
	FX_FORCE_INLINE void SetTag(eObjectTag tag) { SetFlag(Tags, tag); }
	FX_FORCE_INLINE void ClearTag(eObjectTag tag) { ClearFlag(Tags, tag); }
	FX_FORCE_INLINE void SetTags(eObjectTag tags) { Tags = tags; }

	FX_FORCE_INLINE bool IsShadowCaster() const { return (Flags & eObjectFlags::ShadowCaster) != 0; }

	/**
	 * @brief Sets whether light probe bakes can see this object and everything attached to it.
	 */
	void SetProbeVisible(bool value);

	/**
	 * @brief True if light probe bakes should include this object. Objects on the player layer (the view model) are
	 * never part of the level, so they are always left out.
	 */
	FX_FORCE_INLINE bool IsProbeVisible() const
	{
		return !HasFlag(Flags, eObjectFlags::NotProbeVisible) && (mObjectLayer != eObjectLayer::PlayerLayer);
	}

	void SetUnlit(const bool value);
	FX_FORCE_INLINE bool IsUnlit() const { return (Flags & eObjectFlags::Unlit) != 0; }

	FX_FORCE_INLINE bool IsSkinned() const { return (pMesh != nullptr) && pMesh->VertexList.IsSkinned(); }

	/// A skinned mesh can only be drawn once its pose is in the bone buffer this frame. Otherwise the shader would read
	/// matrices outside of what was written, or past the end of the buffer.
	FX_FORCE_INLINE bool HasBonesForDraw() const { return !IsSkinned() || BoneBufferBase != Skeleton::scNoBones; }

	void SetCullable(bool value);
	FX_FORCE_INLINE bool IsCullable() const { return !HasFlag(Flags, eObjectFlags::DisableCulling); }

	void Destroy();
	~Object() override { Destroy(); }

protected:
	/**
	 * @brief Finalizes any changes that have been made to the object before it was finished loading.
	 */
	void FinalizeWhenReady();

private:
	/**
	 * @brief Render the bare model for the object. Note that there are no `CheckIfReady` checks in here as they are
	 * done by RenderShallow et. al!
	 */
	void RenderMesh(renderer::Pipeline* pipeline);

	void SyncObjectWithPhysics(physics::Body* phys);

public:
	Ref<PrimitiveMesh> pMesh { nullptr };
	physics::BodyID PhysicsID = physics::BodyID::scNull;

	eObjectTag Tags = eObjectTag::None;

	Ref<Skeleton> pSkeleton { nullptr };
	uint32 BoneBufferBase = Skeleton::scNoBones;

	ObjectID ParentID = ObjectID::scNull;
	PagedArray<ObjectID> AttachedNodes;

	AABB Bounds { Vec3f::sZero, Vec3f::sZero };

	Ref<script::FoxScript> pScript { nullptr };

	std::atomic_bool bIsAddedToWorld = false;


private:
	MaterialID mMaterialID = MaterialID::scNull;
	/// Object slots allocated following this object. Used by other instances of this object.
	uint16 mInstanceSlots = 0;
	uint16 mInstanceSlotsInUse = 0;

	TileIndex mTileIndex = TileIndexNull;
	/// Number of tiles (width, height) this object spans starting at mTileIndex's XY.
	Vec2u mTileSpan = Vec2u(1, 1);

	eObjectFlags Flags = eObjectFlags::None;
	eObjectLayer mObjectLayer = eObjectLayer::WorldLayer;

	friend class WorldGrid;
};


FX_VALIDATE_ENTITY_TYPE(Object);

} // namespace fx
