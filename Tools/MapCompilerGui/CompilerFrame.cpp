#include "CompilerFrame.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/filepicker.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <Map/CompiledMap.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>

namespace fx::mapc {

namespace {

enum eColumn : int32
{
	Column_Name = 0,
	Column_State,
	Column_Seal,
};

constexpr const char* scPrototypeExtension = ".prx";

constexpr int scBorder = 6;
constexpr int scMinVerifyRays = 1000;
constexpr int scMaxVerifyRays = 1000000;
constexpr double scMaxChunkSize = 1024.0;
constexpr double scMinProbeSpacing = 0.1;
constexpr double scMaxProbeSpacing = 64.0;
constexpr double scProbeSpacingStep = 0.25;
constexpr double scMinLightmapDensity = 1.0;
constexpr double scMaxLightmapDensity = 128.0;
constexpr double scLightmapDensityStep = 4.0;
constexpr double scMinSunRadius = 0.0;
constexpr double scMaxSunRadius = 10.0;
constexpr double scSunRadiusStep = 0.25;

} // namespace

CompilerFrame::CompilerFrame(const String& map_directory)
	: wxFrame(nullptr, wxID_ANY, "Raptor Map Compiler", wxDefaultPosition, wxSize(980, 760))
{
	const SurfaceOptions default_options;

	wxPanel* root = new wxPanel(this, wxID_ANY);
	wxBoxSizer* root_sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* directory_sizer = new wxBoxSizer(wxHORIZONTAL);

	directory_sizer->Add(new wxStaticText(root, wxID_ANY, "Prototypes"),
						 wxSizerFlags().CenterVertical().Border(wxRIGHT, scBorder));

	mpDirectory = new wxDirPickerCtrl(root, wxID_ANY, wxString::FromUTF8(map_directory.CStr()), "Choose a folder of prototypes",
									  wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
	directory_sizer->Add(mpDirectory, wxSizerFlags(1).CenterVertical().Border(wxRIGHT, scBorder));

	mpRefresh = new wxButton(root, wxID_ANY, "Refresh");
	directory_sizer->Add(mpRefresh, wxSizerFlags().CenterVertical());

	root_sizer->Add(directory_sizer, wxSizerFlags().Expand().Border(wxALL, scBorder));

	wxBoxSizer* middle_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpMaps = new wxListCtrl(root, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
	mpMaps->InsertColumn(Column_Name, "Map", wxLIST_FORMAT_LEFT, 220);
	mpMaps->InsertColumn(Column_State, "Compiled", wxLIST_FORMAT_LEFT, 120);
	mpMaps->InsertColumn(Column_Seal, "Seal", wxLIST_FORMAT_LEFT, 110);
	middle_sizer->Add(mpMaps, wxSizerFlags(1).Expand().Border(wxRIGHT, scBorder));

	wxStaticBoxSizer* options_sizer = new wxStaticBoxSizer(wxVERTICAL, root, "Options");
	wxWindow* options_box = options_sizer->GetStaticBox();

	const auto add_checkbox = [&](const char* label, bool value, const char* tooltip) {
		wxCheckBox* checkbox = new wxCheckBox(options_box, wxID_ANY, label);
		checkbox->SetValue(value);
		checkbox->SetToolTip(tooltip);
		options_sizer->Add(checkbox, wxSizerFlags().Border(wxALL, scBorder));
		return checkbox;
	};

	mpCullHidden = add_checkbox("Remove hidden faces", default_options.bCullHidden,
								"Drop faces buried in other brushes and, on sealed maps, faces only the void can see");
	mpMerge = add_checkbox("Merge faces", default_options.bMerge,
						   "Join neighbouring pieces with the same material and texture layout before triangulating");
	mpFixTJunctions = add_checkbox("Fix T-junctions", default_options.bFixTJunctions,
								   "Split edges where another face's corner touches them, so no cracks show");
	mpKeepOutside = add_checkbox("Keep faces towards the void", false,
								 "Keep the outside of a sealed map. Buried faces are still removed");
	mpWriteObj = add_checkbox("Write an OBJ next to the map", false,
							  "Also write the compiled triangles as a Wavefront OBJ, to look at in another program");

	wxBoxSizer* chunk_sizer = new wxBoxSizer(wxHORIZONTAL);
	chunk_sizer->Add(new wxStaticText(options_box, wxID_ANY, "Chunk size"),
					 wxSizerFlags().CenterVertical().Border(wxRIGHT, scBorder));

	mpChunkSize = new wxSpinCtrlDouble(options_box, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS,
									   0.0, scMaxChunkSize, default_options.ChunkSize, 4.0);
	mpChunkSize->SetToolTip("Size of the grid that surfaces are split along, 0 for one surface per material");
	chunk_sizer->Add(mpChunkSize, wxSizerFlags().CenterVertical());

	options_sizer->Add(chunk_sizer, wxSizerFlags().Border(wxALL, scBorder));

	mpPlaceProbes = add_checkbox("Place light probes", true,
								 "Work out where the light probes go. The engine bakes them from these positions "
								 "when the compiled map is loaded");

	const auto add_number = [&](const char* label, double min, double max, double value, double step,
								const char* tooltip) {
		wxBoxSizer* spacing_sizer = new wxBoxSizer(wxHORIZONTAL);
		spacing_sizer->Add(new wxStaticText(options_box, wxID_ANY, label),
						   wxSizerFlags().CenterVertical().Border(wxRIGHT, scBorder));

		wxSpinCtrlDouble* spin = new wxSpinCtrlDouble(options_box, wxID_ANY, "", wxDefaultPosition, wxSize(90, -1),
													  wxSP_ARROW_KEYS, min, max, value, step);
		spin->SetDigits(2);
		spin->SetToolTip(tooltip);
		spacing_sizer->Add(spin, wxSizerFlags().CenterVertical());

		options_sizer->Add(spacing_sizer, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, scBorder));
		return spin;
	};

	mpProbeLevelSpacing = add_number("Probe spacing, level", scMinProbeSpacing, scMaxProbeSpacing,
									 ProbePlacementSettings::scDefaultLevelSpacing, scProbeSpacingStep,
									 "Spacing of the probes along the surfaces of the level. Raised automatically "
									 "if the probes don't fit the budget");
	mpProbeBrushSpacing = add_number("Probe spacing, volumes", scMinProbeSpacing, scMaxProbeSpacing,
									 ProbePlacementSettings::scDefaultBrushSpacing, scProbeSpacingStep,
									 "Spacing of the probes inside probe volume brushes");

	const LightmapOptions default_lightmap {};

	mpBakeLightmap = add_checkbox("Bake sun shadows", default_lightmap.bEnabled,
								  "Trace the sun's shadows into a lightmap, which the engine uses in place of the "
								  "sun's shadow map on the compiled surfaces");
	mpLightmapDensity = add_number("Lightmap density", scMinLightmapDensity, scMaxLightmapDensity,
								   default_lightmap.Density, scLightmapDensityStep,
								   "Lightmap texels per metre of surface. Lowered automatically if the lightmap "
								   "doesn't fit");
	mpSunRadius = add_number("Sun radius, degrees", scMinSunRadius, scMaxSunRadius,
							 default_lightmap.SunRadiusDegrees, scSunRadiusStep,
							 "Half the angle the sun covers in the sky. Wider makes softer shadow edges");

	wxBoxSizer* verify_sizer = new wxBoxSizer(wxHORIZONTAL);

	mpVerify = new wxCheckBox(options_box, wxID_ANY, "Verify with rays");
	mpVerify->SetToolTip("Trace random rays through the brushes and the compiled triangles and compare what they hit");
	verify_sizer->Add(mpVerify, wxSizerFlags().CenterVertical().Border(wxRIGHT, scBorder));

	mpVerifyRays = new wxSpinCtrl(options_box, wxID_ANY, "", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS,
								  scMinVerifyRays, scMaxVerifyRays, CompileOptions::scDefaultVerifyRays);
	mpVerifyRays->Disable();
	verify_sizer->Add(mpVerifyRays, wxSizerFlags().CenterVertical());

	options_sizer->Add(verify_sizer, wxSizerFlags().Border(wxALL, scBorder));

	options_sizer->AddStretchSpacer(1);

	mpCompileSelected = new wxButton(options_box, wxID_ANY, "Compile Selected");
	mpCompileAll = new wxButton(options_box, wxID_ANY, "Compile All");

	options_sizer->Add(mpCompileSelected, wxSizerFlags().Expand().Border(wxALL, scBorder));
	options_sizer->Add(mpCompileAll, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, scBorder));

	middle_sizer->Add(options_sizer, wxSizerFlags().Expand());

	root_sizer->Add(middle_sizer, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT, scBorder));

