use std::sync::atomic::{AtomicBool, AtomicU32, Ordering};

pub struct RenderState {
	pre_exposure: AtomicU32,
	delta_time: AtomicU32,
	tonemapper: AtomicU32,
	reflection_debug_view: AtomicU32,
	disable_probes: AtomicBool,
	disable_reflection_probes: AtomicBool,
	disable_decals: AtomicBool,
	only_render_probes: AtomicBool,
	render_probe_visibility: AtomicBool,
}

impl Default for RenderState {
	fn default() -> Self {
		Self {
			pre_exposure: AtomicU32::new(1.0f32.to_bits()),
			delta_time: AtomicU32::new((1.0f32 / 60.0).to_bits()),
			tonemapper: AtomicU32::new(1),
			reflection_debug_view: AtomicU32::new(0),
			disable_probes: AtomicBool::new(false),
			disable_reflection_probes: AtomicBool::new(false),
			disable_decals: AtomicBool::new(false),
			only_render_probes: AtomicBool::new(false),
			render_probe_visibility: AtomicBool::new(false),
		}
	}
}

macro_rules! flag {
	($get:ident, $set:ident, $field:ident) => {
		pub fn $get(&self) -> bool {
			self.$field.load(Ordering::Relaxed)
		}

		pub fn $set(&self, value: bool) {
			self.$field.store(value, Ordering::Relaxed);
		}
	};
}

impl RenderState {
	flag!(disable_probes, set_disable_probes, disable_probes);
	flag!(
		disable_reflection_probes,
		set_disable_reflection_probes,
		disable_reflection_probes
	);
	flag!(disable_decals, set_disable_decals, disable_decals);
	flag!(
		only_render_probes,
		set_only_render_probes,
		only_render_probes
	);
	flag!(
		render_probe_visibility,
		set_render_probe_visibility,
		render_probe_visibility
	);

	pub fn pre_exposure(&self) -> f32 {
		f32::from_bits(self.pre_exposure.load(Ordering::Relaxed))
	}

	pub fn set_pre_exposure(&self, value: f32) {
		self.pre_exposure.store(value.to_bits(), Ordering::Relaxed);
	}

	pub fn delta_time(&self) -> f32 {
		f32::from_bits(self.delta_time.load(Ordering::Relaxed))
	}

	pub fn set_delta_time(&self, value: f32) {
		self.delta_time.store(value.to_bits(), Ordering::Relaxed);
	}

	pub fn tonemapper(&self) -> u32 {
		self.tonemapper.load(Ordering::Relaxed)
	}

	pub fn set_tonemapper(&self, value: u32) {
		self.tonemapper.store(value, Ordering::Relaxed);
	}

	pub fn reflection_debug_view(&self) -> u32 {
		self.reflection_debug_view.load(Ordering::Relaxed)
	}

	pub fn set_reflection_debug_view(&self, value: u32) {
		self.reflection_debug_view.store(value, Ordering::Relaxed);
	}

	pub fn has_debug_view(&self) -> bool {
		self.only_render_probes()
			|| self.render_probe_visibility()
			|| self.reflection_debug_view() != 0
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn defaults_match_the_old_backend_members() {
		let state = RenderState::default();

		assert_eq!(state.pre_exposure(), 1.0);
		assert_eq!(state.tonemapper(), 1);
		assert!((state.delta_time() - 1.0 / 60.0).abs() < 1e-6);
		assert!(!state.has_debug_view());
	}

	#[test]
	fn any_debug_view_turns_the_debug_flag_on() {
		let state = RenderState::default();

		state.set_reflection_debug_view(2);
		assert!(state.has_debug_view());

		state.set_reflection_debug_view(0);
		state.set_only_render_probes(true);
		assert!(state.has_debug_view());
	}
}
