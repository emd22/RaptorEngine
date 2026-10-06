#pragma once

#include "Physics/Body.hpp"


#include <Asset/Animation.hpp>
#include <Core/Name.hpp>
#include <Core/PagedArray.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/TSRef.hpp>
#include <Entity.hpp>
#include <Material/MaterialID.hpp>
#include <Math/BoundingBox.hpp>
#include <Math/Frustum.hpp>
#include <Math/OrientedBoundingBox.hpp>
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
	/// A blockout brush that marks out a light probe volume instead of level geometry
	ProbeVolume = (1 << 2),
	ReflectionProbe = (1 << 3),
	Bleeds = (1 << 4),
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
	SharedMesh = (1 << 7),
	/// The object has a mesh to draw
	HasMesh = (1 << 8),
	/// The mesh of the object is skinned
	Skinned = (1 << 9),
};

FxEnumFlags(eObjectFlags);


class PrimitiveMesh;
struct SkeletonCloneMap;

/**
 * @brief The nodes attached to an object. The list is kept by Rust, with the rest of the object's record.
 */
class ObjectChildren
{
public:
	explicit ObjectChildren(RxObjectCore* core) : mpCore(core) {}

	uint32 Size() const
	{
		size_t count = 0;
		rx_object_children(mpCore, &count);
		return static_cast<uint32>(count);
	}

	bool IsEmpty() const { return Size() == 0; }

	void Insert(const ObjectID& id) { rx_object_add_child(mpCore, id.GetID()); }
	void Clear() { rx_object_clear_children(mpCore); }

	ObjectID& operator[](uint32 index) const { return Data()[index]; }

	ObjectID* begin() const { return Data(); }
	ObjectID* end() const { return Data() + Size(); }

private:
	ObjectID* Data() const
	{
		size_t count = 0;
		return reinterpret_cast<ObjectID*>(const_cast<uint32*>(rx_object_children(mpCore, &count)));
	}

private:
	RxObjectCore* mpCore;
};

class Object final : public Entity
{
	friend class AssetManager;

public:
	static constexpr eEntityType scEntityType = eEntityType::Object;

public:
	/// An object whose records are kept by the object store, which outlives it
	Object(RxEntityCore* entity_core, RxObjectCore* object_core)
		: Entity(entity_core), mpObjectCore(object_core)
	{
	}

	Object* CloneWithOwnSkeleton(const std::string& name) const;

	void Create(const Ref<PrimitiveMesh>& mesh, const MaterialID& material);

	/**
	 * @brief Gives the object the mesh it draws. The record that the renderer culls objects from learns of it.
	 */
	void SetMesh(const Ref<PrimitiveMesh>& mesh);

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
	void SetScale(const float scale) override;

	void OnAttached(World* scene) override;

	void PrintDebug() const;

	float32 GetDirectionScale(const Vec3f& direction);

	// XXX: TEMP
	void UpdateAnimation();


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

	/// Sets the material of an object that has just been made, before anything knows of it
	void SetMaterialDirect(const MaterialID& id) { mMaterialID = id; }

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
	 * @brief Turns this object into a light probe volume marker, or back into normal geometry.
	 */
	void SetProbeVolume(bool value);

	/// True if this object marks out a probe volume rather than being level geometry
	FX_FORCE_INLINE bool IsProbeVolume() const { return HasTags(eObjectTag::ProbeVolume); }

	void SetReflectionProbe(bool value);
	FX_FORCE_INLINE bool Bleeds() const { return HasTags(eObjectTag::Bleeds); }
	FX_FORCE_INLINE bool IsReflectionProbe() const { return HasTags(eObjectTag::ReflectionProbe); }

	float32 RaycastBounds(const Vec3f& origin, const Vec3f& direction, Vec3f& out_face);

	bool ContainsPoint(const Vec3f& point);

	void SetBounds(const AABB& bounds);

	FX_FORCE_INLINE OBB GetWorldOBB() { return OBB::FromLocalBounds(Bounds, GetWorldMatrix()); }

	bool CanBeFrustumCulled() const;

	bool IsOutsideFrustum(const Frustum& frustum, uint32 plane_mask = scFrustumAllPlanes);

	/**
	 * @brief True if light probe bakes should include this object. Objects on the player layer (the view model) are
	 * never part of the level, so they are always left out.
	 */
	FX_FORCE_INLINE bool IsProbeVisible() const { return rx_object_is_probe_visible(mpObjectCore) != 0; }

	void SetUnlit(const bool value);
	FX_FORCE_INLINE bool IsUnlit() const { return (Flags & eObjectFlags::Unlit) != 0; }

	FX_FORCE_INLINE bool IsSkinned() const { return (pMesh != nullptr) && pMesh->IsSkinned(); }

	/// A skinned mesh can only be drawn once its pose is in the bone buffer this frame. Otherwise the shader would read
	/// matrices outside of what was written, or past the end of the buffer.
	FX_FORCE_INLINE bool HasBonesForDraw() const { return !IsSkinned() || BoneBufferBase != Skeleton::scNoBones; }

	void SetCullable(bool value);
	FX_FORCE_INLINE bool IsCullable() const { return !HasFlag(Flags, eObjectFlags::DisableCulling); }

	FX_FORCE_INLINE eObjectFlags GetFlags() const { return Flags; }

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

	Object* CloneNode(const std::string& name, SkeletonCloneMap& skeletons) const;

private:
	RxObjectCore* mpObjectCore = nullptr;

public:
	Ref<PrimitiveMesh> pMesh { nullptr };
	physics::BodyID& PhysicsID = *reinterpret_cast<physics::BodyID*>(&mpObjectCore->physics_id);

	eObjectTag& Tags = *reinterpret_cast<eObjectTag*>(&mpObjectCore->tags);

	Ref<Skeleton> pSkeleton { nullptr };
	uint32& BoneBufferBase = mpObjectCore->bone_buffer_base;

	ObjectID& ParentID = *reinterpret_cast<ObjectID*>(&mpObjectCore->parent_id);
	ObjectChildren AttachedNodes { mpObjectCore };

	AABB& Bounds = *reinterpret_cast<AABB*>(mpObjectCore->bounds_min);

	std::atomic_bool bIsAddedToWorld = false;


private:
	MaterialID& mMaterialID = *reinterpret_cast<MaterialID*>(&mpObjectCore->material_id);
	/// Object slots allocated following this object. Used by other instances of this object.
	uint16& mInstanceSlots = mpObjectCore->instance_slots;
	uint16& mInstanceSlotsInUse = mpObjectCore->instance_slots_in_use;

	eObjectFlags& Flags = *reinterpret_cast<eObjectFlags*>(&mpObjectCore->flags);
	eObjectLayer& mObjectLayer = *reinterpret_cast<eObjectLayer*>(&mpObjectCore->layer);

};


FX_VALIDATE_ENTITY_TYPE(Object);

} // namespace fx
