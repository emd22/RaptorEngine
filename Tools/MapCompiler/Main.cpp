#include "Compiler.hpp"

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
	std::printf("Usage: mapc <prototype.prx> [options]\n"
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
				"  --no-lightmap    Do not bake the sun's shadows\n"
				"  --lightmap-density <n>     Lightmap texels per metre (default: 16)\n"
				"  --lightmap-size <n>        Largest width and height of the lightmap (default: 4096)\n"
				"  --lightmap-samples <n>     Rays per texel along each axis (default: 4)\n"
				"  --sun-radius <degrees>     Half the angle the sun covers, wider is softer (default: 1)\n"
				"  --lightmap-image <path>    Also write the lightmap as a PGM image\n"
				"  --obj <path>     Also write the compiled triangles as a Wavefront OBJ\n"
				"  --verify [rays]  Trace random rays through the brushes and the compiled triangles and compare them\n");
}

bool ParseArguments(int argc, char** argv, CompileOptions& out_arguments)
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
			out_arguments.Probes.BrushSpacing = static_cast<float32>(std::atof(argv[++i]));
		}
		else if (argument == "--probe-level-spacing" && has_value) {
			out_arguments.Probes.LevelSpacing = static_cast<float32>(std::atof(argv[++i]));
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

	return !out_arguments.InputPath.IsEmpty();
}

} // namespace

int main(int argc, char** argv)
{
	CompileOptions options;

	if (!ParseArguments(argc, argv, options)) {
		PrintUsage();
		return scExitUsage;
	}

	const CompileResult result = CompileMap(options, [](const String& line) { std::printf("%s\n", line.CStr()); });

	if (!result.bSucceeded) {
		return 1;
	}

	return result.bVerifyFailed ? scExitVerifyFailed : 0;
}
