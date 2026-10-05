use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use raptor_gpu::{
	Device, ReflectionEntry, SHADER_COMPUTE, SHADER_PIXEL, SHADER_VERTEX, ShaderProgramRecord,
};
use raptor_shader::compiler::{CompileResult, Compiler, compile_into_pack};
use raptor_shader::datapack::DataPack;
use raptor_shader::preproc::Stage;
use raptor_shader::program::{
	MacroRef, ProgramBlob, ProgramKind, hash_macros, input_location_mask, shader_id,
};
use raptor_shader::{Log, LogLevel};

/// The hash a set of macros starts from, and the one the programs of a shader are cached by.
const HASH_INIT: u64 = 0xCBF2_9CE4_8422_2325;

#[derive(Debug, PartialEq, Eq)]
pub enum LoadError
{
	/// The shader compiler could not be loaded
	CompilerUnavailable,
	/// The shader did not compile
	CompileFailed,
}

fn stage_kind(stage: Stage) -> ProgramKind
{
	match stage {
		Stage::Vertex => ProgramKind::Vertex,
		Stage::Pixel => ProgramKind::Pixel,
		Stage::Compute => ProgramKind::Compute,
	}
}

fn stage_bits(stage: Stage) -> u32
{
	match stage {
		Stage::Vertex => SHADER_VERTEX,
		Stage::Pixel => SHADER_PIXEL,
		Stage::Compute => SHADER_COMPUTE,
	}
}

fn stage_index(stage: Stage) -> usize
{
	match stage {
		Stage::Vertex => 0,
		Stage::Pixel => 1,
		Stage::Compute => 2,
	}
}

type ProgramCache = HashMap<u64, Option<Arc<ShaderProgramRecord>>>;

struct Shader
{
	name: String,
	source_path: String,
	pack_path: String,
	pack: DataPack,
	/// The programs made so far for each stage, by the hash of their macros. A program that was
	/// looked for and not found is kept too, as nothing about it changes.
	programs: [ProgramCache; 3],
	/// The sets of macros the shader was compiled for in this run
	compiled: HashSet<u64>,
}

/// Finds the compiled programs of the shaders, compiling them from their source when they are asked
/// for.
pub struct ShaderLibrary
{
	directory: String,
	shaders: HashMap<String, Shader>,
	compiler: Option<Compiler>,
}

impl ShaderLibrary
{
	/// `directory` holds the shaders and ends with a slash. Their compiled packs are in its `Spirv`
	/// folder.
	pub fn new(directory: &str) -> Self
	{
		Self {
			directory: directory.to_owned(),
			shaders: HashMap::new(),
			compiler: None,
		}
	}

	/// Gets the program of `stage` for `macros` from the shader called `name`, or none if the
	/// shader has no such stage. A shader is compiled the first time a set of macros is asked
	/// for, in every run, so changes to its source are picked up.
	pub fn program(
		&mut self,
		device: &Device,
		name: &str,
		stage: Stage,
		macros: &[MacroRef],
		log: &mut dyn Log,
	) -> Result<Option<Arc<ShaderProgramRecord>>, LoadError>
	{
		let macro_hash = hash_macros(macros, HASH_INIT);

		let directory = &self.directory;

		let shader = self.shaders.entry(name.to_owned()).or_insert_with(|| {
			let mut shader = Shader {
				name: name.to_owned(),
				source_path: format!("{directory}{name}.hlsl"),
				pack_path: format!("{directory}Spirv/{name}.spack"),
				pack: DataPack::default(),
				programs: Default::default(),
				compiled: HashSet::new(),
			};

			shader.pack.read_file(shader.pack_path.as_ref());

			shader
		});

		if let Some(cached) = shader.programs[stage_index(stage)].get(&macro_hash) {
			return Ok(cached.clone());
		}

		if shader.pack.is_empty() {
			shader.pack.read_file(shader.pack_path.as_ref());
		}

		if shader.compiled.insert(macro_hash) {
			if self.compiler.is_none() {
				match Compiler::new() {
					Ok(compiler) => self.compiler = Some(compiler),
					Err(error) => {
						shader.compiled.remove(&macro_hash);

						log.log(
							LogLevel::Error,
							&format!("Could not load the shader compiler: {error}"),
						);
						return Err(LoadError::CompilerUnavailable);
					}
				}
			}

			let compiler = self.compiler.as_ref().expect("the compiler was just made");

			let result =
				compile_into_pack(compiler, &shader.source_path, macros, &mut shader.pack, log);

			if result == CompileResult::Failed {
				shader.compiled.remove(&macro_hash);
				return Err(LoadError::CompileFailed);
			}

			// The pack is kept for the next run. It is not an error if that is not possible.
			let _ = shader.pack.write_file(shader.pack_path.as_ref());
		}

		let program = Self::make_program(device, shader, stage, macros, log);

		shader.programs[stage_index(stage)].insert(macro_hash, program.clone());

		Ok(program)
	}

	fn make_program(
		device: &Device,
		shader: &Shader,
		stage: Stage,
		macros: &[MacroRef],
		log: &mut dyn Log,
	) -> Option<Arc<ShaderProgramRecord>>
	{
		let bytes = shader.pack.get(shader_id(stage_kind(stage), macros))?;
		let blob = ProgramBlob::parse(bytes)?;

		if blob.spirv.is_empty() {
			return None;
		}

		if blob.spirv.len() % 4 != 0 {
			log.log(
				LogLevel::Error,
				&format!(
					"Shader '{}' has a program that is not a whole number of words",
					shader.name
				),
			);
			return None;
		}

		let (words, _) = blob.spirv.as_chunks::<4>();
		let code: Vec<u32> = words.iter().map(|word| u32::from_ne_bytes(*word)).collect();

		let reflection = blob
			.reflection
			.iter()
			.map(|entry| ReflectionEntry {
				kind: (entry >> 16) as u16,
				set: (entry >> 8) as u8,
				binding: *entry as u8,
			})
			.collect();

		let mask = if stage == Stage::Vertex {
			input_location_mask(&code)
		} else {
			!0
		};

		match ShaderProgramRecord::create(
			device,
			&code,
			reflection,
			stage_bits(stage),
			mask,
			&shader.name,
		) {
			Ok(program) => Some(program),
			Err(error) => {
				log.log(
					LogLevel::Error,
					&format!("Could not create Vulkan shader module: {error:?}"),
				);
				None
			}
		}
	}

	/// Destroys the programs the library made, if nothing else holds onto them.
	///
	/// # Safety
	///
	/// The device must be the one the programs were made with, and no pipeline may be being made
	/// from them.
	pub unsafe fn destroy(self, device: &Device)
	{
		for shader in self.shaders.into_values() {
			for cache in shader.programs {
				for program in cache.into_values().flatten() {
					// SAFETY: guaranteed by the caller.
					unsafe { ShaderProgramRecord::release_arc(program, device) };
				}
			}
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn each_stage_has_its_own_index_and_bit()
	{
		assert_eq!(stage_index(Stage::Vertex), 0);
		assert_eq!(stage_index(Stage::Pixel), 1);
		assert_eq!(stage_index(Stage::Compute), 2);

		assert_eq!(stage_bits(Stage::Vertex), SHADER_VERTEX);
		assert_eq!(stage_bits(Stage::Pixel), SHADER_PIXEL);
		assert_eq!(stage_bits(Stage::Compute), SHADER_COMPUTE);
	}

	#[test]
	fn the_cache_hash_starts_from_the_fnv_offset()
	{
		assert_eq!(hash_macros(&[], HASH_INIT), HASH_INIT);
	}
}
