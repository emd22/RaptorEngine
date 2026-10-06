use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::time::{Duration, Instant};

use wxdragon::prelude::*;

use super::panels::Actions;
use crate::host::ObjectId;
use crate::host::{
	CvarInfo, TAG_BLOCKOUT, TAG_LOCK_TRANSFORM, TAG_PROBE_VOLUME, TAG_REFLECTION_PROBE,
};
use crate::ui_model::{ObjectRow, UiAction};

const REFRESH_INTERVAL: Duration = Duration::from_millis(500);

const TAG_LABELS: [(u32, &str); 4] = [
	(TAG_BLOCKOUT, "Blockout"),
	(TAG_LOCK_TRANSFORM, "Lock Transform"),
	(TAG_PROBE_VOLUME, "Probe Volume"),
	(TAG_REFLECTION_PROBE, "Reflection Probe"),
];

fn tag_list(tags: u32) -> String {
	TAG_LABELS
		.iter()
		.filter(|(bit, _)| tags & bit != 0)
		.map(|(_, name)| *name)
		.collect::<Vec<_>>()
		.join(", ")
}

pub struct ObjectListWindow {
	pub frame: Frame,
	list: ListCtrl,
	ids: Rc<RefCell<Vec<ObjectId>>>,
	last_request: Cell<Option<Instant>>,
	refresh_wanted: Rc<Cell<bool>>,
}

