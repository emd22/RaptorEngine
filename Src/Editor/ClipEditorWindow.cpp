#include "ClipEditorWindow.hpp"

#include "EditorThread.hpp"

#include <miniaudio.h>
#include <wx/button.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <Asset/ConfigFile.hpp>
#include <Audio/AudioSystem.hpp>
#include <Core/FilesystemIO.hpp>
#include <Core/Log.hpp>
#include <Core/Random.hpp>
#include <Engine.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fx::editor {

namespace fs = std::filesystem;

enum eClipColumn : int32
{
	ClipColumn_Index = 0,
	ClipColumn_Start,
	ClipColumn_End,
	ClipColumn_Length,
};

static constexpr int32 scRulerHeight = 20;
static constexpr int32 scEdgeGrabPixels = 5;
static constexpr int32 scMinCreatePixels = 3;
static constexpr float64 scMinClipLength = 0.001;
static constexpr float64 scMinViewLength = 0.01;
static constexpr float64 scDetectWindow = 0.002;
static constexpr float64 scDetectMinLength = 0.01;
static constexpr float64 scDetectPreRoll = 0.005;
static constexpr float64 scDetectTail = 0.015;
static constexpr const char* scAudioWildcard = "Audio files (*.wav;*.mp3;*.flac)|*.wav;*.mp3;*.flac";

float64 ClipAudio::GetDuration() const
{
	if (SampleRate == 0) {
		return 0.0;
	}

	return static_cast<float64>(FrameCount) / SampleRate;
}

static fs::path GetSoundDirectory()
{
	const String resolved = FilesystemIO::ResolvePath(String(AudioSystem::scSoundDirectory));
	std::error_code error;
	fs::path path = fs::weakly_canonical(fs::absolute(resolved.CStr()), error);

	if (error) {
		return fs::path(resolved.CStr());
	}

	return path;
}

static wxString FormatSeconds(float64 seconds) { return wxString::Format("%.3f", seconds); }

struct ClipPreviewPlayer
{
	ma_engine Engine;
	ma_audio_buffer_ref Buffer;
	ma_sound Sound;

	bool bEngineReady = false;
	bool bEngineFailed = false;
	bool bSoundReady = false;

	float64 RangeStart = 0.0;
	uint32 SampleRate = 0;

	~ClipPreviewPlayer()
	{
		Stop();

		if (bEngineReady) {
			ma_engine_uninit(&Engine);
		}
	}

	bool Play(const ClipAudio& audio, float64 start, float64 end)
	{
		Stop();

		if (!audio.IsLoaded()) {
			return false;
		}

		if (!bEngineReady && !bEngineFailed) {
			if (ma_engine_init(nullptr, &Engine) == MA_SUCCESS) {
				bEngineReady = true;
			}
			else {
				bEngineFailed = true;
				LogError(LC_AUDIO, "Clip editor could not open an audio device for preview");
			}
		}

		if (!bEngineReady) {
			return false;
		}

		if (ma_audio_buffer_ref_init(ma_format_f32, audio.Channels, audio.Samples.data(), audio.FrameCount, &Buffer) !=
			MA_SUCCESS) {
			return false;
		}

		Buffer.sampleRate = audio.SampleRate;

		const ma_uint64 begin = std::min(static_cast<ma_uint64>(start * audio.SampleRate), audio.FrameCount);
		const ma_uint64 finish = std::clamp(static_cast<ma_uint64>(end * audio.SampleRate), begin,
											static_cast<ma_uint64>(audio.FrameCount));

		ma_data_source_set_range_in_pcm_frames(&Buffer, begin, finish);

		if (ma_sound_init_from_data_source(&Engine, &Buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &Sound) !=
			MA_SUCCESS) {
			ma_audio_buffer_ref_uninit(&Buffer);
			return false;
		}

		bSoundReady = true;
		RangeStart = static_cast<float64>(begin) / audio.SampleRate;
		SampleRate = audio.SampleRate;

		ma_sound_start(&Sound);

		return true;
	}

	void Stop()
	{
		if (!bSoundReady) {
			return;
		}

		ma_sound_uninit(&Sound);
		ma_audio_buffer_ref_uninit(&Buffer);
		bSoundReady = false;
	}

	float64 GetPlayhead()
	{
		if (!bSoundReady || ma_sound_at_end(&Sound) || !ma_sound_is_playing(&Sound)) {
			return -1.0;
		}

		ma_uint64 cursor = 0;

		if (ma_sound_get_cursor_in_pcm_frames(&Sound, &cursor) != MA_SUCCESS) {
			return -1.0;
		}

		return RangeStart + static_cast<float64>(cursor) / SampleRate;
	}
};

class ClipWaveformPanel : public wxPanel
{
public:
	ClipWaveformPanel(wxWindow* parent, ClipEditorWindow* owner)
		: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(900, 260), wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS),
		  mpOwner(owner)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);

		Bind(wxEVT_PAINT, &ClipWaveformPanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &ClipWaveformPanel::OnLeftDown, this);
		Bind(wxEVT_LEFT_UP, &ClipWaveformPanel::OnLeftUp, this);
		Bind(wxEVT_LEFT_DCLICK, &ClipWaveformPanel::OnDoubleClick, this);
		Bind(wxEVT_MIDDLE_DOWN, &ClipWaveformPanel::OnMiddleDown, this);
		Bind(wxEVT_MIDDLE_UP, &ClipWaveformPanel::OnMiddleUp, this);
		Bind(wxEVT_MOTION, &ClipWaveformPanel::OnMotion, this);
		Bind(wxEVT_MOUSEWHEEL, &ClipWaveformPanel::OnWheel, this);
		Bind(wxEVT_KEY_DOWN, &ClipWaveformPanel::OnKeyDown, this);
		Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { EndDrag(); });
	}

	void FitView()
	{
		mViewStart = 0.0;
		mViewLength = std::max(mpOwner->GetAudio().GetDuration(), scMinViewLength);
		Refresh();
	}

	void ShowTime(float64 start, float64 end)
	{
		if (start >= mViewStart && end <= mViewStart + mViewLength) {
			return;
		}

		mViewStart = (start + end) * 0.5 - mViewLength * 0.5;
		ClampView();
		Refresh();
	}

	void SetPlayhead(float64 seconds)
	{
		if (seconds == mPlayhead) {
			return;
		}

		mPlayhead = seconds;
		Refresh();
	}

