/*
 * File:        Blockout.hpp
 * Author:      emd22
 * Created:     29/08/2026
 * Description: Blockout
 */

#pragma once

#include <Asset/ConfigFile.hpp>
#include <Brush.hpp>
#include <BrushEntry.hpp>
#include <Core/FreeArray.hpp>
#include <Core/HashMap.hpp>
#include <Core/Name.hpp>
#include <Core/Ref.hpp>
#include <Core/SizedArray.hpp>
#include <Core/StackArray.hpp>
#include <Core/String.hpp>
#include <Material/MaterialID.hpp>
#include <Material/MaterialLibrary.hpp>
#include <Math/BBox.hpp>
#include <Math/Mat4.hpp>
#include <Math/Quat.hpp>
#include <Math/Vec3.hpp>
#include <Object/ObjectID.hpp>
#include <Physics/BodyID.hpp>
#include <Renderer/ProbePlacement.hpp>
#include <string>
#include <vector>

namespace fx {
class World;
class Object;
class CompiledMap;
class PrimitiveMesh;

namespace physics {
enum class eMotionType;
}

/**
 * @brief A change to the texture on one face of a brush
 */
enum class eFaceTextureEdit : uint32
{
	/// Adds the amount's X and Y to the offset
	Shift = 0,
	/// Multiplies the scale by the amount's X and Y
	Scale,
	/// Adds the amount's X to the rotation, in degrees
	Rotate,
	/// Resets the offset, scale and rotation
	Reset,
};

enum class ePrototypeLoadStatus
{
	/// The prototype or compiled map was successfully loaded
	Success,
	/// There were errors when loading (corrupt prototype, missing files)
	HasErrors,
	/// Files are missing when requesting a compiled map
	NotCompiled,
};

class Blockout
{
public:
	static constexpr float32 scMinThickness = 0.1f;

public:
	Blockout();

	void Create(World* world);

	/**
	 * @brief Loads a level from either its prototype (.prx) or its compiled map (.fxmap). A prototype loads as
	 * editable brushes. A compiled map loads the geometry the map compiler made, plus everything of its prototype
	 * that the compiler leaves alone (lights, models, dynamic and data brushes); if it is missing or older than the
	 * prototype, the prototype is loaded instead.
	 */
	ePrototypeLoadStatus Load(const String& path);

	/**
	 * @brief Saves the prototype. A compiled map path is saved to the prototype next to it.
	 */
	void Save(const String& path);

	/**
	 * @brief Moves the face facing along `face_normal` out by `distance` (in by a negative distance), keeping the rest
	 * of the brush where it is. The brush is kept at least scMinBlockoutThickness thick behind the face.
	 */
	bool MoveFace(Object* object, const Vec3f face_normal, float32 distance);

	/**
	 * @brief Rebuilds a blockout from a set of planes, e.g. to undo an edit. Anything the planes have in common with
	 * the old brush stays where it is, even though the object may have to move for that.
	 */
	bool SetBrushPlanes(Object* object, const Brush::PlaneList& planes);

	/**
	 * @brief Works out how a blockout splits along the plane through `point_a` and `point_b` that is perpendicular to
	 * the face they were drawn on. Everything is in world space.
	 * @param out_kept The blockout's brush after the split
	 * @param out_split The piece that splits off, for a new blockout at `out_split_position`
	 */
	bool GetClipPieces(Object* object, const Vec3f point_a, const Vec3f point_b, const Vec3f face_normal,
					   Brush::PlaneList& out_kept, Brush::PlaneList& out_split, Vec3f& out_split_position);

	bool GetSubtractPieces(Object* target, Object* cutter, std::vector<Brush::PlaneList>& out_pieces,
						   std::vector<Vec3f>& out_positions);

	void RefreshCappedPatches();


	/**
	 * @brief Returns the brush for a new blockout filling `min` to `max` in world space, with its textures lined up
	 * with the world grid
	 * @param out_position Where the blockout goes
	 */
	Brush MakeWorldBox(const Vec3f min, const Vec3f max, Vec3f& out_position) const;

	/**
	 * @brief Shows a see-through preview of a brush on an object with the given transform
	 */
	void ShowPreview(const Vec3f position, const Quat rotation, const Brush& brush);
	void HidePreview();

	/**
	 * @brief Returns the planes of the object's brush with the texture on one face changed, without applying them
	 */
	bool GetFaceTextureEdit(Object* object, const Vec3f face_normal, eFaceTextureEdit edit, const Vec2f& amount,
							Brush::PlaneList& out_planes);

