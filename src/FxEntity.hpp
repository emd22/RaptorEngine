#pragma once


#include <Asset/FxModel.hpp>
#include <Asset/FxImage.hpp>

#include <Math/Vector.hpp>

#include <Math/Mat4.hpp>

#include <Core/FxPagedArray.hpp>

class FxScript
{
public:
    virtual void Init() = 0;

    virtual void FrameTick() = 0;
    virtual void PhysicsTick() = 0;

    virtual void Destroy();
};


class FxEntity
{
public:
    FxEntity()
    {
        Children.reserve(4);
    }

    void AddChild(FxEntity* entity)
    {
        Children.push_back(entity);
    }

public:
    FxVec3f Transform = FxVec3f::Zero;
    FxVec3f Rotation = FxVec3f::Zero;



    std::vector<FxEntity*> Children;
};

class FxEntityManager
{
public:
    static FxEntityManager& GetGlobalManager();

    void Create(uint32 entities_per_page=64);

    static FxEntity* NewEntity();

private:
    FxPagedArray<FxEntity> mEntityPool;
};

class FxSceneObject : public FxEntity {
public:
    FxSceneObject() = default;

    // void AttachScript(std::unique_ptr<FxScript>& script)
    // {
    //     mScript = std::move(script);
    // }

    // void AttachModel(std::unique_ptr<FxModel>& model)
    // {
    //     mModel = std::move(model);
    // }

private:

    // std::unique_ptr<FxModel> mModel;
    // std::unique_ptr<FxScript> mScript;
};