private:
	enum class eDrag
	{
		None,
		Create,
		Move,
		ResizeStart,
		ResizeEnd,
		Pan,
	};

	float64 XToTime(int x) const
	{
		const int width = std::max(GetClientSize().x, 1);
		return mViewStart + mViewLength * static_cast<float64>(x) / width;
	}

	int TimeToX(float64 seconds) const
	{
		const int width = std::max(GetClientSize().x, 1);
		return static_cast<int>(std::lround((seconds - mViewStart) / mViewLength * width));
	}

	float64 ClampTime(float64 seconds) const { return std::clamp(seconds, 0.0, mpOwner->GetAudio().GetDuration()); }

	void ClampView()
	{
		const float64 duration = mpOwner->GetAudio().GetDuration();
		mViewLength = std::clamp(mViewLength, scMinViewLength, std::max(duration, scMinViewLength));
		mViewStart = std::clamp(mViewStart, 0.0, std::max(duration - mViewLength, 0.0));
	}

	eDrag HitTest(int x, int32& out_index) const
	{
		const std::vector<ClipRegion>& clips = mpOwner->GetClips();
		const int32 selected = mpOwner->GetSelected();

		out_index = -1;

		for (int32 pass = 0; pass < 2; pass++) {
			for (int32 i = 0; i < static_cast<int32>(clips.size()); i++) {
				if ((pass == 0) != (i == selected)) {
					continue;
				}

				if (std::abs(x - TimeToX(clips[i].Start)) <= scEdgeGrabPixels) {
					out_index = i;
					return eDrag::ResizeStart;
				}

				if (std::abs(x - TimeToX(clips[i].End)) <= scEdgeGrabPixels) {
					out_index = i;
					return eDrag::ResizeEnd;
				}
			}
		}

		for (int32 i = static_cast<int32>(clips.size()) - 1; i >= 0; i--) {
			if (x >= TimeToX(clips[i].Start) && x <= TimeToX(clips[i].End)) {
				out_index = i;
				return eDrag::Move;
			}
		}

		return eDrag::Create;
	}

	void OnLeftDown(wxMouseEvent& event)
	{
		SetFocus();

		if (!mpOwner->GetAudio().IsLoaded()) {
			return;
		}

		std::vector<ClipRegion>& clips = mpOwner->GetClips();
		const float64 time = ClampTime(XToTime(event.GetX()));

		int32 index = -1;
		mDrag = HitTest(event.GetX(), index);

		if (mDrag == eDrag::Create) {
			clips.push_back(ClipRegion { .Start = time, .End = time });
			index = static_cast<int32>(clips.size()) - 1;
			mDragAnchor = time;
		}
		else if (mDrag == eDrag::Move) {
			mDragAnchor = time - clips[index].Start;
		}

		mDragIndex = index;
		mpOwner->SetSelected(index);

		CaptureMouse();
		Refresh();
	}

	void OnMotion(wxMouseEvent& event)
	{
		if (mDrag == eDrag::None) {
			UpdateCursor(event.GetX());
			return;
		}

		if (mDrag == eDrag::Pan) {
			mViewStart = mPanStartView - (event.GetX() - mPanStartX) * mViewLength / std::max(GetClientSize().x, 1);
			ClampView();
			Refresh();
			return;
		}

		std::vector<ClipRegion>& clips = mpOwner->GetClips();

		if (mDragIndex < 0 || mDragIndex >= static_cast<int32>(clips.size())) {
			return;
		}

		ClipRegion& clip = clips[mDragIndex];
		const float64 time = ClampTime(XToTime(event.GetX()));
		const float64 duration = mpOwner->GetAudio().GetDuration();

		if (mDrag == eDrag::Create) {
			clip.Start = std::min(mDragAnchor, time);
			clip.End = std::max(mDragAnchor, time);
		}
		else if (mDrag == eDrag::Move) {
			const float64 length = clip.End - clip.Start;
			clip.Start = std::clamp(time - mDragAnchor, 0.0, std::max(duration - length, 0.0));
			clip.End = clip.Start + length;
		}
		else if (mDrag == eDrag::ResizeStart) {
			if (time > clip.End) {
				clip.Start = clip.End;
				clip.End = time;
				mDrag = eDrag::ResizeEnd;
			}
			else {
				clip.Start = time;
			}
		}
		else if (mDrag == eDrag::ResizeEnd) {
			if (time < clip.Start) {
				clip.End = clip.Start;
				clip.Start = time;
				mDrag = eDrag::ResizeStart;
			}
			else {
				clip.End = time;
			}
		}

		mpOwner->OnClipChanged(mDragIndex);
		Refresh();
	}

	void OnLeftUp(wxMouseEvent&)
	{
		if (mDrag == eDrag::None || mDrag == eDrag::Pan) {
			return;
		}

		std::vector<ClipRegion>& clips = mpOwner->GetClips();
		const eDrag drag = mDrag;

		EndDrag();

		if (mDragIndex < 0 || mDragIndex >= static_cast<int32>(clips.size())) {
			return;
		}

		ClipRegion& clip = clips[mDragIndex];

		if (drag == eDrag::Create && TimeToX(clip.End) - TimeToX(clip.Start) < scMinCreatePixels) {
			clips.erase(clips.begin() + mDragIndex);
			mpOwner->SetSelected(-1);
			Refresh();
			return;
		}

		if (clip.End - clip.Start < scMinClipLength) {
			clip.End = std::min(clip.Start + scMinClipLength, mpOwner->GetAudio().GetDuration());
			clip.Start = std::min(clip.Start, clip.End - scMinClipLength);
		}

		mpOwner->OnClipsCommitted();
		Refresh();
	}

	void OnDoubleClick(wxMouseEvent& event)
	{
		int32 index = -1;

		if (HitTest(event.GetX(), index) == eDrag::Create) {
			FitView();
			return;
		}

		mpOwner->SetSelected(index);
		mpOwner->PlaySelected();
	}

	void OnMiddleDown(wxMouseEvent& event)
	{
		if (mDrag != eDrag::None) {
			return;
		}

		mDrag = eDrag::Pan;
		mPanStartX = event.GetX();
		mPanStartView = mViewStart;
		CaptureMouse();
	}

	void OnMiddleUp(wxMouseEvent&)
	{
		if (mDrag == eDrag::Pan) {
			EndDrag();
		}
	}

	void EndDrag()
	{
		mDrag = eDrag::None;

		if (HasCapture()) {
			ReleaseMouse();
		}
	}

	void OnWheel(wxMouseEvent& event)
	{
		if (!mpOwner->GetAudio().IsLoaded()) {
			return;
		}

		const float64 steps = static_cast<float64>(event.GetWheelRotation()) / std::max(event.GetWheelDelta(), 1);

		if (event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || event.ShiftDown()) {
			mViewStart += steps * mViewLength * 0.1;
		}
		else {
			const float64 anchor = XToTime(event.GetX());
			const float64 old_length = mViewLength;

			mViewLength *= std::pow(1.2, -steps);
			ClampView();
			mViewStart = anchor - (anchor - mViewStart) * (mViewLength / old_length);
		}

		ClampView();
		Refresh();
	}

	void OnKeyDown(wxKeyEvent& event)
	{
		const int key = event.GetKeyCode();

		if (key == WXK_DELETE || key == WXK_BACK) {
			mpOwner->DeleteSelected();
		}
		else if (key == WXK_SPACE) {
			mpOwner->PlaySelected();
		}
		else if (key == 'R') {
			mpOwner->PlayRandom();
		}
		else if (key == 'F') {
			FitView();
		}
		else if (key == WXK_ESCAPE) {
			mpOwner->StopPlayback();
		}
		else {
			event.Skip();
		}
	}

	void UpdateCursor(int x)
	{
		int32 index = -1;
		const eDrag hit = HitTest(x, index);

		if (hit == eDrag::ResizeStart || hit == eDrag::ResizeEnd) {
			SetCursor(wxCursor(wxCURSOR_SIZEWE));
		}
		else if (hit == eDrag::Move) {
			SetCursor(wxCursor(wxCURSOR_HAND));
		}
		else {
			SetCursor(wxCursor(wxCURSOR_IBEAM));
		}
	}

	static float64 PickRulerStep(float64 seconds_per_pixel)
	{
		static constexpr float64 scSteps[] = { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1,	0.2,
											   0.5,	  1.0,	 2.0,	5.0,  10.0, 20.0, 30.0, 60.0 };

		for (const float64 step : scSteps) {
			if (step / seconds_per_pixel >= 80.0) {
				return step;
			}
		}

		return 120.0;
	}

	void OnPaint(wxPaintEvent&)
	{
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(wxColour(30, 30, 32)));
		dc.Clear();

		const wxSize client = GetClientSize();
		const ClipAudio& audio = mpOwner->GetAudio();

		if (!audio.IsLoaded()) {
			dc.SetTextForeground(wxColour(150, 150, 150));
			const wxString text = "Open a sound file or a sound .conf to start";
			const wxSize extent = dc.GetTextExtent(text);
			dc.DrawText(text, (client.x - extent.x) / 2, (client.y - extent.y) / 2);
			return;
		}

		const int wave_top = scRulerHeight;
		const int wave_height = std::max(client.y - wave_top, 1);
		const int mid = wave_top + wave_height / 2;
		const float64 half = wave_height * 0.47;

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(44, 44, 48)));
		dc.DrawRectangle(0, 0, client.x, scRulerHeight);

		const std::vector<ClipRegion>& clips = mpOwner->GetClips();
		const int32 selected = mpOwner->GetSelected();

		for (int32 i = 0; i < static_cast<int32>(clips.size()); i++) {
			const int x0 = TimeToX(clips[i].Start);
			const int x1 = std::max(TimeToX(clips[i].End), x0 + 1);

			if (x1 < 0 || x0 > client.x) {
				continue;
			}

			if (i == selected) {
				dc.SetBrush(wxBrush(wxColour(255, 190, 40, 60)));
				dc.SetPen(wxPen(wxColour(255, 190, 40), 2));
			}
			else {
				dc.SetBrush(wxBrush(wxColour(60, 150, 230, 50)));
				dc.SetPen(wxPen(wxColour(60, 150, 230), 1));
			}

			dc.DrawRectangle(x0, wave_top, x1 - x0, wave_height);
		}

		dc.SetPen(wxPen(wxColour(70, 70, 76), 1));
		dc.DrawLine(0, mid, client.x, mid);

		dc.SetPen(wxPen(wxColour(120, 205, 140), 1));

		const uint32 channels = audio.Channels;
		const float64 frames_per_pixel = mViewLength * audio.SampleRate / std::max(client.x, 1);

		for (int x = 0; x < client.x; x++) {
			const float64 first = (mViewStart * audio.SampleRate) + x * frames_per_pixel;

			if (first >= static_cast<float64>(audio.FrameCount)) {
				break;
			}

			const uint64 begin = static_cast<uint64>(std::max(first, 0.0));
			const uint64 end = std::min(std::max(static_cast<uint64>(first + frames_per_pixel), begin + 1),
										audio.FrameCount);

			float32 low = 0.0f;
			float32 high = 0.0f;

			for (uint64 frame = begin; frame < end; frame++) {
				for (uint32 channel = 0; channel < channels; channel++) {
					const float32 value = audio.Samples[frame * channels + channel];
					low = std::min(low, value);
					high = std::max(high, value);
				}
			}

			const int y_high = mid - static_cast<int>(high * half);
			const int y_low = mid - static_cast<int>(low * half);
			dc.DrawLine(x, y_high, x, y_low + 1);
		}

		dc.SetTextForeground(wxColour(170, 170, 176));
		dc.SetPen(wxPen(wxColour(110, 110, 118), 1));

		const float64 step = PickRulerStep(mViewLength / std::max(client.x, 1));
		const int decimals = std::clamp(static_cast<int>(std::ceil(-std::log10(step))), 0, 3);

		for (float64 tick = std::floor(mViewStart / step) * step; tick <= mViewStart + mViewLength; tick += step) {
			const int x = TimeToX(tick);
			dc.DrawLine(x, scRulerHeight - 6, x, scRulerHeight);
			dc.DrawText(wxString::Format("%.*fs", decimals, tick), x + 3, 2);
		}

		for (int32 i = 0; i < static_cast<int32>(clips.size()); i++) {
			const int x0 = TimeToX(clips[i].Start);

			if (x0 < -40 || x0 > client.x) {
				continue;
			}

			if (i == selected) {
				dc.SetTextForeground(wxColour(255, 210, 90));
			}
			else {
				dc.SetTextForeground(wxColour(140, 195, 250));
			}

			dc.DrawText(wxString::Format("%d", i + 1), x0 + 4, wave_top + 3);
		}

		if (mPlayhead >= 0.0) {
			const int x = TimeToX(mPlayhead);
			dc.SetPen(wxPen(wxColour(240, 80, 70), 1));
			dc.DrawLine(x, 0, x, client.y);
		}
	}

