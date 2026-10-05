use std::fs;
use std::path::{Path, PathBuf};

use hassle_rs::{Dxc, DxcCompiler, DxcIncludeHandler, DxcLibrary};

use crate::datapack::DataPack;
use crate::preproc::{Macro, Stage};
use crate::program::{MacroRef, ProgramKind, pack_program, shader_id};
use crate::{Log, LogLevel, process};

/// The shader model every program is compiled for.
const SHADER_MODEL: &str = "6_7";

const ARGS: [&str; 3] = [
	"-O3",
	// Makes min16float a real half precision type (it stays full precision without this), see the
	// probe blending in ProbeCommon.hlsli. Needs shaderFloat16 on the device.
	"-enable-16bit-types",
	"-spirv",
];

/// Where the compiler library is looked for after `RAPTOR_DXC_LIB` and the places the system looks.
const LIBRARY_PATHS: [&str; 2] = [
	"/usr/local/lib/libdxcompiler.dylib",
	"/opt/homebrew/lib/libdxcompiler.dylib",
];

pub fn profile(stage: Stage) -> String
{
	let prefix = match stage {
		Stage::Vertex => "vs",
		Stage::Pixel => "ps",
		Stage::Compute => "cs",
	};

	format!("{prefix}_{SHADER_MODEL}")
}

fn stage_name(stage: Stage) -> &'static str
{
	match stage {
		Stage::Vertex => "Vertex",
		Stage::Pixel => "Pixel",
		Stage::Compute => "Compute",
	}
}

fn program_kind(stage: Stage) -> ProgramKind
{
	match stage {
		Stage::Vertex => ProgramKind::Vertex,
		Stage::Pixel => ProgramKind::Pixel,
		Stage::Compute => ProgramKind::Compute,
	}
}

/// Finds the files a shader includes, next to the shader if they are not found as they are written.
struct Includes
{
	directory: PathBuf,
}

impl DxcIncludeHandler for Includes
{
	fn load_source(&mut self, filename: String) -> Option<String>
	{
		let path = Path::new(&filename);

		fs::read_to_string(path)
			.or_else(|_| fs::read_to_string(self.directory.join(path)))
			.ok()
	}
}

/// The DirectX shader compiler, which turns HLSL into SPIR-V.
pub struct Compiler
{
	// The compiler and library are objects of the loaded library, which has to stay loaded.
	compiler: DxcCompiler,
	library: DxcLibrary,
	_dxc: Dxc,
}

impl Compiler
{
	pub fn new() -> Result<Self, String>
	{
		let mut candidates: Vec<Option<PathBuf>> = Vec::new();

		if let Ok(path) = std::env::var("RAPTOR_DXC_LIB") {
			candidates.push(Some(PathBuf::from(path)));
		}

		candidates.push(None);
		candidates.extend(LIBRARY_PATHS.iter().map(|path| Some(PathBuf::from(path))));

		let mut failure = String::new();

		for candidate in candidates {
			match Dxc::new(candidate) {
				Ok(dxc) => {
					let compiler = dxc.create_compiler().map_err(|error| error.to_string())?;
					let library = dxc.create_library().map_err(|error| error.to_string())?;

					return Ok(Self {
						compiler,
						library,
						_dxc: dxc,
					});
				}
				Err(error) => failure = error.to_string(),
			}
		}

		Err(failure)
	}

