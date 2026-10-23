/*
 * File:        Presets.hpp
 * Author:      emd22
 * Created:     23/10/2026
 * Description: Named sets of compile options, saved as a config file
 */

#pragma once

#include "Compiler.hpp"

namespace fx::mapc {

struct CompilePreset
{
	String Name;
	CompileOptions Options;
};

class PresetLibrary
{
public:
	static constexpr const char* scDefaultPath = "Config/mapc_presets.conf";

	bool Load(const String& path = scDefaultPath);
	bool Save(const String& path = scDefaultPath) const;

	const CompilePreset* Find(const String& name) const;
	void Store(const String& name, const CompileOptions& options);
	bool Remove(const String& name);

	const Array<CompilePreset>& GetPresets() const { return mPresets; }

private:
	Array<CompilePreset> mPresets;
};

bool ParseDenoiseQuality(const String& name, eDenoiseQuality& out_quality);
const char* GetDenoiseQualityName(eDenoiseQuality quality);

} // namespace fx::mapc