private:
	ClipEditorWindow* mpOwner = nullptr;

	float64 mViewStart = 0.0;
	float64 mViewLength = 1.0;
	float64 mPlayhead = -1.0;

	eDrag mDrag = eDrag::None;
	int32 mDragIndex = -1;
	float64 mDragAnchor = 0.0;

	int mPanStartX = 0;
	float64 mPanStartView = 0.0;
};

ClipEditorWindow::ClipEditorWindow(wxWindow* parent)
	: wxFrame(parent, wxID_ANY, "Clip Editor", wxDefaultPosition, wxSize(1000, 620)), mTimer(this)
{
	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* file_row = new wxBoxSizer(wxHORIZONTAL);
	wxButton* open_sound_button = new wxButton(root, wxID_ANY, "Open Sound...");
	wxButton* open_conf_button = new wxButton(root, wxID_ANY, "Open Sound Def...");
	mpFileLabel = new wxStaticText(root, wxID_ANY, "No sound loaded");
	file_row->Add(open_sound_button, wxSizerFlags().Border(wxRIGHT, 4));
	file_row->Add(open_conf_button, wxSizerFlags().Border(wxRIGHT, 12));
	file_row->Add(mpFileLabel, wxSizerFlags(1).CenterVertical());
	sizer->Add(file_row, wxSizerFlags().Expand().Border(wxALL, 6));

	mpWaveform = new ClipWaveformPanel(root, this);
	sizer->Add(mpWaveform, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, 6));

	wxStaticText* hint = new wxStaticText(
		root, wxID_ANY,
		"Drag to add a clip, drag its edges to trim, drag inside to move. Wheel zooms, Shift+wheel or middle-drag "
		"pans.\nSpace plays, R plays a random clip, Delete removes, F or double-click on empty space fits the view.");
	hint->SetForegroundColour(wxColour(140, 140, 140));
	sizer->Add(hint, wxSizerFlags().Expand().Border(wxALL, 6));

	wxBoxSizer* bottom = new wxBoxSizer(wxHORIZONTAL);

	mpList = new wxListCtrl(root, wxID_ANY, wxDefaultPosition, wxSize(360, 200), wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->InsertColumn(ClipColumn_Index, "#", wxLIST_FORMAT_LEFT, 40);
	mpList->InsertColumn(ClipColumn_Start, "Start", wxLIST_FORMAT_LEFT, 90);
	mpList->InsertColumn(ClipColumn_End, "End", wxLIST_FORMAT_LEFT, 90);
	mpList->InsertColumn(ClipColumn_Length, "Length", wxLIST_FORMAT_LEFT, 90);
	bottom->Add(mpList, wxSizerFlags().Expand().Border(wxLEFT | wxBOTTOM, 6));

	wxBoxSizer* controls = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* range_row = new wxBoxSizer(wxHORIZONTAL);
	range_row->Add(new wxStaticText(root, wxID_ANY, "Start"), wxSizerFlags().CenterVertical().Border(wxRIGHT, 4));
	mpStart = new wxSpinCtrlDouble(root, wxID_ANY, "", wxDefaultPosition, wxSize(100, -1), wxSP_ARROW_KEYS, 0.0, 1.0,
								   0.0, 0.001);
	mpStart->SetDigits(3);
	range_row->Add(mpStart, wxSizerFlags().CenterVertical().Border(wxRIGHT, 12));
	range_row->Add(new wxStaticText(root, wxID_ANY, "End"), wxSizerFlags().CenterVertical().Border(wxRIGHT, 4));
	mpEnd = new wxSpinCtrlDouble(root, wxID_ANY, "", wxDefaultPosition, wxSize(100, -1), wxSP_ARROW_KEYS, 0.0, 1.0, 0.0,
								 0.001);
	mpEnd->SetDigits(3);
	range_row->Add(mpEnd, wxSizerFlags().CenterVertical());
	controls->Add(range_row, wxSizerFlags().Border(wxBOTTOM, 8));

	wxBoxSizer* play_row = new wxBoxSizer(wxHORIZONTAL);
	mpPlayButton = new wxButton(root, wxID_ANY, "Play");
	mpRandomButton = new wxButton(root, wxID_ANY, "Play Random");
	wxButton* stop_button = new wxButton(root, wxID_ANY, "Stop");
	mpDeleteButton = new wxButton(root, wxID_ANY, "Delete Clip");
	play_row->Add(mpPlayButton, wxSizerFlags().Border(wxRIGHT, 4));
	play_row->Add(mpRandomButton, wxSizerFlags().Border(wxRIGHT, 4));
	play_row->Add(stop_button, wxSizerFlags().Border(wxRIGHT, 12));
	play_row->Add(mpDeleteButton, wxSizerFlags());
	controls->Add(play_row, wxSizerFlags().Border(wxBOTTOM, 8));

	wxBoxSizer* detect_row = new wxBoxSizer(wxHORIZONTAL);
	detect_row->Add(new wxStaticText(root, wxID_ANY, "Threshold (dB below peak)"),
					wxSizerFlags().CenterVertical().Border(wxRIGHT, 4));
	mpThreshold = new wxSpinCtrlDouble(root, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1.0,
									   90.0, 30.0, 1.0);
	detect_row->Add(mpThreshold, wxSizerFlags().CenterVertical().Border(wxRIGHT, 12));
	detect_row->Add(new wxStaticText(root, wxID_ANY, "Min gap (ms)"),
					wxSizerFlags().CenterVertical().Border(wxRIGHT, 4));
	mpMinGap = new wxSpinCtrlDouble(root, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1.0, 2000.0,
									80.0, 5.0);
	detect_row->Add(mpMinGap, wxSizerFlags().CenterVertical().Border(wxRIGHT, 12));
	mpDetectButton = new wxButton(root, wxID_ANY, "Auto Detect");
	detect_row->Add(mpDetectButton, wxSizerFlags().CenterVertical());
	controls->Add(detect_row, wxSizerFlags().Border(wxBOTTOM, 8));

	wxBoxSizer* save_row = new wxBoxSizer(wxHORIZONTAL);
	mpSaveButton = new wxButton(root, wxID_ANY, "Save");
	mpSaveAsButton = new wxButton(root, wxID_ANY, "Save As...");
	save_row->Add(mpSaveButton, wxSizerFlags().Border(wxRIGHT, 4));
	save_row->Add(mpSaveAsButton, wxSizerFlags());
	controls->Add(save_row, wxSizerFlags().Border(wxBOTTOM, 8));

	mpStatus = new wxStaticText(root, wxID_ANY, "");
	controls->Add(mpStatus, wxSizerFlags().Expand());

	bottom->Add(controls, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 6));
	sizer->Add(bottom, wxSizerFlags().Expand());

	root->SetSizer(sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	open_sound_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenSound(); });
	open_conf_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenConf(); });
	mpPlayButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PlaySelected(); });
	mpRandomButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { PlayRandom(); });
	stop_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StopPlayback(); });
	mpDeleteButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { DeleteSelected(); });
	mpDetectButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { AutoDetect(); });
	mpSaveButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Save(false); });
	mpSaveAsButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Save(true); });

	auto on_range_edit = [this](wxSpinDoubleEvent&)
	{
		if (mbUpdatingControls || mSelected < 0 || mSelected >= static_cast<int32>(mClips.size())) {
			return;
		}

		ClipRegion& clip = mClips[mSelected];
		const float64 duration = mAudio.GetDuration();

		clip.Start = std::clamp(mpStart->GetValue(), 0.0, std::max(duration - scMinClipLength, 0.0));
		clip.End = std::clamp(mpEnd->GetValue(), clip.Start + scMinClipLength, std::max(duration, scMinClipLength));

		OnClipsCommitted();
		mpWaveform->ShowTime(clip.Start, clip.End);
	};

	mpStart->Bind(wxEVT_SPINCTRLDOUBLE, on_range_edit);
	mpEnd->Bind(wxEVT_SPINCTRLDOUBLE, on_range_edit);

	mpList->Bind(wxEVT_LIST_ITEM_SELECTED,
				 [this](wxListEvent& event)
				 {
					 if (mbUpdatingControls) {
						 return;
					 }

					 SetSelected(static_cast<int32>(event.GetIndex()));

					 if (mSelected >= 0) {
						 mpWaveform->ShowTime(mClips[mSelected].Start, mClips[mSelected].End);
					 }
				 });
	mpList->Bind(wxEVT_LIST_ITEM_ACTIVATED,
				 [this](wxListEvent& event)
				 {
					 SetSelected(static_cast<int32>(event.GetIndex()));
					 PlaySelected();
				 });

	Bind(wxEVT_TIMER, &ClipEditorWindow::OnTimer, this);
	Bind(wxEVT_CLOSE_WINDOW, &ClipEditorWindow::OnClose, this);

	UpdateSelectionControls();
	UpdateTitle();
}