	/**
	 * @brief Finds the nearest blockout that a world space ray hits
	 * @param direction The direction of the ray, with the length of how far it reaches
	 * @param out_face_normal The normal of the face that was hit, in the blockout's local space
	 */
	Object* RaycastBlockout(const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal);

	/**
	 * @brief Finds the face of a blockout that a world space ray hits
	 * @param out_face_normal The normal of the face that was hit, in the object's local space
	 * @param out_point Where the face was hit, in world space
	 */
	bool RaycastFace(Object* object, const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal,
					 Vec3f* out_point = nullptr);

	void ReloadSingleObject(Object* object);

	void RebuildObject(Object* object);

	/**
	 * @brief Creates a new blockout objectC
	 */
	Object* NewObject(const Vec3f position, MaterialID material = MaterialID::scNull);

	Object* DupeObject(Object* object);

	/// A name for a new brush that no other object has
	std::string MakeUniqueBrushName() const;

	/**
	 * @brief Recreates a destroyed blockout object from a snapshot (undo of Delete).
	 * Falls back to a unique name if the original name is taken.
	 */
	Object* RestoreObject(const Vec3f position, const Brush::PlaneList& planes, MaterialID material,
						  const Quat rotation, const Name& name, bool is_dynamic = false, bool is_decal = false);

	Object* RestorePatch(const Vec3f position, const Patch& patch, MaterialID material, const Quat rotation,
						 const Name& name);

	bool SetPatch(Object* object, Patch&& patch);

	bool IsDynamic(const Object* object) const;

	MaterialID GetFaceMaterial(const String& name) const;

	bool IsFaceNode(const Object* object) const;

	const String& GetMaterialName(MaterialLibraryID id) const { return mMaterials.GetName(id); }

	bool HasBrush(const Object* object);

	bool ContainsPoint(Object* object, const Vec3f point);

	/**
	 * @brief Clears the prototype of all brushes and objects, and creates the basis for a new prototype.
	 */
	void ResetToTemplate();

	/**
	 * @brief Rebuilds a brush's physics body as dynamic (and active) or static, keeping its mesh and transform.
	 * @returns False if the object isn't a brush.
	 */
	bool SetDynamic(Object* object, bool dynamic);

	void DestroyObject(Object* object);

	/**
	 * @brief Rebuilds the mesh of every decal whose volume or receiving brushes changed since it was last built
	 */
	void RefreshDecals(bool force = false);

	void RaycastDecalSurfaces(const Vec3f origin, const Vec3f direction, float32 max_distance,
							  std::vector<float32>& out_distances);

	const MaterialLibrary& GetMaterialLibrary() const { return mMaterials; }

	MaterialID GetMaterialForID(MaterialLibraryID id) const;
	MaterialLibraryID GetIDForMaterial(const MaterialID& material) const;
	MaterialID GetDefaultMaterial() const;

	MaterialID GetSpecialMaterial(eSpecialMaterial special) const;

	/**
	 * @brief True if the static world brushes were replaced by a map compiled with mapc. The prototype can't be saved
	 * while they are, as those brushes aren't loaded.
	 */
	bool IsCompiledMap() const { return mbIsCompiledMap; }

	/**
	 * @brief Where the map compiler put the light probes, or nullptr unless a compiled world with probes is loaded.
	 * The brushes that placement would test against are not loaded then, so the probe bake uses this instead.
	 */
	const ProbeLayout* GetCompiledProbeLayout() const
	{
		return (mbIsCompiledMap && mCompiledProbeLayout.VolumeCount > 0) ? &mCompiledProbeLayout : nullptr;
	}

	bool HasCompiledProbes() const
	{
		return GetCompiledProbeLayout() != nullptr && mCompiledProbeIrradiance.Size > 0 &&
			   mCompiledProbeMoments.Size > 0;
	}

	const SizedArray<float32>& GetCompiledProbeIrradiance() const { return mCompiledProbeIrradiance; }
	const SizedArray<uint16>& GetCompiledProbeMoments() const { return mCompiledProbeMoments; }

	/// True for the surfaces of a compiled map, which stand in for many brushes each
	bool IsCompiledObject(const Object* object) const;

	bool HasBakedSun() const { return mbHasBakedSun; }
	bool IsBakedSunCurrent(const Vec3f sun_position) const;

