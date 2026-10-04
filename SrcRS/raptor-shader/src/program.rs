const FNV64_INIT: u64 = 0xCBF2_9CE4_8422_2325;
const FNV64_PRIME: u64 = 0x0000_0100_0000_01B3;

const SPIRV_MAGIC: u32 = 0x0723_0203;
const SPIRV_HEADER_WORDS: usize = 5;

const OP_VARIABLE: u32 = 59;
const OP_DECORATE: u32 = 71;
const DECORATION_LOCATION: u32 = 30;
const STORAGE_CLASS_INPUT: u32 = 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProgramKind
{
	Vertex,
	Pixel,
	Compute,
	Other,
}

#[derive(Clone, Copy, Debug)]
pub struct MacroRef<'a>
{
	pub name: Option<&'a [u8]>,
	pub value: Option<&'a [u8]>,
}

pub fn fnv64(mut hash: u64, bytes: &[u8]) -> u64
{
	for byte in bytes {
		hash ^= u64::from(*byte);
		hash = hash.wrapping_mul(FNV64_PRIME);
	}

	hash
}

pub fn hash_macros(macros: &[MacroRef], mut hash: u64) -> u64
{
	for entry in macros {
		if let Some(name) = entry.name {
			hash = fnv64(hash, name);
		}

		hash = fnv64(hash, b"=");

		if let Some(value) = entry.value {
			hash = fnv64(hash, value);
		}

		hash = fnv64(hash, b";");
	}

	hash
}

pub fn shader_id(kind: ProgramKind, macros: &[MacroRef]) -> u64
{
	let seed = match kind {
		ProgramKind::Vertex => fnv64(FNV64_INIT, b"VERTSHADER"),
		ProgramKind::Pixel => fnv64(FNV64_INIT, b"FRAGSHADER"),
		ProgramKind::Compute => fnv64(FNV64_INIT, b"COMPUTESHADER"),
		ProgramKind::Other => FNV64_INIT,
	};

	hash_macros(macros, seed)
}

pub fn macro_hash(macros: &[MacroRef]) -> u64
{
	hash_macros(macros, FNV64_INIT)
}

pub fn input_location_mask(words: &[u32]) -> u32
{
	if words.len() < SPIRV_HEADER_WORDS || words[0] != SPIRV_MAGIC {
		return !0;
	}

	let id_bound = words[3] as usize;

	let mut locations = vec![u32::MAX; id_bound];
	let mut is_input = vec![false; id_bound];

	let mut index = SPIRV_HEADER_WORDS;

	while index < words.len() {
		let opcode = words[index] & 0xFFFF;
		let length = (words[index] >> 16) as usize;

		if length == 0 || index + length > words.len() {
			return !0;
		}

		if opcode == OP_DECORATE && length >= 4 && words[index + 2] == DECORATION_LOCATION {
			let target = words[index + 1] as usize;

			if target < id_bound {
				locations[target] = words[index + 3];
			}
		} else if opcode == OP_VARIABLE && length >= 4 && words[index + 3] == STORAGE_CLASS_INPUT {
			let result = words[index + 2] as usize;

			if result < id_bound {
				is_input[result] = true;
			}
		}

		index += length;
	}

	let mut mask = 0;

	for (id, input) in is_input.iter().enumerate() {
		if *input && locations[id] < 32 {
			mask |= 1u32 << locations[id];
		}
	}

	mask
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn instruction(opcode: u32, operands: &[u32]) -> Vec<u32>
	{
		let mut words = vec![((operands.len() as u32 + 1) << 16) | opcode];
		words.extend_from_slice(operands);
		words
	}

	fn module(id_bound: u32, body: &[Vec<u32>]) -> Vec<u32>
	{
		let mut words = vec![SPIRV_MAGIC, 0x0001_0600, 0, id_bound, 0];

		for instruction in body {
			words.extend_from_slice(instruction);
		}

		words
	}

	#[test]
	fn fnv64_matches_the_reference_vectors()
	{
		assert_eq!(fnv64(FNV64_INIT, b""), 0xcbf2_9ce4_8422_2325);
		assert_eq!(fnv64(FNV64_INIT, b"a"), 0xaf63_dc4c_8601_ec8c);
		assert_eq!(fnv64(FNV64_INIT, b"foobar"), 0x8594_4171_f739_67e8);
	}

	#[test]
	fn macros_keep_name_value_and_separators_apart()
	{
		let flag = [MacroRef {
			name: Some(b"A"),
			value: None,
		}];
		let one = [MacroRef {
			name: Some(b"A"),
			value: Some(b"1"),
		}];
		let two = [MacroRef {
			name: Some(b"A"),
			value: Some(b"2"),
		}];

		assert_eq!(macro_hash(&[]), FNV64_INIT);
		assert_ne!(macro_hash(&flag), macro_hash(&one));
		assert_ne!(macro_hash(&one), macro_hash(&two));
		assert_eq!(macro_hash(&flag), fnv64(FNV64_INIT, b"A=;"));
		assert_eq!(macro_hash(&one), fnv64(FNV64_INIT, b"A=1;"));
	}

	#[test]
	fn ids_are_seeded_by_the_stage()
	{
		assert_eq!(
			shader_id(ProgramKind::Vertex, &[]),
			fnv64(FNV64_INIT, b"VERTSHADER")
		);
		assert_eq!(
			shader_id(ProgramKind::Pixel, &[]),
			fnv64(FNV64_INIT, b"FRAGSHADER")
		);
		assert_eq!(
			shader_id(ProgramKind::Compute, &[]),
			fnv64(FNV64_INIT, b"COMPUTESHADER")
		);
		assert_eq!(shader_id(ProgramKind::Other, &[]), FNV64_INIT);
		assert_ne!(
			shader_id(ProgramKind::Vertex, &[]),
			shader_id(ProgramKind::Pixel, &[])
		);
	}

	#[test]
	fn the_mask_lists_input_locations_only()
	{
		let words = module(
			10,
			&[
				instruction(OP_DECORATE, &[3, DECORATION_LOCATION, 0]),
				instruction(OP_DECORATE, &[4, DECORATION_LOCATION, 2]),
				instruction(OP_DECORATE, &[5, DECORATION_LOCATION, 1]),
				instruction(OP_DECORATE, &[6, DECORATION_LOCATION, 40]),
				instruction(OP_VARIABLE, &[1, 3, STORAGE_CLASS_INPUT]),
				instruction(OP_VARIABLE, &[1, 4, STORAGE_CLASS_INPUT]),
				instruction(OP_VARIABLE, &[1, 5, 3]),
				instruction(OP_VARIABLE, &[1, 6, STORAGE_CLASS_INPUT]),
			],
		);

		assert_eq!(input_location_mask(&words), 0b101);
	}

	#[test]
	fn bad_modules_expose_every_location()
	{
		assert_eq!(input_location_mask(&[]), !0);
		assert_eq!(input_location_mask(&[0; 8]), !0);

		let mut truncated = module(4, &[instruction(OP_VARIABLE, &[1, 2, STORAGE_CLASS_INPUT])]);
		truncated.pop();
		assert_eq!(input_location_mask(&truncated), !0);

		let zero_length = module(4, &[vec![0]]);
		assert_eq!(input_location_mask(&zero_length), !0);
	}

	#[test]
	fn ids_beyond_the_bound_are_ignored()
	{
		let words = module(
			4,
			&[
				instruction(OP_DECORATE, &[9, DECORATION_LOCATION, 0]),
				instruction(OP_VARIABLE, &[1, 9, STORAGE_CLASS_INPUT]),
			],
		);

		assert_eq!(input_location_mask(&words), 0);
	}
}
