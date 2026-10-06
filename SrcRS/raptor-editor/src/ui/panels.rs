use std::cell::{Cell, RefCell};
use std::rc::Rc;
use std::sync::mpsc::Sender;

use raptor_math::Vec3f;
use wxdragon::prelude::*;

use super::fields::{FloatField, Vec3Field};
use crate::host::{
	DEBUG_BOUNDS_LIGHTS, DEBUG_BOUNDS_OBJECTS, DEBUG_BOUNDS_PHYSICS, FLAG_DISABLE_CULLING,
	FLAG_IS_INSTANCE, FLAG_NOT_PROBE_VISIBLE, FLAG_PHYSICS_ENABLED, FLAG_READY_TO_RENDER,
	FLAG_SHADOW_CASTER, FLAG_UNLIT, TAG_BLEEDS, TAG_BLOCKOUT, TAG_LOCK_TRANSFORM, TAG_PROBE_VOLUME,
	TAG_REFLECTION_PROBE,
};
use crate::ui_model::{LightPanel, ObjectPanel, UiAction, WorldPanel};

pub type Actions = Sender<UiAction>;

const DEBUG_LAYERS: [(&str, u32); 5] = [
	("None", 0),
	("Object Bounds", DEBUG_BOUNDS_OBJECTS),
	("Light Bounds", DEBUG_BOUNDS_LIGHTS),
	("Physics Bounds", DEBUG_BOUNDS_PHYSICS),
	(
		"All Bounds",
		DEBUG_BOUNDS_OBJECTS | DEBUG_BOUNDS_LIGHTS | DEBUG_BOUNDS_PHYSICS,
	),
];

const REFLECTION_VIEWS: [&str; 3] = ["Off", "Mirror Reflections", "Probe Coverage"];

const TAG_NAMES: [(u32, &str, Option<&str>); 5] = [
	(
		TAG_BLOCKOUT,
		"Blockout",
		Some("Marks level geometry. Set when the brush is created"),
	),
	(TAG_LOCK_TRANSFORM, "Lock Transform", None),
	(
		TAG_PROBE_VOLUME,
		"Probe Volume",
		Some("Blockout brushes only"),
	),
	(
		TAG_REFLECTION_PROBE,
		"Reflection Probe",
		Some("Blockout brushes only"),
	),
	(TAG_BLEEDS, "Bleeds", None),
];

const FLAG_NAMES: [(u32, &str, Option<&str>); 7] = [
	(
		FLAG_READY_TO_RENDER,
		"Ready To Render",
		Some("Set by the renderer once the mesh and material have loaded"),
	),
	(
		FLAG_PHYSICS_ENABLED,
		"Physics Enabled",
		Some("Dynamic physics bodies only"),
	),
	(
		FLAG_IS_INSTANCE,
		"Is Instance",
		Some("Set for instances of another object"),
	),
	(
		FLAG_SHADOW_CASTER,
		"Shadow Caster",
		Some("Probe volumes never cast shadows"),
	),
	(
		FLAG_UNLIT,
		"Unlit",
		Some("Applied to the shared material when the object loads, so it can't be changed here"),
	),
	(FLAG_DISABLE_CULLING, "Disable Culling", None),
	(
		FLAG_NOT_PROBE_VISIBLE,
		"Not Probe Visible",
		Some("Probe volumes are never visible to probes"),
	),
];

fn title<P: WxWidget>(parent: &P, text: &str) -> StaticText {
	let label = StaticText::builder(parent).with_label(text).build();

	if let Some(font) = label.get_font() {
		let mut bold = font.to_owned();
		bold.make_bold();
		label.set_font(&bold);
	}

	label
}

fn row_with_choice<P: WxWidget>(parent: &P, caption: &str, items: &[&str]) -> (BoxSizer, Choice) {
	let row = BoxSizer::builder(Orientation::Horizontal).build();
	let label = StaticText::builder(parent).with_label(caption).build();

	row.add(&label, 0, SizerFlag::AlignCenterVertical, 0);

	let choice = Choice::builder(parent).build();

	for item in items {
		choice.append(item);
	}

	row.add(&choice, 0, SizerFlag::Left, 6);

	(row, choice)
}