	mpLog = new wxTextCtrl(root, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
						   wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	mpLog->SetFont(wxFont(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE)));
	root_sizer->Add(mpLog, wxSizerFlags(1).Expand().Border(wxALL, scBorder));

	root->SetSizer(root_sizer);

	wxBoxSizer* frame_sizer = new wxBoxSizer(wxVERTICAL);
	frame_sizer->Add(root, wxSizerFlags(1).Expand());
	SetSizer(frame_sizer);

	CreateStatusBar();

	mpRefresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshMaps(); });
	mpDirectory->Bind(wxEVT_DIRPICKER_CHANGED, [this](wxFileDirPickerEvent&) { RefreshMaps(); });
	mpPlaceProbes->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		mpProbeLevelSpacing->Enable(mpPlaceProbes->GetValue());
		mpProbeBrushSpacing->Enable(mpPlaceProbes->GetValue());
	});
	mpBakeLightmap->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		mpLightmapDensity->Enable(mpBakeLightmap->GetValue());
		mpSunRadius->Enable(mpBakeLightmap->GetValue());
	});
	mpVerify->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { mpVerifyRays->Enable(mpVerify->GetValue()); });
	mpMaps->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { StartCompile(GetSelectedMaps()); });
	mpCompileSelected->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartCompile(GetSelectedMaps()); });

	mpCompileAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		Array<size_t> indices;

		for (size_t i = 0; i < mMaps.Size; i++) {
			indices.Emplace(i);
		}

		StartCompile(indices);
	});

	Bind(wxEVT_CLOSE_WINDOW, &CompilerFrame::OnClose, this);

	RefreshMaps();
}

