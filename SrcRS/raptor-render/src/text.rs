pub const MAX_GLYPHS: usize = 256;
pub const GLYPH_WIDTH: u32 = 6;
pub const GLYPH_HEIGHT: u32 = 12;
pub const ATLAS_COLUMNS: u32 = 16;
pub const MARGIN: [f32; 2] = [20.0, 20.0];

pub const INSTANCE_SIZE: u32 = std::mem::size_of::<InstanceData>() as u32;

/// The characters of the font atlas, row by row.
const GLYPH_MAP: &[u8] = b" !\"#$%&'()*+,-./\
0123456789:;<=>?\
@ABCDEFGHIJKLMNO\
PQRSTUVWXYZ[\\]^_\
`abcdefghijklmno\
pqrstuvwxyz{|}~ ";

/// One quad of text or image, placed by its bottom left corner relative to the centre of the window
/// with +Y up. Matches what the text shader reads.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct InstanceData
{
	pub position: [f32; 2],
	pub size: [f32; 2],
	pub uv_min: [f32; 2],
	pub uv_max: [f32; 2],
}

/// Where a character is in the atlas, if it has a glyph.
pub fn glyph_index(character: u8) -> Option<usize>
{
	GLYPH_MAP.iter().position(|glyph| *glyph == character)
}

/// What is left to track between draws of text in a frame: how much of the frame's instance buffer
/// is used, and where the next line of text goes.
#[derive(Default)]
pub struct TextState
{
	last_frame: u32,
	tape_offset: u32,
	cursor: [f32; 2],
}

impl TextState
{
	/// Rewinds the instance buffer and the text cursor on the first draw of each frame.
	pub fn begin_frame_if_needed(&mut self, frame_number: u32)
	{
		if frame_number != self.last_frame {
			self.last_frame = frame_number;
			self.tape_offset = 0;
			self.cursor = [0.0, 0.0];
		}
	}

	pub fn cursor(&self) -> [f32; 2]
	{
		self.cursor
	}

	pub fn move_cursor_down(&mut self, amount: f32)
	{
		self.cursor[1] += amount;
	}

	/// Takes room for `count` quads in the frame's instance buffer, and returns where they start in
	/// bytes. Returns none, taking nothing, if they do not fit.
	pub fn reserve(&mut self, count: u32) -> Option<u32>
	{
		let size = count * INSTANCE_SIZE;

		if self.tape_offset + size > MAX_GLYPHS as u32 * INSTANCE_SIZE {
			return None;
		}

		let offset = self.tape_offset;

		self.tape_offset += size;

		Some(offset)
	}
}

/// Lays `text` out from `origin`, a position measured down from the top left of the window plus the
/// margin, into one quad for each character that has a glyph. Characters without one leave a gap,
/// and anything past `MAX_GLYPHS` is cut off. Also returns the height of a line.
pub fn layout_text(
	text: &[u8],
	scale: f32,
	origin: [f32; 2],
	window: (u32, u32),
	atlas: (u32, u32),
) -> (Vec<InstanceData>, f32)
{
	let glyph_width = GLYPH_WIDTH as f32 * scale;
	let glyph_height = GLYPH_HEIGHT as f32 * scale;

	let atlas_width = atlas.0 as f32;
	let atlas_height = atlas.1 as f32;

	let half_window = [window.0 as f32 * 0.5, window.1 as f32 * 0.5];

	let mut cursor = [
		origin[0] - half_window[0] + MARGIN[0],
		origin[1] - half_window[1] + MARGIN[1],
	];

	let mut instances = Vec::new();

	for character in text.iter().take_while(|character| **character != 0) {
		if instances.len() >= MAX_GLYPHS {
			break;
		}

		let Some(glyph) = glyph_index(*character) else {
			cursor[0] += glyph_width;
			continue;
		};

		let column = glyph as u32 % ATLAS_COLUMNS;
		let row = glyph as u32 / ATLAS_COLUMNS;

		instances.push(InstanceData {
			position: [cursor[0], -cursor[1]],
			size: [glyph_width, glyph_height],
			uv_min: [
				(column as f32 * GLYPH_WIDTH as f32) / atlas_width,
				(row as f32 * GLYPH_HEIGHT as f32) / atlas_height,
			],
			uv_max: [
				((column + 1) as f32 * GLYPH_WIDTH as f32) / atlas_width,
				((row + 1) as f32 * GLYPH_HEIGHT as f32) / atlas_height,
			],
		});

		cursor[0] += glyph_width;
	}

	(instances, glyph_height)
}

