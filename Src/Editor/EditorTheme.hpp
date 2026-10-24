#pragma once

#ifdef FX_IS_EDITOR

#include <wx/colour.h>

#include <Color.hpp>
#include <Core/DynArray.hpp>
#include <Core/Serialize2.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>

namespace fx::editor {

struct EditorTheme
{
	enum class eAppearance
	{
		Dark,
		Light,
		System,
	};

	struct FrameColors
	{
		Color Background = Color::FromRGBA(30, 30, 30, 255);
		Color Sash = Color::FromRGBA(46, 46, 46, 255);
		Color SashGrip = Color::FromRGBA(110, 110, 110, 255);
		Color StatusText = Color::FromRGBA(170, 175, 185, 255);
		Color Text = Color::FromRGBA(223, 223, 223, 255);
	};

	struct ButtonColors
	{
		Color Selected = Color::FromRGBA(100, 200, 120, 255);
		Color Text = Color::FromRGBA(255, 255, 255, 255);
		Color DisabledText = Color::FromRGBA(150, 150, 150, 255);

		int32 Lightness = 112;
		int32 HoverLightness = 125;
		int32 PressedLightness = 85;
	};

	struct MaterialPickerColors
	{
		Color Background = Color::FromRGBA(30, 30, 30, 255);
		Color Text = Color::FromRGBA(223, 223, 223, 255);
		Color Highlight = Color::FromRGBA(0, 88, 208, 255);
		Color HighlightText = Color::FromRGBA(255, 255, 255, 255);
		Color Placeholder = Color::FromRGBA(128, 128, 128, 255);
	};

	struct GridColors
	{
		Color Background = Color::FromRGBA(14, 16, 19, 255);
		Color Snap = Color::FromRGBA(34, 38, 44, 255);
		Color Minor = Color::FromRGBA(44, 49, 57, 255);
		Color Major = Color::FromRGBA(66, 74, 86, 255);
		Color Label = Color::FromRGBA(110, 120, 134, 255);
		Color ViewLabel = Color::FromRGBA(150, 160, 176, 255);
		Color AxisX = Color::FromRGBA(150, 62, 62, 255);
		Color AxisY = Color::FromRGBA(66, 140, 76, 255);
		Color AxisZ = Color::FromRGBA(62, 100, 170, 255);
	};

	struct BrushColors
	{
		Color Geometry = Color::FromRGBA(206, 211, 221, 255);
		Color ProbeVolume = Color::FromRGBA(60, 220, 255, 255);
		Color Reflection = Color::FromRGBA(255, 120, 220, 255);
		Color Volume = Color::FromRGBA(120, 255, 90, 255);
		Color NvBrush = Color::FromRGBA(255, 150, 40, 255);
		Color DBound = Color::FromRGBA(90, 130, 255, 255);
		Color Portal = Color::FromRGBA(255, 225, 60, 255);
		Color Decal = Color::FromRGBA(255, 120, 200, 255);
		Color Patch = Color::FromRGBA(120, 200, 255, 255);
	};

	struct ToolColors
	{
		Color Selected = Color::FromRGBA(255, 92, 72, 255);
		Color SelectedFill = Color::FromRGBA(255, 92, 72, 46);
		Color Create = Color::FromRGBA(120, 255, 140, 255);
		Color CreateFill = Color::FromRGBA(120, 255, 140, 36);
		Color Marquee = Color::FromRGBA(120, 170, 255, 255);
		Color MarqueeFill = Color::FromRGBA(120, 170, 255, 28);
		Color Rotate = Color::FromRGBA(255, 205, 70, 255);
		Color Face = Color::FromRGBA(255, 205, 70, 255);
		Color Vertex = Color::FromRGBA(255, 205, 70, 255);
		Color Handle = Color::FromRGBA(30, 32, 38, 255);
		Color HandleOutline = Color::FromRGBA(235, 240, 250, 255);
		Color Clip = Color::FromRGBA(255, 140, 220, 255);
		Color ClipFrontFill = Color::FromRGBA(255, 140, 220, 40);
		Color ClipBackFill = Color::FromRGBA(120, 200, 255, 40);
	};

	struct MarkerColors
	{
		Color Player = Color::FromRGBA(255, 220, 60, 255);
		Color Light = Color::FromRGBA(255, 170, 40, 255);
		Color LightSelected = Color::FromRGBA(255, 255, 90, 255);
		Color Spawn = Color::FromRGBA(70, 220, 255, 255);
		Color SpawnIdle = Color::FromRGBA(70, 130, 150, 255);
		Color SelectedOutline = Color::FromRGBA(255, 255, 255, 255);
	};

	struct ViewportColors
	{
		Color Grab = Color::FromRGBA(255, 120, 40, 255);
		Color ModelSelected = Color::FromRGBA(255, 220, 60, 255);
		Color EditedBounds = Color::FromRGBA(60, 220, 255, 255);
		Color Selection = Color::FromRGBA(60, 200, 255, 70);
		Color SelectedFace = Color::FromRGBA(255, 205, 70, 110);
	};

	bool Load(const String& path);

public:
	String ID;
	String Name;
	eAppearance Appearance = eAppearance::Dark;

	FrameColors Frame;
	ButtonColors Button;
	MaterialPickerColors MaterialPicker;
	GridColors Grid;
	BrushColors Brush;
	ToolColors Tool;
	MarkerColors Marker;
	ViewportColors Viewport;
};

class EditorThemeLibrary
{
public:
	static constexpr const char* scThemeDirectory = "Config/themes";

