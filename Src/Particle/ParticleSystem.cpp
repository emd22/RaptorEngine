#include "ParticleSystem.hpp"

#include <Asset/AssetManager.hpp>
#include <Asset/ConfigFile.hpp>
#include <CVar.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <Math/Frustum.hpp>
#include <Math/Vec2.hpp>
#include <Object/ObjectLayer.hpp>
#include <Renderer/Backend/Commands.hpp>
#include <Renderer/Backend/DescriptorCache.hpp>
#include <Renderer/Backend/Descriptors.hpp>
#include <Renderer/Backend/Image.hpp>
#include <Renderer/Backend/Pipeline.hpp>
#include <Renderer/Backend/Sampler/SamplerCache.hpp>
#include <Renderer/Camera.hpp>
#include <Renderer/Constants.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/PipelineCache.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace fx {

FX_SET_MODULE_NAME("ParticleSystem")

namespace {

constexpr float32 scTwoPi = 6.28318530718f;
constexpr float32 scDegToRad = 0.01745329252f;
constexpr float32 scMaxHalf = 65000.0f;

using Clock = std::chrono::steady_clock;

float32 MillisecondsSince(Clock::time_point start)
{
	return std::chrono::duration<float32, std::milli>(Clock::now() - start).count();
}

float32 RandomUnit() { return static_cast<float32>(FastRand32() >> 8) * (1.0f / 16777216.0f); }

float32 RandomInRange(const ParticleRange& range) { return range.Min + (range.Max - range.Min) * RandomUnit(); }

uint32 PackHalf2(float32 low, float32 high)
{
	const _Float16 half_low = static_cast<_Float16>(std::min(low, scMaxHalf));
	const _Float16 half_high = static_cast<_Float16>(std::min(high, scMaxHalf));

	uint16 low_bits;
	uint16 high_bits;
	memcpy(&low_bits, &half_low, sizeof(low_bits));
	memcpy(&high_bits, &half_high, sizeof(high_bits));

	return static_cast<uint32>(low_bits) | (static_cast<uint32>(high_bits) << 16);
}

uint32 PackUnorm2(float32 low, float32 high)
{
	const uint32 low_bits = static_cast<uint32>(std::clamp(low, 0.0f, 1.0f) * 65535.0f + 0.5f);
	const uint32 high_bits = static_cast<uint32>(std::clamp(high, 0.0f, 1.0f) * 65535.0f + 0.5f);

	return low_bits | (high_bits << 16);
}

void StoreFloat3(const Vec3f value, float32* out)
{
	out[0] = value.X;
	out[1] = value.Y;
	out[2] = value.Z;
}

void MakeBasis(const Vec3f forward, Vec3f& out_tangent, Vec3f& out_bitangent)
{
	Vec3f reference = Vec3f::sUp;

	if (std::abs(forward.Y) > 0.99f) {
		reference = Vec3f::sRight;
	}

	out_tangent = reference.Cross(forward).Normalize();
	out_bitangent = forward.Cross(out_tangent);
}

Vec3f RandomDirectionInCone(const Vec3f forward, const Vec3f tangent, const Vec3f bitangent, float32 spread_cos)
{
	const float32 cos_theta = 1.0f + (spread_cos - 1.0f) * RandomUnit();
	const float32 sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
	const float32 phi = scTwoPi * RandomUnit();

	return (tangent * (std::cos(phi) * sin_theta)) + (bitangent * (std::sin(phi) * sin_theta)) + (forward * cos_theta);
}

const ConfigEntry* Child(const ConfigEntry& parent, const char* name)
{
	if (!parent.Members.IsInited()) {
		return nullptr;
	}

	return parent.GetMember(HashStr32(name));
}

template <typename T>
T Member(const ConfigEntry& parent, const char* name, T fallback)
{
	const ConfigEntry* entry = Child(parent, name);

	if (entry == nullptr) {
		return fallback;
	}

	return entry->GetValue<T>();
}

ParticleRange RangeMember(const ConfigEntry& parent, const char* name, ParticleRange fallback, float32 scale = 1.0f)
{
	const ConfigEntry* entry = Child(parent, name);

	if (entry == nullptr) {
		return fallback;
	}

	ParticleRange range;

	if (entry->bIsArray && entry->ArrayData.IsInited() && entry->ArrayData.Size() >= 2) {
		range.Min = entry->ArrayData[0].Get<float32>();
		range.Max = entry->ArrayData[1].Get<float32>();
	}
	else if (!entry->bIsArray) {
		range.Min = entry->GetValue<float32>();
		range.Max = range.Min;
	}
	else {
		return fallback;
	}

	range.Min *= scale;
	range.Max *= scale;

	return range;
}

void ColorMember(const ConfigEntry& parent, const char* name, float32* out_color)
{
	const ConfigEntry* entry = Child(parent, name);

	if (entry == nullptr || !entry->bIsArray || !entry->ArrayData.IsInited() || entry->ArrayData.Size() < 3) {
		return;
	}

	const uint32 count = std::min<uint32>(static_cast<uint32>(entry->ArrayData.Size()), 4);

	for (uint32 i = 0; i < count; i++) {
		out_color[i] = entry->ArrayData[i].Get<float32>();
	}
}

eParticleShape ShapeFromName(const char* name)
{
	if (strcmp(name, "sphere") == 0) {
		return eParticleShape::Sphere;
	}

	if (strcmp(name, "disc") == 0) {
		return eParticleShape::Disc;
	}

	return eParticleShape::Point;
}

} // namespace

