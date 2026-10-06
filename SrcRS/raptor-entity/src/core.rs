use raptor_math::mat4::Quat;
use raptor_math::quat_platform as q;
use raptor_math::{Mat4f, Vec3f};

pub const NO_ID: u32 = u32::MAX;

const PHYSICS_OUT_OF_DATE: u8 = 1 << 0;
const MATRIX_OUT_OF_DATE: u8 = 1 << 1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum TransformMode
{
	Default = 0,
	FromOrigin = 1,
}

/// Where an entity is and which way it faces, with the matrix that follows from it. The engine
/// reads this straight out of the record, so the layout is fixed: it is also described in the C
/// header.
#[repr(C, align(16))]
pub struct EntityCore
{
	pub matrix: [f32; 16],
	pub position: [f32; 4],
	/// A quaternion, x y z w
	pub rotation: [f32; 4],
	pub rotation_origin: [f32; 4],
	pub scale: f32,
	pub transform_mode: u32,
	pub id: u32,
	flags: u8,
	/// The frames in flight the matrix has been written to the object buffer for
	submitted_frames: u8,
}

impl Default for EntityCore
{
	fn default() -> Self
	{
		Self {
			matrix: Mat4f::identity().to_rows(),
			position: [0.0; 4],
			rotation: [0.0, 0.0, 0.0, 1.0],
			rotation_origin: [0.0; 4],
			scale: 1.0,
			transform_mode: TransformMode::Default as u32,
			id: NO_ID,
			flags: PHYSICS_OUT_OF_DATE | MATRIX_OUT_OF_DATE,
			submitted_frames: 0,
		}
	}
}

fn vec3(values: [f32; 4]) -> Vec3f
{
	Vec3f::new(values[0], values[1], values[2])
}

impl EntityCore
{
	pub fn mark_transform_out_of_date(&mut self)
	{
		self.flags |= PHYSICS_OUT_OF_DATE | MATRIX_OUT_OF_DATE;
	}

	pub fn mark_matrix_out_of_date(&mut self)
	{
		self.flags |= MATRIX_OUT_OF_DATE;
	}

	pub fn is_matrix_out_of_date(&self) -> bool
	{
		self.flags & MATRIX_OUT_OF_DATE != 0
	}

	pub fn is_physics_out_of_date(&self) -> bool
	{
		self.flags & PHYSICS_OUT_OF_DATE != 0
	}

	pub fn set_physics_out_of_date(&mut self, value: bool)
	{
		if value {
			self.flags |= PHYSICS_OUT_OF_DATE;
		} else {
			self.flags &= !PHYSICS_OUT_OF_DATE;
		}
	}

	pub fn set_position(&mut self, position: [f32; 3])
	{
		self.position = [position[0], position[1], position[2], 0.0];
		self.mark_transform_out_of_date();
	}

	pub fn set_rotation(&mut self, rotation: [f32; 4])
	{
		self.rotation = rotation;
		self.mark_transform_out_of_date();
	}

	pub fn set_scale(&mut self, scale: f32)
	{
		self.scale = scale;
		self.mark_transform_out_of_date();
	}

	/// Rotates about a point other than the entity's position from now on.
	pub fn set_rotation_origin(&mut self, origin: [f32; 3])
	{
		self.rotation_origin = [origin[0], origin[1], origin[2], 0.0];
		self.transform_mode = TransformMode::FromOrigin as u32;
		self.mark_matrix_out_of_date();
	}

	/// The rotation after turning by `angle` about an axis of the entity's own space, renormalized
	/// so that repeated turns do not add scale to the matrix.
	pub fn rotated_by_axis(&self, axis: [f32; 3], angle: f32) -> [f32; 4]
	{
		let turn = q::from_axis_angle(Vec3f::new(axis[0], axis[1], axis[2]).0, angle);
		let current = q::set(
			self.rotation[0],
			self.rotation[1],
			self.rotation[2],
			self.rotation[3],
		);

		q::get_values(q::normalize(q::mul(current, turn)))
	}

	fn recalculate_matrix(&mut self)
	{
		let rotation = Quat {
			x: self.rotation[0],
			y: self.rotation[1],
			z: self.rotation[2],
			w: self.rotation[3],
		};

		let scale = Mat4f::as_scale(Vec3f::splat(self.scale));
		let rotation = Mat4f::as_rotation(rotation);
		let position = vec3(self.position);

		let matrix = if self.transform_mode == TransformMode::FromOrigin as u32 {
			let origin = vec3(self.rotation_origin);

			scale
				* Mat4f::as_translation(origin)
				* rotation
				* Mat4f::as_translation(-origin)
				* Mat4f::as_translation(position)
		} else {
			scale * rotation * Mat4f::as_translation(position)
		};

		self.matrix = matrix.to_rows();
		self.submitted_frames = 0;
		self.flags &= !MATRIX_OUT_OF_DATE;
	}

