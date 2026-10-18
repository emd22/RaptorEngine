#pragma once

#ifdef FX_IS_EDITOR

#include <wx/colour.h>

#include <Color.hpp>
#include <Core/String.hpp>
#include <Core/Types.hpp>
// #include <vector>
#include <Core/DynArray.hpp>

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
	};

	bool Load(const String& path);

	template <typename TFunc>
	void ForEachColor(TFunc&& func);

	template <typename TFunc>
	void ForEachInt(TFunc&& func);

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

template <typename TFunc>
void EditorTheme::ForEachColor(TFunc&& func)
{
	func("frame", "background", Frame.Background);
	func("frame", "sash", Frame.Sash);
	func("frame", "sash_grip", Frame.SashGrip);
	func("frame", "status_text", Frame.StatusText);
	func("frame", "text", Frame.Text);

	func("button", "selected", Button.Selected);
	func("button", "text", Button.Text);
	func("button", "disabled_text", Button.DisabledText);

	func("material_picker", "background", MaterialPicker.Background);
	func("material_picker", "text", MaterialPicker.Text);
	func("material_picker", "highlight", MaterialPicker.Highlight);
	func("material_picker", "highlight_text", MaterialPicker.HighlightText);
	func("material_picker", "placeholder", MaterialPicker.Placeholder);

	func("grid", "background", Grid.Background);
	func("grid", "snap", Grid.Snap);
	func("grid", "minor", Grid.Minor);
	func("grid", "major", Grid.Major);
	func("grid", "label", Grid.Label);
	func("grid", "view_label", Grid.ViewLabel);
	func("grid", "axis_x", Grid.AxisX);
	func("grid", "axis_y", Grid.AxisY);
	func("grid", "axis_z", Grid.AxisZ);

	func("brush", "geometry", Brush.Geometry);
	func("brush", "probe_volume", Brush.ProbeVolume);
	func("brush", "reflection", Brush.Reflection);
	func("brush", "volume", Brush.Volume);
	func("brush", "nvbrush", Brush.NvBrush);
	func("brush", "dbound", Brush.DBound);
	func("brush", "decal", Brush.Decal);
	func("brush", "patch", Brush.Patch);

	func("tool", "selected", Tool.Selected);
	func("tool", "selected_fill", Tool.SelectedFill);
	func("tool", "create", Tool.Create);
	func("tool", "create_fill", Tool.CreateFill);
	func("tool", "marquee", Tool.Marquee);
	func("tool", "marquee_fill", Tool.MarqueeFill);
	func("tool", "rotate", Tool.Rotate);
	func("tool", "face", Tool.Face);
	func("tool", "vertex", Tool.Vertex);
	func("tool", "handle", Tool.Handle);
	func("tool", "handle_outline", Tool.HandleOutline);
	func("tool", "clip", Tool.Clip);
	func("tool", "clip_front_fill", Tool.ClipFrontFill);
	func("tool", "clip_back_fill", Tool.ClipBackFill);

	func("marker", "player", Marker.Player);
	func("marker", "light", Marker.Light);
	func("marker", "light_selected", Marker.LightSelected);
	func("marker", "spawn", Marker.Spawn);
	func("marker", "spawn_idle", Marker.SpawnIdle);
	func("marker", "selected_outline", Marker.SelectedOutline);

	func("viewport", "grab", Viewport.Grab);
	func("viewport", "model_selected", Viewport.ModelSelected);
	func("viewport", "edited_bounds", Viewport.EditedBounds);
}

template <typename TFunc>
void EditorTheme::ForEachInt(TFunc&& func)
{
	func("button", "lightness", Button.Lightness);
	func("button", "hover_lightness", Button.HoverLightness);
	func("button", "pressed_lightness", Button.PressedLightness);
}

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

#endif