void ParticleSystem::Create()
{
	mParticles.InitCapacity(scMaxParticles);
	mSortEntries.InitCapacity(scMaxParticles);

	mpEnabledCVar = gCVars->Set("r_particles", 1);

	mInstanceBuffer.Create(renderer::eGpuBufferType::StorageWithOffset,
						   static_cast<uint64>(scMaxParticles) * sizeof(renderer::ParticleGpuData) *
							   renderer::FramesInFlight,
						   VMA_MEMORY_USAGE_CPU_ONLY, eGpuBufferFlags::PersistentMapped);

	mAtlasConfig.Load(scAtlasDirectory, scAtlasConfigPath, scDefaultAtlasImage);

	mAtlasTicket = gAssetManager->LoadImage(eImageType::Flat, eImageFormat::RGBA8_SRGB,
											mAtlasConfig.GetImagePath().CStr(), eImageCreateFlags::None);

	mAtlasTicket.OnLoaded(
		[this](void* data)
		{
			Image* atlas = static_cast<Image*>(data);
			BuildDescriptorSet(atlas);
			mpAtlas.store(atlas);
		});

	LoadEffects();
}

void ParticleSystem::Destroy()
{
	Clear();

	if (mInstanceBuffer.Initialized) {
		mInstanceBuffer.Destroy();
	}

	mpDescriptorSet = nullptr;
	mpAtlas.store(nullptr);
}

void ParticleSystem::BuildDescriptorSet(Image* atlas)
{
	using namespace renderer;

	SamplerProps sampler_props {};
	sampler_props.SetLinear();
	sampler_props.AddressMode = eSamplerAddressMode::ClampToEdge;

	SizedArray<DescriptorEntry> entries = {
		DescriptorEntry::AsBuffer(0, eShaderType::Vertex, &mInstanceBuffer, 0,
								  sizeof(ParticleGpuData) * scMaxParticles),
		DescriptorEntry::AsImage(1, eShaderType::Pixel, atlas, gSamplerCache->Request(sampler_props)),
	};

	mpDescriptorSet = gDescriptorCache->Request(entries).second;
}

void ParticleSystem::LoadEffects()
{
	Clear();
	mEffectDefs.clear();

	const String directory = FilesystemIO::ResolvePath(scEffectDirectory);

	if (!FilesystemIO::FileExists(directory)) {
		LogWarning(LC_ASSET, "Particle effect directory '{}' does not exist", directory);
		return;
	}

	PagedArray<std::string> files = FilesystemIO::DirListIfHasExtension(directory.CStr(), ".conf", false);

	for (const std::string& file : files) {
		const std::string name = file.substr(0, file.size() - strlen(".conf"));

		ParticleEffectDef def;

		if (LoadEffect((std::string(scEffectDirectory) + "/" + file).c_str(), String(name.c_str()), def)) {
			mEffectDefs.push_back(std::move(def));
		}
	}

	LogInfo(LC_ASSET, "Loaded {} particle effects", mEffectDefs.size());
}

