use std::cell::Cell;
use std::rc::Rc;

use raptor_math::Vec3f;
use wxdragon::prelude::*;

pub const FIELD_WIDTH: i32 = 56;
pub const FLOAT_PRECISION: usize = 3;

fn format_value(value: f32) -> String {
	format!("{value:.FLOAT_PRECISION$}")
}

fn parse_value(text: &str, limits: (f32, f32)) -> f32 {
	text.trim()
		.parse::<f32>()
		.unwrap_or(0.0)
		.clamp(limits.0, limits.1)
}

fn make_text(parent: &dyn WxWidget) -> TextCtrl {
	TextCtrl::builder(parent)
		.with_value("0")
		.with_size(Size::new(FIELD_WIDTH, -1))
		.with_style(TextCtrlStyle::ProcessEnter)
		.build()
}

fn commit_on(text: &TextCtrl, commit: impl Fn() + 'static) {
	let commit = Rc::new(commit);
	let on_enter = commit.clone();

	text.on_text_enter(move |_| on_enter());

	text.on_kill_focus(move |event| {
		commit();
		event.skip(true);
	});
}

pub struct FloatField {
	sizer: BoxSizer,
	text: TextCtrl,
	updating: Rc<Cell<bool>>,
	limits: (f32, f32),
}

impl FloatField {
	pub fn new<P: WxWidget>(
		parent: &P,
		label: &str,
		limits: (f32, f32),
		on_change: impl Fn(f32) + 'static,
	) -> Self {
		let sizer = BoxSizer::builder(Orientation::Horizontal).build();

		let caption = StaticText::builder(parent).with_label(label).build();
		sizer.add(&caption, 0, SizerFlag::AlignCenterVertical, 0);

		let text = make_text(parent);
		sizer.add(
			&text,
			0,
			SizerFlag::AlignCenterVertical | SizerFlag::Left,
			2,
		);

		let updating = Rc::new(Cell::new(false));
		let guard = updating.clone();

		commit_on(&text, move || {
			if !guard.get() {
				on_change(parse_value(&text.get_value(), limits));
			}
		});

		Self {
			sizer,
			text,
			updating,
			limits,
		}
	}

	pub fn sizer(&self) -> &BoxSizer {
		&self.sizer
	}

	pub fn has_focus(&self) -> bool {
		self.text.has_focus()
	}

	pub fn set_value(&self, value: f32) {
		let value = value.clamp(self.limits.0, self.limits.1);

		self.updating.set(true);
		self.text.change_value(&format_value(value));
		self.updating.set(false);
	}
}

pub struct Vec3Field {
	sizer: BoxSizer,
	texts: [TextCtrl; 3],
	updating: Rc<Cell<bool>>,
}

impl Vec3Field {
	pub fn new<P: WxWidget>(
		parent: &P,
		label: &str,
		limits: (f32, f32),
		on_change: impl Fn(Vec3f) + 'static,
	) -> Self {
		let sizer = BoxSizer::builder(Orientation::Horizontal).build();

		let caption = StaticText::builder(parent).with_label(label).build();
		sizer.add(&caption, 0, SizerFlag::AlignCenterVertical, 0);

		let updating = Rc::new(Cell::new(false));
		let texts: [TextCtrl; 3] = std::array::from_fn(|_| make_text(parent));

		for (axis, text) in ["X", "Y", "Z"].iter().zip(&texts) {
			let axis_label = StaticText::builder(parent).with_label(axis).build();

			sizer.add(
				&axis_label,
				0,
				SizerFlag::AlignCenterVertical | SizerFlag::Left,
				6,
			);
			sizer.add(text, 0, SizerFlag::AlignCenterVertical | SizerFlag::Left, 2);
		}

		let on_change = Rc::new(on_change);

		for text in &texts {
			let all = texts.clone();
			let guard = updating.clone();
			let on_change = on_change.clone();

			commit_on(text, move || {
				if guard.get() {
					return;
				}

				let [x, y, z] = std::array::from_fn(|i| parse_value(&all[i].get_value(), limits));

				on_change(Vec3f::new(x, y, z));
			});
		}

		Self {
			sizer,
			texts,
			updating,
		}
	}

	pub fn sizer(&self) -> &BoxSizer {
		&self.sizer
	}

	pub fn has_focus(&self) -> bool {
		self.texts.iter().any(|text| text.has_focus())
	}

	pub fn set_enabled(&self, enabled: bool) {
		for text in &self.texts {
			text.enable(enabled);
		}
	}

	pub fn set_value(&self, value: Vec3f) {
		self.updating.set(true);

		for (text, component) in self.texts.iter().zip([value.x, value.y, value.z]) {
			text.change_value(&format_value(component));
		}

		self.updating.set(false);
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn text_parses_with_limits_and_falls_back_to_zero() {
		assert_eq!(parse_value(" 2.5 ", (0.0, 10.0)), 2.5);
		assert_eq!(parse_value("99", (0.0, 10.0)), 10.0);
		assert_eq!(parse_value("abc", (-1.0, 1.0)), 0.0);
		assert_eq!(format_value(1.0), "1.000");
	}
}
