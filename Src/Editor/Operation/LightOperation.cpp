#include "LightOperation.hpp"

#include <Editor/RaptorEditor.hpp>
#include <Engine.hpp>
#include <Renderer/Light.hpp>
#include <Renderer/LightManager.hpp>
#include <Renderer/LightProbe.hpp>
#include <Script/ObjectScripts.hpp>
#include <World.hpp>


namespace fx {

namespace editor {


LightSnapshot LightSnapshot::Capture(const LightSpot& light)
{
	return LightSnapshot {
		.LightName = light.Name,
		.Position = light.GetPosition(),
		.Direction = light.GetDirection().Normalize(),
		.Radius = light.GetRadius(),
		.InnerAngle = light.GetInnerAngle(),
		.OuterAngle = light.GetOuterAngle(),
		.bCastShadows = light.bCastShadows,
		.Colour = light.Color,
		.Intensity = light.Intensity,
	};
}


/////////////////////////////////////
// Basic Operations
/////////////////////////////////////

LightEditOperation::LightEditOperation(LightSpot* light, int32 group_size)
	: EditOperation(group_size), mpLight(light), mLightID((light != nullptr) ? light->ID : LightID::scNull)
{
}

LightSpot* LightEditOperation::ResolveLight() const
{
	if (mpLight == nullptr || mLightID.IsInvalid()) {
		return nullptr;
	}

	LightBase* live = gLightManager->GetLight(mLightID);
	return (live == mpLight) ? mpLight : nullptr;
}

void LightEditOperation::Restore(const LightSnapshot& snapshot)
{
	Ref<LightSpot> light = gLightManager->NewLight<LightSpot>(snapshot.LightName.Get());

	light->SetPosition(snapshot.Position);
	light->SetDirection(snapshot.Direction);
	light->SetRadius(snapshot.Radius);
	light->SetConeAngles(snapshot.InnerAngle, snapshot.OuterAngle);
	light->bCastShadows = snapshot.bCastShadows;
	light->Color = snapshot.Colour;
	light->Intensity = snapshot.Intensity;

	mpLight = &(*light);
	mLightID = light->ID;
}

void LightEditOperation::Destroy()
{
	if (ResolveLight() != nullptr) {
		gLightManager->RemoveLight(mLightID);
	}

	mpLight = nullptr;
}


/////////////////////////////////////
// Light operations
/////////////////////////////////////

LightTransformOperation::LightTransformOperation(LightSpot* light, const Vec3f& position_before,
												 const Vec3f& position_after, const Vec3f& direction_before,
												 const Vec3f& direction_after)
	: LightEditOperation(light, 1), mPositionBefore(position_before), mPositionAfter(position_after),
	  mDirectionBefore(direction_before), mDirectionAfter(direction_after)
{
}

void LightTransformOperation::Execute(EditorSelection& selection)
{
	if (LightSpot* light = ResolveLight()) {
		light->SetPosition(mPositionAfter);
		light->SetDirection(mDirectionAfter);
	}
}

void LightTransformOperation::Undo(EditorSelection& selection)
{
	if (LightSpot* light = ResolveLight()) {
		light->SetPosition(mPositionBefore);
		light->SetDirection(mDirectionBefore);
	}
}

LightCreateOperation::LightCreateOperation(const LightSnapshot& snapshot)
	: LightEditOperation(nullptr, 1), mSnapshot(snapshot)
{
}

void LightCreateOperation::Execute(EditorSelection& selection) { Restore(mSnapshot); }

void LightCreateOperation::Undo(EditorSelection& selection) { Destroy(); }

LightDeleteOperation::LightDeleteOperation(LightSpot* light)
	: LightEditOperation(light, 1), mSnapshot(LightSnapshot::Capture(*light))
{
}

void LightDeleteOperation::Execute(EditorSelection& selection) { Destroy(); }

void LightDeleteOperation::Undo(EditorSelection& selection) { Restore(mSnapshot); }


} // namespace editor
} // namespace fx
