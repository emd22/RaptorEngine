#include "Compiler.hpp"
#include "Presets.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

using namespace fx;
using namespace fx::mapc;

constexpr int scExitUsage = 2;
constexpr int scExitVerifyFailed = 3;

void PrintUsage()
{
	std::printf(
		"Usage: mapc <prototype.prx> [options]\n"
		"\n"
		"Compiles the world brushes of a prototype file into batched triangle meshes and collision hulls.\n"
		"\n"
		"  -o <path>        Where to write the compiled map (default: next to the prototype, as .fxmap)\n"
		"  --chunk <size>   Size of the grid that surfaces are split along, 0 for one per material (default: 32)\n"
		"  --keep-outside   Keep the faces that only the void can see, even if the map is sealed\n"
		"  --no-cull        Keep every face, including the ones buried inside other brushes\n"
		"  --no-merge       Do not merge neighbouring faces\n"
		"  --no-tjunctions  Do not fix T-junctions\n"
		"  --no-probes      Do not place light probes\n"
		"  --probe-spacing <m>        Probe spacing inside probe volume brushes (default: 2.5)\n"
		"  --probe-level-spacing <m>  Probe spacing along the surfaces of the level (default: 0.5)\n"
		"  --no-lightmap    Do not bake a lightmap (sun shadows and bounced light)\n"
		"  --lightmap-density <n>     Lightmap texels per metre (default: 16)\n"
		"  --lightmap-size <n>        Largest width and height of the lightmap (default: 4096)\n"
		"  --lightmap-samples <n>     Rays per texel along each axis (default: 4)\n"
		"  --sun-radius <degrees>     Half the angle the sun covers, wider is softer (default: 1)\n"
		"  --lightmap-image <path>    Also write the lightmap as a PGM image\n"
		"  --no-indirect    Do not bake the light that bounces between surfaces into the lightmap\n"
		"  --indirect-rays <n>        Rays per lightmap texel for each bounce (default: 256)\n"
		"  --indirect-bounces <n>     How many times light bounces (default: 3)\n"
		"  --indirect-cache <MB>      Memory for keeping the first bounce's ray hits so later bounces skip tracing "
		"them, 8 bytes per ray (default: 0, off)\n"
		"  --indirect-denoise <level> How hard to smooth the noise out of the bounced light: off, low, medium "
		"or high (default: medium)\n"
		"  --probe-texel-rays <n>     Rays per probe visibility texel along each axis when baking probes, 0 to "
		"leave the probes to the engine (default: 4)\n"
		"  --no-fog-shadow  Do not bake the sun shadow pages that the fog is shadowed by\n"
		"  --fog-shadow-texel <m>     Size of a fog shadow texel close up (default: 0.1)\n"
		"  --obj <path>     Also write the compiled triangles as a Wavefront OBJ\n"
		"  --verify [rays]  Trace random rays through the brushes and the compiled triangles and compare them\n"
		"  --preset <name>  Start from a saved preset, the other options change it\n"
		"  --save-preset <name>       Save the options as a preset, the prototype can be left out\n"
		"  --list-presets   Print the saved presets\n"
		"\n"
		"Presets are kept in %s\n",
		PresetLibrary::scDefaultPath);
}

struct PresetArguments
{
	String SavePreset;
	bool bList = false;
};