ClipEditorWindow::~ClipEditorWindow()
{
	mTimer.Stop();
	mpPlayer.reset();
}

void ClipEditorWindow::SetSelected(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(mClips.size())) {
		index = -1;
	}

	mSelected = index;

	mbUpdatingControls = true;

	const long current = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (current >= 0 && current != index) {
		mpList->SetItemState(current, 0, wxLIST_STATE_SELECTED);
	}

	if (index >= 0 && index < mpList->GetItemCount()) {
		mpList->SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		mpList->EnsureVisible(index);
	}

	mbUpdatingControls = false;

	UpdateSelectionControls();
	mpWaveform->Refresh();
}

void ClipEditorWindow::OnClipChanged(int32 index)
{
	if (index >= mpList->GetItemCount()) {
		RebuildList();
	}
	else {
		UpdateListRow(index);
	}

	UpdateSelectionControls();
}

void ClipEditorWindow::OnClipsCommitted()
{
	SortClips();
	RebuildList();
	SetSelected(mSelected);
	SetDirty(true);
}

void ClipEditorWindow::SortClips()
{
	ClipRegion selected_clip;
	const bool has_selection = mSelected >= 0 && mSelected < static_cast<int32>(mClips.size());

	if (has_selection) {
		selected_clip = mClips[mSelected];
	}

	std::stable_sort(mClips.begin(), mClips.end(),
					 [](const ClipRegion& a, const ClipRegion& b) { return a.Start < b.Start; });

	if (!has_selection) {
		return;
	}

	for (int32 i = 0; i < static_cast<int32>(mClips.size()); i++) {
		if (mClips[i].Start == selected_clip.Start && mClips[i].End == selected_clip.End) {
			mSelected = i;
			return;
		}
	}
}

