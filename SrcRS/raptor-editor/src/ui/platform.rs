#[cfg(target_os = "macos")]
mod mac {
	use std::ffi::c_void;

	use objc2::rc::Retained;
	use objc2::{MainThreadMarker, msg_send};
	use objc2_app_kit::{NSApplication, NSCursor, NSView, NSWindow};
	use objc2_quartz_core::CAMetalLayer;

	#[link(name = "CoreGraphics", kind = "framework")]
	unsafe extern "C" {
		fn CGAssociateMouseAndMouseCursorPosition(connected: bool) -> i32;
		fn CGGetLastMouseDelta(dx: *mut i32, dy: *mut i32);
	}

	pub fn attach_metal_layer(ns_view: *mut c_void) -> *mut c_void {
		if ns_view.is_null() {
			return std::ptr::null_mut();
		}

		// SAFETY: wx hands out a live NSView that stays alive as long as the viewport window does.
		let view: &NSView = unsafe { &*ns_view.cast::<NSView>() };

		let layer: Retained<CAMetalLayer> = CAMetalLayer::layer();

		layer.setContentsScale(1.0);

		// SAFETY: the view is on the main thread and retains the layer once it is set.
		unsafe {
			let () = msg_send![view, setLayer: &*layer];
		}

		view.setWantsLayer(true);

		Retained::as_ptr(&layer).cast_mut().cast::<c_void>()
	}

	pub fn set_relative_mouse(ns_view: *mut c_void, enabled: bool) {
		if enabled {
			if !ns_view.is_null() {
				// SAFETY: see `attach_metal_layer`.
				let view: &NSView = unsafe { &*ns_view.cast::<NSView>() };

				if let Some(window) = view.window() {
					window.setAcceptsMouseMovedEvents(true);
				}
			}

			let (mut dx, mut dy) = (0, 0);

			// SAFETY: plain CoreGraphics calls with valid pointers.
			unsafe {
				CGAssociateMouseAndMouseCursorPosition(false);
				CGGetLastMouseDelta(&mut dx, &mut dy);
			}

			NSCursor::hide();
		} else {
			// SAFETY: plain CoreGraphics call.
			unsafe {
				CGAssociateMouseAndMouseCursorPosition(true);
			}

			NSCursor::unhide();
		}
	}

	pub fn consume_mouse_delta() -> (f32, f32) {
		let (mut dx, mut dy) = (0, 0);

		// SAFETY: both pointers are valid for the call.
		unsafe { CGGetLastMouseDelta(&mut dx, &mut dy) };

		(dx as f32, dy as f32)
	}

	pub fn activate_app() {
		if let Some(mtm) = MainThreadMarker::new() {
			#[allow(deprecated)]
			NSApplication::sharedApplication(mtm).activateIgnoringOtherApps(true);
		}
	}

	pub fn disable_window_tabbing() {
		if let Some(mtm) = MainThreadMarker::new() {
			NSWindow::setAllowsAutomaticWindowTabbing(false, mtm);
		}
	}

	pub fn is_app_active() -> bool {
		MainThreadMarker::new().is_none_or(|mtm| NSApplication::sharedApplication(mtm).isActive())
	}
}

#[cfg(target_os = "macos")]
pub use mac::*;

#[cfg(not(target_os = "macos"))]
use std::ffi::c_void;

#[cfg(not(target_os = "macos"))]
pub fn attach_metal_layer(_ns_view: *mut c_void) -> *mut c_void {
	std::ptr::null_mut()
}

#[cfg(not(target_os = "macos"))]
pub fn set_relative_mouse(_ns_view: *mut c_void, _enabled: bool) {}

#[cfg(not(target_os = "macos"))]
pub fn consume_mouse_delta() -> (f32, f32) {
	(0.0, 0.0)
}

#[cfg(not(target_os = "macos"))]
pub fn activate_app() {}

#[cfg(not(target_os = "macos"))]
pub fn disable_window_tabbing() {}

#[cfg(not(target_os = "macos"))]
pub fn is_app_active() -> bool {
	true
}
