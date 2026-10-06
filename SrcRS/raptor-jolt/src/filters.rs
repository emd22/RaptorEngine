use std::ffi::c_void;
use std::sync::Once;

use oxijolt_sys::*;
use raptor_physics::{Layer, broad_phase_collides, layers_collide};

pub fn layer_from(raw: u32) -> Option<Layer> {
	match raw {
		0 => Some(Layer::Static),
		1 => Some(Layer::Dynamic),
		2 => Some(Layer::Deactivated),
		_ => None,
	}
}

fn encode(value: usize) -> *mut c_void {
	value as *mut c_void
}

unsafe extern "C" fn object_layer_should_collide(
	user: *mut c_void,
	layer: JPH_ObjectLayer,
) -> bool {
	match (layer_from(user as usize as u32), layer_from(layer)) {
		(Some(own), Some(other)) => layers_collide(own, other),
		_ => false,
	}
}

unsafe extern "C" fn broad_phase_should_collide(
	user: *mut c_void,
	layer: JPH_BroadPhaseLayer,
) -> bool {
	match (
		layer_from(user as usize as u32),
		layer_from(u32::from(layer)),
	) {
		(Some(own), Some(other)) => broad_phase_collides(own, other),
		_ => false,
	}
}

unsafe extern "C" fn body_should_collide(user: *mut c_void, body: JPH_BodyID) -> bool {
	body != user as usize as u32
}

static OBJECT_LAYER_PROCS: JPH_ObjectLayerFilter_Procs = JPH_ObjectLayerFilter_Procs {
	ShouldCollide: Some(object_layer_should_collide),
};

static BROAD_PHASE_PROCS: JPH_BroadPhaseLayerFilter_Procs = JPH_BroadPhaseLayerFilter_Procs {
	ShouldCollide: Some(broad_phase_should_collide),
};

static BODY_PROCS: JPH_BodyFilter_Procs = JPH_BodyFilter_Procs {
	ShouldCollide: Some(body_should_collide),
	ShouldCollideLocked: None,
};

pub fn install_procs() {
	static INSTALLED: Once = Once::new();

	INSTALLED.call_once(|| {
		// SAFETY: the proc tables are statics, so the pointers stay valid, and this runs once
		// before any filter is created.
		unsafe {
			JPH_ObjectLayerFilter_SetProcs(&OBJECT_LAYER_PROCS);
			JPH_BroadPhaseLayerFilter_SetProcs(&BROAD_PHASE_PROCS);
			JPH_BodyFilter_SetProcs(&BODY_PROCS);
		}
	});
}

pub struct LayerFilters {
	object: [*mut JPH_ObjectLayerFilter; 3],
	broad_phase: [*mut JPH_BroadPhaseLayerFilter; 3],
}

impl LayerFilters {
	pub fn new() -> Self {
		install_procs();

		let mut object = [std::ptr::null_mut(); 3];
		let mut broad_phase = [std::ptr::null_mut(); 3];

		for layer in 0..3 {
			// SAFETY: the create functions only store the user data, which is a layer number.
			unsafe {
				object[layer] = JPH_ObjectLayerFilter_Create(encode(layer));
				broad_phase[layer] = JPH_BroadPhaseLayerFilter_Create(encode(layer));
			}
		}

		Self {
			object,
			broad_phase,
		}
	}

	pub fn object(&self, layer: Layer) -> *const JPH_ObjectLayerFilter {
		self.object[layer as usize]
	}

	pub fn broad_phase(&self, layer: Layer) -> *const JPH_BroadPhaseLayerFilter {
		self.broad_phase[layer as usize]
	}
}

impl Drop for LayerFilters {
	fn drop(&mut self) {
		for filter in self.object {
			// SAFETY: the filter came from its create function and is destroyed once.
			unsafe { JPH_ObjectLayerFilter_Destroy(filter) };
		}

		for filter in self.broad_phase {
			// SAFETY: as above.
			unsafe { JPH_BroadPhaseLayerFilter_Destroy(filter) };
		}
	}
}

pub struct BodyFilter(*mut JPH_BodyFilter);

impl BodyFilter {
	pub fn ignoring(body: u32) -> Self {
		install_procs();

		// SAFETY: the create function only stores the user data, which is the body to ignore.
		Self(unsafe { JPH_BodyFilter_Create(encode(body as usize)) })
	}

	pub fn as_ptr(&self) -> *const JPH_BodyFilter {
		self.0
	}
}

impl Drop for BodyFilter {
	fn drop(&mut self) {
		// SAFETY: the filter came from its create function and is destroyed once.
		unsafe { JPH_BodyFilter_Destroy(self.0) };
	}
}