void ClipEditorWindow::RebuildList()
{
	mbUpdatingControls = true;

	mpList->Freeze();
	mpList->DeleteAllItems();

	for (int32 i = 0; i < static_cast<int32>(mClips.size()); i++) {
		mpList->InsertItem(i, wxString::Format("%d", i + 1));
		UpdateListRow(i);
	}

	mpList->Thaw();

	mbUpdatingControls = false;
}

void ClipEditorWindow::UpdateListRow(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(mClips.size()) || index >= mpList->GetItemCount()) {
		return;
	}

	const ClipRegion& clip = mClips[index];

	mpList->SetItem(index, ClipColumn_Start, FormatSeconds(clip.Start));
	mpList->SetItem(index, ClipColumn_End, FormatSeconds(clip.End));
	mpList->SetItem(index, ClipColumn_Length, FormatSeconds(clip.End - clip.Start));
}

void ClipEditorWindow::UpdateSelectionControls()
{
	const bool loaded = mAudio.IsLoaded();
	const bool has_selection = mSelected >= 0 && mSelected < static_cast<int32>(mClips.size());
	const float64 duration = std::max(mAudio.GetDuration(), scMinClipLength);

	mbUpdatingControls = true;

	mpStart->SetRange(0.0, duration);
	mpEnd->SetRange(0.0, duration);

	if (has_selection) {
		mpStart->SetValue(mClips[mSelected].Start);
		mpEnd->SetValue(mClips[mSelected].End);
	}

	mbUpdatingControls = false;

	mpStart->Enable(has_selection);
	mpEnd->Enable(has_selection);
	mpDeleteButton->Enable(has_selection);
	mpPlayButton->Enable(loaded);
	mpRandomButton->Enable(!mClips.empty());
	mpDetectButton->Enable(loaded);
	mpSaveButton->Enable(loaded && !mClips.empty());
	mpSaveAsButton->Enable(loaded && !mClips.empty());
}