	bool IsBakedIndirectCurrent(bool sun_enabled, const Vec3f sun_position) const;

	/**
	 * @brief The brush that a blockout object is built from, or nullptr if the object is not a blockout.
	 */
	Brush* GetBrush(const Object* object);
	Patch* GetPatch(const Object* object);
	bool HasPatch(const Object* object) { return GetPatch(object) != nullptr; }

	bool GetLocalCenter(const Object* object, Vec3f& out_center);

	/**
	 * @brief True if the loaded blockout file has an `objects` entry. Files saved before models moved into the
	 * blockout don't, and get their models from the scene's info.prx instead (see LoadLegacyModels).
	 */
	bool HasModelsEntry() const { return mbHasModelsEntry; }

	bool IsModel(const Object* object) const;

	/**
	 * @brief Loads the models from the `objects` entry of the scene's info.prx, which is where they lived before they
	 * were saved in the blockout file. Does nothing until the scene path is known.
	 */
	void LoadLegacyModels();

	/**
	 * @brief Links models to the colliders their `collider` member names, for colliders made after the models
	 */
	void LinkModelColliders();

	/////////////////////////////////////
	// Subviews
	/////////////////////////////////////

	static constexpr uint32 scNoSubview = 0;

	struct SubviewSolid
	{
		const Brush* pBrush = nullptr;
		Mat4f World = Mat4f::scIdentity;
		Object* pOwner = nullptr;
		bool bTransparent = false;
	};

	/// The subview instance that is being edited, or scNoSubview while the level itself is
	uint32 GetActiveSubview() const { return mActiveSubview; }
	bool IsEditingSubview() const { return mActiveSubview != scNoSubview; }

	/// The name of the subview that an instance places, which is empty if there is no such instance
	String GetSubviewName(uint32 instance) const;

	/// The names of every subview that can be placed, one per line
	String GetSubviewNames() const;

	/// Turns a name into one that can be saved and that no other subview has
	String MakeSubviewName(const String& wanted) const;

	bool IsSubviewObject(const Object* object) const;
	bool IsPlacedInActiveSubview(uint32 instance) const;
	Object* GetSubviewObject(uint32 instance) const;
	uint32 GetSubviewBrushCount(uint32 instance) const;

	void GetSubviewMembers(uint32 instance, std::vector<Object*>& out_members) const;
	void GetSubviewSolids(std::vector<SubviewSolid>& out_solids);

	bool GetSubviewPlacement(uint32 instance, BrushPlacement& out_placement);

	/// The world space bounds of every brush of an instance
	bool GetSubviewBounds(uint32 instance, BBox& out_bounds);

	/// The world space bounds of a set of brushes
	bool GetBrushBounds(Object* const* objects, uint32 count, BBox& out_bounds);

	/**
	 * @brief Makes a subview out of brushes that are in the level, and an instance of it that is not placed yet.
	 * The brushes themselves are left as they are.
	 * @param origin The point of the level that becomes the origin of the subview
	 */
	uint32 CreateSubview(const String& name, Object* const* objects, uint32 count, const Vec3f origin);

	/// Makes an instance of a subview that is not placed yet. Returns scNoSubview if there is no such subview.
	uint32 NewSubviewInstance(const String& subview_name);

	Object* PlaceSubviewInstance(uint32 instance, const BrushPlacement& placement);
	void RemoveSubviewInstance(uint32 instance);

	uint32 DissolveSubviewInstance(uint32 instance, std::vector<ObjectID>& out_brushes,
								   std::vector<uint32>& out_instances);

	void SyncSubviewInstances();

	/**
	 * @brief Starts editing the subview through one of its instances. Brushes made from here on belong to it.
	 */
	bool EnterSubview(uint32 instance);

	/**
	 * @brief Stops editing the subview, and rebuilds every other instance of it to match
	 */
	void LeaveSubview();

	~Blockout();

private:
	struct Model
	{
		ObjectID ID = ObjectID::scNull;
		String MeshPath;
		String ColliderName;
	};

	struct SubviewMember
	{
		Brush LocalBrush;
		Mat4f ToSubview = Mat4f::scIdentity;
		MaterialID Material = MaterialID::scNull;
		bool bMerged = false;
	};

	struct SubviewGroup
	{
		MaterialID Material = MaterialID::scNull;
		Ref<PrimitiveMesh> pMesh { nullptr };
		BBox Bounds { Vec3f::sZero, Vec3f::sZero };
	};