/// The quad for an image drawn with its top left corner at `position` in window pixels, measured
/// down from the top left of the window.
pub fn image_instance(position: [f32; 2], size: [f32; 2], window: (u32, u32)) -> InstanceData
{
	let half_width = window.0 as f32 * 0.5;
	let half_height = window.1 as f32 * 0.5;

	InstanceData {
		position: [
			position[0] - half_width,
			half_height - (position[1] + size[1]),
		],
		size,
		uv_min: [0.0, 0.0],
		uv_max: [1.0, 1.0],
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn glyphs_follow_the_atlas_rows()
	{
		assert_eq!(glyph_index(b' '), Some(0));
		assert_eq!(glyph_index(b'0'), Some(16));
		assert_eq!(glyph_index(b'A'), Some(33));
		assert_eq!(glyph_index(b'~'), Some(94));
		assert_eq!(glyph_index(0x7f), None);
		assert_eq!(glyph_index(b'\n'), None);
	}

	#[test]
	fn text_is_laid_out_left_to_right_from_the_top_left_corner()
	{
		let (instances, line_height) = layout_text(b"A B", 2.0, [0.0, 0.0], (800, 600), (96, 72));

		assert_eq!(line_height, 24.0);
		assert_eq!(instances.len(), 3);

		assert_eq!(instances[0].position, [-400.0 + 20.0, -(-300.0 + 20.0)]);
		assert_eq!(instances[1].position[0], instances[0].position[0] + 12.0);
		assert_eq!(instances[2].position[0], instances[0].position[0] + 24.0);
		assert_eq!(instances[0].size, [12.0, 24.0]);
		assert_eq!(instances[0].uv_min, [1.0 * 6.0 / 96.0, 2.0 * 12.0 / 72.0]);
	}

	#[test]
	fn a_character_without_a_glyph_leaves_a_gap()
	{
		let (instances, _) = layout_text(b"A\x01B", 1.0, [0.0, 0.0], (100, 100), (96, 72));

		assert_eq!(instances.len(), 2);
		assert_eq!(instances[1].position[0], instances[0].position[0] + 12.0);
	}

	#[test]
	fn text_stops_at_a_nul_and_at_the_glyph_limit()
	{
		let (instances, _) = layout_text(b"AB\0CD", 1.0, [0.0, 0.0], (100, 100), (96, 72));
		assert_eq!(instances.len(), 2);

		let long = vec![b'A'; MAX_GLYPHS + 10];
		let (instances, _) = layout_text(&long, 1.0, [0.0, 0.0], (100, 100), (96, 72));
		assert_eq!(instances.len(), MAX_GLYPHS);
	}

	#[test]
	fn the_tape_gives_out_room_until_it_is_full_and_rewinds_each_frame()
	{
		let mut state = TextState::default();

		state.begin_frame_if_needed(1);

		assert_eq!(state.reserve(100), Some(0));
		assert_eq!(state.reserve(100), Some(100 * INSTANCE_SIZE));
		assert_eq!(state.reserve(57), None);
		assert_eq!(state.reserve(56), Some(200 * INSTANCE_SIZE));
		assert_eq!(state.reserve(1), None);

		state.move_cursor_down(24.0);
		assert_eq!(state.cursor(), [0.0, 24.0]);

		state.begin_frame_if_needed(1);
		assert_eq!(state.reserve(1), None);

		state.begin_frame_if_needed(2);
		assert_eq!(state.reserve(1), Some(0));
		assert_eq!(state.cursor(), [0.0, 0.0]);
	}

	#[test]
	fn an_image_is_placed_by_its_bottom_left_corner_with_y_up()
	{
		let instance = image_instance([10.0, 20.0], [32.0, 32.0], (800, 600));

		assert_eq!(instance.position, [10.0 - 400.0, 300.0 - 52.0]);
		assert_eq!(instance.size, [32.0, 32.0]);
		assert_eq!(instance.uv_max, [1.0, 1.0]);
	}
}