bool ParticleSystem::LoadEffect(const char* path, const String& name, ParticleEffectDef& out_def)
{
	ConfigFile config;
	config.Load(path);

	if (config.HasErrors()) {
		LogError(LC_ASSET, "Could not parse particle effect '{}'", path);
		return false;
	}

	const ConfigEntry* emitters = config.GetEntry(HashStr32("emitters"));

	if (emitters == nullptr || !emitters->IsObjectArray() || !emitters->Members.IsInited()) {
		LogError(LC_ASSET, "Particle effect '{}' needs an 'emitters' array of tables", path);
		return false;
	}

	out_def.Name = name;
	out_def.NameHash = HashStr32(name.CStr());

	for (const ConfigEntry& entry : emitters->Members) {
		if (out_def.Emitters.size() >= scMaxEmittersPerEffect) {
			LogWarning(LC_ASSET, "Particle effect '{}' has more than {} emitters, ignoring the rest", path,
					   scMaxEmittersPerEffect);
			break;
		}

		ParticleEmitterDef emitter;

		emitter.Shape = ShapeFromName(Member<const char*>(entry, "shape", "point"));
		emitter.Radius = Member<float32>(entry, "radius", 0.0f);

		const float32 spread = std::clamp(Member<float32>(entry, "spread", 180.0f), 0.0f, 180.0f);
		emitter.SpreadCos = std::cos(spread * scDegToRad);

		emitter.Burst = static_cast<uint32>(std::max(Member<int32>(entry, "burst", 0), 0));
		emitter.Rate = std::max(Member<float32>(entry, "rate", 0.0f), 0.0f);
		emitter.Duration = std::max(Member<float32>(entry, "duration", 0.0f), 0.0f);
		emitter.bLoop = Member<int32>(entry, "loop", 0) != 0;

		emitter.Lifetime = RangeMember(entry, "lifetime", emitter.Lifetime);
		emitter.Speed = RangeMember(entry, "speed", emitter.Speed);
		emitter.Size = RangeMember(entry, "size", emitter.Size);
		emitter.SizeEndScale = Member<float32>(entry, "size_end", 1.0f);
		emitter.Rotation = RangeMember(entry, "rotation", emitter.Rotation, scDegToRad);
		emitter.Spin = RangeMember(entry, "spin", emitter.Spin, scDegToRad);

		emitter.GravityScale = Member<float32>(entry, "gravity", 0.0f);
		emitter.Drag = std::max(Member<float32>(entry, "drag", 0.0f), 0.0f);

		ColorMember(entry, "color_start", emitter.ColorStart);
		ColorMember(entry, "color_end", emitter.ColorEnd);
		emitter.Intensity = Member<float32>(entry, "intensity", 1.0f);
		emitter.FadeIn = std::clamp(Member<float32>(entry, "fade_in", 0.0f), 0.0f, 1.0f);
		emitter.bAdditive = strcmp(Member<const char*>(entry, "blend", "alpha"), "additive") == 0;

		emitter.Stretch = std::max(Member<float32>(entry, "stretch", 0.0f), 0.0f);

		emitter.SpriteName = String(Member<const char*>(entry, "sprite", ""));

		if (emitter.SpriteName.Length > 0) {
			mAtlasConfig.GetRegion(emitter.SpriteName.CStr(), emitter.Region);
		}

		if (const ConfigEntry* frames = Child(entry, "frames")) {
			const Vec2i frame_grid = frames->GetValue<Vec2i>();
			emitter.FramesX = static_cast<uint32>(std::max(frame_grid.X, 1));
			emitter.FramesY = static_cast<uint32>(std::max(frame_grid.Y, 1));
		}

		if (emitter.FramesX * emitter.FramesY > 256) {
			LogWarning(LC_ASSET, "Particle effect '{}' has more than 256 frames, only the first 256 are used", path);
			emitter.FramesY = std::max(256U / emitter.FramesX, 1U);
		}

		emitter.FrameRate = std::max(Member<float32>(entry, "frame_rate", 0.0f), 0.0f);
		emitter.bRandomFrame = Member<int32>(entry, "random_frame", 0) != 0;

		if (emitter.Lifetime.Min <= 0.0f || emitter.Lifetime.Max <= 0.0f) {
			LogWarning(LC_ASSET, "Particle effect '{}' has an emitter with no lifetime, skipping it", path);
			continue;
		}

		out_def.Emitters.push_back(std::move(emitter));
	}

	if (out_def.Emitters.empty()) {
		LogError(LC_ASSET, "Particle effect '{}' has no usable emitters", path);
		return false;
	}

	return true;
}

