mod fields;
mod keymap;
mod panels;
pub mod platform;
mod windows;

use std::cell::{Cell, RefCell};
use std::ffi::c_void;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::mpsc::Sender;

use wxdragon::prelude::*;

use crate::editor::Notice;
use crate::host::CvarInfo;
use crate::input::SharedInput;
use crate::key::Key;
use crate::tools::EditorTool;
use crate::ui_model::{ObjectRow, PanelSnapshot, ReloadTarget, UiAction};
use panels::{LightPanelUi, ObjectPanelUi, WorldPanelUi};
use windows::{CvarListWindow, ObjectListWindow};

const MENU_OPEN: i32 = 6001;
const MENU_SAVE: i32 = 6002;
const MENU_SAVE_AS: i32 = 6003;
const MENU_RELOAD_WORLD: i32 = 6004;
const MENU_RELOAD_PROTOTYPE: i32 = 6005;
const MENU_RELOAD_SCRIPTS: i32 = 6006;
const MENU_OBJECT_LIST: i32 = 6007;
const MENU_CVAR_LIST: i32 = 6008;

const DEFAULT_BLOCKOUT_PATH: &str = "RaptorData/Data/blockouts/btemp.prx";

pub struct GuiConfig {
	pub title: String,
	pub viewport_size: (u32, u32),
	pub asset_root: PathBuf,
	pub working_dir: PathBuf,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct GuiRequests {
	pub object_rows: bool,
	pub cvars: bool,
}

struct Shared {
	close_requested: Cell<bool>,
	active: Cell<bool>,
	resize_pending: Cell<bool>,
	relative_mouse: Cell<bool>,
	last_mouse: Cell<Option<(i32, i32)>>,
	show_object_list: Cell<bool>,
	show_cvar_list: Cell<bool>,
}

#[derive(Clone, Copy)]
enum ToolButton {
	Icon(BitmapToggleButton),
	Text(ToggleButton),
}

impl ToolButton {
	fn get_value(&self) -> bool {
		match self {
			ToolButton::Icon(button) => button.get_value(),
			ToolButton::Text(button) => button.get_value(),
		}
	}

