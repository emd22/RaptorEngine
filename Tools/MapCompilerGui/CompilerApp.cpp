#include "CompilerFrame.hpp"

#include <wx/app.h>
#include <wx/cmdline.h>

#include <Core/FilesystemIO.hpp>
#include <filesystem>

namespace fx::mapc {

namespace {

constexpr const char* scConstantsPath = "Config/Internal/Constants.conf";
constexpr const char* scDefaultMapDirectory = "RaptorData/Data/blockouts";

void FindProjectDirectory()
{
	std::error_code error;

	if (std::filesystem::exists(scConstantsPath, error)) {
		return;
	}

	std::filesystem::path directory = FilesystemIO::GetExecutablePath();

	while (!directory.empty() && directory != directory.parent_path()) {
		if (std::filesystem::exists(directory / scConstantsPath, error)) {
			std::filesystem::current_path(directory, error);
			return;
		}

		directory = directory.parent_path();
	}
}

} // namespace

class CompilerApp : public wxApp
{
public:
	bool OnInit() override
	{
		if (!wxApp::OnInit()) {
			return false;
		}

		SetAppearance(wxApp::Appearance::Dark);

		FindProjectDirectory();

		std::error_code error;
		const String map_directory(std::filesystem::absolute(mMapDirectory.CStr(), error).string());

		CompilerFrame* frame = new CompilerFrame(map_directory);
		frame->Show();

		return true;
	}

	void OnInitCmdLine(wxCmdLineParser& parser) override
	{
		wxApp::OnInitCmdLine(parser);
		parser.AddParam("folder of prototypes", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	}

	bool OnCmdLineParsed(wxCmdLineParser& parser) override
	{
		if (parser.GetParamCount() > 0) {
			mMapDirectory = parser.GetParam(0).utf8_string();
		}

		return wxApp::OnCmdLineParsed(parser);
	}

private:
	String mMapDirectory = scDefaultMapDirectory;
};

} // namespace fx::mapc

wxIMPLEMENT_APP(fx::mapc::CompilerApp);
