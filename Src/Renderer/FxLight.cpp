#include "FxLight.hpp"
#include "FxCamera.hpp"

#include "Renderer.hpp"

using VertexType = FxLight::VertexType;

void FxLight::SetLightVolume(const FxRef<FxMesh<VertexType>>& volume)
{
    LightVolume = volume;
}


void FxLight::Render(const FxCamera& camera)
{
    RvkFrameData* frame = Renderer->GetFrame();

    LightVolume->Render(frame->LightCommandBuffer, Renderer->DeferredRenderer->LightingPipeline);
}