ParticleEffectID ParticleSystem::Play(const char* name, const Vec3f position, const Vec3f direction)
{
	return Play(HashStr32(name), position, direction);
}

ParticleEffectID ParticleSystem::Play(Hash32 name_hash, const Vec3f position, const Vec3f direction)
{
	uint32 def_index = UINT32_MAX;

	for (uint32 i = 0; i < mEffectDefs.size(); i++) {
		if (mEffectDefs[i].NameHash == name_hash) {
			def_index = i;
			break;
		}
	}

	if (def_index == UINT32_MAX) {
		return {};
	}

	uint32 slot = UINT32_MAX;

	for (uint32 i = 0; i < scMaxEffects; i++) {
		const uint32 candidate = (mNextEffectSlot + i) % scMaxEffects;

		if (!mEffects[candidate].bActive) {
			slot = candidate;
			break;
		}
	}

	if (slot == UINT32_MAX) {
		if (!bWarnedAboutEffectOverflow) {
			LogWarning(LC_RENDER, "Too many particle effects playing ({}), dropping new ones", scMaxEffects);
			bWarnedAboutEffectOverflow = true;
		}
		return {};
	}

	mNextEffectSlot = (slot + 1) % scMaxEffects;

	EffectInstance& effect = mEffects[slot];
	const uint32 generation = effect.Generation;

	effect = EffectInstance {};
	effect.Generation = generation;
	effect.DefIndex = def_index;
	effect.Position = position;
	effect.Direction = direction.Normalize();
	effect.bActive = true;

	++mActiveEffectCount;

	const ParticleEffectDef& def = mEffectDefs[def_index];

	for (uint32 i = 0; i < def.Emitters.size(); i++) {
		SpawnParticles(slot, i, def.Emitters[i].Burst);
	}

	return ParticleEffectID { .Slot = slot, .Generation = generation };
}

ParticleSystem::EffectInstance* ParticleSystem::GetInstance(ParticleEffectID id)
{
	if (!id.IsValid() || id.Slot >= scMaxEffects) {
		return nullptr;
	}

	EffectInstance& effect = mEffects[id.Slot];

	if (!effect.bActive || effect.Generation != id.Generation) {
		return nullptr;
	}

	return &effect;
}

const ParticleSystem::EffectInstance* ParticleSystem::GetInstance(ParticleEffectID id) const
{
	return const_cast<ParticleSystem*>(this)->GetInstance(id);
}

void ParticleSystem::SetTransform(ParticleEffectID id, const Vec3f position, const Vec3f direction)
{
	EffectInstance* effect = GetInstance(id);

	if (effect == nullptr) {
		return;
	}

	effect->Position = position;
	effect->Direction = direction.Normalize();
}

void ParticleSystem::Stop(ParticleEffectID id)
{
	EffectInstance* effect = GetInstance(id);

	if (effect != nullptr) {
		effect->bStopped = true;
	}
}

void ParticleSystem::Kill(ParticleEffectID id)
{
	EffectInstance* effect = GetInstance(id);

	if (effect == nullptr) {
		return;
	}

	uint32 index = 0;

	while (index < mParticles.Size) {
		if (mParticles[index].EffectSlot == id.Slot) {
			mParticles[index] = mParticles[mParticles.Size - 1];
			--mParticles.Size;
			continue;
		}

		++index;
	}

	effect->LiveParticles = 0;
	effect->bStopped = true;

	RetireEffects();
}

