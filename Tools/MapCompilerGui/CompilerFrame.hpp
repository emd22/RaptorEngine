/*
 * File:        CompilerFrame.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Main window of the map compiler frontend
 */

#pragma once

#include <wx/frame.h>

#include <Presets.hpp>
#include <thread>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxCloseEvent;
class wxDirPickerCtrl;
class wxListCtrl;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxTextCtrl;

namespace fx::mapc {

class CompilerFrame : public wxFrame
{
public:
	explicit CompilerFrame(const String& map_directory);
	~CompilerFrame() override;

private:
	struct MapEntry
	{
		String Name;
		String Path;
	};

	void RefreshMaps();
	void ShowMapState(size_t index);

	Array<size_t> GetSelectedMaps() const;
	CompileOptions ReadOptions() const;
	void WriteOptions(const CompileOptions& options);

	void RefreshPresets(const String& selected_name);
	void ApplySelectedPreset();
	void SavePreset();
	void DeletePreset();

	void StartCompile(const Array<size_t>& indices);
	void OnMapCompiled(size_t index, const CompileResult& result);
	void OnAllCompiled();

	void AppendLog(const String& line);
	void SetBusy(bool busy);
	void UpdateLightmapControls();
	void UpdateProbeControls();

	void OnClose(wxCloseEvent& event);

private:
	wxDirPickerCtrl* mpDirectory = nullptr;
	wxListCtrl* mpMaps = nullptr;

	wxChoice* mpPreset = nullptr;
	wxButton* mpSavePreset = nullptr;
	wxButton* mpDeletePreset = nullptr;

	wxCheckBox* mpCullHidden = nullptr;
	wxCheckBox* mpMerge = nullptr;
	wxCheckBox* mpFixTJunctions = nullptr;
	wxCheckBox* mpKeepOutside = nullptr;
	wxCheckBox* mpWriteObj = nullptr;
	wxCheckBox* mpVerify = nullptr;
	wxSpinCtrl* mpVerifyRays = nullptr;
	wxSpinCtrlDouble* mpChunkSize = nullptr;
	wxCheckBox* mpPlaceProbes = nullptr;
	wxSpinCtrlDouble* mpProbeLevelSpacing = nullptr;
	wxSpinCtrlDouble* mpProbeBrushSpacing = nullptr;
	wxCheckBox* mpBakeLightmap = nullptr;
	wxSpinCtrlDouble* mpLightmapDensity = nullptr;
	wxSpinCtrlDouble* mpSunRadius = nullptr;
	wxCheckBox* mpBakeIndirect = nullptr;
	wxSpinCtrl* mpIndirectRays = nullptr;
	wxSpinCtrl* mpIndirectBounces = nullptr;
	wxSpinCtrl* mpIndirectCache = nullptr;
	wxChoice* mpIndirectDenoise = nullptr;

	wxButton* mpCompileSelected = nullptr;
	wxButton* mpCompileAll = nullptr;
	wxButton* mpRefresh = nullptr;

	wxTextCtrl* mpLog = nullptr;

	/// Sized once per refresh from the number of prototypes in the folder
	SizedArray<MapEntry> mMaps;

	PresetLibrary mPresets;
	CompileOptions mBaseOptions;

	std::thread mWorker;
	bool mbBusy = false;

	uint32 mCompiledCount = 0;
	uint32 mFailedCount = 0;
};

} // namespace fx::mapc
