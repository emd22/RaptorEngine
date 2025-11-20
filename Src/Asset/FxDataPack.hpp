#pragma once

#include <Core/FxFile.hpp>
#include <Core/FxHash.hpp>
#include <Core/FxSizedArray.hpp>
#include <Core/FxSlice.hpp>

class FxFile;

struct FxDataPackEntry
{
    FxDataPackEntry(FxSizedArray<uint8>&& ident, FxSizedArray<uint8>&& data)
        : Identifier(std::move(ident)), Data(std::move(data)) {};

    FxDataPackEntry(FxSizedArray<uint8>&& ident, FxHash ident_hash, FxSizedArray<uint8>&& data)
        : Identifier(std::move(ident)), IdentifierHash(ident_hash), Data(std::move(data)) {};

    FxDataPackEntry(FxDataPackEntry&& other) { Data = std::move(other.Data); }

    FxSizedArray<uint8> Identifier;
    FxHash IdentifierHash = 0;

    FxSizedArray<uint8> Data;
};

class FxDataPack
{
public:
    FxDataPack() = default;
    FxDataPack(const FxDataPack& other) = delete;


    void AddEntry(const FxSlice<uint8>& identifier, const FxSlice<uint8>& data);

    void WriteToFile(const char* name);
    void ReadFromFile(const char* name);

    ~FxDataPack();

private:
    void TextWriteHeader(FxFile& file);
    void TextWriteData(FxFile& file);

    void TextReadHeader(FxFile& file);

public:
    FxPagedArray<FxDataPackEntry> Entries;
};

/*
 * [EntryStart] [IdSize] [Id] [Location]
 * [EntryStart] ...
 *
 * [Data...]
 */
