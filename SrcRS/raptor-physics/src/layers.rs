#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Layer
{
	Static = 0,
	Dynamic = 1,
	Deactivated = 2,
}

pub const NUM_LAYERS: usize = 3;

/// Whether bodies on two object layers collide. Static bodies only meet dynamic ones, dynamic
/// bodies meet everything that is not deactivated, and deactivated bodies meet nothing.
pub fn layers_collide(first: Layer, second: Layer) -> bool
{
	match first {
		Layer::Static => second == Layer::Dynamic,
		Layer::Dynamic => second != Layer::Deactivated,
		Layer::Deactivated => false,
	}
}

/// Whether an object layer collides with a broad phase layer, which are the same three.
pub fn broad_phase_collides(object: Layer, broad_phase: Layer) -> bool
{
	match object {
		Layer::Static => broad_phase == Layer::Dynamic,
		Layer::Dynamic => true,
		Layer::Deactivated => false,
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn static_bodies_only_collide_with_dynamic_ones()
	{
		assert!(layers_collide(Layer::Static, Layer::Dynamic));
		assert!(!layers_collide(Layer::Static, Layer::Static));
		assert!(!layers_collide(Layer::Static, Layer::Deactivated));
	}

	#[test]
	fn deactivated_bodies_collide_with_nothing()
	{
		for other in [Layer::Static, Layer::Dynamic, Layer::Deactivated] {
			assert!(!layers_collide(Layer::Deactivated, other));
		}
	}

	#[test]
	fn the_broad_phase_agrees_with_the_object_layers()
	{
		for first in [Layer::Static, Layer::Dynamic, Layer::Deactivated] {
			for second in [Layer::Static, Layer::Dynamic] {
				assert_eq!(
					broad_phase_collides(first, second),
					layers_collide(first, second)
				);
			}
		}
	}
}
