#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <Core/Types.hpp>

#include <memory>
#include <string>
#include <vector>

class wxButton;
class wxListCtrl;
class wxSpinCtrlDouble;
class wxStaticText;

namespace fx::editor {

struct ClipRegion
{
	float64 Start = 0.0;
	float64 End = 0.0;
};

struct ClipAudio
{
	std::vector<float32> Samples;
	uint32 Channels = 0;
	uint32 SampleRate = 0;
	uint64 FrameCount = 0;

	bool IsLoaded() const { return FrameCount > 0 && SampleRate > 0; }
	float64 GetDuration() const;
};

class ClipWaveformPanel;
struct ClipPreviewPlayer;

class ClipEditorWindow : public wxFrame
{
public:
	explicit ClipEditorWindow(wxWindow* parent);
	~ClipEditorWindow() override;

	const ClipAudio& GetAudio() const { return mAudio; }
	std::vector<ClipRegion>& GetClips() { return mClips; }
	const std::vector<ClipRegion>& GetClips() const { return mClips; }
	int32 GetSelected() const { return mSelected; }

	void SetSelected(int32 index);
	void OnClipChanged(int32 index);
	void OnClipsCommitted();

	void PlaySelected();
	void PlayRandom();
	void StopPlayback();
	void DeleteSelected();

private:
	void OpenSound();
	void OpenConf();
	bool LoadAudio(const std::string& path);
	bool LoadConf(const std::string& path);
	bool ConfirmDiscard();

	void Save(bool choose_path);
	bool ResolveSoundFileKey(std::string& out_key);

	void AutoDetect();
	void PlayRange(float64 start, float64 end);

	void SortClips();
	void RebuildList();
	void UpdateListRow(int32 index);
	void UpdateSelectionControls();
	void UpdateTitle();
	void SetDirty(bool dirty);

	void OnTimer(wxTimerEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	ClipWaveformPanel* mpWaveform = nullptr;
	wxListCtrl* mpList = nullptr;
	wxSpinCtrlDouble* mpStart = nullptr;
	wxSpinCtrlDouble* mpEnd = nullptr;
	wxSpinCtrlDouble* mpThreshold = nullptr;
	wxSpinCtrlDouble* mpMinGap = nullptr;
	wxButton* mpPlayButton = nullptr;
	wxButton* mpRandomButton = nullptr;
	wxButton* mpDeleteButton = nullptr;
	wxButton* mpDetectButton = nullptr;
	wxButton* mpSaveButton = nullptr;
	wxButton* mpSaveAsButton = nullptr;
	wxStaticText* mpFileLabel = nullptr;
	wxStaticText* mpStatus = nullptr;

	ClipAudio mAudio;
	std::vector<ClipRegion> mClips;
	int32 mSelected = -1;
	int32 mLastRandom = -1;

	std::string mAudioPath;
	std::string mConfPath;
	bool mbConfHasMultipleFiles = false;
	bool mbDirty = false;
	bool mbUpdatingControls = false;

	std::unique_ptr<ClipPreviewPlayer> mpPlayer;
	wxTimer mTimer;
};

} // namespace fx::editor