	struct Subview
	{
		String Name;
		String LegacyName;
		/// The brushes as they are saved, placed in the subview's own space
		ConfigEntry Brushes;
		ConfigEntry Instances;
		ConfigEntry Flat;

		std::vector<SubviewMember> Members;
		std::vector<SubviewGroup> Groups;
		SizedArray<SizedArray<Vec3f>> Hulls;
		BBox Bounds { Vec3f::sZero, Vec3f::sZero };

		bool bHasFile = false;
		bool bDirty = false;
	};

	struct SubviewInstance
	{
		uint32 ID = scNoSubview;
		String Name;
		String SubviewName;

		uint32 Parent = scNoSubview;

		ObjectID RootID = ObjectID::scNull;
		BrushPlacement Placement;
		bool bPlaced = false;
		/// The subview could not be read, so the instance is only kept to be saved again
		bool bMissing = false;
	};

	Subview* FindSubview(const String& name);
	const Subview* FindSubview(const String& name) const;
	Subview* LoadSubview(const ConfigFile* info, const String& name);
	SubviewInstance* FindSubviewInstance(uint32 instance);
	const SubviewInstance* FindSubviewInstance(uint32 instance) const;

	uint32 AddSubviewInstance(const String& subview_name, const String& wanted_name);
	uint32 AddNestedSubviewInstance(const ConfigEntry& entry, const BrushPlacement& parent);
	bool IsEnteredSubview(uint32 instance) const;
	bool SubviewContains(const String& outer, const String& inner) const;
	bool CanPlaceSubview(const String& subview_name) const;

	void BuildSubview(Subview& subview);
	bool WriteSubviewFile(Subview& subview);
	void RenameSubview(const String& previous_name, const String& name);

	/// Takes the subview's brushes from the instance being edited, then rebuilds its other instances
	void CommitEnteredSubviews();
	void CommitSubview(uint32 instance, const BrushPlacement& placement);
	void RebuildSubviewUsers(const String& subview_name);
	void RemoveNestedSubviewInstances(uint32 parent);

	uint32 CreateSubviewBrushes(const SubviewInstance& instance, const BrushPlacement& placement, bool entities_only,
								bool own_only, const CompiledMap* compiled_map);
	Object* CreateSubviewObject(SubviewInstance& instance, Subview& subview, const BrushPlacement& placement);
	void PlaceSubviewEntities(const SubviewInstance& instance);

	void LoadSubviews(ConfigFile& info, const CompiledMap* compiled_map);
	void SaveSubviews(ConfigFile& info);

	MaterialID ReadBrushMaterial(ConfigEntry& entry);

	ConfigEntry MakeBrushEntry(Object* object, const String& name, const BrushPlacement& placement);

	/**
	 * @brief Creates a new brush from a prototype entry
	 */
	ObjectID CreateBrush(ConfigEntry& entry);

	/**
	 * @brief Creates a new brush from values
	 * @returns A null ObjectID on failure, the ObjectID of the brush otherwise
	 */
	ObjectID CreateBrush(const String& name, const Vec3f position, const Quat rotation, const MaterialID material_id,
						 const BBox bounds);

	/**
	 * @brief Brings the models in the world in line with a list of model entries. Models that are already loaded are
	 * updated in place, the rest are loaded, and models that are no longer listed are removed.
	 * @param mesh_root Prefixed to each entry's `mesh` path
	 */
	void LoadModels(const ConfigEntry* list, const std::string& mesh_root);
	void SaveModels(ConfigFile& info);
	void ApplyModelProperties(Object& object, Model& model, const ConfigEntry& entry);
	bool RemoveModelFromWorld(ObjectID id);
	void RemovePendingModels();

	/**
	 * @brief Loads the sun and the point/spot lights from the blockout's `sun`/`lights` entries
	 */
	void LoadLights(ConfigFile& info);
	void SaveLights(ConfigFile& info);
	void LoadCamera(ConfigFile& info);
	void SaveCamera(ConfigFile& info);
	void LoadPlayerSpawn(ConfigFile& info);
	void SavePlayerSpawn(ConfigFile& info);
	void AddOrUpdateLightFromEntry(const ConfigEntry& light_entry);

	/**
	 * @brief Loads the compiled map that belongs to a prototype, if it was compiled from the prototype as it is now
	 */
	bool LoadCompiledMap(const String& prototype_path, const ConfigFile& info, CompiledMap& out_map) const;
	void CreateCompiledWorld(const CompiledMap& map);

