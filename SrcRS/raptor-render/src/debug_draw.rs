use raptor_math::mat4::{self, Mat4, Quat};

pub const MAX_SHAPES: usize = 4096;

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Shape
{
	Line = 0,
	WireBox = 1,
	SolidBox = 2,
}

impl Shape
{
	pub const ALL: [Self; 3] = [Self::Line, Self::WireBox, Self::SolidBox];

	pub fn from_raw(raw: u32) -> Option<Self>
	{
		Self::ALL.get(raw as usize).copied()
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Queued
{
	Yes,
	/// The queue was full, and this is the first shape dropped since it was last cleared
	DroppedFirst,
	Dropped,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DrawCommand
{
	pub combined_matrix: [f32; 16],
	pub color: u32,
	pub shape: u32,
}

struct QueuedShape
{
	world: Mat4,
	color: u32,
	shape: Shape,
}

/// The shapes queued to be drawn this frame. They are drawn once, at the end of the frame, and the
/// queue is emptied.
#[derive(Default)]
pub struct DebugDraw
{
	shapes: Vec<QueuedShape>,
	commands: Vec<DrawCommand>,
	warned_about_overflow: bool,
}

impl DebugDraw
{
	pub fn draw(&mut self, shape: Shape, world: Mat4, color: u32) -> Queued
	{
		if self.shapes.len() >= MAX_SHAPES {
			return if std::mem::replace(&mut self.warned_about_overflow, true) {
				Queued::Dropped
			} else {
				Queued::DroppedFirst
			};
		}

		self.shapes.push(QueuedShape {
			world,
			color,
			shape,
		});

		Queued::Yes
	}

	/// A line is a shape that stretches from the origin to (1, 0, 0), so it is placed by the matrix
	/// that maps that onto the line.
	pub fn line(&mut self, from: [f32; 3], to: [f32; 3], color: u32) -> Queued
	{
		let delta = [to[0] - from[0], to[1] - from[1], to[2] - from[2]];

		let world = [
			[delta[0], delta[1], delta[2], 0.0],
			[0.0; 4],
			[0.0; 4],
			[from[0], from[1], from[2], 1.0],
		];

		self.draw(Shape::Line, world, color)
	}

	pub fn queued_count(&self) -> usize
	{
		self.shapes.len()
	}

	/// The draws for the queued shapes, with each shape's matrix taken through `camera`. They are
	/// grouped by kind of shape so each pipeline and mesh is bound once, and keep the order
	/// they were queued in inside a group. The commands stay valid until the next call or
	/// `clear`.
	pub fn commands(&mut self, camera: &Mat4) -> &[DrawCommand]
	{
		self.commands.clear();

		for kind in Shape::ALL {
			for queued in self.shapes.iter().filter(|queued| queued.shape == kind) {
				self.commands.push(DrawCommand {
					combined_matrix: mat4::flatten(&mat4::multiply(&queued.world, camera)),
					color: queued.color,
					shape: kind as u32,
				});
			}
		}

		&self.commands
	}

	pub fn clear(&mut self)
	{
		self.shapes.clear();
		self.warned_about_overflow = false;
	}
}

pub fn box_matrix(center: [f32; 3], half_extent: [f32; 3], rotation: Quat) -> Mat4
{
	mat4::box_matrix(center, half_extent, rotation)
}

/// The center and half extent of an axis aligned box.
pub fn aabb_center_and_extent(min: [f32; 3], max: [f32; 3]) -> ([f32; 3], [f32; 3])
{
	(
		[
			(min[0] + max[0]) * 0.5,
			(min[1] + max[1]) * 0.5,
			(min[2] + max[2]) * 0.5,
		],
		[
			(max[0] - min[0]) * 0.5,
			(max[1] - min[1]) * 0.5,
			(max[2] - min[2]) * 0.5,
		],
	)
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn commands_are_grouped_by_shape_and_keep_their_order()
	{
		let mut draw = DebugDraw::default();

		draw.draw(Shape::SolidBox, mat4::IDENTITY, 1);
		draw.draw(Shape::Line, mat4::IDENTITY, 2);
		draw.draw(Shape::WireBox, mat4::IDENTITY, 3);
		draw.draw(Shape::Line, mat4::IDENTITY, 4);

		let order: Vec<_> = draw
			.commands(&mat4::IDENTITY)
			.iter()
			.map(|command| (command.shape, command.color))
			.collect();

		assert_eq!(order, [(0, 2), (0, 4), (1, 3), (2, 1)]);
	}

	#[test]
	fn the_camera_is_applied_after_the_world_matrix()
	{
		let mut draw = DebugDraw::default();

		draw.draw(Shape::WireBox, mat4::translation([1.0, 0.0, 0.0]), 0);

		let command = draw.commands(&mat4::translation([0.0, 5.0, 0.0]))[0];

		assert_eq!(&command.combined_matrix[12..], &[1.0, 5.0, 0.0, 1.0]);
	}

	#[test]
	fn a_line_matrix_maps_the_unit_x_segment_onto_the_line()
	{
		let mut draw = DebugDraw::default();

		draw.line([1.0, 2.0, 3.0], [4.0, 6.0, 3.0], 0);

		let command = draw.commands(&mat4::IDENTITY)[0];

		let matrix = mat4::unflatten(&command.combined_matrix);
		let end = [
			matrix[0][0] + matrix[3][0],
			matrix[0][1] + matrix[3][1],
			matrix[0][2] + matrix[3][2],
		];

		assert_eq!(&matrix[3][..3], &[1.0, 2.0, 3.0]);
		assert_eq!(end, [4.0, 6.0, 3.0]);
	}

	#[test]
	fn a_full_queue_drops_shapes_and_reports_the_first_once()
	{
		let mut draw = DebugDraw::default();

		for _ in 0..MAX_SHAPES {
			assert_eq!(draw.draw(Shape::Line, mat4::IDENTITY, 0), Queued::Yes);
		}

		assert_eq!(
			draw.draw(Shape::Line, mat4::IDENTITY, 0),
			Queued::DroppedFirst
		);
		assert_eq!(draw.draw(Shape::Line, mat4::IDENTITY, 0), Queued::Dropped);
		assert_eq!(draw.queued_count(), MAX_SHAPES);

		draw.clear();

		assert_eq!(draw.queued_count(), 0);
		assert_eq!(draw.draw(Shape::Line, mat4::IDENTITY, 0), Queued::Yes);
	}

	#[test]
	fn an_aabb_becomes_a_center_and_half_extent()
	{
		let (center, extent) = aabb_center_and_extent([-1.0, 0.0, 2.0], [3.0, 4.0, 6.0]);

		assert_eq!(center, [1.0, 2.0, 4.0]);
		assert_eq!(extent, [2.0, 2.0, 2.0]);
	}
}
