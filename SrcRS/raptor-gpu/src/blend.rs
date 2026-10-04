use ash::vk;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BlendAttachment
{
	pub enabled: bool,
	pub write_mask: vk::ColorComponentFlags,
	pub color_op: vk::BlendOp,
	pub alpha_op: vk::BlendOp,
	pub src_color: vk::BlendFactor,
	pub dst_color: vk::BlendFactor,
	pub src_alpha: vk::BlendFactor,
	pub dst_alpha: vk::BlendFactor,
	pub target_index: u32,
}

impl Default for BlendAttachment
{
	fn default() -> Self
	{
		Self {
			enabled: false,
			write_mask: vk::ColorComponentFlags::RGBA,
			color_op: vk::BlendOp::ADD,
			alpha_op: vk::BlendOp::ADD,
			src_color: vk::BlendFactor::ZERO,
			dst_color: vk::BlendFactor::ZERO,
			src_alpha: vk::BlendFactor::ZERO,
			dst_alpha: vk::BlendFactor::ZERO,
			target_index: 0,
		}
	}
}

impl BlendAttachment
{
	pub fn state(&self) -> vk::PipelineColorBlendAttachmentState
	{
		vk::PipelineColorBlendAttachmentState {
			blend_enable: vk::Bool32::from(self.enabled),
			src_color_blend_factor: self.src_color,
			dst_color_blend_factor: self.dst_color,
			color_blend_op: self.color_op,
			src_alpha_blend_factor: self.src_alpha,
			dst_alpha_blend_factor: self.dst_alpha,
			alpha_blend_op: self.alpha_op,
			color_write_mask: self.write_mask,
		}
	}
}

#[derive(Debug, PartialEq, Eq)]
pub struct TargetOutOfRange
{
	pub target_index: u32,
	pub count: u32,
}

pub fn blend_states(
	attachments: &[BlendAttachment],
	count: u32,
) -> Result<Vec<vk::PipelineColorBlendAttachmentState>, TargetOutOfRange>
{
	let mut states = vec![BlendAttachment::default().state(); count as usize];

	for attachment in attachments {
		let Some(slot) = states.get_mut(attachment.target_index as usize) else {
			return Err(TargetOutOfRange {
				target_index: attachment.target_index,
				count,
			});
		};

		*slot = attachment.state();
	}

	Ok(states)
}

#[cfg(test)]
mod tests
{
	use super::*;

	#[test]
	fn unlisted_targets_get_the_default_state()
	{
		let states = blend_states(&[], 3).unwrap();

		assert_eq!(states.len(), 3);

		for state in &states {
			assert_eq!(state.blend_enable, vk::FALSE);
			assert_eq!(state.color_write_mask, vk::ColorComponentFlags::RGBA);
			assert_eq!(state.src_color_blend_factor, vk::BlendFactor::ZERO);
		}
	}

	#[test]
	fn listed_targets_replace_their_slot_and_later_ones_win()
	{
		let alpha = BlendAttachment {
			enabled: true,
			src_color: vk::BlendFactor::SRC_ALPHA,
			dst_color: vk::BlendFactor::ONE_MINUS_SRC_ALPHA,
			target_index: 1,
			..Default::default()
		};
		let replaced = BlendAttachment {
			enabled: true,
			src_color: vk::BlendFactor::ONE,
			target_index: 1,
			..Default::default()
		};

		let states = blend_states(&[alpha], 2).unwrap();
		assert_eq!(states[0].blend_enable, vk::FALSE);
		assert_eq!(states[1].blend_enable, vk::TRUE);
		assert_eq!(states[1].src_color_blend_factor, vk::BlendFactor::SRC_ALPHA);

		let states = blend_states(&[alpha, replaced], 2).unwrap();
		assert_eq!(states[1].src_color_blend_factor, vk::BlendFactor::ONE);
	}

	#[test]
	fn a_target_past_the_count_is_an_error_not_a_write()
	{
		let attachment = BlendAttachment {
			target_index: 4,
			..Default::default()
		};

		assert_eq!(
			blend_states(&[attachment], 2).err(),
			Some(TargetOutOfRange {
				target_index: 4,
				count: 2
			})
		);
	}
}
