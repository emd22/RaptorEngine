/*
 * File:        BaseOperation.hpp
 * Author:      emd22
 * Created:     16/10/2026
 * Description: Base classes for edit operations
 */


#pragma once

#include <Color.hpp>
#include <Core/Name.hpp>
#include <Core/Ref.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Material/MaterialID.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Patch.hpp>
#include <Renderer/LightID.hpp>

namespace fx {

class Object;
class LightSpot;
class LightSnapshot;

namespace editor {

class EditorSelection;

/////////////////////////////////////
// Base Edit Operations
/////////////////////////////////////


class EditOperation
{
public:
	explicit EditOperation(int32 group_size = 1) : GroupSize(group_size) {}
	virtual ~EditOperation() = default;

	/// Applies the operation. Objects that it destroys are removed from `selection` first.
	virtual void Execute(EditorSelection& selection) = 0;

	/// Reverts the operation. Objects that it destroys are removed from `selection` first.
	virtual void Undo(EditorSelection& selection) = 0;

	virtual void Redo(EditorSelection& selection) { Execute(selection); }

	/// True if applying or reverting the operation creates or destroys objects
	virtual bool ChangesObjects() const { return false; }

public:
	/// The size of the operation group this is in. For example, when moving 10 objects, there will be 10 operations(one
	/// for each event) making the GroupSize = 10.
	int32 GroupSize;

	uint64 Revision = 0;
};

class ObjectEditOperation : public EditOperation
{
public:
	void Retarget(Object* object);

protected:
	ObjectEditOperation(Object* object, int32 group_size);

	Object* ResolveTarget() const;

private:
	Object* mpObject = nullptr;
	ObjectID mObjectID = ObjectID::scNull;
};

/**
 * @brief A snapshot of the current object. Used for rolling back large or destructive operations.
 */
struct ObjectSnapshot
{
	Vec3f Position = Vec3f::sZero;
	MaterialID Material = MaterialID::scNull;
	Quat Rotation = Quat::scIdentity;
	Name ObjectName;

	// TODO: replace the below with flags.

	/// So that undoing the delete of a probe volume brush brings back a probe volume, not solid geometry
	bool bIsProbeVolume = false;
	bool bIsReflectionProbe = false;
	bool bIsSpawn = false;
	bool bIsTrigger = false;
	bool bIsNvBrush = false;
	bool bIsDBound = false;
	bool bIsDecal = false;
	bool bIsDynamic = false;

	String ScriptPath;

	Vec3f EnterDirection = Vec3f::sZero;

	/// So that undoing the delete of a placed subview brings its brushes back as that subview
	uint32 SubviewInstance = 0;
	uint32 SubviewMember = 0;

	std::shared_ptr<const Patch> pPatch;

	/// Records everything needed to bring `object` back. `material` is passed in as a selected object wears the
	/// selection material instead of its own.
	static ObjectSnapshot Capture(const Object& object, MaterialID material);
};


/////////////////////////////////////
// Light Base Operations
/////////////////////////////////////


struct LightSnapshot
{
	Name LightName;
	Vec3f Position = Vec3f::sZero;
	Vec3f Direction = Vec3f::sForward;
	float32 Radius = 5.0f;

	float32 InnerAngle = 0.0f;
	float32 OuterAngle = 0.0f;

	bool bCastShadows = true;

	Color Colour = Color::sWhite;
	float32 Intensity = 100000.0f;

	static LightSnapshot Capture(const LightSpot& light);
};


class LightEditOperation : public EditOperation
{
protected:
	LightEditOperation(LightSpot* light, int32 group_size);

	LightSpot* ResolveLight() const;

	void Restore(const LightSnapshot& snapshot);
	void Destroy();

protected:
	LightSpot* mpLight = nullptr;
	LightID mLightID = LightID::scNull;
};


} // namespace editor


} // namespace fx