bool ParseArguments(int argc, char** argv, CompileOptions& out_arguments, PresetArguments& out_presets)
{
	for (int i = 1; i < argc; i++) {
		const String argument = argv[i];
		const bool has_value = (i + 1 < argc);

		if (argument == "-o" && has_value) {
			out_arguments.OutputPath = argv[++i];
		}
		else if (argument == "--obj" && has_value) {
			out_arguments.ObjPath = argv[++i];
		}
		else if (argument == "--chunk" && has_value) {
			out_arguments.Surface.ChunkSize = std::atof(argv[++i]);
		}
		else if (argument == "--probe-spacing" && has_value) {
			out_arguments.ProbeSettings.BrushSpacing = static_cast<float32>(std::atof(argv[++i]));
		}
		else if (argument == "--probe-level-spacing" && has_value) {
			out_arguments.ProbeSettings.LevelSpacing = static_cast<float32>(std::atof(argv[++i]));
		}
		else if (argument == "--lightmap-density" && has_value) {
			out_arguments.Lightmap.Density = std::atof(argv[++i]);
		}
		else if (argument == "--lightmap-size" && has_value) {
			out_arguments.Lightmap.MaxSize = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--lightmap-samples" && has_value) {
			out_arguments.Lightmap.Samples = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--sun-radius" && has_value) {
			out_arguments.Lightmap.SunRadiusDegrees = std::atof(argv[++i]);
		}
		else if (argument == "--lightmap-image" && has_value) {
			out_arguments.Lightmap.ImagePath = argv[++i];
		}
		else if (argument == "--indirect-rays" && has_value) {
			out_arguments.Lightmap.IndirectRays = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--indirect-bounces" && has_value) {
			out_arguments.Lightmap.IndirectBounces = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--indirect-cache" && has_value) {
			out_arguments.Lightmap.IndirectCacheMegabytes = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--indirect-denoise" && has_value) {
			const String level = argv[++i];

			if (!ParseDenoiseQuality(level, out_arguments.Lightmap.IndirectDenoise)) {
				std::printf("Unknown denoise level '%s', expected off, low, medium or high\n\n", level.CStr());
				return false;
			}
		}
		else if (argument == "--probe-texel-rays" && has_value) {
			out_arguments.Lightmap.ProbeTexelRays = static_cast<uint32>(std::atoi(argv[++i]));
		}
		else if (argument == "--preset" && has_value) {
			i++;
		}
		else if (argument == "--save-preset" && has_value) {
			out_presets.SavePreset = argv[++i];
		}
		else if (argument == "--list-presets") {
			out_presets.bList = true;
		}
		else if (argument == "--no-indirect") {
			out_arguments.Lightmap.bIndirect = false;
		}
		else if (argument == "--no-fog-shadow") {
			out_arguments.Lightmap.FogShadow.bEnabled = false;
		}
		else if (argument == "--fog-shadow-texel" && has_value) {
			out_arguments.Lightmap.FogShadow.TexelSize = std::atof(argv[++i]);
		}
		else if (argument == "--no-lightmap") {
			out_arguments.Lightmap.bEnabled = false;
		}
		else if (argument == "--no-probes") {
			out_arguments.bPlaceProbes = false;
		}
		else if (argument == "--keep-outside") {
			out_arguments.bKeepOutside = true;
		}
		else if (argument == "--no-cull") {
			out_arguments.Surface.bCullHidden = false;
		}
		else if (argument == "--no-merge") {
			out_arguments.Surface.bMerge = false;
		}
		else if (argument == "--no-tjunctions") {
			out_arguments.Surface.bFixTJunctions = false;
		}
		else if (argument == "--verify") {
			out_arguments.VerifyRays = CompileOptions::scDefaultVerifyRays;

			if (has_value && std::isdigit(static_cast<unsigned char>(argv[i + 1][0]))) {
				out_arguments.VerifyRays = static_cast<uint32>(std::atoi(argv[++i]));
			}
		}
		else if (!argument.IsEmpty() && argument[0] != '-' && out_arguments.InputPath.IsEmpty()) {
			out_arguments.InputPath = argument;
		}
		else {
			std::printf("Unknown or incomplete option '%s'\n\n", argument.CStr());
			return false;
		}
	}

	return !out_arguments.InputPath.IsEmpty() || !out_presets.SavePreset.IsEmpty() || out_presets.bList;
}

String FindPresetArgument(int argc, char** argv)
{
	String preset;

	for (int i = 1; i + 1 < argc; i++) {
		if (String(argv[i]) == "--preset") {
			preset = argv[++i];
		}
	}

	return preset;
}

void PrintPresets(const PresetLibrary& presets)
{
	if (presets.GetPresets().Size == 0) {
		std::printf("No presets saved in %s\n", PresetLibrary::scDefaultPath);
		return;
	}

	for (const CompilePreset& preset : presets.GetPresets()) {
		std::printf("  %s\n", preset.Name.CStr());
	}
}

} // namespace

int main(int argc, char** argv)
{
	CompileOptions options;
	PresetArguments preset_arguments;

	PresetLibrary presets;

	if (!presets.Load()) {
		std::printf("Could not read the presets in %s\n", PresetLibrary::scDefaultPath);
	}

	const String preset_name = FindPresetArgument(argc, argv);

	if (!preset_name.IsEmpty()) {
		const CompilePreset* preset = presets.Find(preset_name);

		if (preset == nullptr) {
			std::printf("There is no preset named '%s'. The saved presets are:\n", preset_name.CStr());
			PrintPresets(presets);
			return scExitUsage;
		}

		options = preset->Options;
	}

	if (!ParseArguments(argc, argv, options, preset_arguments)) {
		PrintUsage();
		return scExitUsage;
	}

	if (preset_arguments.bList) {
		PrintPresets(presets);
	}

	if (!preset_arguments.SavePreset.IsEmpty()) {
		presets.Store(preset_arguments.SavePreset, options);

		if (!presets.Save()) {
			std::printf("Could not write the presets to %s\n", PresetLibrary::scDefaultPath);
			return 1;
		}

		std::printf("Saved preset '%s'\n", preset_arguments.SavePreset.CStr());
	}

	if (options.InputPath.IsEmpty()) {
		return 0;
	}

	const CompileResult result = CompileMap(options, [](const String& line) { std::printf("%s\n", line.CStr()); });

	if (!result.bSucceeded) {
		return 1;
	}

	return result.bVerifyFailed ? scExitVerifyFailed : 0;
}