	/// Compiles one stage of a shader to SPIR-V. Warnings are logged, and the errors if it fails.
	pub fn compile_stage(
		&self,
		source: &[u8],
		path: &str,
		stage: Stage,
		log: &mut dyn Log,
	) -> Option<Vec<u8>>
	{
		let text = String::from_utf8_lossy(source);

		let blob = match self.library.create_blob_with_encoding_from_str(&text) {
			Ok(blob) => blob,
			Err(error) => {
				log.log(
					LogLevel::Error,
					&format!("Failed to compile shader '{path}'!\nErr: {error}"),
				);
				return None;
			}
		};

		let mut includes = Includes {
			directory: Path::new(path)
				.parent()
				.map(Path::to_path_buf)
				.unwrap_or_default(),
		};

		let result = self.compiler.compile(
			&blob,
			path,
			"main",
			&profile(stage),
			&ARGS,
			Some(&mut includes),
			&[],
		);

		let operation = match result {
			Ok(operation) => operation,
			Err((operation, _)) => {
				let errors = operation
					.get_error_buffer()
					.ok()
					.and_then(|errors| self.library.get_blob_as_string(&errors.into()).ok())
					.unwrap_or_default();

				log.log(
					LogLevel::Error,
					&format!("Failed to compile shader '{path}'!"),
				);
				log.log(LogLevel::Error, &format!("Err: {errors}"));
				return None;
			}
		};

		if let Ok(warnings) = operation
			.get_error_buffer()
			.map_err(|_| ())
			.and_then(|warnings| {
				self.library
					.get_blob_as_string(&warnings.into())
					.map_err(|_| ())
			})
			&& !warnings.is_empty()
		{
			log.log(
				LogLevel::Warning,
				&format!(
					"Shader '{path}' ({}) compiled with warnings:\n{warnings}",
					stage_name(stage)
				),
			);
		}

		operation.get_result().ok().map(|spirv| spirv.to_vec())
	}
}

/// The result of compiling a shader file.
#[derive(Debug, PartialEq, Eq)]
pub enum CompileResult
{
	Success,
	Failed,
}

/// Compiles the stages of the shader at `path` for `macros`, and puts each program in `pack` under
/// the id it is looked up by, replacing the ones that are there. If a stage fails, the rest are not
/// compiled.
pub fn compile_into_pack(
	compiler: &Compiler,
	path: &str,
	macros: &[MacroRef],
	pack: &mut DataPack,
	log: &mut dyn Log,
) -> CompileResult
{
	let Ok(source) = fs::read(path) else {
		log.log(LogLevel::Error, &format!("Could not read shader '{path}'"));
		return CompileResult::Failed;
	};

	let preproc_macros: Vec<_> = macros
		.iter()
		.filter_map(|entry| {
			entry.name.map(|name| Macro {
				name,
				value: entry.value,
			})
		})
		.collect();

	let output = process(&source, &preproc_macros, log);

	for (index, stage) in [Stage::Vertex, Stage::Pixel, Stage::Compute]
		.into_iter()
		.enumerate()
	{
		if output.programs[index].is_empty() {
			continue;
		}

		let Some(spirv) = compiler.compile_stage(&output.programs[index], path, stage, log) else {
			return CompileResult::Failed;
		};

		let reflection: Vec<u32> = output.reflection[index]
			.iter()
			.map(|entry| {
				(u32::from(entry.kind as u16) << 16)
					| (u32::from(entry.set) << 8)
					| u32::from(entry.binding)
			})
			.collect();

		pack.add_entry(
			shader_id(program_kind(stage), macros),
			&pack_program(&reflection, &spirv),
		);
	}

	CompileResult::Success
}

#[cfg(test)]
mod tests
{
	use super::*;

	struct Quiet;

	impl Log for Quiet
	{
		fn log(&mut self, _: LogLevel, _: &str) {}
	}

	#[test]
	fn the_profile_names_follow_the_stage()
	{
		assert_eq!(profile(Stage::Vertex), "vs_6_7");
		assert_eq!(profile(Stage::Pixel), "ps_6_7");
		assert_eq!(profile(Stage::Compute), "cs_6_7");
	}

	#[test]
	fn a_shader_in_the_repository_compiles_into_a_pack()
	{
		let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../Shaders/DebugLayer.hlsl");

		// The compiler library is not always installed where the tests run
		let Ok(compiler) = Compiler::new() else {
			return;
		};

		let mut pack = DataPack::default();
		let macros: [MacroRef; 0] = [];

		let result = compile_into_pack(
			&compiler,
			path.to_str().unwrap(),
			&macros,
			&mut pack,
			&mut Quiet,
		);

		assert_eq!(result, CompileResult::Success);

		// The vertex and the pixel program
		assert_eq!(pack.len(), 2);

		let vertex = pack.get(shader_id(ProgramKind::Vertex, &macros)).unwrap();
		let blob = crate::program::ProgramBlob::parse(vertex).unwrap();

		assert_eq!(&blob.spirv[..4], &0x0723_0203u32.to_ne_bytes());
	}
}
