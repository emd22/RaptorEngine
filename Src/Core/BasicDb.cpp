#include "BasicDb.hpp"

namespace fx {

void BasicDb::Open(const char* path)
{
    mPath = path;

    mInFile.Open(path, File::Read, File::Text);

    // Error opening file, create it instead
    if (!mInFile.IsFileOpen()) {
        mInFile.Open(path, File::Write, File::Text);
        // mInFile.Close();

        mInFile.Open(path, File::Read, File::Text);
    }

    // Eh, something else is wrong here
    if (!mInFile.IsFileOpen()) {
        LogError("Could not open basic db at {}!", path);
        return;
    }

    mInFile.SeekTo(0);

    mpData = mInFile.Read<char>();

    Parse();

    mInFile.Close();

    mbIsOpen = true;
}

void BasicDb::WriteEntry(const BasicDbEntry& updated_entry)
{
    for (BasicDbEntry& current_entry : mEntryMarkers) {
        if (current_entry.KeyHash == updated_entry.KeyHash) {
            current_entry.Value = updated_entry.Value;
            mbPendingSaveToFile = true;
            return;
        }
    }

    mEntryMarkers.Insert(updated_entry);
    mbPendingSaveToFile = true;
}

void BasicDb::SaveToFile()
{
    if (!mbPendingSaveToFile) {
        return;
    }

    mOutFile.Open(mPath.c_str(), File::Write, File::Text);

    for (const BasicDbEntry& entry : mEntryMarkers) {
        std::string key_str = std::to_string(entry.KeyHash);
        mOutFile.Write(key_str.c_str(), key_str.length());
        mOutFile.Write(',');
        mOutFile.Write(entry.Value.c_str(), entry.Value.length());
        mOutFile.Write('\n');
    }

    mOutFile.Flush();
}

void BasicDb::Parse()
{
    mInFile.SeekTo(0);
    uint64 file_size = mInFile.GetFileSize();

    char* start = mpData;
    char* end = start + file_size;

    if (!mEntryMarkers.IsInited()) {
        mEntryMarkers.Create(32);
    }

    if (!mEntryMarkers.IsEmpty()) {
        mEntryMarkers.Clear();
    }

    char token[128];
    int token_index = 0;

    char tmp_buffer[128];
    BasicDbEntry marker {};

    while (start < end) {
        char ch = (*start);

        if (ch == ',') {
            if (token_index <= 0) {
                ++start;
                continue;
            }

            memcpy(tmp_buffer, token, token_index);
            tmp_buffer[token_index] = '\0';

            marker.KeyHash = std::stoull(std::string(tmp_buffer, token_index));

            token_index = 0;
        }
        else if (ch == '\n') {
            if (token_index <= 0) {
                ++start;
                continue;
            }

            // Null terminate the token
            token[token_index] = 0;

            marker.Value = std::string(token, token_index);

            // Reset token index
            token_index = 0;

            mEntryMarkers.Insert(marker);
        }
        else {
            token[token_index++] = ch;
        }

        ++start;
    }

    gEnginePool->Free(mpData);
}

BasicDbEntry* BasicDb::FindEntry(Hash64 key)
{
    if (!mEntryMarkers.IsInited()) {
        return nullptr;
    }

    for (BasicDbEntry& entry : mEntryMarkers) {
        if (entry.KeyHash == key) {
            return &entry;
        }
    }

    return nullptr;
}

void BasicDb::Close()
{
    if (mpData == nullptr || !mbIsOpen) {
        return;
    }

    SaveToFile();
    mOutFile.Close();

    mpData = nullptr;
    mbIsOpen = false;
}

} // namespace fx
