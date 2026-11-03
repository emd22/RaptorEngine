#include "AudioDebugView.hpp"

#include <Audio/AudioMeter.hpp>
#include <Audio/AudioSystem.hpp>
#include <CVar.hpp>
#include <Color.hpp>
#include <Core/String.hpp>
#include <Engine.hpp>
#include <Renderer/Camera/BaseCamera.hpp>
#include <Renderer/DebugDraw.hpp>
#include <Renderer/Globals.hpp>
#include <Renderer/GraphicsBackend.hpp>
#include <Renderer/TextRenderer.hpp>
#include <algorithm>
#include <cmath>

namespace fx {

using renderer::gDebugDraw;
using renderer::gGraphics;
using renderer::gTextRenderer;

namespace {

constexpr uint32 scMeterWidth = 16;
constexpr float32 scMeterFloorDb = -60.0f;
constexpr float32 scMarkerHalfExtent = 0.15f;
constexpr float32 scLabelHeight = 0.3f;
constexpr float32 scMinClipW = 1e-3f;

const uint32 scWhite = Color::FromRGBA(255, 255, 255, 255).AsUInt();
const uint32 scHeader = Color::FromRGBA(255, 220, 120, 255).AsUInt();
const uint32 scBusColor = Color::FromRGBA(130, 200, 255, 255).AsUInt();
const uint32 scActiveColor = Color::FromRGBA(255, 120, 90, 255).AsUInt();
const uint32 scAudibleColor = Color::FromRGBA(120, 255, 120, 255).AsUInt();
const uint32 scQuietColor = Color::FromRGBA(150, 150, 150, 255).AsUInt();
const uint32 scPendingColor = Color::FromRGBA(255, 230, 80, 255).AsUInt();
const uint32 scStoppingColor = Color::FromRGBA(255, 160, 60, 255).AsUInt();

String FormatDb(float32 db)
{
	if (db <= AudioMeter::scSilenceDb + 1.0f) {
		return String("  -inf");
	}

	return String::Fmt("{:+6.1f}", db);
}

String FormatMeter(float32 rms_db, float32 peak_db)
{
	char bar[scMeterWidth + 3];
	bar[0] = '[';
	bar[scMeterWidth + 1] = ']';
	bar[scMeterWidth + 2] = '\0';

	const auto to_cells = [](float32 db)
	{
		const float32 amount = std::clamp((db - scMeterFloorDb) / -scMeterFloorDb, 0.0f, 1.0f);
		return static_cast<int32>(std::lround(amount * static_cast<float32>(scMeterWidth)));
	};

	const int32 rms_cells = to_cells(rms_db);
	const int32 peak_cell = to_cells(peak_db) - 1;

	for (int32 i = 0; i < static_cast<int32>(scMeterWidth); i++) {
		char cell = '.';

		if (i < rms_cells) {
			cell = '#';
		}

		if (i == peak_cell) {
			cell = '|';
		}

		bar[i + 1] = cell;
	}

	return String(bar);
}

/// The level each voice is heard at: measured for Steam Audio voices, estimated from gains for the others
float32 GetVoiceLevelDb(const AudioVoiceDebug& voice)
{
	if (voice.bSteam) {
		return voice.Spatial.LevelDb;
	}

	return AudioMeter::ToDb(voice.Volume * voice.DistanceGain);
}

uint32 GetVoiceColor(const AudioVoiceDebug& voice)
{
	if (voice.bPending) {
		return scPendingColor;
	}

	if (voice.bStopping) {
		return scStoppingColor;
	}

	if (GetVoiceLevelDb(voice) < AudioDebugView::scQuietDb) {
		return scQuietColor;
	}

	return scAudibleColor;
}

uint32 GetBusDepth(uint32 index)
{
	uint32 depth = 0;
	AudioBusDebug bus;

	while (gAudio->GetBusDebug(index, bus) && bus.Parent != UINT32_MAX) {
		index = bus.Parent;
		++depth;
	}

	return depth;
}

void DrawHeader()
{
	const SpatialAudio& spatial = gAudio->GetSpatial();

	gTextRenderer->DrawText(String::Fmt("AUDIO  voices {}/{}  stolen {}  dropped {}  master {} dBFS  limiter {} dB",
										gAudio->GetVoiceCount(), AudioSystem::scMaxVoices,
										gAudio->GetStolenVoiceCount(), gAudio->GetDroppedVoiceCount(),
										FormatDb(gAudio->GetMasterPeakDb()), FormatDb(gAudio->GetLimiterReductionDb()))
								.CStr(),
							1.0f, scHeader);

	if (!spatial.IsActive()) {
		gTextRenderer->DrawText("steam audio off", 1.0f, scQuietColor);
		return;
	}

	float32 reverb_times[3];
	spatial.GetReverbTimes(reverb_times);

	gTextRenderer->DrawText(String::Fmt("steam  sim {:.3f}ms  refl {:.1f}ms  rt60 {:.2f}/{:.2f}/{:.2f}s  tris {}  "
										"probes {}  pathed {}",
										spatial.GetSimulationMs(), spatial.GetReflectionMs(), reverb_times[0],
										reverb_times[1], reverb_times[2], spatial.GetSceneTriangleCount(),
										spatial.GetBakedProbeCount(), spatial.GetPathedVoiceCount())
								.CStr(),
							1.0f, scWhite);
}

void DrawBuses()
{
	gTextRenderer->DrawText("BUS              GAIN   SNAP   DUCK    LPF  LEVEL", 1.0f, scHeader);

	for (uint32 i = 0; i < gAudio->GetBusCount(); i++) {
		AudioBusDebug bus;

		if (!gAudio->GetBusDebug(i, bus)) {
			continue;
		}

		const String name = String::Fmt("{:{}}{}", "", GetBusDepth(i) * 2, bus.Name);

		String lowpass("     -");

		if (bus.Lowpass < AudioSystem::scLowpassOpen) {
			lowpass = String::Fmt("{:6.0f}", bus.Lowpass);
		}

		uint32 color = scBusColor;

		if (bus.DuckDb < -0.1f || bus.SnapshotDb < -0.1f || bus.Lowpass < AudioSystem::scLowpassOpen) {
			color = scActiveColor;
		}

		gTextRenderer->DrawText(String::Fmt("{:<14} {} {} {} {} {} {}", name.CStr(), FormatDb(bus.GainDb),
											FormatDb(bus.SnapshotDb), FormatDb(bus.DuckDb), lowpass,
											FormatMeter(bus.RmsDb, bus.PeakDb), FormatDb(bus.PeakDb))
									.CStr(),
								1.0f, color);
	}
}

void DrawMixState()
{
	String snapshots("snapshots:");
	bool any_snapshot = false;

	for (uint32 i = 0; i < gAudio->GetSnapshotCount(); i++) {
		AudioSnapshotDebug snapshot;

		if (!gAudio->GetSnapshotDebug(i, snapshot) || (snapshot.Weight <= 0.0f && snapshot.ActiveCount == 0)) {
			continue;
		}

		snapshots += String::Fmt(" {} {:.2f}x{}", snapshot.Name, snapshot.Weight, snapshot.ActiveCount);
		any_snapshot = true;
	}

	if (!any_snapshot) {
		snapshots += " none";
	}

	if (gAudio->GetSnapshotCount() > 0) {
		gTextRenderer->DrawText(snapshots.CStr(), 1.0f, scBusColor);
	}

	String ducks("ducks:");
	bool any_duck = false;

	for (uint32 i = 0; i < gAudio->GetDuckCount(); i++) {
		const MixDuck& duck = gAudio->GetDuck(i);

		if (duck.Envelope < 0.01f) {
			continue;
		}

		ducks += String::Fmt(" {} {:.0f}%", duck.Name, duck.Envelope * 100.0f);
		any_duck = true;
	}

	if (any_duck) {
		gTextRenderer->DrawText(ducks.CStr(), 1.0f, scActiveColor);
	}
}

void DrawVoices(AudioVoiceDebug* voices, uint32 voice_count)
{
	gTextRenderer->DrawText("VOICE              BUS          DIST  DGAIN   OCC  TRANS L/M/H     PATH   LEVEL", 1.0f,
							scHeader);

	const uint32 listed = std::min(voice_count, AudioDebugView::scMaxListedVoices);

	for (uint32 i = 0; i < listed; i++) {
		const AudioVoiceDebug& voice = voices[i];

		String distance("     -");
		String distance_gain("     -");

		if (voice.bSpatial) {
			distance = String::Fmt("{:5.1f}m", voice.Distance);
			distance_gain = FormatDb(AudioMeter::ToDb(voice.DistanceGain));
		}

		String occlusion("    -");
		String transmission("              -");
		String path("     -");

		if (voice.bSteam) {
			const SpatialVoiceDebug& spatial = voice.Spatial;

			occlusion = String::Fmt("{:5.2f}", spatial.Occlusion);
			transmission = String::Fmt("{:4.0f}/{:4.0f}/{:4.0f}", AudioMeter::ToDb(spatial.Transmission[0]),
									   AudioMeter::ToDb(spatial.Transmission[1]),
									   AudioMeter::ToDb(spatial.Transmission[2]));

			if (spatial.bHasPath) {
				path = FormatDb(AudioMeter::ToDb(spatial.PathGain));
			}
		}

		String flags;

		if (voice.bLoop) {
			flags += " loop";
		}

		if (voice.bPending) {
			flags += " pending";
		}

		if (voice.bStopping) {
			flags += " stopping";
		}

		if (!voice.bSpatial) {
			flags += " 2d";
		}

		gTextRenderer->DrawText(String::Fmt("{:<18} {:<10} {} {} {} {} {} {}{}", voice.Name, voice.Bus, distance,
											distance_gain, occlusion, transmission, path,
											FormatDb(GetVoiceLevelDb(voice)), flags)
									.CStr(),
								1.0f, GetVoiceColor(voice));
	}

	if (voice_count > listed) {
		gTextRenderer->DrawText(String::Fmt("... {} more", voice_count - listed).CStr(), 1.0f, scQuietColor);
	}
}

void DrawWorldLabels(const BaseCamera& camera, const AudioVoiceDebug* voices, uint32 voice_count)
{
	const Mat4f& view_projection = camera.GetCameraMatrix(eObjectLayer::WorldLayer);
	const Vec2u window_size = gGraphics->GetWindow()->GetSize();

	uint32 labels = 0;

	for (uint32 i = 0; i < voice_count && labels < AudioDebugView::scMaxWorldLabels; i++) {
		const AudioVoiceDebug& voice = voices[i];

		if (!voice.bSpatial) {
			continue;
		}

		const Vec3f anchor = voice.Position + Vec3f(0.0f, scLabelHeight, 0.0f);
		const Vec4f clip = view_projection * Vec4f(anchor.X, anchor.Y, anchor.Z, 1.0f);

		if (clip.W < scMinClipW) {
			continue;
		}

		const float32 x = clip.X / clip.W;
		const float32 y = clip.Y / clip.W;

		if (x < -1.0f || x > 1.0f || y < -1.0f || y > 1.0f) {
			continue;
		}

		const Vec2f pixel((x * 0.5f + 0.5f) * static_cast<float32>(window_size.X),
						  (y * 0.5f + 0.5f) * static_cast<float32>(window_size.Y));

		String label = String::Fmt("{} {}", voice.Name, FormatDb(GetVoiceLevelDb(voice)));

		if (voice.bSteam) {
			label += String::Fmt(" occ {:.2f}", voice.Spatial.Occlusion);
		}

		gTextRenderer->DrawTextAt(label.CStr(), pixel, 1.0f, GetVoiceColor(voice));
		++labels;
	}
}

uint32 CollectVoices(AudioVoiceDebug* voices)
{
	const uint32 count = gAudio->GetVoiceDebug(voices, AudioSystem::scMaxVoices);

	std::sort(voices, voices + count, [](const AudioVoiceDebug& a, const AudioVoiceDebug& b)
			  { return GetVoiceLevelDb(a) > GetVoiceLevelDb(b); });

	return count;
}

} // namespace

void AudioDebugView::Create() { mpDebugCVar = gCVars->Set<int64>("s_debug", 0); }

bool AudioDebugView::IsEnabled() const
{
	return mpDebugCVar != nullptr && mpDebugCVar->IntValue != 0 && gAudio != nullptr && gAudio->IsActive();
}

void AudioDebugView::DrawWorld()
{
	if (!IsEnabled() || mpDebugCVar->IntValue < 2) {
		return;
	}

	AudioVoiceDebug voices[AudioSystem::scMaxVoices];
	const uint32 count = gAudio->GetVoiceDebug(voices, AudioSystem::scMaxVoices);

	const Vec3f half_extent(scMarkerHalfExtent);

	for (uint32 i = 0; i < count; i++) {
		const AudioVoiceDebug& voice = voices[i];

		if (!voice.bSpatial) {
			continue;
		}

		// Green when the listener hears it directly, red when it is fully occluded
		Color color = Color(GetVoiceColor(voice));

		if (voice.bSteam && !voice.bPending) {
			const float32 occlusion = std::clamp(voice.Spatial.Occlusion, 0.0f, 1.0f);

			color = Color::FromRGBA(static_cast<uint8>(255.0f * (1.0f - occlusion)), static_cast<uint8>(255.0f * occlusion),
									60, 255);
		}

		gDebugDraw->WireBox(voice.Position, half_extent, Quat::scIdentity, color);
	}
}

void AudioDebugView::DrawText(const BaseCamera& camera)
{
	if (!IsEnabled()) {
		return;
	}

	AudioVoiceDebug voices[AudioSystem::scMaxVoices];
	const uint32 voice_count = CollectVoices(voices);

	DrawHeader();
	DrawBuses();
	DrawMixState();
	DrawVoices(voices, voice_count);

	if (mpDebugCVar->IntValue >= 2) {
		DrawWorldLabels(camera, voices, voice_count);
	}
}

} // namespace fx
