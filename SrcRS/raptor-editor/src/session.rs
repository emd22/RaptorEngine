use crate::editor::Editor;
use crate::host::{EditorHost, InputHost};
use crate::input::SharedInput;
use crate::key::Key;
use crate::ui::Gui;
use crate::ui_model::ReloadTarget;

impl InputHost for SharedInput {
	fn key_down(&self, key: Key) -> bool {
		self.lock().key_down(key)
	}

	fn key_pressed(&self, key: Key) -> bool {
		self.lock().key_pressed(key)
	}
}

pub struct EditorSession {
	pub editor: Editor,
	pub gui: Gui,
	pub input: SharedInput,
}

impl EditorSession {
	pub fn new(editor: Editor, gui: Gui, input: SharedInput) -> Self {
		Self { editor, gui, input }
	}

	pub fn frame(&mut self, host: &mut dyn EditorHost, delta_time: f32) -> Vec<ReloadTarget> {
		self.gui.poll_mouse();

		self.editor.update(host, delta_time);

		let snapshot = self.editor.snapshot(host);
		self.gui.update(&snapshot, host.player_position());

		let requests = self.gui.take_requests();

		if requests.object_rows {
			self.gui.set_object_rows(&self.editor.object_rows(host));
		}

		if requests.cvars {
			self.gui.set_cvars(&host.cvars());
		}

		let notices = self.editor.take_notices();

		if !notices.is_empty() {
			self.gui.show_notices(notices);
		}

		self.editor.take_reload_requests()
	}

	pub fn end_frame(&self) {
		self.input.lock().end_frame();
	}

	pub fn should_exit(&self) -> bool {
		self.gui.close_requested()
	}
}