CompilerFrame::~CompilerFrame()
{
	if (mWorker.joinable()) {
		mWorker.join();
	}
}

void CompilerFrame::RefreshMaps()
{
	if (mbBusy) {
		return;
	}

	mpMaps->DeleteAllItems();

	const String directory(mpDirectory->GetPath().utf8_string());

	std::error_code error;

	const auto is_prototype = [&error](const std::filesystem::directory_entry& entry) {
		return entry.is_regular_file(error) && entry.path().extension() == scPrototypeExtension;
	};

	size_t prototype_count = 0;

	for (const auto& entry : std::filesystem::directory_iterator(directory.CStr(), error)) {
		prototype_count += is_prototype(entry) ? 1 : 0;
	}

	mMaps.InitCapacity(prototype_count);

	for (const auto& entry : std::filesystem::directory_iterator(directory.CStr(), error)) {
		// A file added between the two passes is left for the next refresh
		if (is_prototype(entry) && mMaps.Size < mMaps.Capacity) {
			mMaps.Insert(MapEntry { String(entry.path().stem().string()), String(entry.path().string()) });
		}
	}

	std::sort(mMaps.begin(), mMaps.end(), [](const MapEntry& a, const MapEntry& b) {
		return std::strcmp(a.Name.CStr(), b.Name.CStr()) < 0;
	});

	for (size_t i = 0; i < mMaps.Size; i++) {
		mpMaps->InsertItem(static_cast<long>(i), wxString::FromUTF8(mMaps[i].Name.CStr()));
		ShowMapState(i);
	}

	SetStatusText(wxString::FromUTF8(String::Fmt("{} prototypes in {}", mMaps.Size, directory).CStr()));
}

void CompilerFrame::ShowMapState(size_t index)
{
	const MapEntry& entry = mMaps[index];
	const long row = static_cast<long>(index);

	CompiledMap map;

	if (!map.Load(CompiledMap::GetPathForPrototype(entry.Path))) {
		mpMaps->SetItem(row, Column_State, "Not compiled");
		mpMaps->SetItem(row, Column_Seal, "");
		return;
	}

	const bool up_to_date = (map.SourceHash == CompiledMap::HashFile(entry.Path));

	mpMaps->SetItem(row, Column_State, up_to_date ? "Up to date" : "Out of date");
	mpMaps->SetItem(row, Column_Seal, map.bSealed ? "Sealed" : "Not sealed");
}

Array<size_t> CompilerFrame::GetSelectedMaps() const
{
	Array<size_t> indices;

	for (long row = mpMaps->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); row >= 0;
		 row = mpMaps->GetNextItem(row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) {
		indices.Emplace(static_cast<size_t>(row));
	}

	return indices;
}

