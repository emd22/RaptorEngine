#pragma once

#include <Asset/AssetTicket.hpp>
#include <Core/SizedArray.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
#include <Math/Vec3.hpp>
#include <Renderer/Backend/GpuBuffer.hpp>
#include <Texture/AtlasConfig.hpp>
#include <atomic>
#include <vector>

namespace fx {

class Camera;
class Image;
class CVarValue;

namespace renderer {
class CommandBuffer;
class DescriptorSet;
} // namespace renderer

struct ParticleEffectID
{
	uint32 Slot = UINT32_MAX;
	uint32 Generation = 0;

	bool IsValid() const { return Slot != UINT32_MAX; }
};

enum class eParticleShape : uint8
{
	Point,
	Sphere,
	Disc,
};

struct ParticleRange
{
	float32 Min = 0.0f;
	float32 Max = 0.0f;
};

struct ParticleEmitterDef
{
	eParticleShape Shape = eParticleShape::Point;
	float32 Radius = 0.0f;
	float32 SpreadCos = -1.0f;

	uint32 Burst = 0;
	float32 Rate = 0.0f;
	float32 Duration = 0.0f;
	bool bLoop = false;

	ParticleRange Lifetime { 1.0f, 1.0f };
	ParticleRange Speed { 0.0f, 0.0f };
	ParticleRange Size { 0.1f, 0.1f };
	float32 SizeEndScale = 1.0f;
	ParticleRange Rotation { 0.0f, 0.0f };
	ParticleRange Spin { 0.0f, 0.0f };

	float32 GravityScale = 0.0f;
	float32 Drag = 0.0f;

	float32 ColorStart[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	float32 ColorEnd[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
	float32 Intensity = 1.0f;
	float32 FadeIn = 0.0f;
	bool bAdditive = false;

	float32 Stretch = 0.0f;

	AtlasRegion Region;
	String SpriteName;
	uint32 FramesX = 1;
	uint32 FramesY = 1;
	float32 FrameRate = 0.0f;
	bool bRandomFrame = false;
};

struct ParticleEffectDef
{
	String Name;
	Hash32 NameHash = 0;
	std::vector<ParticleEmitterDef> Emitters;
};

namespace renderer {

struct alignas(16) ParticleGpuData
{
	float32 Position[3];
	float32 Size;
	float32 Stretch[3];
	float32 Rotation;
	uint32 Color[2];
	uint32 UvRect[2];
};

static_assert(sizeof(ParticleGpuData) == 48);

struct alignas(16) ParticlePushConstants
{
	float32 ViewProjection[16];
	float32 CameraRight[4];
	float32 CameraUp[4];
	float32 EyePosition[4];
};

static_assert(sizeof(ParticlePushConstants) <= 128);

} // namespace renderer

class ParticleSystem
{
public:
	static constexpr uint32 scMaxParticles = 8192;
	static constexpr uint32 scMaxEffects = 256;
	static constexpr uint32 scMaxEmittersPerEffect = 4;
	static constexpr float32 scMaxDeltaTime = 0.1f;
	static constexpr float32 scGravity = 9.81f;

	static constexpr const char* scEffectDirectory = "RaptorData/Data/Particles";
	static constexpr const char* scAtlasDirectory = "Textures/";
	static constexpr const char* scAtlasConfigPath = "Textures/particles.conf";
	static constexpr const char* scDefaultAtlasImage = "particles.png";

public:
	ParticleSystem() = default;
	~ParticleSystem() { Destroy(); }

	ParticleSystem(const ParticleSystem&) = delete;
	ParticleSystem& operator=(const ParticleSystem&) = delete;

	void Create();
	void Destroy();

	void LoadEffects();

	ParticleEffectID Play(const char* name, const Vec3f position, const Vec3f direction);
	ParticleEffectID Play(Hash32 name_hash, const Vec3f position, const Vec3f direction);

	void SetTransform(ParticleEffectID id, const Vec3f position, const Vec3f direction);
	void Stop(ParticleEffectID id);
	void Kill(ParticleEffectID id);
	bool IsAlive(ParticleEffectID id) const;

	void Clear();

	void Update(float32 delta_time);
	void Render(const renderer::CommandBuffer& cmd, const Camera& camera);

	renderer::RawGpuBuffer& GetInstanceBuffer() { return mInstanceBuffer; }

	uint32 GetParticleCount() const { return mParticles.Size; }
	uint32 GetEffectCount() const { return mActiveEffectCount; }
	uint32 GetDrawnCount() const { return mDrawnCount; }
	float32 GetCpuMs() const { return mCpuMs; }

private:
	struct Particle
	{
		Vec3f Position;
		Vec3f Velocity;
		float32 Age;
		float32 Lifetime;
		float32 Size;
		float32 Rotation;
		float32 Spin;
		uint16 EffectSlot;
		uint8 EmitterIndex;
		uint8 StartFrame;
	};

	struct EmitterState
	{
		float32 SpawnAccumulator = 0.0f;
		float32 CycleTime = 0.0f;
	};

	struct EffectInstance
	{
		Vec3f Position = Vec3f::sZero;
		Vec3f Direction = Vec3f::sUp;
		uint32 DefIndex = 0;
		uint32 Generation = 0;
		uint32 LiveParticles = 0;
		float32 Age = 0.0f;
		EmitterState Emitters[scMaxEmittersPerEffect];
		bool bActive = false;
		bool bStopped = false;
	};

	struct SortEntry
	{
		float32 Depth;
		uint32 Index;
	};

private:
	bool LoadEffect(const char* path, const String& name, ParticleEffectDef& out_def);
	void ResolveRegions();

	EffectInstance* GetInstance(ParticleEffectID id);
	const EffectInstance* GetInstance(ParticleEffectID id) const;

	void SpawnParticles(uint32 effect_slot, uint32 emitter_index, uint32 count);
	void UpdateEmitters(float32 delta_time);
	void UpdateParticles(float32 delta_time);
	void RetireEffects();

	void BuildDescriptorSet(Image* atlas);

private:
	std::vector<ParticleEffectDef> mEffectDefs;

	SizedArray<Particle> mParticles;
	SizedArray<SortEntry> mSortEntries;

	EffectInstance mEffects[scMaxEffects];
	uint32 mActiveEffectCount = 0;
	uint32 mNextEffectSlot = 0;

	renderer::RawGpuBuffer mInstanceBuffer;
	renderer::DescriptorSet* mpDescriptorSet = nullptr;

	AtlasConfig mAtlasConfig;
	AssetTicket mAtlasTicket { nullptr };
	std::atomic<Image*> mpAtlas = nullptr;

	CVarValue* mpEnabledCVar = nullptr;

	uint32 mDrawnCount = 0;
	float32 mUpdateMs = 0.0f;
	float32 mCpuMs = 0.0f;

	bool bWarnedAboutOverflow = false;
	bool bWarnedAboutEffectOverflow = false;
};

} // namespace fx