	pub fn update_matrix_if_out_of_date(&mut self)
	{
		if self.is_matrix_out_of_date() {
			self.recalculate_matrix();
		}
	}

	pub fn world_matrix(&mut self) -> &[f32; 16]
	{
		self.update_matrix_if_out_of_date();

		&self.matrix
	}

	/// Uses a matrix made elsewhere, which the next update does not replace.
	pub fn set_model_matrix(&mut self, matrix: &[f32; 16])
	{
		self.flags &= !MATRIX_OUT_OF_DATE;
		self.matrix = *matrix;
		self.submitted_frames = 0;
	}

	/// Whether the matrix has still to be written to the object buffer for `frame`, which it is
	/// then marked as having been.
	pub fn submit_needed(&mut self, frame: u32) -> bool
	{
		let bit = 1u8 << frame;

		if self.submitted_frames & bit != 0 {
			return false;
		}

		self.submitted_frames |= bit;

		true
	}
}

#[cfg(test)]
mod tests
{
	use std::mem::{align_of, offset_of, size_of};

	use super::*;

	#[test]
	fn the_layout_is_the_one_the_header_declares()
	{
		assert_eq!(align_of::<EntityCore>(), 16);
		assert_eq!(offset_of!(EntityCore, matrix), 0);
		assert_eq!(offset_of!(EntityCore, position), 64);
		assert_eq!(offset_of!(EntityCore, rotation), 80);
		assert_eq!(offset_of!(EntityCore, rotation_origin), 96);
		assert_eq!(offset_of!(EntityCore, scale), 112);
		assert_eq!(offset_of!(EntityCore, transform_mode), 116);
		assert_eq!(offset_of!(EntityCore, id), 120);
		assert_eq!(size_of::<EntityCore>() % 16, 0);
	}

	#[test]
	fn a_new_entity_has_an_out_of_date_matrix_that_is_made_when_asked_for()
	{
		let mut core = EntityCore::default();

		assert!(core.is_matrix_out_of_date());
		assert!(core.is_physics_out_of_date());

		core.set_position([1.0, 2.0, 3.0]);
		core.set_scale(2.0);

		let matrix = *core.world_matrix();

		assert_eq!(&matrix[0..3], &[2.0, 0.0, 0.0]);
		assert_eq!(&matrix[12..15], &[1.0, 2.0, 3.0]);
		assert!(!core.is_matrix_out_of_date());
		assert!(core.is_physics_out_of_date());
	}

	#[test]
	fn a_matrix_set_directly_is_not_replaced_by_the_next_update()
	{
		let mut core = EntityCore::default();
		let mut matrix = Mat4f::identity().to_rows();

		matrix[12] = 9.0;

		core.set_model_matrix(&matrix);
		core.update_matrix_if_out_of_date();

		assert_eq!(core.matrix[12], 9.0);
	}

	#[test]
	fn rotating_about_an_origin_moves_the_entity_around_it()
	{
		let mut core = EntityCore::default();

		core.set_rotation_origin([-1.0, 0.0, 0.0]);
		core.set_rotation(core.rotated_by_axis([0.0, 1.0, 0.0], std::f32::consts::PI));

		let matrix = *core.world_matrix();

		assert!((matrix[12] - 2.0).abs() < 1e-4, "{matrix:?}");
		assert_eq!(core.transform_mode, TransformMode::FromOrigin as u32);
	}

	#[test]
	fn repeated_turns_keep_the_rotation_a_unit_quaternion()
	{
		let mut core = EntityCore::default();

		for _ in 0..200 {
			core.set_rotation(core.rotated_by_axis([1.0, 0.0, 0.0], 0.1));
		}

		let length: f32 = core
			.rotation
			.iter()
			.map(|value| value * value)
			.sum::<f32>()
			.sqrt();

		assert!((length - 1.0).abs() < 1e-5);
	}

	#[test]
	fn a_matrix_is_submitted_once_per_frame_until_it_changes()
	{
		let mut core = EntityCore::default();

		assert!(core.submit_needed(1));
		assert!(!core.submit_needed(1));
		assert!(core.submit_needed(2));

		core.set_position([1.0, 0.0, 0.0]);
		core.update_matrix_if_out_of_date();

		assert!(core.submit_needed(1));
	}
}