void ClipEditorWindow::UpdateTitle()
{
	wxString title = "Clip Editor";

	if (!mAudioPath.empty()) {
		title += " - " + wxString::FromUTF8(fs::path(mAudioPath).filename().string());
	}

	if (!mConfPath.empty()) {
		title += " (" + wxString::FromUTF8(fs::path(mConfPath).filename().string()) + ")";
	}

	if (mbDirty) {
		title += " *";
	}

	SetTitle(title);
}

void ClipEditorWindow::SetDirty(bool dirty)
{
	mbDirty = dirty;
	UpdateTitle();
}

bool ClipEditorWindow::ConfirmDiscard()
{
	if (!mbDirty) {
		return true;
	}

	return wxMessageBox("Discard the unsaved clips?", "Clip Editor", wxYES_NO | wxICON_QUESTION, this) == wxYES;
}

bool ClipEditorWindow::LoadAudio(const std::string& path)
{
	StopPlayback();

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	ma_decoder decoder;

	if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
		wxMessageBox(wxString::FromUTF8("Could not decode '" + path + "'."), "Clip Editor", wxOK | wxICON_ERROR, this);
		return false;
	}

	ClipAudio audio;
	audio.Channels = decoder.outputChannels;
	audio.SampleRate = decoder.outputSampleRate;

	constexpr ma_uint64 scChunkFrames = 16384;
	std::vector<float32> chunk(scChunkFrames * audio.Channels);

	while (true) {
		ma_uint64 frames_read = 0;
		const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), scChunkFrames, &frames_read);

		audio.Samples.insert(audio.Samples.end(), chunk.begin(), chunk.begin() + frames_read * audio.Channels);
		audio.FrameCount += frames_read;

		if (result != MA_SUCCESS || frames_read < scChunkFrames) {
			break;
		}
	}

	ma_decoder_uninit(&decoder);

	if (!audio.IsLoaded()) {
		wxMessageBox(wxString::FromUTF8("'" + path + "' has no audio."), "Clip Editor", wxOK | wxICON_ERROR, this);
		return false;
	}

	mAudio = std::move(audio);
	mAudioPath = path;
	mClips.clear();
	mSelected = -1;
	mLastRandom = -1;

	mpFileLabel->SetLabel(wxString::FromUTF8(path) + wxString::Format("  (%.3f s, %u Hz, %u ch)", mAudio.GetDuration(),
																	  mAudio.SampleRate, mAudio.Channels));
	mpWaveform->FitView();
	RebuildList();
	UpdateSelectionControls();

	return true;
}