pub struct WorldPanelUi {
	pub panel: Panel,
	position: Vec3Field,
	debug_layer: Choice,
	camera_pane: CollapsiblePane,
	aperture: FloatField,
	shutter: FloatField,
	iso: FloatField,
	compensation: FloatField,
	probe_pane: CollapsiblePane,
	probe_status: StaticText,
	reflections_enabled: CheckBox,
	reflection_fallback: CheckBox,
	show_volumes: CheckBox,
	reflection_view: Choice,
	shown_status: RefCell<String>,
	layout_dirty: Rc<Cell<bool>>,
}

impl WorldPanelUi {
	pub fn new<P: WxWidget>(parent: &P, actions: &Actions) -> Self {
		let panel = Panel::builder(parent).build();
		let sizer = BoxSizer::builder(Orientation::Vertical).build();
		let layout_dirty = Rc::new(Cell::new(false));

		let heading = title(&panel, "World");
		sizer.add(&heading, 0, SizerFlag::All, 6);

		let tx = actions.clone();
		let position = Vec3Field::new(
			&panel,
			"Player",
			(-100000.0, 100000.0),
			move |value: Vec3f| {
				let _ = tx.send(UiAction::TeleportPlayer(value));
			},
		);
		sizer.add_sizer(position.sizer(), 0, SizerFlag::All, 6);

		let layer_names: Vec<&str> = DEBUG_LAYERS.iter().map(|(name, _)| *name).collect();
		let (layer_row, debug_layer) = row_with_choice(&panel, "Debug Layer", &layer_names);
		sizer.add_sizer(
			&layer_row,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		debug_layer.on_selection_changed(move |event| {
			if let Some(layer) = event
				.get_selection()
				.and_then(|index| DEBUG_LAYERS.get(index as usize))
			{
				let _ = tx.send(UiAction::SetCvarInt(
					"r_debug_bounds".into(),
					i64::from(layer.1),
				));
			}
		});

		let camera_pane = CollapsiblePane::builder(&panel)
			.with_label("Camera")
			.build();
		let camera_win = camera_pane.get_pane().expect("camera pane");
		let camera_sizer = BoxSizer::builder(Orientation::Vertical).build();

		let float_cvar = |name: &'static str, convert: fn(f32) -> f32| {
			let tx = actions.clone();

			move |value: f32| {
				let _ = tx.send(UiAction::SetCvarFloat(name.into(), convert(value)));
			}
		};

		let aperture = FloatField::new(
			&camera_win,
			"Aperture (f/)",
			(0.7, 64.0),
			float_cvar("r_aperture", |v| v),
		);
		let shutter = FloatField::new(
			&camera_win,
			"Shutter (1/s)",
			(1.0, 8000.0),
			float_cvar("r_shutter", |v| 1.0 / v),
		);
		let iso = FloatField::new(
			&camera_win,
			"ISO",
			(25.0, 25600.0),
			float_cvar("r_iso", |v| v),
		);
		let compensation = FloatField::new(
			&camera_win,
			"Compensation (EV)",
			(-10.0, 10.0),
			float_cvar("r_exposure_ev", |v| v),
		);

		for field in [&aperture, &shutter, &iso, &compensation] {
			camera_sizer.add_sizer(field.sizer(), 0, SizerFlag::All, 6);
		}

		camera_win.set_sizer(camera_sizer, true);
		sizer.add(&camera_pane, 0, SizerFlag::Expand | SizerFlag::All, 4);

		let probe_pane = CollapsiblePane::builder(&panel)
			.with_label("Probes")
			.build();
		let probe_win = probe_pane.get_pane().expect("probe pane");
		let probe_sizer = BoxSizer::builder(Orientation::Vertical).build();

		let probe_status = StaticText::builder(&probe_win).build();
		probe_sizer.add(&probe_status, 0, SizerFlag::Expand | SizerFlag::All, 6);

		let reflections_enabled = CheckBox::builder(&probe_win).with_label("Enabled").build();
		probe_sizer.add(
			&reflections_enabled,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		reflections_enabled.on_toggled(move |event| {
			let _ = tx.send(UiAction::SetCvarBool(
				"r_reflection_probes".into(),
				event.is_checked(),
			));
		});

		let reflection_fallback = CheckBox::builder(&probe_win)
			.with_label("Reflection fallback probe")
			.build();
		probe_sizer.add(
			&reflection_fallback,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		reflection_fallback.on_toggled(move |event| {
			let _ = tx.send(UiAction::SetReflectionFallback(event.is_checked()));
		});

		let show_volumes = CheckBox::builder(&probe_win)
			.with_label("Show probe volumes")
			.build();
		probe_sizer.add(
			&show_volumes,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		show_volumes.on_toggled(move |event| {
			let _ = tx.send(UiAction::SetCvarBool(
				"r_show_volumes".into(),
				event.is_checked(),
			));
		});

		let (view_row, reflection_view) =
			row_with_choice(&probe_win, "Reflection View", &REFLECTION_VIEWS);
		probe_sizer.add_sizer(
			&view_row,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		reflection_view.on_selection_changed(move |event| {
			if let Some(index) = event.get_selection() {
				let _ = tx.send(UiAction::SetCvarInt(
					"r_reflection_debug".into(),
					i64::from(index),
				));
			}
		});

		let buttons = GridSizer::builder(2, 2).with_vgap(4).with_hgap(4).build();

		let probe_buttons: [(&str, UiAction); 6] = [
			("New At Player", UiAction::NewReflectionProbeAtPlayer),
			(
				"Mark Selection",
				UiAction::MarkSelectionReflectionProbe(true),
			),
			(
				"Unmark Selection",
				UiAction::MarkSelectionReflectionProbe(false),
			),
			("Bake Reflections", UiAction::BakeReflections),
			("Bake All", UiAction::BakeAll),
			("Save Probes", UiAction::SaveProbes),
		];

		for (label, action) in probe_buttons {
			let button = Button::builder(&probe_win).with_label(label).build();
			let tx = actions.clone();

			button.on_click(move |_| {
				let _ = tx.send(action.clone());
			});

			buttons.add(&button, 0, SizerFlag::Expand, 0);
		}

		probe_sizer.add_sizer(
			&buttons,
			0,
			SizerFlag::Expand | SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);
		probe_win.set_sizer(probe_sizer, true);
		sizer.add(&probe_pane, 0, SizerFlag::Expand | SizerFlag::All, 4);

		for pane in [camera_pane, probe_pane] {
			let dirty = layout_dirty.clone();

			pane.on_changed(move |_| dirty.set(true));
		}

		panel.set_sizer(sizer, true);

		Self {
			panel,
			position,
			debug_layer,
			camera_pane,
			aperture,
			shutter,
			iso,
			compensation,
			probe_pane,
			probe_status,
			reflections_enabled,
			reflection_fallback,
			show_volumes,
			reflection_view,
			shown_status: RefCell::new(String::new()),
			layout_dirty,
		}
	}

	pub fn take_layout_dirty(&self) -> bool {
		self.layout_dirty.replace(false)
	}

	pub fn update(&self, world: &WorldPanel, player: Vec3f) {
		if !self.position.has_focus() {
			self.position.set_value(player);
		}

		let layer = DEBUG_LAYERS
			.iter()
			.position(|(_, mask)| i64::from(*mask) == world.debug_bounds_mask);

		match layer {
			Some(index) => {
				if self.debug_layer.get_selection() != Some(index as u32) {
					self.debug_layer.set_selection(index as u32);
				}
			}
			None => {}
		}

		if self.probe_pane.is_expanded() {
			let probes = &world.probes;

			let status = if probes.baking_reflections {
				format!("Baking {} probe(s)...", probes.reflection_probes)
			} else if probes.baking_irradiance {
				"Baking irradiance...".to_owned()
			} else if probes.reflection_probes == 0 {
				"No reflection probes".to_owned()
			} else if probes.reflections_baked {
				format!("{} probe(s), baked", probes.reflection_probes)
			} else {
				format!("{} probe(s), needs a bake", probes.reflection_probes)
			};

			if *self.shown_status.borrow() != status {
				self.probe_status.set_label(&status);
				*self.shown_status.borrow_mut() = status;
			}

			let sync = |check: &CheckBox, value: bool| {
				if check.get_value() != value {
					check.set_value(value);
				}
			};

			sync(&self.reflections_enabled, world.reflections_enabled);
			sync(&self.reflection_fallback, world.reflection_fallback);
			sync(&self.show_volumes, world.show_probe_volumes);

			let view = world.reflection_view.clamp(0, 2) as u32;

			if self.reflection_view.get_selection() != Some(view) {
				self.reflection_view.set_selection(view);
			}
		}

		if self.camera_pane.is_expanded() {
			let sync = |field: &FloatField, value: f32| {
				if !field.has_focus() {
					field.set_value(value);
				}
			};

			sync(&self.aperture, world.aperture);
			sync(&self.shutter, world.shutter_denominator);
			sync(&self.iso, world.iso);
			sync(&self.compensation, world.compensation);
		}
	}
}

struct BitRow {
	bit: u32,
	check: CheckBox,
}

pub struct ObjectPanelUi {
	pub panel: Panel,
	name: StaticText,
	material: Choice,
	materials_shown: RefCell<Vec<String>>,
	tags: Vec<BitRow>,
	flags: Vec<BitRow>,
}

impl ObjectPanelUi {
	fn group(
		panel: &Panel,
		sizer: &BoxSizer,
		caption: &str,
		names: &[(u32, &str, Option<&str>)],
		is_tag: bool,
		actions: &Actions,
	) -> Vec<BitRow> {
		let group =
			StaticBoxSizerBuilder::new_with_label(Orientation::Vertical, panel, caption).build();
		let static_box = group.get_static_box().expect("static box");

		let mut rows = Vec::new();

		for (bit, name, hint) in names {
			let check = CheckBox::builder(&static_box).with_label(name).build();

			if let Some(hint) = hint {
				check.set_tooltip(hint);
			}

			let tx = actions.clone();
			let bit = *bit;

			check.on_toggled(move |event| {
				let _ = tx.send(UiAction::SetObjectBit {
					is_tag,
					bit,
					enabled: event.is_checked(),
				});
			});

			group.add(&check, 0, SizerFlag::All, 2);
			rows.push(BitRow { bit, check });
		}

		sizer.add_sizer(&group, 0, SizerFlag::Expand | SizerFlag::All, 6);

		rows
	}

	pub fn new<P: WxWidget>(parent: &P, actions: &Actions) -> Self {
		let panel = Panel::builder(parent).with_size(Size::new(240, -1)).build();
		let sizer = BoxSizer::builder(Orientation::Vertical).build();

		let heading = title(&panel, "Properties");
		sizer.add(&heading, 0, SizerFlag::All, 6);

		let name = StaticText::builder(&panel).build();
		sizer.add(
			&name,
			0,
			SizerFlag::Expand | SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let (material_row, material) = row_with_choice(&panel, "Material", &[]);
		sizer.add_sizer(
			&material_row,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		material.on_selection_changed(move |event| {
			if let Some(slot) = event.get_selection() {
				let _ = tx.send(UiAction::SetMaterialSlot(slot as usize));
			}
		});

		let tags = Self::group(&panel, &sizer, "Tags", &TAG_NAMES, true, actions);
		let flags = Self::group(&panel, &sizer, "Flags", &FLAG_NAMES, false, actions);

		panel.set_sizer(sizer, true);

		Self {
			panel,
			name,
			material,
			materials_shown: RefCell::new(Vec::new()),
			tags,
			flags,
		}
	}

	pub fn update(&self, materials: &[String], object: Option<&ObjectPanel>) {
		if *self.materials_shown.borrow() != materials {
			self.material.clear();

			for name in materials {
				self.material.append(name);
			}

			*self.materials_shown.borrow_mut() = materials.to_vec();
		}

		let Some(object) = object else {
			self.name.set_label("No block selected");
			self.material.enable(false);

			for row in self.tags.iter().chain(&self.flags) {
				row.check.set_value(false);
				row.check.enable(false);
			}

			return;
		};

		self.name.set_label(&object.label);
		self.material.enable(object.material_editable);

		if object.material_slot >= 0
			&& self.material.get_selection() != Some(object.material_slot as u32)
		{
			self.material.set_selection(object.material_slot as u32);
		}

		for row in &self.tags {
			row.check.set_value(object.tags & row.bit != 0);
			row.check.enable(object.editable_tags & row.bit != 0);
		}

		for row in &self.flags {
			row.check.set_value(object.flags & row.bit != 0);
			row.check.enable(object.editable_flags & row.bit != 0);
		}
	}
}

pub struct LightPanelUi {
	pub panel: Panel,
	name: StaticText,
	color: ColourPickerCtrl,
	position: Vec3Field,
	radius: FloatField,
	intensity: FloatField,
	lumens: FloatField,
	outer: FloatField,
	inner: FloatField,
}

impl LightPanelUi {
	pub fn new<P: WxWidget>(parent: &P, actions: &Actions) -> Self {
		let panel = Panel::builder(parent).build();
		let sizer = BoxSizer::builder(Orientation::Vertical).build();

		let heading = title(&panel, "Light");
		sizer.add(&heading, 0, SizerFlag::All, 6);

		let name = StaticText::builder(&panel).build();
		sizer.add(
			&name,
			0,
			SizerFlag::Expand | SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let color_row = BoxSizer::builder(Orientation::Horizontal).build();
		let color_label = StaticText::builder(&panel).with_label("Colour").build();
		color_row.add(&color_label, 0, SizerFlag::AlignCenterVertical, 0);

		let color = ColourPickerCtrl::builder(&panel)
			.with_initial_colour(Colour::new(255, 255, 255, 255))
			.build();
		color_row.add(&color, 0, SizerFlag::Left, 6);
		sizer.add_sizer(
			&color_row,
			0,
			SizerFlag::Left | SizerFlag::Right | SizerFlag::Bottom,
			6,
		);

		let tx = actions.clone();
		color.on_colour_changed(move |event| {
			if let Some(colour) = event.get_colour() {
				let _ = tx.send(UiAction::LightColor([colour.r, colour.g, colour.b, 255]));
			}
		});

		let tx = actions.clone();
		let position = Vec3Field::new(&panel, "Position", (-100000.0, 100000.0), move |value| {
			let _ = tx.send(UiAction::LightPosition(value));
		});
		sizer.add_sizer(position.sizer(), 0, SizerFlag::All, 6);

		let float_field = |label: &str, limits: (f32, f32), make: fn(f32) -> UiAction| {
			let tx = actions.clone();

			FloatField::new(&panel, label, limits, move |value| {
				let _ = tx.send(make(value));
			})
		};

		let radius = float_field("Radius", (3.0, 150.0), UiAction::LightRadius);
		let intensity = float_field(
			"Intensity (cd)",
			(0.0, 100_000_000.0),
			UiAction::LightIntensity,
		);
		let lumens = float_field("Lumens", (0.0, 100_000_000.0), UiAction::LightLumens);
		let outer = float_field("Outer angle", (1.0, 90.0), UiAction::LightOuterAngle);
		let inner = float_field("Inner angle", (0.0, 90.0), UiAction::LightInnerAngle);

		for field in [&radius, &intensity, &lumens, &outer, &inner] {
			sizer.add_sizer(field.sizer(), 0, SizerFlag::All, 6);
		}

		panel.set_sizer(sizer, true);

		Self {
			panel,
			name,
			color,
			position,
			radius,
			intensity,
			lumens,
			outer,
			inner,
		}
	}

	pub fn update(&self, light: Option<&LightPanel>) {
		let Some(light) = light else {
			self.name.set_label("No light selected");
			self.color.enable(false);
			self.position.set_enabled(false);

			return;
		};

		self.name.set_label(&format!("Selected '{}'", light.name));
		self.color.enable(true);
		self.position.set_enabled(true);

		self.color.set_colour(Colour::new(
			light.color[0],
			light.color[1],
			light.color[2],
			255,
		));

		if !self.position.has_focus() {
			self.position.set_value(light.position);
		}

		let sync = |field: &FloatField, value: f32| {
			if !field.has_focus() {
				field.set_value(value);
			}
		};

		sync(&self.radius, light.radius);
		sync(&self.intensity, light.intensity);
		sync(&self.lumens, light.lumens);
		sync(&self.outer, light.outer_degrees);
		sync(&self.inner, light.inner_degrees);
	}
}