	fn set_value(&self, value: bool) {
		match self {
			ToolButton::Icon(button) => button.set_value(value),
			ToolButton::Text(button) => button.set_value(value),
		}
	}
}

pub struct Gui {
	frame: Frame,
	viewport: Panel,
	metal_layer: *mut c_void,
	shared: Rc<Shared>,
	input: SharedInput,
	actions: Sender<UiAction>,
	tool_buttons: Vec<ToolButton>,
	world: WorldPanelUi,
	object: ObjectPanelUi,
	light: LightPanelUi,
	component_panel: Panel,
	object_list: ObjectListWindow,
	cvar_list: CvarListWindow,
	blockout_path: Rc<RefCell<Option<String>>>,
	working_dir: PathBuf,
}

fn load_icon(root: &Path, relative: &str) -> Option<Bitmap> {
	let path = ["", "../"]
		.iter()
		.map(|prefix| root.join(prefix).join(relative))
		.find(|path| path.exists())?;

	let image = image::open(path).ok()?.into_rgba8();

	Bitmap::from_rgba(image.as_raw(), image.width(), image.height())
}

fn to_blockout_path(chosen: &Path, working_dir: &Path) -> String {
	chosen
		.strip_prefix(working_dir)
		.unwrap_or(chosen)
		.to_string_lossy()
		.into_owned()
}

impl Gui {
	fn build(config: &GuiConfig, actions: Sender<UiAction>, input: SharedInput) -> Gui {
		let shared = Rc::new(Shared {
			close_requested: Cell::new(false),
			active: Cell::new(true),
			resize_pending: Cell::new(false),
			relative_mouse: Cell::new(false),
			last_mouse: Cell::new(None),
			show_object_list: Cell::new(false),
			show_cvar_list: Cell::new(false),
		});

		platform::disable_window_tabbing();

		let frame = Frame::builder().with_title(&config.title).build();

		let blockout_path: Rc<RefCell<Option<String>>> = Rc::default();

		Self::build_menus(
			&frame,
			&actions,
			&blockout_path,
			&config.working_dir,
			&shared,
		);

		let root = Panel::builder(&frame).build();
		let root_sizer = BoxSizer::builder(Orientation::Vertical).build();
		let body = BoxSizer::builder(Orientation::Horizontal).build();

		let tool_sizer = BoxSizer::builder(Orientation::Vertical).build();
		let mut tool_buttons = Vec::new();

		for tool in EditorTool::ALL {
			let tx = actions.clone();
			let on_click = move |_| {
				let _ = tx.send(UiAction::SetTool(tool));
			};

			let button = match load_icon(&config.asset_root, tool.icon()) {
				Some(bitmap) => {
					let button = BitmapToggleButton::builder(&root)
						.with_bitmap(Some(bitmap))
						.build();

					button.on_toggle(on_click);

					ToolButton::Icon(button)
				}
				None => {
					let button = ToggleButton::builder(&root).with_label(tool.name()).build();

					button.on_toggle(on_click);

					ToolButton::Text(button)
				}
			};

			match &button {
				ToolButton::Icon(inner) => inner.set_tooltip(tool.name()),
				ToolButton::Text(inner) => inner.set_tooltip(tool.name()),
			}

			match &button {
				ToolButton::Icon(inner) => tool_sizer.add(inner, 0, SizerFlag::All, 2),
				ToolButton::Text(inner) => tool_sizer.add(inner, 0, SizerFlag::All, 2),
			};

			tool_buttons.push(button);
		}

		body.add_sizer(&tool_sizer, 0, SizerFlag::All, 6);

		let (width, height) = config.viewport_size;

		let viewport = Panel::builder(&root)
			.with_size(Size::new(width as i32, height as i32))
			.with_style(PanelStyle::BorderNone)
			.build();

		viewport.set_background_style(BackgroundStyle::Paint);

		let metal_layer = platform::attach_metal_layer(viewport.get_handle());

		body.add(&viewport, 1, SizerFlag::Expand, 0);

		let component_panel = Panel::builder(&root).build();
		let component_sizer = BoxSizer::builder(Orientation::Vertical).build();

		let world = WorldPanelUi::new(&component_panel, &actions);
		component_sizer.add(&world.panel, 0, SizerFlag::Expand, 0);

		let object = ObjectPanelUi::new(&component_panel, &actions);
		component_sizer.add(&object.panel, 0, SizerFlag::Expand, 0);

		let light = LightPanelUi::new(&component_panel, &actions);
		component_sizer.add(&light.panel, 0, SizerFlag::Expand, 0);
		light.panel.show(false);

		component_panel.set_sizer(component_sizer, true);
		body.add(&component_panel, 0, SizerFlag::Expand, 0);

		root_sizer.add_sizer(&body, 1, SizerFlag::Expand, 0);
		root.set_sizer(root_sizer, true);

		let frame_sizer = BoxSizer::builder(Orientation::Vertical).build();
		frame_sizer.add(&root, 1, SizerFlag::Expand, 0);
		frame.set_sizer_and_fit(frame_sizer, true);

		viewport.set_min_size(Size::new(64, 64));

		Self::bind_viewport(&viewport, &shared, &input, &actions);
		Self::bind_frame(&frame, &shared, &input);

		let object_list = ObjectListWindow::new(&frame, &actions);
		let cvar_list = CvarListWindow::new(&frame, &actions);

		frame.show(true);
		frame.raise();
		platform::activate_app();
		viewport.set_focus();

		Gui {
			frame,
			viewport,
			metal_layer,
			shared,
			input,
			actions,
			tool_buttons,
			world,
			object,
			light,
			component_panel,
			object_list,
			cvar_list,
			blockout_path,
			working_dir: config.working_dir.clone(),
		}
	}

