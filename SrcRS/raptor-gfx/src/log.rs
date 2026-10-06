use raptor_gpu::{Level, Log};

pub struct GfxLog;

impl Log for GfxLog {
	fn log(&self, level: Level, message: &str) {
		match level {
			Level::Debug => raptor_core::log_debug!(Render; "{message}"),
			Level::Info => raptor_core::log_info!(Render; "{message}"),
			Level::Warning => raptor_core::log_warn!(Render; "{message}"),
			Level::Error => raptor_core::log_error!(Render; "{message}"),
		}
	}
}
