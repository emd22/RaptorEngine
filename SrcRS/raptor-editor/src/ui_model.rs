use raptor_math::Vec3f;

use crate::host::{LightId, ObjectId, ProbeStatus, Rgba};
use crate::tools::EditorTool;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReloadTarget {
	World,
	Prototype,
	Scripts,
}

#[derive(Clone, Debug)]
pub enum UiAction {
	SetTool(EditorTool),
	Reload(ReloadTarget),
	OpenBlockout(String),
	SaveBlockout,
	SaveBlockoutAs(String),
	SelectObject(ObjectId),
	SetMaterialSlot(usize),
	SetObjectBit {
		is_tag: bool,
		bit: u32,
		enabled: bool,
	},
	TeleportPlayer(Vec3f),
	SetCvarInt(String, i64),
	SetCvarFloat(String, f32),
	SetCvarBool(String, bool),
	SetCvarFromStr(String, String),
	SetReflectionFallback(bool),
	NewReflectionProbeAtPlayer,
	MarkSelectionReflectionProbe(bool),
	BakeReflections,
	BakeAll,
	SaveProbes,
	LightPosition(Vec3f),
	LightRadius(f32),
	LightIntensity(f32),
	LightLumens(f32),
	LightOuterAngle(f32),
	LightInnerAngle(f32),
	LightColor(Rgba),
	ViewportFocusLost,
}

#[derive(Clone, Debug, Default)]
pub struct ObjectPanel {
	pub label: String,
	pub tags: u32,
	pub flags: u32,
	pub editable_tags: u32,
	pub editable_flags: u32,
	pub material_slot: i32,
	pub material_editable: bool,
}

#[derive(Clone, Debug)]
pub struct LightPanel {
	pub id: LightId,
	pub name: String,
	pub color: Rgba,
	pub position: Vec3f,
	pub radius: f32,
	pub intensity: f32,
	pub lumens: f32,
	pub outer_degrees: f32,
	pub inner_degrees: f32,
}

#[derive(Clone, Debug, Default)]
pub struct WorldPanel {
	pub debug_bounds_mask: i64,
	pub reflection_view: i64,
	pub reflections_enabled: bool,
	pub reflection_fallback: bool,
	pub show_probe_volumes: bool,
	pub probes: ProbeStatus,
	pub aperture: f32,
	pub shutter_denominator: f32,
	pub iso: f32,
	pub compensation: f32,
}

#[derive(Clone, Debug)]
pub struct ObjectRow {
	pub id: ObjectId,
	pub name: String,
	pub tags: u32,
	pub position: Vec3f,
}

#[derive(Clone, Debug)]
pub struct PanelSnapshot {
	pub tool: EditorTool,
	pub world: WorldPanel,
	pub materials: Vec<String>,
	pub object: Option<ObjectPanel>,
	pub light: Option<LightPanel>,
	pub blockout_path: Option<String>,
}