	void Load();

	const DynArray<EditorTheme>& GetThemes() const { return mThemes; }
	uint32 GetActiveIndex() const { return mActiveIndex; }
	const EditorTheme& GetActive() const;

	void Select(uint32 index);
	bool ReloadActive();

private:
	DynArray<EditorTheme> mThemes;
	uint32 mActiveIndex = 0;
};

EditorThemeLibrary& GetThemeLibrary();

const EditorTheme& GetEditorTheme();

void StoreThemeChoice(const String& id);

inline wxColour ToWxColour(const Color& color) { return wxColour(color.R, color.G, color.B, color.A); }

} // namespace fx::editor

/////////////////////////////////////
// Serialization
/////////////////////////////////////

RS_OBJECT_BEGIN(fx::editor::EditorTheme::FrameColors)
RS_FIELD_NAMED(Background, "background")
RS_FIELD_NAMED(Sash, "sash")
RS_FIELD_NAMED(SashGrip, "sash_grip")
RS_FIELD_NAMED(StatusText, "status_text")
RS_FIELD_NAMED(Text, "text")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::ButtonColors)
RS_FIELD_NAMED(Selected, "selected")
RS_FIELD_NAMED(Text, "text")
RS_FIELD_NAMED(DisabledText, "disabled_text")
RS_FIELD_NAMED(Lightness, "lightness")
RS_FIELD_NAMED(HoverLightness, "hover_lightness")
RS_FIELD_NAMED(PressedLightness, "pressed_lightness")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::MaterialPickerColors)
RS_FIELD_NAMED(Background, "background")
RS_FIELD_NAMED(Text, "text")
RS_FIELD_NAMED(Highlight, "highlight")
RS_FIELD_NAMED(HighlightText, "highlight_text")
RS_FIELD_NAMED(Placeholder, "placeholder")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::GridColors)
RS_FIELD_NAMED(Background, "background")
RS_FIELD_NAMED(Snap, "snap")
RS_FIELD_NAMED(Minor, "minor")
RS_FIELD_NAMED(Major, "major")
RS_FIELD_NAMED(Label, "label")
RS_FIELD_NAMED(ViewLabel, "view_label")
RS_FIELD_NAMED(AxisX, "axis_x")
RS_FIELD_NAMED(AxisY, "axis_y")
RS_FIELD_NAMED(AxisZ, "axis_z")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::BrushColors)
RS_FIELD_NAMED(Geometry, "geometry")
RS_FIELD_NAMED(ProbeVolume, "probe_volume")
RS_FIELD_NAMED(Reflection, "reflection")
RS_FIELD_NAMED(Volume, "volume")
RS_FIELD_NAMED(NvBrush, "nvbrush")
RS_FIELD_NAMED(DBound, "dbound")
RS_FIELD_NAMED(Portal, "portal")
RS_FIELD_NAMED(Decal, "decal")
RS_FIELD_NAMED(Patch, "patch")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::ToolColors)
RS_FIELD_NAMED(Selected, "selected")
RS_FIELD_NAMED(SelectedFill, "selected_fill")
RS_FIELD_NAMED(Create, "create")
RS_FIELD_NAMED(CreateFill, "create_fill")
RS_FIELD_NAMED(Marquee, "marquee")
RS_FIELD_NAMED(MarqueeFill, "marquee_fill")
RS_FIELD_NAMED(Rotate, "rotate")
RS_FIELD_NAMED(Face, "face")
RS_FIELD_NAMED(Vertex, "vertex")
RS_FIELD_NAMED(Handle, "handle")
RS_FIELD_NAMED(HandleOutline, "handle_outline")
RS_FIELD_NAMED(Clip, "clip")
RS_FIELD_NAMED(ClipFrontFill, "clip_front_fill")
RS_FIELD_NAMED(ClipBackFill, "clip_back_fill")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::MarkerColors)
RS_FIELD_NAMED(Player, "player")
RS_FIELD_NAMED(Light, "light")
RS_FIELD_NAMED(LightSelected, "light_selected")
RS_FIELD_NAMED(Spawn, "spawn")
RS_FIELD_NAMED(SpawnIdle, "spawn_idle")
RS_FIELD_NAMED(SelectedOutline, "selected_outline")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme::ViewportColors)
RS_FIELD_NAMED(Grab, "grab")
RS_FIELD_NAMED(ModelSelected, "model_selected")
RS_FIELD_NAMED(EditedBounds, "edited_bounds")
RS_FIELD_NAMED(Selection, "selection")
RS_FIELD_NAMED(SelectedFace, "selected_face")
RS_OBJECT_END()

RS_OBJECT_BEGIN(fx::editor::EditorTheme)
RS_FIELD_NAMED(Name, "name")
RS_FIELD_NAMED(Frame, "frame")
RS_FIELD_NAMED(Button, "button")
RS_FIELD_NAMED(MaterialPicker, "material_picker")
RS_FIELD_NAMED(Grid, "grid")
RS_FIELD_NAMED(Brush, "brush")
RS_FIELD_NAMED(Tool, "tool")
RS_FIELD_NAMED(Marker, "marker")
RS_FIELD_NAMED(Viewport, "viewport")
RS_OBJECT_END()

#endif