void ClipEditorWindow::OpenSound()
{
	if (!ConfirmDiscard()) {
		return;
	}

	wxFileDialog dialog(this, "Open sound", wxString::FromUTF8(GetSoundDirectory().string()), "", scAudioWildcard,
						wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	if (LoadAudio(std::string(dialog.GetPath().utf8_string()))) {
		mConfPath.clear();
		mbConfHasMultipleFiles = false;
		SetDirty(false);
		mpStatus->SetLabel("Drag on the waveform to add clips, or use Auto Detect");
	}
}

void ClipEditorWindow::OpenConf()
{
	if (!ConfirmDiscard()) {
		return;
	}

	wxFileDialog dialog(this, "Open sound definition", wxString::FromUTF8(GetSoundDirectory().string()), "",
						"Sound definitions (*.conf)|*.conf", wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	LoadConf(std::string(dialog.GetPath().utf8_string()));
}

bool ClipEditorWindow::LoadConf(const std::string& path)
{
	ConfigFile config;
	config.Load(path);

	if (config.HasErrors()) {
		wxMessageBox(wxString::FromUTF8("Could not parse '" + path + "'."), "Clip Editor", wxOK | wxICON_ERROR, this);
		return false;
	}

	std::vector<std::string> files;

	const ConfigEntry* files_entry = config.GetEntry(HashStr32("files"));

	if (files_entry != nullptr && files_entry->bIsArray && files_entry->ArrayData.IsInited()) {
		for (uint32 i = 0; i < files_entry->ArrayData.Size(); i++) {
			files.push_back(files_entry->ArrayData[i].Get<const char*>());
		}
	}

	const ConfigEntry* file_entry = config.GetEntry(HashStr32("file"));

	if (file_entry != nullptr && !file_entry->bIsArray) {
		files.push_back(file_entry->GetValue<const char*>());
	}

	if (files.empty()) {
		wxMessageBox("This sound definition has no 'file' or 'files'.", "Clip Editor", wxOK | wxICON_ERROR, this);
		return false;
	}

	const std::string audio_path = (GetSoundDirectory() / files[0]).string();

	if (!LoadAudio(audio_path)) {
		return false;
	}

	const ConfigEntry* clips_entry = config.GetEntry(HashStr32("clips"));

	if (clips_entry != nullptr && clips_entry->Members.IsInited()) {
		for (const ConfigEntry& clip : clips_entry->Members) {
			if (!clip.bIsArray || !clip.ArrayData.IsInited() || clip.ArrayData.Size() != 2) {
				continue;
			}

			const float64 start = clip.ArrayData[0].Get<float32>();
			const float64 end = clip.ArrayData[1].Get<float32>();

			if (start >= 0.0 && end > start) {
				mClips.push_back(ClipRegion { .Start = start, .End = end });
			}
		}
	}

	mConfPath = path;
	mbConfHasMultipleFiles = files.size() > 1;

	SortClips();
	RebuildList();
	SetSelected(-1);
	SetDirty(false);

	if (mbConfHasMultipleFiles) {
		mpStatus->SetLabel(wxString::Format(
			"This def lists %zu files; showing the first. Clips apply to every file and saving keeps the list.",
			files.size()));
	}
	else {
		mpStatus->SetLabel(wxString::Format("Loaded %zu clips", mClips.size()));
	}

	return true;
}

void ClipEditorWindow::AutoDetect()
{
	if (!mAudio.IsLoaded()) {
		return;
	}

	if (!mClips.empty() && wxMessageBox("Replace the current clips with detected ones?", "Clip Editor",
										wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		return;
	}

	const uint64 window = std::max<uint64>(static_cast<uint64>(scDetectWindow * mAudio.SampleRate), 1);
	const uint64 window_count = (mAudio.FrameCount + window - 1) / window;

	std::vector<float32> envelope(window_count, 0.0f);
	float32 peak = 0.0f;

	for (uint64 w = 0; w < window_count; w++) {
		const uint64 end = std::min((w + 1) * window, mAudio.FrameCount);

		for (uint64 frame = w * window; frame < end; frame++) {
			for (uint32 channel = 0; channel < mAudio.Channels; channel++) {
				envelope[w] = std::max(envelope[w], std::fabs(mAudio.Samples[frame * mAudio.Channels + channel]));
			}
		}

		peak = std::max(peak, envelope[w]);
	}

	if (peak <= 0.0f) {
		mpStatus->SetLabel("The sound is silent, nothing to detect");
		return;
	}

	const float32 threshold = peak * static_cast<float32>(std::pow(10.0, -mpThreshold->GetValue() / 20.0));
	const float64 window_seconds = static_cast<float64>(window) / mAudio.SampleRate;
	const float64 min_gap = mpMinGap->GetValue() / 1000.0;

	std::vector<ClipRegion> detected;
	bool active = false;
	float64 start = 0.0;

	for (uint64 w = 0; w <= window_count; w++) {
		const bool loud = w < window_count && envelope[w] >= threshold;

		if (loud && !active) {
			start = w * window_seconds;
			active = true;
		}
		else if (!loud && active) {
			const float64 end = w * window_seconds;
			active = false;

			if (!detected.empty() && start - detected.back().End < min_gap) {
				detected.back().End = end;
			}
			else {
				detected.push_back(ClipRegion { .Start = start, .End = end });
			}
		}
	}

	std::erase_if(detected, [](const ClipRegion& clip) { return clip.End - clip.Start < scDetectMinLength; });

	const float64 duration = mAudio.GetDuration();

	for (size_t i = 0; i < detected.size(); i++) {
		float64 earliest = 0.0;
		float64 latest = duration;

		if (i > 0) {
			earliest = detected[i - 1].End;
		}

		if (i + 1 < detected.size()) {
			latest = detected[i + 1].Start - scDetectPreRoll;
		}

		detected[i].Start = std::max(detected[i].Start - scDetectPreRoll, earliest);
		detected[i].End = std::clamp(detected[i].End + scDetectTail, detected[i].End,
									 std::max(latest, detected[i].End));
		detected[i].End = std::min(detected[i].End, duration);
	}

	mClips = std::move(detected);
	mSelected = -1;
	mLastRandom = -1;

	RebuildList();
	SetSelected(-1);
	SetDirty(true);

	mpStatus->SetLabel(wxString::Format("Detected %zu clips", mClips.size()));
}

void ClipEditorWindow::PlayRange(float64 start, float64 end)
{
	if (mpPlayer == nullptr) {
		mpPlayer = std::make_unique<ClipPreviewPlayer>();
	}

	if (mpPlayer->Play(mAudio, start, end)) {
		mTimer.Start(16);
	}
	else if (mpPlayer->bEngineFailed) {
		mpStatus->SetLabel("Preview is unavailable: no audio output device");
	}
}

void ClipEditorWindow::PlaySelected()
{
	if (!mAudio.IsLoaded()) {
		return;
	}

	if (mSelected >= 0 && mSelected < static_cast<int32>(mClips.size())) {
		PlayRange(mClips[mSelected].Start, mClips[mSelected].End);
	}
	else {
		PlayRange(0.0, mAudio.GetDuration());
	}
}

void ClipEditorWindow::PlayRandom()
{
	const uint32 count = static_cast<uint32>(mClips.size());

	if (count == 0) {
		return;
	}

	uint32 index = FastRand32() % count;

	if (count > 1 && static_cast<int32>(index) == mLastRandom) {
		index = (index + 1) % count;
	}

	mLastRandom = static_cast<int32>(index);

	SetSelected(static_cast<int32>(index));
	PlayRange(mClips[index].Start, mClips[index].End);
}

void ClipEditorWindow::StopPlayback()
{
	if (mpPlayer != nullptr) {
		mpPlayer->Stop();
	}

	mTimer.Stop();
	mpWaveform->SetPlayhead(-1.0);
}

void ClipEditorWindow::DeleteSelected()
{
	if (mSelected < 0 || mSelected >= static_cast<int32>(mClips.size())) {
		return;
	}

	mClips.erase(mClips.begin() + mSelected);

	const int32 next = std::min(mSelected, static_cast<int32>(mClips.size()) - 1);

	RebuildList();
	SetSelected(next);
	SetDirty(true);
}

bool ClipEditorWindow::ResolveSoundFileKey(std::string& out_key)
{
	const fs::path sound_directory = GetSoundDirectory();

	std::error_code error;
	fs::path audio = fs::weakly_canonical(fs::absolute(mAudioPath), error);

	if (error) {
		audio = fs::path(mAudioPath);
	}

	const fs::path relative = audio.lexically_relative(sound_directory);

	if (!relative.empty() && *relative.begin() != "..") {
		out_key = relative.generic_string();
		return true;
	}

	const fs::path target = sound_directory / audio.filename();

	const wxString question = wxString::FromUTF8("Sounds have to live in " + sound_directory.string() + ".\n\nCopy '" +
												 audio.filename().string() + "' there?");

	if (wxMessageBox(question, "Clip Editor", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
		return false;
	}

	if (fs::exists(target) && wxMessageBox(wxString::FromUTF8("'" + target.filename().string() +
															  "' already exists in the sounds folder. Replace it?"),
										   "Clip Editor", wxYES_NO | wxICON_WARNING, this) != wxYES) {
		return false;
	}

	fs::copy_file(audio, target, fs::copy_options::overwrite_existing, error);

	if (error) {
		wxMessageBox(wxString::FromUTF8("Could not copy the sound: " + error.message()), "Clip Editor",
					 wxOK | wxICON_ERROR, this);
		return false;
	}

	mAudioPath = target.string();
	out_key = target.filename().generic_string();

	return true;
}

static std::string GetEntryName(const std::string& line)
{
	const size_t position = line.find_first_not_of(" \t");

	if (position == std::string::npos) {
		return "";
	}

	const size_t name_end = line.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_",
												   position);
	const size_t equals = line.find_first_not_of(" \t", name_end);

	if (name_end == position || equals == std::string::npos || line[equals] != '=') {
		return "";
	}

	return line.substr(position, name_end - position);
}

static void CountBrackets(const std::string& line, int32& depth)
{
	bool in_string = false;

	for (const char c : line) {
		if (c == '"') {
			in_string = !in_string;
		}
		else if (!in_string && c == '[') {
			depth++;
		}
		else if (!in_string && c == ']') {
			depth--;
		}
	}
}

void ClipEditorWindow::Save(bool choose_path)
{
	if (!mAudio.IsLoaded() || mClips.empty()) {
		return;
	}

	std::string file_key;

	if (!mbConfHasMultipleFiles && !ResolveSoundFileKey(file_key)) {
		return;
	}

	std::string conf_path = mConfPath;
	const fs::path sound_directory = GetSoundDirectory();

	if (choose_path || conf_path.empty()) {
		wxFileDialog dialog(this, "Save sound definition", wxString::FromUTF8(sound_directory.string()),
							wxString::FromUTF8(fs::path(mAudioPath).stem().string() + ".conf"),
							"Sound definitions (*.conf)|*.conf", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

		if (dialog.ShowModal() != wxID_OK) {
			return;
		}

		conf_path = std::string(dialog.GetPath().utf8_string());

		if (fs::path(conf_path).extension() != ".conf") {
			conf_path += ".conf";
		}
	}

	std::ostringstream clips_text;
	clips_text << "clips = [\n";

	char buffer[64];

	for (const ClipRegion& clip : mClips) {
		std::snprintf(buffer, sizeof(buffer), "\t[%.3f, %.3f],\n", clip.Start, clip.End);
		clips_text << buffer;
	}

	clips_text << "]";

	std::string file_line;

	if (!mbConfHasMultipleFiles) {
		file_line = "file = \"" + file_key + "\"";
	}

	std::vector<std::string> lines;
	bool placed_file = mbConfHasMultipleFiles;
	bool placed_clips = false;

	{
		std::ifstream existing(conf_path);

		if (!existing.is_open()) {
			lines.push_back("bus = \"sfx\"");
		}

		std::string line;
		int32 skip_depth = 0;

		while (std::getline(existing, line)) {
			if (skip_depth > 0) {
				CountBrackets(line, skip_depth);
				continue;
			}

			const std::string name = GetEntryName(line);
			const bool is_file = !mbConfHasMultipleFiles && (name == "file" || name == "files");
			const bool is_clips = name == "clips";

			if (!is_file && !is_clips) {
				lines.push_back(line);
				continue;
			}

			CountBrackets(line, skip_depth);

			if (is_file && !placed_file) {
				lines.push_back(file_line);
				placed_file = true;
			}
			else if (is_clips && !placed_clips) {
				lines.push_back(clips_text.str());
				placed_clips = true;
			}
		}
	}

	while (!lines.empty() && lines.back().find_first_not_of(" \t\r") == std::string::npos) {
		lines.pop_back();
	}

	if (!placed_file) {
		lines.insert(lines.begin(), file_line);
	}

	if (!placed_clips) {
		lines.push_back(clips_text.str());
	}

	std::ostringstream text;

	for (const std::string& line : lines) {
		text << line << "\n";
	}

	{
		std::ofstream out(conf_path, std::ios::trunc);

		if (!out.is_open()) {
			wxMessageBox(wxString::FromUTF8("Could not write '" + conf_path + "'."), "Clip Editor", wxOK | wxICON_ERROR,
						 this);
			return;
		}

		out << text.str();
	}

	mConfPath = conf_path;
	SetDirty(false);

	thread::PostToGame(
		[]
		{
			if (gAudio != nullptr) {
				gAudio->LoadSounds();
			}
		});

	LogInfo(LC_AUDIO, "Clip editor saved {} clips to '{}'", mClips.size(), conf_path);

	std::error_code error;
	const fs::path saved_directory = fs::weakly_canonical(fs::absolute(conf_path).parent_path(), error);

	if (!error && saved_directory != sound_directory) {
		mpStatus->SetLabel(wxString::FromUTF8("Saved, but only defs directly in " + sound_directory.string() +
											  " are loaded by the game"));
	}
	else {
		mpStatus->SetLabel(wxString::FromUTF8("Saved " + fs::path(conf_path).filename().string() + " (" +
											  std::to_string(mClips.size()) + " clips) and reloaded sounds"));
	}
}

void ClipEditorWindow::OnTimer(wxTimerEvent&)
{
	if (mpPlayer == nullptr) {
		mTimer.Stop();
		return;
	}

	const float64 playhead = mpPlayer->GetPlayhead();

	mpWaveform->SetPlayhead(playhead);

	if (playhead < 0.0) {
		mTimer.Stop();
	}
}

void ClipEditorWindow::OnClose(wxCloseEvent& event)
{
	StopPlayback();

	if (event.CanVeto()) {
		Hide();
		event.Veto();
		return;
	}

	event.Skip();
}

} // namespace fx::editor