impl ObjectListWindow {
	pub fn new(parent: &Frame, actions: &Actions) -> Self {
		let frame = Frame::builder()
			.with_parent(parent)
			.with_title("Object List")
			.with_size(Size::new(480, 400))
			.build();

		let root = Panel::builder(&frame).build();
		let sizer = BoxSizer::builder(Orientation::Vertical).build();

		let list = ListCtrl::builder(&root)
			.with_style(ListCtrlStyle::Report | ListCtrlStyle::SingleSel)
			.build();

		for (index, (heading, width)) in
			[("Name", 160), ("ID", 80), ("Tags", 140), ("Position", 160)]
				.into_iter()
				.enumerate()
		{
			list.insert_column(index as i64, heading, ListColumnFormat::Left, width);
		}

		sizer.add(&list, 1, SizerFlag::Expand | SizerFlag::All, 6);

		let refresh = Button::builder(&root).with_label("Refresh").build();
		sizer.add(
			&refresh,
			0,
			SizerFlag::AlignRight | SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		root.set_sizer(sizer, true);

		let frame_sizer = BoxSizer::builder(Orientation::Vertical).build();
		frame_sizer.add(&root, 1, SizerFlag::Expand, 0);
		frame.set_sizer(frame_sizer, true);

		let ids: Rc<RefCell<Vec<ObjectId>>> = Rc::default();
		let refresh_wanted = Rc::new(Cell::new(true));

		let wanted = refresh_wanted.clone();
		refresh.on_click(move |_| wanted.set(true));

		let tx = actions.clone();
		let row_ids = ids.clone();

		list.on_item_activated(move |event| {
			let row = usize::try_from(event.get_item_index()).ok();

			if let Some(id) = row.and_then(|row| row_ids.borrow().get(row).copied()) {
				let _ = tx.send(UiAction::SelectObject(id));
			}
		});

		let hide = frame;
		frame.on_close(move |_| hide.show(false));

		Self {
			frame,
			list,
			ids,
			last_request: Cell::new(None),
			refresh_wanted,
		}
	}

	pub fn show(&self) {
		self.refresh_wanted.set(true);
		self.frame.show(true);
		self.frame.raise();
	}

	pub fn wants_rows(&self) -> bool {
		self.frame.is_shown() && self.refresh_wanted.replace(false)
	}

	pub fn set_rows(&self, rows: &[ObjectRow]) {
		self.list.freeze();
		self.list.delete_all_items();

		let mut ids = self.ids.borrow_mut();
		ids.clear();

		for (index, row) in rows.iter().enumerate() {
			let name = if row.name.is_empty() {
				"(unnamed)"
			} else {
				&row.name
			};
			let at = index as i64;

			self.list.insert_item(at, name, None);
			self.list
				.set_item_text_by_column(at, 1, &row.id.0.to_string());
			self.list
				.set_item_text_by_column(at, 2, &tag_list(row.tags));
			self.list.set_item_text_by_column(
				at,
				3,
				&format!(
					"{:.2}, {:.2}, {:.2}",
					row.position.x, row.position.y, row.position.z
				),
			);

			ids.push(row.id);
		}

		self.list.thaw();
		self.frame
			.set_title(&format!("Object List ({})", rows.len()));
		self.last_request.set(Some(Instant::now()));
	}
}

pub struct CvarListWindow {
	pub frame: Frame,
	filter: TextCtrl,
	list: ListCtrl,
	selected_label: StaticText,
	value: TextCtrl,
	set_button: Button,
	status: StaticText,
	shown: Rc<RefCell<Vec<CvarInfo>>>,
	last_request: Cell<Instant>,
}

impl CvarListWindow {
	pub fn new(parent: &Frame, actions: &Actions) -> Self {
		let frame = Frame::builder()
			.with_parent(parent)
			.with_title("CVar List")
			.with_size(Size::new(520, 480))
			.build();

		let root = Panel::builder(&frame).build();
		let sizer = BoxSizer::builder(Orientation::Vertical).build();

		let filter = TextCtrl::builder(&root).build();
		sizer.add(&filter, 0, SizerFlag::Expand | SizerFlag::All, 6);

		let list = ListCtrl::builder(&root)
			.with_style(ListCtrlStyle::Report | ListCtrlStyle::SingleSel)
			.build();

		for (index, (heading, width)) in [("Name", 200), ("Type", 70), ("Value", 200)]
			.into_iter()
			.enumerate()
		{
			list.insert_column(index as i64, heading, ListColumnFormat::Left, width);
		}

		sizer.add(
			&list,
			1,
			SizerFlag::Expand | SizerFlag::Left | SizerFlag::Right,
			6,
		);

		let edit = BoxSizer::builder(Orientation::Horizontal).build();

		let selected_label = StaticText::builder(&root)
			.with_label("(select a CVar)")
			.build();
		edit.add(
			&selected_label,
			0,
			SizerFlag::AlignCenterVertical | SizerFlag::Right,
			6,
		);

		let value = TextCtrl::builder(&root)
			.with_style(TextCtrlStyle::ProcessEnter)
			.build();
		value.enable(false);
		edit.add(
			&value,
			1,
			SizerFlag::AlignCenterVertical | SizerFlag::Right,
			6,
		);

		let set_button = Button::builder(&root).with_label("Set").build();
		set_button.enable(false);
		edit.add(&set_button, 0, SizerFlag::AlignCenterVertical, 0);

		sizer.add_sizer(&edit, 0, SizerFlag::Expand | SizerFlag::All, 6);

		let status = StaticText::builder(&root).build();
		sizer.add(
			&status,
			0,
			SizerFlag::Expand | SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		root.set_sizer(sizer, true);

		let frame_sizer = BoxSizer::builder(Orientation::Vertical).build();
		frame_sizer.add(&root, 1, SizerFlag::Expand, 0);
		frame.set_sizer(frame_sizer, true);

		let this = Self {
			frame,
			filter,
			list,
			selected_label,
			value,
			set_button,
			status,
			shown: Rc::default(),
			last_request: Cell::new(Instant::now() - REFRESH_INTERVAL),
		};

		this.bind(actions);

		this
	}

	fn selected(list: &ListCtrl, shown: &RefCell<Vec<CvarInfo>>) -> Option<CvarInfo> {
		let row = usize::try_from(list.get_first_selected_item()).ok()?;

		shown.borrow().get(row).cloned()
	}

	fn bind(&self, actions: &Actions) {
		let apply = {
			let list = self.list;
			let shown = self.shown.clone();
			let value = self.value;
			let tx = actions.clone();

			move || {
				if let Some(cvar) = Self::selected(&list, &shown) {
					let _ = tx.send(UiAction::SetCvarFromStr(cvar.name, value.get_value()));
				}
			}
		};

		let on_enter = apply.clone();
		self.value.on_text_enter(move |_| on_enter());
		self.set_button.on_click(move |_| apply());

		let show_selected = {
			let list = self.list;
			let shown = self.shown.clone();
			let label = self.selected_label;
			let value = self.value;
			let set_button = self.set_button;
			let status = self.status;

			move || {
				let cvar = Self::selected(&list, &shown);

				value.enable(cvar.is_some());
				set_button.enable(cvar.is_some());

				match cvar {
					Some(cvar) => {
						label.set_label(&cvar.name);
						value.change_value(&cvar.value);
					}
					None => {
						label.set_label("(select a CVar)");
						value.clear();
					}
				}

				status.set_label("");
			}
		};

		let on_select = show_selected.clone();
		self.list.on_item_selected(move |_| on_select());

		let hide = self.frame;
		self.frame.on_close(move |_| hide.show(false));
	}

	pub fn show(&self) {
		self.last_request.set(Instant::now() - REFRESH_INTERVAL);
		self.frame.show(true);
		self.frame.raise();
	}

	pub fn wants_cvars(&self) -> bool {
		if !self.frame.is_shown() || self.last_request.get().elapsed() < REFRESH_INTERVAL {
			return false;
		}

		self.last_request.set(Instant::now());

		true
	}

	pub fn set_status(&self, text: &str) {
		self.status.set_label(text);
	}

	pub fn set_cvars(&self, cvars: &[CvarInfo]) {
		let filter = self.filter.get_value().to_lowercase();

		let visible: Vec<CvarInfo> = cvars
			.iter()
			.filter(|cvar| filter.is_empty() || cvar.name.to_lowercase().contains(&filter))
			.cloned()
			.collect();

		let same_rows = {
			let shown = self.shown.borrow();

			shown.len() == visible.len()
				&& shown.iter().zip(&visible).all(|(a, b)| a.name == b.name)
		};

		if same_rows {
			for (row, cvar) in visible.iter().enumerate() {
				if self.list.get_item_text(row as i64, 2) != cvar.value {
					self.list
						.set_item_text_by_column(row as i64, 2, &cvar.value);
				}
			}

			*self.shown.borrow_mut() = visible;
		} else {
			let previous = Self::selected(&self.list, &self.shown).map(|cvar| cvar.name);

			self.list.freeze();
			self.list.delete_all_items();

			for (row, cvar) in visible.iter().enumerate() {
				let at = row as i64;

				self.list.insert_item(at, &cvar.name, None);
				self.list.set_item_text_by_column(at, 1, cvar.type_name);
				self.list.set_item_text_by_column(at, 2, &cvar.value);

				if previous.as_deref() == Some(cvar.name.as_str()) {
					self.list
						.set_item_state(at, ListItemState::Selected, ListItemState::Selected);
				}
			}

			self.list.thaw();

			*self.shown.borrow_mut() = visible;
		}

		self.frame
			.set_title(&format!("CVar List ({})", self.shown.borrow().len()));
	}
}
