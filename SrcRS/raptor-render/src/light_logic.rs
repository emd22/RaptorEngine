use raptor_math::{Mat4f, Vec3f};

pub fn inv_radius_sq(radius: f32) -> f32
{
	1.0 / (radius * radius).max(1e-8)
}

/// The inner and outer angle of a cone, with the outer limited to a right angle and the inner to
/// the outer.
pub fn clamp_cone(inner: f32, outer: f32, max_outer: f32) -> (f32, f32)
{
	let outer = outer.clamp(0.0, max_outer);

	(inner.clamp(0.0, outer), outer)
}

/// The box around the part of a sphere of `radius` that a cone with its apex at the centre
/// reaches.
pub fn spot_bounds(
	position: Vec3f,
	direction: Vec3f,
	outer_angle: f32,
	radius: f32,
) -> (Vec3f, Vec3f)
{
	let direction = direction.normalize().to_array();

	let mut low = [0.0f32; 3];
	let mut high = [0.0f32; 3];

	for axis in 0..3 {
		let angle_positive = direction[axis].clamp(-1.0, 1.0).acos();
		let angle_negative = std::f32::consts::PI - angle_positive;

		let reach = |angle: f32| {
			if angle <= outer_angle {
				1.0
			} else {
				(angle - outer_angle).cos()
			}
		};

		high[axis] = radius * reach(angle_positive).max(0.0);
		low[axis] = -radius * reach(angle_negative).max(0.0);
	}

	(
		position + Vec3f::from_array(low),
		position + Vec3f::from_array(high),
	)
}

pub fn spot_solid_angle(inner: f32, outer: f32) -> f32
{
	let cos_inner = inner.cos();
	let cos_outer = outer.cos();

	2.0 * std::f32::consts::PI * ((1.0 - cos_inner) + ((cos_inner - cos_outer) / 3.0))
}

pub fn intensity_from_lumens(lumens: f32, inner: f32, outer: f32) -> f32
{
	lumens / spot_solid_angle(inner, outer).max(1e-4)
}

/// The cosine of the outer angle and the rate the light falls off at between the two angles.
pub fn spot_falloff(inner: f32, outer: f32) -> (f32, f32)
{
	let cos_inner = inner.cos();
	let cos_outer = outer.cos();

	(cos_outer, 1.0 / (cos_inner - cos_outer).max(1e-4))
}

pub struct SpotShadowSettings
{
	pub fov_padding: f32,
	pub max_half_fov: f32,
	pub near_plane: f32,
}

/// The matrix a spot light's shadow map is rendered with.
pub fn spot_shadow_matrix(
	position: Vec3f,
	direction: Vec3f,
	outer_angle: f32,
	radius: f32,
	settings: &SpotShadowSettings,
) -> Mat4f
{
	let direction = direction.normalize();

	let up = if direction.y.abs() > 0.99 {
		Vec3f::new(0.0, 0.0, 1.0)
	} else {
		Vec3f::UP
	};

	let view = Mat4f::look_at(position, position + direction, up);

	let half_fov = (outer_angle + settings.fov_padding).min(settings.max_half_fov);
	let far_plane = radius.max(settings.near_plane * 2.0);

	let projection = Mat4f::perspective(half_fov * 2.0, 1.0, far_plane, settings.near_plane);

	view * projection
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn a_zero_radius_light_gets_a_finite_reciprocal()
	{
		assert!(inv_radius_sq(0.0).is_finite());
		assert_eq!(inv_radius_sq(2.0), 0.25);
	}

	#[test]
	fn the_cone_is_clamped_to_a_right_angle_and_the_inner_to_the_outer()
	{
		let right = std::f32::consts::FRAC_PI_2;

		assert_eq!(clamp_cone(3.0, 5.0, right), (right, right));
		assert_eq!(clamp_cone(0.5, 0.25, right), (0.25, 0.25));
		assert_eq!(clamp_cone(-1.0, 0.5, right), (0.0, 0.5));
	}

	#[test]
	fn a_narrow_cone_reaches_only_forward()
	{
		let (min, max) = spot_bounds(Vec3f::ZERO, Vec3f::new(0.0, 0.0, 1.0), 0.1, 10.0);

		assert!((max.z - 10.0).abs() < 1e-4);
		assert!(min.z.abs() < 1e-4);
		assert!(max.x > 0.0 && max.x < 2.0);
	}

	#[test]
	fn lumens_and_intensity_round_trip()
	{
		let (inner, outer) = (0.3, 0.6);
		let intensity = intensity_from_lumens(100.0, inner, outer);

		assert!((intensity * spot_solid_angle(inner, outer) - 100.0).abs() < 1e-2);
	}

	#[test]
	fn a_hard_edged_cone_keeps_a_finite_falloff()
	{
		let (_, scale) = spot_falloff(0.5, 0.5);

		assert_eq!(scale, 1.0 / 1e-4);
	}
}
