#include "Log.hpp"

#include <fstream>

static std::ofstream sCurrentLogFile;

std::ofstream& LogGetFile(bool* can_write)
{
    if (!sCurrentLogFile.is_open()) {
        LogToStdout<LogChannel::Error>("Attempting to write to log file that has not been opened");
        (*can_write) = false;
    }
    else {
        (*can_write) = true;
    }

    return sCurrentLogFile;
}

void LogCreateFile(const std::string& path) { sCurrentLogFile.open(path.c_str()); }
