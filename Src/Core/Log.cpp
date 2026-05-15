#include "Log.hpp"

#include <fstream>

namespace fx {

static std::ofstream sCurrentLogFile;

std::ofstream& LogGetFile(bool* can_write)
{
    if (!sCurrentLogFile.is_open()) {
        LogToStdout<eLogSeverity::Error>("Attempting to write to log file that has not been opened");
        (*can_write) = false;
    }
    else {
        (*can_write) = true;
    }

    return sCurrentLogFile;
}

void LogCreateFile(const std::string& path) { sCurrentLogFile.open(path.c_str()); }

void LogCategoryText(eLogCategory category)
{
    static const char* scCatNames[] = {
        "[CORE]    ", /* Core */
        "[SHADER]  ", /* Shader */
        "[RENDER]  ", /* Render */
        "[PHYSICS] ", /* Physics */
        "[MEMORY]  ", /* Memory */
        "[ASSET]   ", /* Asset */
        "[SCRIPT]  ", /* Script */
    };

    static const char* scCatColors[] = {
        FX_LOG_COLOR_FG(116), /* Core */
        FX_LOG_COLOR_FG(218), /* Shader */
        FX_LOG_COLOR_FG(215), /* Render */
        FX_LOG_COLOR_FG(154), /* Physics */
        FX_LOG_COLOR_FG(212), /* Memory */
        FX_LOG_COLOR_FG(192), /* Asset */
        FX_LOG_COLOR_FG(84),  /* Script */
    };

    const int cat_index = static_cast<int>(category);

#ifdef FX_LOG_OUTPUT_TO_STDOUT
    // No need to reset the styles here, the severity is logged after.
    std::cout << scCatColors[cat_index] << scCatNames[cat_index];
#endif
#ifdef FX_LOG_OUTPUT_TO_FILE
    bool can_write = false;
    std::ofstream& stream = LogGetFile(&can_write);

    if (!can_write) {
        return;
    }

    stream << scCatColors[cat_index] << scCatNames[cat_index];
#endif
}

} // namespace fx