bool ParticleSystem::IsAlive(ParticleEffectID id) const { return GetInstance(id) != nullptr; }

void ParticleSystem::Clear()
{
	mParticles.Size = 0;

	for (EffectInstance& effect : mEffects) {
		if (effect.bActive) {
			effect.bActive = false;
			++effect.Generation;
		}
	}

	mActiveEffectCount = 0;
	mDrawnCount = 0;
}

void ParticleSystem::SpawnParticles(uint32 effect_slot, uint32 emitter_index, uint32 count)
{
	EffectInstance& effect = mEffects[effect_slot];
	const ParticleEmitterDef& emitter = mEffectDefs[effect.DefIndex].Emitters[emitter_index];

	Vec3f tangent;
	Vec3f bitangent;
	MakeBasis(effect.Direction, tangent, bitangent);

	const uint32 frame_count = emitter.FramesX * emitter.FramesY;

	for (uint32 i = 0; i < count; i++) {
		if (mParticles.Size >= scMaxParticles) {
			if (!bWarnedAboutOverflow) {
				LogWarning(LC_RENDER, "Particle pool is full ({}), dropping new particles", scMaxParticles);
				bWarnedAboutOverflow = true;
			}
			return;
		}

		Vec3f offset = Vec3f::sZero;

		if (emitter.Shape == eParticleShape::Sphere) {
			const Vec3f direction = RandomDirectionInCone(effect.Direction, tangent, bitangent, -1.0f);
			offset = direction * (emitter.Radius * std::cbrt(RandomUnit()));
		}
		else if (emitter.Shape == eParticleShape::Disc) {
			const float32 angle = scTwoPi * RandomUnit();
			const float32 distance = emitter.Radius * std::sqrt(RandomUnit());
			offset = (tangent * (std::cos(angle) * distance)) + (bitangent * (std::sin(angle) * distance));
		}

		const Vec3f direction = RandomDirectionInCone(effect.Direction, tangent, bitangent, emitter.SpreadCos);

		uint8 start_frame = 0;

		if (emitter.bRandomFrame && frame_count > 1) {
			start_frame = static_cast<uint8>(FastRand32() % frame_count);
		}

		Particle* particle = mParticles.Insert();
		particle->Position = effect.Position + offset;
		particle->Velocity = direction * RandomInRange(emitter.Speed);
		particle->Age = 0.0f;
		particle->Lifetime = RandomInRange(emitter.Lifetime);
		particle->Size = RandomInRange(emitter.Size);
		particle->Rotation = RandomInRange(emitter.Rotation);
		particle->Spin = RandomInRange(emitter.Spin);
		particle->EffectSlot = static_cast<uint16>(effect_slot);
		particle->EmitterIndex = static_cast<uint8>(emitter_index);
		particle->StartFrame = start_frame;

		++effect.LiveParticles;
	}
}

void ParticleSystem::Update(float32 delta_time)
{
	mUpdateMs = 0.0f;

	if (delta_time <= 0.0f) {
		return;
	}

	const Clock::time_point start = Clock::now();

	delta_time = std::min(delta_time, scMaxDeltaTime);

	UpdateParticles(delta_time);
	UpdateEmitters(delta_time);
	RetireEffects();

	mUpdateMs = MillisecondsSince(start);
}

void ParticleSystem::UpdateParticles(float32 delta_time)
{
	const Vec3f gravity = Vec3f(0.0f, -scGravity * delta_time, 0.0f);

	uint32 index = 0;

	while (index < mParticles.Size) {
		Particle& particle = mParticles[index];

		particle.Age += delta_time;

		if (particle.Age >= particle.Lifetime) {
			--mEffects[particle.EffectSlot].LiveParticles;

			particle = mParticles[mParticles.Size - 1];
			--mParticles.Size;
			continue;
		}

		const EffectInstance& effect = mEffects[particle.EffectSlot];
		const ParticleEmitterDef& emitter = mEffectDefs[effect.DefIndex].Emitters[particle.EmitterIndex];

		particle.Velocity += gravity * emitter.GravityScale;
		particle.Velocity = particle.Velocity * (1.0f / (1.0f + emitter.Drag * delta_time));
		particle.Position += particle.Velocity * delta_time;
		particle.Rotation += particle.Spin * delta_time;

		++index;
	}
}