	fn build_menus(
		frame: &Frame,
		actions: &Sender<UiAction>,
		path: &Rc<RefCell<Option<String>>>,
		working_dir: &Path,
		shared: &Rc<Shared>,
	) {
		let file = Menu::builder()
			.with_title("File")
			.append_item(
				MENU_OPEN,
				"Open...\tCtrl+O",
				"Opens an existing blockout file",
			)
			.append_separator()
			.append_item(MENU_SAVE, "Save", "Saves the blockout to its current file")
			.append_item(
				MENU_SAVE_AS,
				"Save As...\tCtrl+Shift+S",
				"Saves the blockout under a new name",
			)
			.build();

		let world = Menu::builder()
			.with_title("World")
			.append_item(
				MENU_RELOAD_WORLD,
				"Reload World",
				"Reloads the world from disk",
			)
			.append_item(
				MENU_RELOAD_PROTOTYPE,
				"Reload Prototype",
				"Reloads prototype geometry from disk",
			)
			.append_item(
				MENU_RELOAD_SCRIPTS,
				"Reload Scripts",
				"Reloads all loaded scripts",
			)
			.build();

		let tools = Menu::builder()
			.with_title("Tools")
			.append_item(
				MENU_OBJECT_LIST,
				"Open Object List",
				"View all objects currently in the world",
			)
			.append_item(
				MENU_CVAR_LIST,
				"Open CVar List",
				"View and edit all registered console variables",
			)
			.build();

		let bar = MenuBar::builder()
			.append(file, "&File")
			.append(world, "&World")
			.append(tools, "&Tools")
			.build();

		frame.set_menu_bar(bar);

		let tx = actions.clone();
		let path = path.clone();
		let working_dir = working_dir.to_path_buf();
		let parent = *frame;
		let shared = shared.clone();

		frame.on_menu(move |event| {
			let send = |action: UiAction| {
				let _ = tx.send(action);
			};

			match event.get_id() {
				MENU_OPEN => {
					let confirm = MessageDialog::builder(
						&parent,
						"Open another blockout? Anything you haven't saved will be lost.",
						"Open blockout",
					)
					.with_style(MessageDialogStyle::YesNo | MessageDialogStyle::IconQuestion)
					.build();

					if confirm.show_modal() != ID_YES {
						return;
					}

					if let Some(chosen) = Self::choose_file(&parent, &path, &working_dir, false) {
						send(UiAction::OpenBlockout(chosen));
					}
				}
				MENU_SAVE => {
					if path.borrow().is_some() {
						send(UiAction::SaveBlockout);
					} else if let Some(chosen) =
						Self::choose_file(&parent, &path, &working_dir, true)
					{
						send(UiAction::SaveBlockoutAs(chosen));
					}
				}
				MENU_SAVE_AS => {
					if let Some(chosen) = Self::choose_file(&parent, &path, &working_dir, true) {
						send(UiAction::SaveBlockoutAs(chosen));
					}
				}
				MENU_RELOAD_WORLD => send(UiAction::Reload(ReloadTarget::World)),
				MENU_RELOAD_PROTOTYPE => send(UiAction::Reload(ReloadTarget::Prototype)),
				MENU_RELOAD_SCRIPTS => send(UiAction::Reload(ReloadTarget::Scripts)),
				MENU_OBJECT_LIST => shared.show_object_list.set(true),
				MENU_CVAR_LIST => shared.show_cvar_list.set(true),
				_ => {}
			}
		});
	}

	fn choose_file(
		parent: &Frame,
		current: &Rc<RefCell<Option<String>>>,
		working_dir: &Path,
		save: bool,
	) -> Option<String> {
		let current = current
			.borrow()
			.clone()
			.unwrap_or_else(|| DEFAULT_BLOCKOUT_PATH.to_owned());

		let current = working_dir.join(current);

		let directory = current
			.parent()
			.map(|dir| dir.to_string_lossy().into_owned())
			.unwrap_or_default();
		let file = current
			.file_name()
			.map(|name| name.to_string_lossy().into_owned())
			.unwrap_or_default();

		let style = if save {
			FileDialogStyle::Save | FileDialogStyle::OverwritePrompt
		} else {
			FileDialogStyle::Open | FileDialogStyle::FileMustExist
		};

		let dialog = FileDialog::builder(parent)
			.with_message(if save {
				"Save blockout as"
			} else {
				"Open blockout"
			})
			.with_default_dir(&directory)
			.with_default_file(&file)
			.with_wildcard("Blockout files (*.prx)|*.prx")
			.with_style(style)
			.build();

		if dialog.show_modal() != ID_OK {
			return None;
		}

		let mut chosen = PathBuf::from(dialog.get_path()?);

		if save && chosen.extension().is_none() {
			chosen.set_extension("prx");
		}

		Some(to_blockout_path(&chosen, working_dir))
	}

