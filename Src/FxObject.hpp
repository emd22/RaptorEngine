#pragma once

#include <ThirdParty/Jolt/Jolt.h>
#include <ThirdParty/Jolt/Physics/Body/Body.h>
#include <ThirdParty/Jolt/Physics/Body/BodyID.h>

#include <Core/MemPool/FxMPPagedArray.hpp>
#include <FxEntity.hpp>
#include <FxMaterial.hpp>
#include <Renderer/FxPrimitiveMesh.hpp>


struct FxPhysicsProperties
{
public:
    FxPhysicsProperties() = default;

    float32 ConvexRadius = 0.01f;
    float32 Friction = 0.5f;
    float32 Restitution = 0.3f;
};

class FxObject : public FxAssetBase, public FxEntity
{
    friend class FxLoaderGltf;
    friend class FxAssetManager;

public:
    enum PhysicsFlags
    {
        PF_CreateInactive = 0x01,
    };

    enum class PhysicsType
    {
        Static,
        Dynamic,
    };

public:
    FxObject() = default;

    void Create(const FxRef<FxPrimitiveMesh<>>& mesh, const FxRef<FxMaterial>& material);

    void Render(const FxCamera& camera);
    bool CheckIfReady();


    void AttachObject(const FxRef<FxObject>& object);

    void CreatePhysicsBody(PhysicsFlags flags, PhysicsType type, const FxPhysicsProperties& properties);
    void DestroyPhysicsBody();

    FX_FORCE_INLINE JPH::Body* GetPhysicsBody() { return mpPhysicsBody; };
    FX_FORCE_INLINE const JPH::BodyID& GetPhysicsBodyId() { return mpPhysicsBody->GetID(); };

    void SetPhysicsEnabled(bool enabled);
    FX_FORCE_INLINE bool GetPhysicsEnabled() { return mbPhysicsEnabled; }

    void Update();

    void Destroy() override;
    ~FxObject() override { Destroy(); }

private:
    void RenderMesh();

    void UpdatePhysics();
    void UpdateJoltForDirectTransform();

public:
    FxRef<FxPrimitiveMesh<>> Mesh { nullptr };
    FxRef<FxMaterial> Material { nullptr };

    FxPagedArray<FxRef<FxObject>> AttachedNodes;

    FxVec3f Dimensions = FxVec3f::sZero;

private:
    RxUniformBufferObject mUbo;

    JPH::Body* mpPhysicsBody = nullptr;

    bool mbReadyToRender : 1 = false;
    bool mbPhysicsEnabled : 1 = false;
    bool mbHasPhysicsBody : 1 = false;
};