CompileOptions CompilerFrame::ReadOptions() const
{
	CompileOptions options;

	options.Surface.bCullHidden = mpCullHidden->GetValue();
	options.Surface.bMerge = mpMerge->GetValue();
	options.Surface.bFixTJunctions = mpFixTJunctions->GetValue();
	options.Surface.ChunkSize = mpChunkSize->GetValue();

	options.bKeepOutside = mpKeepOutside->GetValue();

	options.bPlaceProbes = mpPlaceProbes->GetValue();
	options.Probes.LevelSpacing = static_cast<float32>(mpProbeLevelSpacing->GetValue());
	options.Probes.BrushSpacing = static_cast<float32>(mpProbeBrushSpacing->GetValue());
	options.Lightmap.bEnabled = mpBakeLightmap->GetValue();
	options.Lightmap.Density = mpLightmapDensity->GetValue();
	options.Lightmap.SunRadiusDegrees = mpSunRadius->GetValue();
	options.VerifyRays = mpVerify->GetValue() ? static_cast<uint32>(mpVerifyRays->GetValue()) : 0;

	return options;
}

void CompilerFrame::StartCompile(const Array<size_t>& indices)
{
	if (mbBusy) {
		return;
	}

	if (indices.Size == 0) {
		SetStatusText("Select a map to compile first");
		return;
	}

	if (mWorker.joinable()) {
		mWorker.join();
	}

	struct Job
	{
		size_t Index = 0;
		CompileOptions Options;
	};

	SizedArray<Job> jobs;
	jobs.InitCapacity(indices.Size);

	for (const size_t index : indices) {
		CompileOptions options = ReadOptions();
		options.InputPath = mMaps[index].Path;

		if (mpWriteObj->GetValue()) {
			options.ObjPath = String(
				std::filesystem::path(mMaps[index].Path.CStr()).replace_extension(".obj").string());
		}

		jobs.Insert(Job { index, std::move(options) });

		mpMaps->SetItem(static_cast<long>(index), Column_State, "Waiting...");
	}

	mCompiledCount = 0;
	mFailedCount = 0;

	mpLog->Clear();
	SetBusy(true);
	SetStatusText(wxString::FromUTF8(String::Fmt("Compiling {} maps...", jobs.Size).CStr()));

	mWorker = std::thread([this, jobs = std::move(jobs)]() {
		for (const auto& [index, options] : jobs) {
			const CompileResult result = CompileMap(options, [this](const String& line) {
				CallAfter([this, line]() { AppendLog(line); });
			});

			CallAfter([this, map_index = index, result]() { OnMapCompiled(map_index, result); });
		}

		CallAfter([this]() { OnAllCompiled(); });
	});
}

void CompilerFrame::OnMapCompiled(size_t index, const CompileResult& result)
{
	const long row = static_cast<long>(index);

	AppendLog("");

	if (!result.bSucceeded) {
		mFailedCount++;
		mpMaps->SetItem(row, Column_State, "Failed");
		mpMaps->SetItem(row, Column_Seal, "");
		return;
	}

	mCompiledCount++;

	ShowMapState(index);

	if (result.bVerifyFailed) {
		mFailedCount++;
		mpMaps->SetItem(row, Column_State, "Verify failed");
	}

	if (result.Seal == eSealState::Leaked) {
		mpMaps->SetItem(row, Column_Seal, "Leaked");
	}
}

void CompilerFrame::OnAllCompiled()
{
	if (mWorker.joinable()) {
		mWorker.join();
	}

	SetBusy(false);

	if (mFailedCount > 0) {
		SetStatusText(wxString::FromUTF8(String::Fmt("Compiled {} maps, {} with problems. A map that fails to load "
													 "prints its errors to the terminal.",
													 mCompiledCount, mFailedCount)
											 .CStr()));
	}
	else {
		SetStatusText(wxString::FromUTF8(String::Fmt("Compiled {} maps", mCompiledCount).CStr()));
	}
}

void CompilerFrame::AppendLog(const String& line)
{
	mpLog->AppendText(wxString::FromUTF8(line.CStr()));
	mpLog->AppendText("\n");
}

void CompilerFrame::SetBusy(bool busy)
{
	mbBusy = busy;

	mpCompileSelected->Enable(!busy);
	mpCompileAll->Enable(!busy);
	mpRefresh->Enable(!busy);
	mpDirectory->Enable(!busy);
}

void CompilerFrame::OnClose(wxCloseEvent& event)
{
	if (mbBusy && event.CanVeto()) {
		SetStatusText("Still compiling, close the window again once it is done");
		event.Veto();
		return;
	}

	if (mWorker.joinable()) {
		mWorker.join();
	}

	Destroy();
}

} // namespace fx::mapc