	fn bind_viewport(
		viewport: &Panel,
		shared: &Rc<Shared>,
		input: &SharedInput,
		actions: &Sender<UiAction>,
	) {
		viewport.on_paint(move |_| {});

		for is_down in [true, false] {
			let input = input.clone();

			let handler = move |event: WindowEventData| {
				let WindowEventData::Keyboard(keyboard) = &event else {
					return;
				};

				let key = keyboard
					.get_key_code()
					.map_or(Key::Unknown, keymap::convert_key);

				if key == Key::Unknown {
					event.skip(true);
					return;
				}

				let mut state = input.lock();

				state.post_button(key, is_down);

				if !is_down && matches!(key, Key::Lmeta | Key::Rmeta) {
					state.release_non_modifiers();
				}
			};

			if is_down {
				viewport.on_key_down(handler);
			} else {
				viewport.on_key_up(handler);
			}
		}

		let bind_button = |key: Key, is_down: bool| {
			let input = input.clone();
			let focus = *viewport;

			move |_: WindowEventData| {
				if is_down {
					focus.set_focus();
				}

				input.lock().post_button(key, is_down);
			}
		};

		viewport.on_mouse_left_down(bind_button(Key::MouseLeft, true));
		viewport.on_mouse_left_up(bind_button(Key::MouseLeft, false));
		viewport.on_mouse_middle_down(bind_button(Key::MouseMiddle, true));
		viewport.on_mouse_middle_up(bind_button(Key::MouseMiddle, false));
		viewport.on_mouse_right_down(bind_button(Key::MouseRight, true));
		viewport.on_mouse_right_up(bind_button(Key::MouseRight, false));

		let motion_shared = shared.clone();
		let motion_input = input.clone();

		viewport.on_mouse_motion(move |event| {
			let WindowEventData::MouseMotion(motion) = &event else {
				return;
			};

			let Some(position) = motion.get_position() else {
				return;
			};

			if !motion_shared.relative_mouse.get()
				&& let Some((last_x, last_y)) = motion_shared.last_mouse.get()
			{
				motion_input
					.lock()
					.post_motion((position.x - last_x) as f32, (position.y - last_y) as f32);
			}

			motion_shared.last_mouse.set(Some((position.x, position.y)));
		});

		let size_shared = shared.clone();

		viewport.on_size(move |event| {
			size_shared.resize_pending.set(true);
			event.skip(true);
		});

		let focus_input = input.clone();
		let focus_actions = actions.clone();

		viewport.on_kill_focus(move |event| {
			focus_input.lock().release_all();
			let _ = focus_actions.send(UiAction::ViewportFocusLost);
			event.skip(true);
		});
	}

	fn bind_frame(frame: &Frame, shared: &Rc<Shared>, input: &SharedInput) {
		let close_shared = shared.clone();

		frame.on_close(move |_| close_shared.close_requested.set(true));

		let activate_shared = shared.clone();
		let activate_input = input.clone();

		frame.on_activate(move |event| {
			if let WindowEventData::Activate(activate) = &event {
				let active = activate.is_active();

				activate_shared.active.set(active);

				if !active {
					activate_input.lock().release_all();
				}
			}

			event.skip(true);
		});
	}

	pub fn native_view(&self) -> *mut c_void {
		self.viewport.get_handle()
	}

	pub fn metal_layer(&self) -> *mut c_void {
		self.metal_layer
	}

	pub fn viewport_size(&self) -> (u32, u32) {
		let size = self.viewport.get_client_size();

		(size.width.max(1) as u32, size.height.max(1) as u32)
	}

	pub fn consume_resize(&self) -> bool {
		self.shared.resize_pending.replace(false)
	}

	pub fn close_requested(&self) -> bool {
		self.shared.close_requested.get()
	}

	pub fn is_active(&self) -> bool {
		self.shared.active.get() && platform::is_app_active() && !self.frame.is_iconized()
	}

	pub fn set_title(&self, title: &str) {
		self.frame.set_title(title);
	}