void ParticleSystem::UpdateEmitters(float32 delta_time)
{
	for (uint32 slot = 0; slot < scMaxEffects; slot++) {
		EffectInstance& effect = mEffects[slot];

		if (!effect.bActive || effect.bStopped) {
			continue;
		}

		effect.Age += delta_time;

		const ParticleEffectDef& def = mEffectDefs[effect.DefIndex];

		for (uint32 i = 0; i < def.Emitters.size(); i++) {
			const ParticleEmitterDef& emitter = def.Emitters[i];
			EmitterState& state = effect.Emitters[i];

			if (emitter.bLoop && emitter.Duration > 0.0f) {
				state.CycleTime += delta_time;

				while (state.CycleTime >= emitter.Duration) {
					state.CycleTime -= emitter.Duration;
					SpawnParticles(slot, i, emitter.Burst);
				}
			}

			const bool is_emitting = emitter.bLoop || (effect.Age < emitter.Duration);

			if (!is_emitting || emitter.Rate <= 0.0f) {
				continue;
			}

			state.SpawnAccumulator += emitter.Rate * delta_time;

			const uint32 spawn_count = static_cast<uint32>(state.SpawnAccumulator);
			state.SpawnAccumulator -= static_cast<float32>(spawn_count);

			SpawnParticles(slot, i, spawn_count);
		}
	}
}

void ParticleSystem::RetireEffects()
{
	for (EffectInstance& effect : mEffects) {
		if (!effect.bActive || effect.LiveParticles > 0) {
			continue;
		}

		bool is_finished = effect.bStopped;

		if (!is_finished) {
			is_finished = true;

			for (const ParticleEmitterDef& emitter : mEffectDefs[effect.DefIndex].Emitters) {
				if (emitter.bLoop || effect.Age < emitter.Duration) {
					is_finished = false;
					break;
				}
			}
		}

		if (is_finished) {
			effect.bActive = false;
			++effect.Generation;
			--mActiveEffectCount;
		}
	}
}

