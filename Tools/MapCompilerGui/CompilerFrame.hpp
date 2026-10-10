/*
 * File:        CompilerFrame.hpp
 * Author:      emd22
 * Created:     10/10/2026
 * Description: Main window of the map compiler frontend
 */

#pragma once

#include <wx/frame.h>

#include <Compiler.hpp>
#include <thread>

class wxButton;
class wxCheckBox;
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

	void StartCompile(const Array<size_t>& indices);
	void OnMapCompiled(size_t index, const CompileResult& result);
	void OnAllCompiled();

	void AppendLog(const String& line);
	void SetBusy(bool busy);

	void OnClose(wxCloseEvent& event);

private:
	wxDirPickerCtrl* mpDirectory = nullptr;
	wxListCtrl* mpMaps = nullptr;

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

	wxButton* mpCompileSelected = nullptr;
	wxButton* mpCompileAll = nullptr;
	wxButton* mpRefresh = nullptr;

	wxTextCtrl* mpLog = nullptr;

	/// Sized once per refresh from the number of prototypes in the folder
	SizedArray<MapEntry> mMaps;

	std::thread mWorker;
	bool mbBusy = false;

	uint32 mCompiledCount = 0;
	uint32 mFailedCount = 0;
};

} // namespace fx::mapc