	pub fn set_relative_mouse(&self, enabled: bool) {
		if self.shared.relative_mouse.replace(enabled) == enabled {
			return;
		}

		platform::set_relative_mouse(self.viewport.get_handle(), enabled);
		self.shared.last_mouse.set(None);
	}

	pub fn poll_mouse(&self) {
		if !self.shared.relative_mouse.get() {
			return;
		}

		let (dx, dy) = platform::consume_mouse_delta();

		if dx != 0.0 || dy != 0.0 {
			self.input.lock().post_motion(dx, dy);
		}
	}

	pub fn mouse_position(&self) -> (f32, f32) {
		let position = self.viewport.screen_to_client(get_mouse_position());

		(position.x as f32, position.y as f32)
	}

	pub fn update(&self, snapshot: &PanelSnapshot, player: raptor_math::Vec3f) {
		*self.blockout_path.borrow_mut() = snapshot.blockout_path.clone();

		for (index, button) in self.tool_buttons.iter().enumerate() {
			let selected = index == snapshot.tool.index();

			if button.get_value() != selected {
				button.set_value(selected);
			}
		}

		self.world.update(&snapshot.world, player);
		self.object
			.update(&snapshot.materials, snapshot.object.as_ref());

		let show_light = snapshot.tool == EditorTool::Light;

		if self.light.panel.is_shown() != show_light {
			self.light.panel.show(show_light);
			self.relayout();
		}

		if show_light {
			self.light.update(snapshot.light.as_ref());
		}

		if self.world.take_layout_dirty() {
			self.relayout();
		}
	}

	fn relayout(&self) {
		self.component_panel.layout();

		if let Some(parent) = self.component_panel.get_parent() {
			parent.layout();
		}
	}

	pub fn take_requests(&self) -> GuiRequests {
		if self.shared.show_object_list.replace(false) {
			self.object_list.show();
		}

		if self.shared.show_cvar_list.replace(false) {
			self.cvar_list.show();
		}

		GuiRequests {
			object_rows: self.object_list.wants_rows(),
			cvars: self.cvar_list.wants_cvars(),
		}
	}

	pub fn set_object_rows(&self, rows: &[ObjectRow]) {
		self.object_list.set_rows(rows);
	}

	pub fn set_cvars(&self, cvars: &[CvarInfo]) {
		self.cvar_list.set_cvars(cvars);
	}

	pub fn show_notices(&self, notices: Vec<Notice>) {
		for notice in notices {
			let (message, style, caption) = match &notice {
				Notice::Error(message) => (
					message.as_str(),
					MessageDialogStyle::OK | MessageDialogStyle::IconError,
					"Error",
				),
				Notice::Info(message) => (
					message.as_str(),
					MessageDialogStyle::OK | MessageDialogStyle::IconInformation,
					"Info",
				),
			};

			if matches!(notice, Notice::Error(_)) {
				self.cvar_list.set_status(message);
			}

			MessageDialog::builder(&self.frame, message, caption)
				.with_style(style)
				.build()
				.show_modal();
		}
	}

	pub fn actions(&self) -> Sender<UiAction> {
		self.actions.clone()
	}

	pub fn working_dir(&self) -> &Path {
		&self.working_dir
	}
}

pub type FrameFn = Box<dyn FnMut() -> bool>;

pub fn run<F>(
	config: GuiConfig,
	actions: Sender<UiAction>,
	input: SharedInput,
	init: F,
) -> Result<(), String>
where
	F: FnOnce(Gui) -> FrameFn + 'static,
{
	wxdragon::main(move |app| {
		let _ = set_appearance(Appearance::Dark);

		let gui = Gui::build(&config, actions, input);
		let frame = gui.frame;

		let frame_fn: Rc<RefCell<Option<FrameFn>>> = Rc::new(RefCell::new(Some(init(gui))));

		let timer = Timer::new(&frame);
		let tick_fn = frame_fn.clone();

		timer.on_tick(move |_| {
			let keep_going = match tick_fn.borrow_mut().as_mut() {
				Some(tick) => tick(),
				None => false,
			};

			if !keep_going {
				tick_fn.borrow_mut().take();
				app.exit_main_loop();
			}
		});

		timer.start(1, false);

		std::mem::forget(timer);
	})
	.map_err(|error| error.to_string())
}
