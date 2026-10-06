use raptor_core::Key;
use sdl3_sys::everything as sdl;

use crate::Controls;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WindowEvent {
	Quit,
	Resized,
}

pub fn pump_sdl(controls: &mut Controls) -> Vec<WindowEvent> {
	let mut events = Vec::new();
	let mut event = std::mem::MaybeUninit::<sdl::SDL_Event>::zeroed();

	// SAFETY: the event is written by SDL before it is read, and only the union member that the
	// event type names is accessed.
	unsafe {
		while sdl::SDL_PollEvent(event.as_mut_ptr()) {
			let event = event.assume_init_ref();

			match sdl::SDL_EventType(event.r#type) {
				sdl::SDL_EVENT_QUIT => events.push(WindowEvent::Quit),
				sdl::SDL_EVENT_WINDOW_RESIZED => events.push(WindowEvent::Resized),
				sdl::SDL_EVENT_KEY_DOWN | sdl::SDL_EVENT_KEY_UP => {
					let key = Key::from_scancode(event.key.scancode.0 as u32);

					controls.post_button(key, event.key.down);
				}
				sdl::SDL_EVENT_MOUSE_BUTTON_DOWN | sdl::SDL_EVENT_MOUSE_BUTTON_UP => {
					let key = Key::from_mouse_button(event.button.button);

					controls.post_button(key, event.button.down);
				}
				sdl::SDL_EVENT_MOUSE_MOTION => {
					controls.post_mouse_motion([event.motion.xrel, event.motion.yrel]);
				}
				_ => {}
			}
		}
	}

	events
}