	/**
	 * @brief Reads a blockout's brush from either a box (`scale`) or a list of planes (`planes`, with optional face
	 * textures in `uvs`)
	 */
	Brush ReadBrushEntry(ConfigEntry& entry) const;
	void WriteBrushEntry(ConfigEntry& entry, const Object* object);

	/**
	 * @brief Builds the object's mesh, bounds and collider from the brush, and takes ownership of the brush.
	 */
	void RebuildBrush(Object* object, Brush&& brush, physics::eMotionType motion_type);

	/**
	 * @brief Applies a brush that replaces the object's current one, moving the object so the parts they share stay
	 * where they are
	 */
	void ApplyBrushInPlace(Object* object, Brush&& brush);

	void RebuildPatch(Object* object, Patch&& patch);
	bool RaycastPatch(Object* object, const Vec3f origin, const Vec3f direction, Vec3f& out_face_normal,
					  Vec3f* out_point);

	void RemoveBlockoutFromWorld(World* world);
	void RemoveSingleObjectFromWorld(Object* object);

	struct DecalReceiver
	{
		const Brush* pBrush = nullptr;
		const Patch* pPatch = nullptr;
		Mat4f World = Mat4f::scIdentity;
		ObjectID ID = ObjectID::scNull;
	};

	struct CompiledDecalReceiver
	{
		BBox Bounds;
		SizedArray<Vec3f> Positions;
		SizedArray<Vec3f> Normals;
		SizedArray<uint32> Indices;
	};

	bool IsDecalReceiver(Object* object);
	void GatherDecalReceivers(const BBox& bounds, std::vector<DecalReceiver>& out_receivers);
	uint64 HashDecal(Object* decal, const Brush& volume, const BBox& bounds,
					 const std::vector<DecalReceiver>& receivers) const;
	void RebuildDecalMesh(Object* decal, const Brush& volume);

	void CreateCompiledShadows(const CompiledMap& map);

	void RebuildFaceMeshes(Object* object, const Brush& brush);
	void DestroyFaceNodes(Object* object);

public:
	FreeArray<ObjectID> BlockoutObjects;
	World* pWorld = nullptr;

	MaterialID SelectionMaterialID = MaterialID::scNull;

	Object* pXFormObject = nullptr;

	/// Shows what the Create and Clip tools will make
	Object* pPreviewObject = nullptr;

private:
	MaterialLibraryID GetCompiledFallbackMaterial(int32 library_index) const;

private:
	MaterialLibrary mMaterials;
	MaterialLibraryID mDefaultMaterialID = MaterialLibraryID::scNull;
	MaterialLibraryID mEditableMaterialID = MaterialLibraryID::scNull;
	HashMap<uint32, Brush> mBrushes;
	HashMap<uint32, Patch> mPatches;

	std::vector<Model> mModels;
	/// Models that were still loading when they had to be removed
	std::vector<ObjectID> mModelsPendingRemoval;
	bool mbHasModelsEntry = false;

	std::vector<ObjectID> mCompiledObjects;
	std::vector<physics::BodyID> mCompiledBodies;
	bool mbIsCompiledMap = false;
	ProbeLayout mCompiledProbeLayout;
	SizedArray<float32> mCompiledProbeIrradiance;
	SizedArray<uint16> mCompiledProbeMoments;

	bool mbHasBakedSun = false;
	bool mbHasBakedIndirect = false;
	bool mbBakedWithSun = false;
	Vec3f mBakedSunDirection = Vec3f::sUp;

	Brush::PlaneList mPreviewPlanes;

	std::vector<CompiledDecalReceiver> mCompiledDecalReceivers;
	HashMap<uint32, uint64> mDecalHashes;
	HashMap<uint32, uint64> mPatchMeshFrames;

	HashMap<uint32, String> mFaceNodeMaterials;

	DynArray<Subview> mSubviews;
	DynArray<SubviewInstance> mSubviewInstances;
	uint32 mNextSubviewInstance = 1;

	struct EnteredSubview
	{
		uint32 Instance = scNoSubview;
		BrushPlacement Placement;
	};

	uint32 mActiveSubview = scNoSubview;
	BrushPlacement mActivePlacement;
	std::vector<EnteredSubview> mOuterSubviews;
	std::vector<String> mLoadingSubviews;

	String mPrototypePath;
};


} // namespace fx
