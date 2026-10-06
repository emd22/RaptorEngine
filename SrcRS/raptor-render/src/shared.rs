use std::ops::Deref;

pub struct Shared<T>(pub T);

// SAFETY: used for GPU resource handles that are only touched on the render thread, even where a
// closure that holds them has to be `Send` to be stored.
unsafe impl<T> Send for Shared<T> {}
// SAFETY: as above.
unsafe impl<T> Sync for Shared<T> {}

impl<T> Deref for Shared<T> {
	type Target = T;

	fn deref(&self) -> &T {
		&self.0
	}
}

pub fn light_gpu_bytes<T: bytemuck::Pod>(value: &T) -> &[u8] {
	bytemuck::bytes_of(value)
}
