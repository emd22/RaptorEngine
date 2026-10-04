use ash::vk;

use crate::Device;

pub const ENTRY_NONE: u32 = 0;
pub const ENTRY_IMAGE: u32 = 1;
pub const ENTRY_BUFFER: u32 = 2;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReflectionEntry
{
	pub kind: u16,
	pub set: u8,
	pub binding: u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DescriptorSlot
{
	pub set: u32,
	pub binding: u32,
	pub kind: u32,
}

pub fn reflection_entry_kind(kind: u16) -> u32
{
	match kind {
		0 | 1 => ENTRY_BUFFER,
		2 => ENTRY_IMAGE,
		_ => ENTRY_NONE,
	}
}

pub fn find_missing_descriptor(
	reflection: &[ReflectionEntry],
	slots: &[DescriptorSlot],
) -> Option<DescriptorSlot>
{
	for entry in reflection {
		let wanted = DescriptorSlot {
			set: u32::from(entry.set),
			binding: u32::from(entry.binding),
			kind: reflection_entry_kind(entry.kind),
		};

		if !slots.contains(&wanted) {
			return Some(wanted);
		}
	}

	None
}

#[derive(Clone, Copy, Debug)]
pub struct AttachmentInfo
{
	pub format: vk::Format,
	pub samples: vk::SampleCountFlags,
	pub load_op: vk::AttachmentLoadOp,
	pub store_op: vk::AttachmentStoreOp,
	pub stencil_load_op: vk::AttachmentLoadOp,
	pub stencil_store_op: vk::AttachmentStoreOp,
	pub initial_layout: vk::ImageLayout,
	pub final_layout: vk::ImageLayout,
}

impl AttachmentInfo
{
	pub fn description(&self) -> vk::AttachmentDescription
	{
		vk::AttachmentDescription::default()
			.format(self.format)
			.samples(self.samples)
			.load_op(self.load_op)
			.store_op(self.store_op)
			.stencil_load_op(self.stencil_load_op)
			.stencil_store_op(self.stencil_store_op)
			.initial_layout(self.initial_layout)
			.final_layout(self.final_layout)
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TargetAspect
{
	Color,
	Depth,
}

#[derive(Clone, Copy, Debug)]
pub struct ClearTarget
{
	pub aspect: Option<TargetAspect>,
	pub load_op: vk::AttachmentLoadOp,
	pub render_pass_only: bool,
}

pub fn clear_values(targets: &[ClearTarget]) -> Vec<vk::ClearValue>
{
	targets
		.iter()
		.filter(|target| !target.render_pass_only && target.load_op == vk::AttachmentLoadOp::CLEAR)
		.filter_map(|target| {
			let mut value = vk::ClearValue::default();

			match target.aspect? {
				TargetAspect::Depth => {
					value.depth_stencil = vk::ClearDepthStencilValue {
						depth: 0.0,
						stencil: 0,
					};
				}
				TargetAspect::Color => {
					value.color = vk::ClearColorValue {
						float32: [0.0, 0.0, 0.0, 0.0],
					};
				}
			}

			Some(value)
		})
		.collect()
}

pub fn find_format_index(formats: &[u16], format: u16, sub_index: i32) -> i32
{
	let mut remaining = sub_index;

	for (index, candidate) in formats.iter().enumerate() {
		if *candidate == format {
			let skip = remaining > 0;
			remaining -= 1;

			if skip {
				continue;
			}

			return index as i32;
		}
	}

	-1
}

pub fn formats_compatible(a: &[u16], b: &[u16]) -> bool
{
	a == b
}

impl Device
{
	/// # Safety
	///
	/// `cmd` must be recording with a graphics pipeline bound that has dynamic viewport and scissor
	/// state.
	pub unsafe fn cmd_set_viewport_scissor(&self, cmd: vk::CommandBuffer, area: vk::Rect2D)
	{
		let viewport = reverse_z_viewport(area);

		// SAFETY: guaranteed by the caller.
		unsafe {
			self.raw().cmd_set_viewport(cmd, 0, &[viewport]);
			self.raw().cmd_set_scissor(cmd, 0, &[area]);
		}
	}
}

pub fn reverse_z_viewport(area: vk::Rect2D) -> vk::Viewport
{
	vk::Viewport {
		x: area.offset.x as f32,
		y: area.offset.y as f32,
		width: area.extent.width as f32,
		height: area.extent.height as f32,
		min_depth: 1.0,
		max_depth: 0.0,
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn slot(set: u32, binding: u32, kind: u32) -> DescriptorSlot
	{
		DescriptorSlot { set, binding, kind }
	}

	#[test]
	fn reflection_kinds_map_to_entry_kinds()
	{
		assert_eq!(reflection_entry_kind(0), ENTRY_BUFFER);
		assert_eq!(reflection_entry_kind(1), ENTRY_BUFFER);
		assert_eq!(reflection_entry_kind(2), ENTRY_IMAGE);
		assert_eq!(reflection_entry_kind(9), ENTRY_NONE);
	}

	#[test]
	fn the_first_unmatched_reflection_entry_is_reported()
	{
		let reflection = [
			ReflectionEntry {
				kind: 1,
				set: 0,
				binding: 0,
			},
			ReflectionEntry {
				kind: 2,
				set: 0,
				binding: 3,
			},
			ReflectionEntry {
				kind: 0,
				set: 1,
				binding: 2,
			},
		];

		assert_eq!(
			find_missing_descriptor(
				&reflection,
				&[
					slot(0, 0, ENTRY_BUFFER),
					slot(0, 3, ENTRY_IMAGE),
					slot(1, 2, ENTRY_BUFFER)
				]
			),
			None
		);

		assert_eq!(
			find_missing_descriptor(
				&reflection,
				&[slot(0, 0, ENTRY_BUFFER), slot(1, 2, ENTRY_BUFFER)]
			),
			Some(slot(0, 3, ENTRY_IMAGE))
		);

		assert_eq!(
			find_missing_descriptor(
				&reflection,
				&[
					slot(0, 0, ENTRY_BUFFER),
					slot(0, 3, ENTRY_BUFFER),
					slot(1, 2, ENTRY_BUFFER)
				]
			),
			Some(slot(0, 3, ENTRY_IMAGE))
		);
	}

	#[test]
	fn attachment_descriptions_carry_every_field()
	{
		let description = AttachmentInfo {
			format: vk::Format::D32_SFLOAT,
			samples: vk::SampleCountFlags::TYPE_1,
			load_op: vk::AttachmentLoadOp::CLEAR,
			store_op: vk::AttachmentStoreOp::STORE,
			stencil_load_op: vk::AttachmentLoadOp::DONT_CARE,
			stencil_store_op: vk::AttachmentStoreOp::DONT_CARE,
			initial_layout: vk::ImageLayout::UNDEFINED,
			final_layout: vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL,
		}
		.description();

		assert_eq!(description.format, vk::Format::D32_SFLOAT);
		assert_eq!(description.load_op, vk::AttachmentLoadOp::CLEAR);
		assert_eq!(
			description.stencil_store_op,
			vk::AttachmentStoreOp::DONT_CARE
		);
		assert_eq!(
			description.final_layout,
			vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL
		);
	}

	#[test]
	fn clear_values_follow_the_clearing_targets_in_order()
	{
		let target = |aspect, load_op, render_pass_only| ClearTarget {
			aspect,
			load_op,
			render_pass_only,
		};

		let values = clear_values(&[
			target(
				Some(TargetAspect::Color),
				vk::AttachmentLoadOp::CLEAR,
				false,
			),
			target(Some(TargetAspect::Color), vk::AttachmentLoadOp::LOAD, false),
			target(
				Some(TargetAspect::Depth),
				vk::AttachmentLoadOp::CLEAR,
				false,
			),
			target(Some(TargetAspect::Color), vk::AttachmentLoadOp::CLEAR, true),
			target(None, vk::AttachmentLoadOp::CLEAR, false),
		]);

		assert_eq!(values.len(), 2);

		// SAFETY: the values were built as these union variants above.
		unsafe {
			assert_eq!(values[0].color.float32, [0.0; 4]);
			assert_eq!(values[1].depth_stencil.depth, 0.0);
			assert_eq!(values[1].depth_stencil.stencil, 0);
		}
	}

	#[test]
	fn format_lookup_skips_earlier_matches_by_sub_index()
	{
		let formats = [4, 10, 4, 4, 7];

		assert_eq!(find_format_index(&formats, 4, 0), 0);
		assert_eq!(find_format_index(&formats, 4, 1), 2);
		assert_eq!(find_format_index(&formats, 4, 2), 3);
		assert_eq!(find_format_index(&formats, 4, 3), -1);
		assert_eq!(find_format_index(&formats, 7, 0), 4);
		assert_eq!(find_format_index(&formats, 9, 0), -1);
		assert_eq!(find_format_index(&formats, 4, -1), 0);
	}

	#[test]
	fn the_viewport_covers_the_area_with_a_flipped_depth_range()
	{
		let viewport = reverse_z_viewport(vk::Rect2D {
			offset: vk::Offset2D { x: 2048, y: 512 },
			extent: vk::Extent2D {
				width: 512,
				height: 512,
			},
		});

		assert_eq!((viewport.x, viewport.y), (2048.0, 512.0));
		assert_eq!((viewport.width, viewport.height), (512.0, 512.0));
		assert_eq!((viewport.min_depth, viewport.max_depth), (1.0, 0.0));
	}

	#[test]
	fn compatibility_needs_the_same_formats_in_the_same_order()
	{
		assert!(formats_compatible(&[1, 2], &[1, 2]));
		assert!(!formats_compatible(&[1, 2], &[2, 1]));
		assert!(!formats_compatible(&[1], &[1, 2]));
		assert!(formats_compatible(&[], &[]));
	}
}
