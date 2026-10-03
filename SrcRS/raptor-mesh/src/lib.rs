mod cube;
mod icosphere;
mod simple;
mod vec;

pub use cube::{CubeOptions, FaceOptions, cube};
pub use icosphere::{MAX_ICOSPHERE_RESOLUTION, icosphere};
pub use simple::{line, quad, wireframe_box};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Mesh {
	pub positions: Vec<[f32; 3]>,
	pub normals: Vec<[f32; 3]>,
	pub tangents: Vec<[f32; 3]>,
	pub texcoords: Vec<[f32; 2]>,
	pub indices: Vec<u32>,
}
