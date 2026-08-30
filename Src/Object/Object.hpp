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
};

FxEnumFlags(eObjectFlags);


class PrimitiveMesh;

class Object : public Entity
{
	friend class AssetManager;

public:
	static constexpr eEntityType scEntityType = eEntityType::Object;

public:
	Object() = default;
	Object(const ObjectID& id);

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

	void OnAttached(World* scene) override;

	void PhysicsCreatePrimitive(physics::ePrimitiveType primitive_type, const Vec3f& dimensions,
								physics::eMotionType motion_type, const physics::BodyProps& physics_properties);

	void PhysicsCreateMesh(Ref<PrimitiveMesh> physics_mesh, physics::eMotionType motion_type,
						   const physics::BodyProps& physics_properties);

	void PrintDebug() const;

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

	void AttachScript(const Ref<script::FoxScript>& script);
	void LoadScript(const String& path);

	/////////////////////////////////////
	// Material
	/////////////////////////////////////

	/**
	 * @brief Returns the ID of the material assigned to this object.
	 */
	FX_FORCE_INLINE const MaterialID& GetMaterialID() const { return mMaterialID; };

	/**
	 * @brief Sets the material ID for the object.
	 */
	FX_FORCE_INLINE void SetMaterialID(const MaterialID& id) { mMaterialID = id; };

	/////////////////////////////////////
	// Physics
	/////////////////////////////////////

	FX_FORCE_INLINE void SetPhysicsID(physics::BodyID phys_id) { PhysicsID = phys_id; }
	FX_FORCE_INLINE physics::BodyID GetPhysicsID() const { return PhysicsID; }
	void SetPhysicsEnabled(bool enabled);
	FX_FORCE_INLINE bool GetPhysicsEnabled() { return (Flags & eObjectFlags::PhysicsEnabled) != 0; }

	FX_FORCE_INLINE void SetObjectLayer(eObjectLayer layer) { mObjectLayer = layer; }
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

	FX_FORCE_INLINE bool IsShadowCaster() const { return (Flags & eObjectFlags::ShadowCaster) != 0; }

	void SetUnlit(const bool value);
	FX_FORCE_INLINE bool IsUnlit() const { return (Flags & eObjectFlags::Unlit) != 0; }

	FX_FORCE_INLINE bool IsSkinned() const { return (pMesh != nullptr) && pMesh->VertexList.IsSkinned(); }

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
	void SetScriptVars();

	void SyncObjectWithPhysics(physics::Body* phys);

public:
	Ref<PrimitiveMesh> pMesh { nullptr };
	physics::BodyID PhysicsID = physics::BodyID::scNull;

	eObjectTag Tags = eObjectTag::None;

	Ref<Skeleton> pSkeleton { nullptr };
	SizedArray<Animation> Animations;
	Animation* pCurrentAnimation = nullptr;
	float32 AnimationTime = 0.0f;

	World* pScene = nullptr;
	ObjectID ParentID = ObjectID::scNull;
	PagedArray<ObjectID> AttachedNodes;

	BoundingBox Bounds { Vec3f::sZero, Vec3f::sZero };

	Ref<script::FoxScript> pScript { nullptr };

	MaterialID mMaterialID = MaterialID::scNull;

private:
	/// Object slots allocated following this object. Used by other instances of this object.
	uint16 mInstanceSlots = 0;
	uint16 mInstanceSlotsInUse = 0;

	TileIndex mTileIndex = TileIndexNull;

	eObjectFlags Flags = eObjectFlags::None;
	eObjectLayer mObjectLayer = eObjectLayer::WorldLayer;

	friend class WorldGrid;
};


FX_VALIDATE_ENTITY_TYPE(Object);

} // namespace fx
