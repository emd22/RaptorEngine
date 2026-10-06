use raptor_entity::light_core::{TYPE_DIRECTIONAL, TYPE_SPOT};
use raptor_entity::{EntityCore, LightCore};
use raptor_math::quat_platform as q;
use raptor_math::{Mat4f, Vec3f};

use crate::light_logic::{inv_radius_sq, spot_falloff};

/// A single light slot in the light buffer. Mirrors `Light` in `Shaders/LightingCommon.hlsli`.
#[repr(C, align(16))]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LightGpuData
{
	pub light_camera_matrix: [f32; 16],
	pub position: [f32; 3],
	pub radius: f32,
	pub color: u32,
	pub light_type: u32,
	pub intensity: f32,
	pub inv_radius_sq: f32,
	pub spot_direction: [f32; 3],
	pub spot_cos_outer: f32,
	pub linear_color: [f32; 3],
	pub spot_angle_scale: f32,
	pub shadow_atlas_rect: [f32; 4],
}

const _: () = assert!(size_of::<LightGpuData>() == 144);

// SAFETY: the struct is `repr(C)` with only 4 byte numeric fields and no padding, as the size
// assert above shows.
unsafe impl bytemuck::Zeroable for LightGpuData {}
// SAFETY: as above, and every bit pattern is a valid value of each field.
unsafe impl bytemuck::Pod for LightGpuData {}

/// What a light needs from the rest of the frame to be written to the light buffer.
pub struct ShadowInputs
{
	/// The matrix of the view the sun's shadow map was drawn from
	pub directional_matrix: [f32; 16],
	pub directional_rect: [f32; 4],
	/// Where a spot light's baked shadow is in the atlas, if it has one
	pub spot_rect: Option<[f32; 4]>,
}

fn srgb_to_linear(value: f32) -> f32
{
	if value <= 0.04045 {
		value / 12.92
	} else {
		((value + 0.055) / 1.055).powf(2.4)
	}
}

fn linear_color(color: u32) -> [f32; 3]
{
	let channel = |shift: u32| srgb_to_linear(((color >> shift) & 0xFF) as f32 / 255.0);

	[channel(0), channel(8), channel(16)]
}

pub fn light_gpu_data(light: &LightCore, entity: &EntityCore, shadow: &ShadowInputs)
-> LightGpuData
{
	let [x, y, z, _] = entity.position;

	let mut data = LightGpuData {
		light_camera_matrix: Mat4f::identity().to_rows(),
		position: [x, y, z],
		radius: light.radius,
		color: light.color,
		light_type: light.light_type,
		intensity: light.intensity,
		inv_radius_sq: inv_radius_sq(light.radius),
		spot_direction: [0.0; 3],
		spot_cos_outer: 0.0,
		linear_color: linear_color(light.color),
		spot_angle_scale: 0.0,
		shadow_atlas_rect: [0.0; 4],
	};

	if light.light_type == TYPE_DIRECTIONAL {
		data.position = Vec3f::new(x, y, z).normalize().to_array();
		data.light_camera_matrix = shadow.directional_matrix;
		data.shadow_atlas_rect = shadow.directional_rect;
	}

	if light.light_type == TYPE_SPOT {
		let rotation = q::set(
			entity.rotation[0],
			entity.rotation[1],
			entity.rotation[2],
			entity.rotation[3],
		);
		let direction = Vec3f(q::get_direction(rotation)).normalize();

		data.spot_direction = direction.to_array();

		let (cos_outer, scale) = spot_falloff(light.inner_angle, light.outer_angle);

		data.spot_cos_outer = cos_outer;
		data.spot_angle_scale = scale;

		if let Some(rect) = shadow.spot_rect {
			data.light_camera_matrix = light.shadow_matrix;
			data.shadow_atlas_rect = rect;
		}
	}

	data
}

#[cfg(test)]
mod tests
{
	use raptor_entity::light_core::TYPE_POINT;

	use super::*;

	fn inputs() -> ShadowInputs
	{
		ShadowInputs {
			directional_matrix: [2.0; 16],
			directional_rect: [1.0, 2.0, 3.0, 4.0],
			spot_rect: None,
		}
	}

	#[test]
	fn a_point_light_has_no_shadow_and_keeps_its_position()
	{
		let mut light = LightCore::default();
		light.light_type = TYPE_POINT;
		light.radius = 4.0;
		let mut entity = EntityCore::default();
		entity.position = [1.0, 2.0, 3.0, 0.0];

		let data = light_gpu_data(&light, &entity, &inputs());

		assert_eq!(data.position, [1.0, 2.0, 3.0]);
		assert_eq!(data.shadow_atlas_rect, [0.0; 4]);
		assert_eq!(data.light_camera_matrix, Mat4f::identity().to_rows());
		assert_eq!(data.inv_radius_sq, 1.0 / 16.0);
	}

	#[test]
	fn the_sun_gets_a_unit_direction_and_the_shadow_view()
	{
		let mut light = LightCore::default();
		light.light_type = TYPE_DIRECTIONAL;
		let mut entity = EntityCore::default();
		entity.position = [0.0, 10.0, 0.0, 0.0];

		let data = light_gpu_data(&light, &entity, &inputs());

		assert_eq!(data.position, [0.0, 1.0, 0.0]);
		assert_eq!(data.light_camera_matrix, [2.0; 16]);
		assert_eq!(data.shadow_atlas_rect, [1.0, 2.0, 3.0, 4.0]);
	}

	#[test]
	fn a_spot_light_points_forward_and_only_shadows_with_a_tile()
	{
		let mut light = LightCore::default();
		light.light_type = TYPE_SPOT;
		light.shadow_matrix = [3.0; 16];
		let entity = EntityCore::default();

		let bare = light_gpu_data(&light, &entity, &inputs());

		assert!((bare.spot_direction[2] - 1.0).abs() < 1e-6);
		assert!(bare.spot_angle_scale > 0.0);
		assert_eq!(bare.shadow_atlas_rect, [0.0; 4]);

		let shadowed = light_gpu_data(
			&light,
			&entity,
			&ShadowInputs {
				spot_rect: Some([0.5; 4]),
				..inputs()
			},
		);

		assert_eq!(shadowed.light_camera_matrix, [3.0; 16]);
		assert_eq!(shadowed.shadow_atlas_rect, [0.5; 4]);
	}

	#[test]
	fn white_is_linear_one_and_black_is_zero()
	{
		assert_eq!(linear_color(0xFFFF_FFFF), [1.0; 3]);
		assert_eq!(linear_color(0xFF00_0000), [0.0; 3]);
	}
}