void ParticleSystem::Render(const renderer::CommandBuffer& cmd, const Camera& camera)
{
	using namespace renderer;

	const Clock::time_point start = Clock::now();

	mDrawnCount = 0;
	mCpuMs = mUpdateMs;

	if (mParticles.Size == 0 || mpDescriptorSet == nullptr || mpAtlas.load() == nullptr) {
		return;
	}

	if (mpEnabledCVar != nullptr && mpEnabledCVar->IntValue == 0) {
		return;
	}

	const Pipeline& pipeline = gPipelineCache->Request(ePipelineName::Particles);

	if (!pipeline.IsBuilt()) {
		return;
	}

	const Mat4f& view_projection = camera.GetCameraMatrix(eObjectLayer::WorldLayer);

	Frustum frustum;
	frustum.Rebuild(view_projection);

	const Vec3f eye = camera.Position;
	const Vec3f forward = camera.Direction.Normalize();
	const Vec3f right = Vec3f::sUp.Cross(forward).Normalize();
	const Vec3f up = forward.Cross(right).Normalize();

	mSortEntries.Clear();

	bool needs_sort = false;

	for (uint32 i = 0; i < mParticles.Size; i++) {
		const Particle& particle = mParticles[i];
		const ParticleEmitterDef& emitter = mEffectDefs[mEffects[particle.EffectSlot].DefIndex]
												.Emitters[particle.EmitterIndex];

		const float32 radius = (particle.Size * std::max(emitter.SizeEndScale, 1.0f)) +
							   (particle.Velocity.Length() * emitter.Stretch);

		if (!frustum.IntersectsSphere(particle.Position, radius)) {
			continue;
		}

		needs_sort = needs_sort || !emitter.bAdditive;

		mSortEntries.Insert(SortEntry { .Depth = (particle.Position - eye).Dot(forward), .Index = i });
	}

	if (mSortEntries.Size == 0) {
		return;
	}

	if (needs_sort) {
		std::sort(mSortEntries.pData, mSortEntries.pData + mSortEntries.Size,
				  [](const SortEntry& a, const SortEntry& b) { return a.Depth > b.Depth; });
	}

	const uint32 base_offset = gGraphics->GetFrameNumber() * scMaxParticles * sizeof(ParticleGpuData);
	ParticleGpuData* instances = reinterpret_cast<ParticleGpuData*>(static_cast<uint8*>(mInstanceBuffer.pMappedBuffer) +
																	base_offset);

	for (uint32 i = 0; i < mSortEntries.Size; i++) {
		const Particle& particle = mParticles[mSortEntries[i].Index];
		const ParticleEmitterDef& emitter = mEffectDefs[mEffects[particle.EffectSlot].DefIndex]
												.Emitters[particle.EmitterIndex];

		const float32 t = std::clamp(particle.Age / particle.Lifetime, 0.0f, 1.0f);

		float32 color[4];

		for (uint32 c = 0; c < 4; c++) {
			color[c] = emitter.ColorStart[c] + (emitter.ColorEnd[c] - emitter.ColorStart[c]) * t;
		}

		float32 alpha = std::clamp(color[3], 0.0f, 1.0f);

		if (emitter.FadeIn > 0.0f) {
			alpha *= std::min(t / emitter.FadeIn, 1.0f);
		}

		const float32 color_scale = emitter.Intensity * alpha;

		float32 coverage = alpha;

		if (emitter.bAdditive) {
			coverage = 0.0f;
		}

		const uint32 frame_count = emitter.FramesX * emitter.FramesY;
		uint32 frame = particle.StartFrame;

		if (emitter.FrameRate > 0.0f) {
			frame += static_cast<uint32>(particle.Age * emitter.FrameRate);
		}

		frame %= frame_count;

		const float32 frame_scale_u = emitter.Region.ScaleU / static_cast<float32>(emitter.FramesX);
		const float32 frame_scale_v = emitter.Region.ScaleV / static_cast<float32>(emitter.FramesY);
		const float32 frame_offset_u = emitter.Region.OffsetU +
									   frame_scale_u * static_cast<float32>(frame % emitter.FramesX);
		const float32 frame_offset_v = emitter.Region.OffsetV +
									   frame_scale_v * static_cast<float32>(frame / emitter.FramesX);

		const Vec3f stretch = particle.Velocity * emitter.Stretch;

		ParticleGpuData& instance = instances[i];
		instance.Position[0] = particle.Position.X;
		instance.Position[1] = particle.Position.Y;
		instance.Position[2] = particle.Position.Z;
		instance.Size = particle.Size * (1.0f + (emitter.SizeEndScale - 1.0f) * t);
		instance.Stretch[0] = stretch.X;
		instance.Stretch[1] = stretch.Y;
		instance.Stretch[2] = stretch.Z;
		instance.Rotation = particle.Rotation;
		instance.Color[0] = PackHalf2(color[0] * color_scale, color[1] * color_scale);
		instance.Color[1] = PackHalf2(color[2] * color_scale, coverage);
		instance.UvRect[0] = PackUnorm2(frame_offset_u, frame_offset_v);
		instance.UvRect[1] = PackUnorm2(frame_scale_u, frame_scale_v);
	}

	mDrawnCount = mSortEntries.Size;

	ParticlePushConstants push_constants {};
	memcpy(push_constants.ViewProjection, view_projection.RawData, sizeof(push_constants.ViewProjection));
	StoreFloat3(right, push_constants.CameraRight);
	StoreFloat3(up, push_constants.CameraUp);
	StoreFloat3(eye, push_constants.EyePosition);

	gPipelineCache->AddBufferOffset(0, base_offset);
	gPipelineCache->Bind(ePipelineName::Particles, cmd);

	mpDescriptorSet->Bind(0, cmd, pipeline, Slice<const uint32>(&base_offset, 1));

	gGraphics->SubmitPushConstants(cmd, pipeline, eShaderType::Vertex, push_constants);

	vkCmdDraw(cmd.Get(), 6, mDrawnCount, 0, 0);

	mCpuMs += MillisecondsSince(start);
}

} // namespace fx
