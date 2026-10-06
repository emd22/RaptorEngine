#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(usize)]
pub enum GpuMarker {
	FrameStart,
	Shadows,
	Prepass,
	LightCulling,
	Ssao,
	Forward,
	ProbeCapture,
	Composition,
}

impl GpuMarker {
	pub const COUNT: usize = 8;
}

const _: () = assert!(GpuMarker::COUNT == raptor_gpu::MARKER_COUNT);
