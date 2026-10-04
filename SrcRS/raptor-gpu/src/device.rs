use std::ffi::{CStr, c_char};

use ash::{khr, vk};

use crate::{Error, Instance, Level, Log, Result};

pub const NULL_QUEUE: u32 = u32::MAX;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct QueueFamilies
{
	pub graphics: u32,
	pub present: u32,
	pub transfer: u32,
}

impl QueueFamilies
{
	pub fn is_complete(&self) -> bool
	{
		self.graphics != NULL_QUEUE && self.present != NULL_QUEUE && self.transfer != NULL_QUEUE
	}

	pub fn has_independent_transfer(&self) -> bool
	{
		self.transfer != self.graphics
	}
}

#[derive(Clone, Copy, Debug, Default)]
pub struct DeviceCaps
{
	pub max_sampler_anisotropy: f32,
	pub supports_cube_arrays: bool,
}

pub struct Device
{
	instance: ash::Instance,
	surface_loader: khr::surface::Instance,
	surface: vk::SurfaceKHR,
	raw: ash::Device,
	physical: vk::PhysicalDevice,
	families: QueueFamilies,
	queues: [vk::Queue; 3],
	caps: DeviceCaps,
	log: std::sync::Arc<dyn Log>,
}

fn find_graphics_family(
	surface_loader: &khr::surface::Instance,
	physical: vk::PhysicalDevice,
	surface: vk::SurfaceKHR,
	families: &[vk::QueueFamilyProperties],
	log: &dyn Log,
) -> Option<u32>
{
	families.iter().enumerate().find_map(|(index, family)| {
		let index = index as u32;

		if !family.queue_flags.contains(vk::QueueFlags::GRAPHICS) {
			return None;
		}

		// SAFETY: the physical device, queue family index and surface are valid.
		match unsafe {
			surface_loader.get_physical_device_surface_support(physical, index, surface)
		} {
			Ok(true) => Some(index),
			Ok(false) => None,
			Err(_) => {
				log.log(
					Level::Error,
					&format!("Could not query present support for family at index {index}"),
				);
				None
			}
		}
	})
}

fn find_transfer_family(families: &[vk::QueueFamilyProperties], graphics: u32) -> Result<u32>
{
	let independent = families.iter().enumerate().find_map(|(index, family)| {
		(family.queue_flags.contains(vk::QueueFlags::TRANSFER) && index as u32 != graphics)
			.then_some(index as u32)
	});

	if let Some(index) = independent {
		return Ok(index);
	}

	let graphics_supports_transfer = families
		.get(graphics as usize)
		.is_some_and(|family| family.queue_flags.contains(vk::QueueFlags::TRANSFER));

	if graphics_supports_transfer {
		Ok(graphics)
	} else {
		Err(Error::new("Could not find a queue that supports transfer!"))
	}
}

fn find_queue_families(
	instance: &ash::Instance,
	surface_loader: &khr::surface::Instance,
	physical: vk::PhysicalDevice,
	surface: vk::SurfaceKHR,
	log: &dyn Log,
) -> QueueFamilies
{
	// SAFETY: the physical device belongs to the instance.
	let properties = unsafe { instance.get_physical_device_queue_family_properties(physical) };

	log.log(
		Level::Info,
		&format!("Amount of queue families: {}", properties.len()),
	);

	let mut families = QueueFamilies {
		graphics: NULL_QUEUE,
		present: NULL_QUEUE,
		transfer: NULL_QUEUE,
	};

	if let Some(graphics) =
		find_graphics_family(surface_loader, physical, surface, &properties, log)
	{
		families.graphics = graphics;
		families.present = graphics;

		if let Ok(transfer) = find_transfer_family(&properties, graphics) {
			families.transfer = transfer;
		}
	}

	families
}

fn name_of(raw: &[c_char]) -> String
{
	// SAFETY: Vulkan guarantees these fixed size names are NUL-terminated.
	unsafe { CStr::from_ptr(raw.as_ptr()) }
		.to_string_lossy()
		.into_owned()
}

fn supports_extension(extensions: &[vk::ExtensionProperties], wanted: &CStr) -> bool
{
	extensions
		.iter()
		.any(|extension| extension.extension_name_as_c_str() == Ok(wanted))
}

impl Device
{
	pub fn create(instance: &Instance, surface: vk::SurfaceKHR) -> Result<Self>
	{
		let log = instance.log().clone();
		let raw_instance = instance.raw().clone();
		let surface_loader = khr::surface::Instance::new(instance.entry(), &raw_instance);

		let (physical, families) =
			pick_physical_device(&raw_instance, &surface_loader, surface, &*log)?;

		let (raw, queues, caps) = create_logical_device(&raw_instance, physical, families, &*log)?;

		log.log(
			Level::Info,
			&format!(
				"Device Queue Families: \n\tPresent: {}\n\tGraphics: {}\n\tTransfer: {}",
				families.present, families.graphics, families.transfer
			),
		);

		Ok(Self {
			instance: raw_instance,
			surface_loader,
			surface,
			raw,
			physical,
			families,
			queues,
			caps,
			log,
		})
	}

	pub fn physical(&self) -> vk::PhysicalDevice
	{
		self.physical
	}

	pub fn handle(&self) -> vk::Device
	{
		self.raw.handle()
	}

	pub fn raw(&self) -> &ash::Device
	{
		&self.raw
	}

	pub fn families(&self) -> QueueFamilies
	{
		self.families
	}

	pub fn graphics_queue(&self) -> vk::Queue
	{
		self.queues[0]
	}

	pub fn present_queue(&self) -> vk::Queue
	{
		self.queues[1]
	}

	pub fn transfer_queue(&self) -> vk::Queue
	{
		self.queues[2]
	}

	pub fn caps(&self) -> DeviceCaps
	{
		self.caps
	}

	pub fn wait_idle(&self)
	{
		// SAFETY: the device is alive.
		let _ = unsafe { self.raw.device_wait_idle() };
	}

	pub fn surface_format(&self) -> Result<vk::SurfaceFormatKHR>
	{
		// SAFETY: the physical device and surface are valid.
		let formats = unsafe {
			self.surface_loader
				.get_physical_device_surface_formats(self.physical, self.surface)
		}
		.map_err(|result| Error::vulkan("Could not query surface formats", result))?;

		let Some(&first) = formats.first() else {
			return Err(Error::new("The surface reports no formats"));
		};

		let srgb = formats.iter().find(|format| {
			format.color_space == vk::ColorSpaceKHR::SRGB_NONLINEAR
				&& matches!(
					format.format,
					vk::Format::B8G8R8A8_SRGB | vk::Format::R8G8B8A8_SRGB
				)
		});

		if let Some(&format) = srgb {
			return Ok(format);
		}

		self.log.log(
			Level::Warning,
			"No sRGB surface format available; the final image will not be gamma encoded",
		);

		let mut best = first;

		for &format in &formats {
			if format.format == vk::Format::R16G16B16A16_SFLOAT {
				return Ok(format);
			}

			if format.format == vk::Format::R8G8B8A8_UNORM {
				best = format;
			}
		}

		Ok(best)
	}

	pub fn instance(&self) -> &ash::Instance
	{
		&self.instance
	}
}

impl Drop for Device
{
	fn drop(&mut self)
	{
		// SAFETY: the owner destroys everything created from the device first.
		unsafe { self.raw.destroy_device(None) };
	}
}

fn is_suitable(
	instance: &ash::Instance,
	surface_loader: &khr::surface::Instance,
	physical: vk::PhysicalDevice,
	surface: vk::SurfaceKHR,
	log: &dyn Log,
) -> Option<QueueFamilies>
{
	let families = find_queue_families(instance, surface_loader, physical, surface, log);

	// SAFETY: the physical device belongs to the instance.
	let version = unsafe { instance.get_physical_device_properties(physical) }.api_version;

	if version > vk::make_api_version(0, 1, 3, 0) && families.is_complete() {
		return Some(families);
	}

	log.log(
		Level::Info,
		&format!(
			"Device not suitable: (Vk: {}.{}.{}), Graphics?: {}, Present?: {}, Xfer?: {}, IsComplete?: {}",
			vk::api_version_major(version),
			vk::api_version_minor(version),
			vk::api_version_patch(version),
			families.graphics != NULL_QUEUE,
			families.present != NULL_QUEUE,
			families.transfer != NULL_QUEUE,
			families.is_complete()
		),
	);

	None
}

fn pick_physical_device(
	instance: &ash::Instance,
	surface_loader: &khr::surface::Instance,
	surface: vk::SurfaceKHR,
	log: &dyn Log,
) -> Result<(vk::PhysicalDevice, QueueFamilies)>
{
	// SAFETY: the instance is alive.
	let devices = unsafe { instance.enumerate_physical_devices() }
		.map_err(|result| Error::vulkan("Could not enumerate physical devices", result))?;

	if devices.is_empty() {
		return Err(Error::new(
			"No usable physical devices found (no vulkan support!)",
		));
	}

	let mut fallback = None;
	let mut chosen = None;

	for physical in devices {
		let Some(families) = is_suitable(instance, surface_loader, physical, surface, log) else {
			continue;
		};

		// SAFETY: the physical device belongs to the instance.
		let properties = unsafe { instance.get_physical_device_properties(physical) };

		if properties.device_type == vk::PhysicalDeviceType::DISCRETE_GPU {
			chosen = Some((physical, families));
			break;
		}

		fallback.get_or_insert((physical, families));
	}

	let Some((physical, families)) = chosen.or(fallback) else {
		return Err(Error::new("Could not find a suitable physical device!"));
	};

	// SAFETY: the physical device belongs to the instance.
	let properties = unsafe { instance.get_physical_device_properties(physical) };

	let type_name = match properties.device_type {
		vk::PhysicalDeviceType::DISCRETE_GPU => "Discrete",
		vk::PhysicalDeviceType::INTEGRATED_GPU => "Integrated",
		_ => "Other",
	};

	log.log(
		Level::Info,
		&format!(
			"Selected physical device: {} ({type_name})",
			name_of(&properties.device_name)
		),
	);

	Ok((physical, families))
}

fn create_logical_device(
	instance: &ash::Instance,
	physical: vk::PhysicalDevice,
	families: QueueFamilies,
	log: &dyn Log,
) -> Result<(ash::Device, [vk::Queue; 3], DeviceCaps)>
{
	let priorities = [1.0f32];

	let mut queue_infos = vec![
		vk::DeviceQueueCreateInfo::default()
			.queue_family_index(families.graphics)
			.queue_priorities(&priorities),
	];

	if families.has_independent_transfer() {
		queue_infos.push(
			vk::DeviceQueueCreateInfo::default()
				.queue_family_index(families.transfer)
				.queue_priorities(&priorities),
		);
	}

	// SAFETY: the physical device belongs to the instance.
	let available =
		unsafe { instance.enumerate_device_extension_properties(physical) }.unwrap_or_default();

	let mut extensions: Vec<*const c_char> = vec![
		khr::swapchain::NAME.as_ptr(),
		khr::synchronization2::NAME.as_ptr(),
		khr::timeline_semaphore::NAME.as_ptr(),
	];

	let portability = supports_extension(&available, khr::portability_subset::NAME);

	if portability {
		log.log(Level::Info, "Device is using portability extension!");
		extensions.push(khr::portability_subset::NAME.as_ptr());
	}

	let mut driver_properties = vk::PhysicalDeviceDriverProperties::default();
	let mut properties2 =
		vk::PhysicalDeviceProperties2::default().push_next(&mut driver_properties);

	// SAFETY: the physical device belongs to the instance.
	unsafe { instance.get_physical_device_properties2(physical, &mut properties2) };

	let properties = properties2.properties;

	log.log(
		Level::Info,
		&format!(
			"Creating device for physical device (Id={}) {} -- driver {}",
			properties.device_id,
			name_of(&properties.device_name),
			name_of(&driver_properties.driver_name)
		),
	);

	// SAFETY: the physical device belongs to the instance.
	let supported = unsafe { instance.get_physical_device_features(physical) };

	let caps = DeviceCaps {
		max_sampler_anisotropy: if supported.sampler_anisotropy == vk::TRUE {
			properties.limits.max_sampler_anisotropy
		} else {
			1.0
		},
		supports_cube_arrays: supported.image_cube_array == vk::TRUE,
	};

	let mut portability_features =
		vk::PhysicalDevicePortabilitySubsetFeaturesKHR::default().mutable_comparison_samplers(true);
	let mut features_1_1 =
		vk::PhysicalDeviceVulkan11Features::default().shader_draw_parameters(true);
	let mut features_1_3 = vk::PhysicalDeviceVulkan13Features::default().synchronization2(true);
	let mut float16 = vk::PhysicalDeviceShaderFloat16Int8Features::default().shader_float16(true);
	let mut timeline =
		vk::PhysicalDeviceTimelineSemaphoreFeatures::default().timeline_semaphore(true);

	let mut features2 = vk::PhysicalDeviceFeatures2::default()
		.features(
			vk::PhysicalDeviceFeatures::default()
				.image_cube_array(supported.image_cube_array == vk::TRUE)
				.sampler_anisotropy(supported.sampler_anisotropy == vk::TRUE),
		)
		.push_next(&mut timeline)
		.push_next(&mut float16)
		.push_next(&mut features_1_3)
		.push_next(&mut features_1_1);

	if portability {
		features2 = features2.push_next(&mut portability_features);
	}

	let create_info = vk::DeviceCreateInfo::default()
		.queue_create_infos(&queue_infos)
		.enabled_extension_names(&extensions)
		.push_next(&mut features2);

	// SAFETY: the create info and everything it points to outlive the call.
	let device = unsafe { instance.create_device(physical, &create_info, None) }
		.map_err(|result| Error::vulkan("Could not create logical device", result))?;

	// SAFETY: each family had a queue created.
	let queues = unsafe {
		[
			device.get_device_queue(families.graphics, 0),
			device.get_device_queue(families.present, 0),
			device.get_device_queue(families.transfer, 0),
		]
	};

	if queues.iter().any(|queue| *queue == vk::Queue::null()) {
		// SAFETY: nothing else uses the device yet.
		unsafe { device.destroy_device(None) };
		return Err(Error::new("Queue cannot be null!"));
	}

	Ok((device, queues, caps))
}

#[cfg(test)]
mod tests
{
	use super::*;

	fn family(flags: vk::QueueFlags) -> vk::QueueFamilyProperties
	{
		vk::QueueFamilyProperties::default()
			.queue_flags(flags)
			.queue_count(1)
	}

	#[test]
	fn transfer_prefers_a_different_family()
	{
		let families = [
			family(vk::QueueFlags::GRAPHICS | vk::QueueFlags::TRANSFER),
			family(vk::QueueFlags::TRANSFER),
		];

		assert_eq!(find_transfer_family(&families, 0).unwrap(), 1);
	}

	#[test]
	fn transfer_falls_back_to_the_graphics_family()
	{
		let families = [family(vk::QueueFlags::GRAPHICS | vk::QueueFlags::TRANSFER)];

		assert_eq!(find_transfer_family(&families, 0).unwrap(), 0);
	}

	#[test]
	fn transfer_fails_when_nothing_supports_it()
	{
		let families = [family(vk::QueueFlags::GRAPHICS)];

		assert!(find_transfer_family(&families, 0).is_err());
		assert!(find_transfer_family(&[], 0).is_err());
	}

	#[test]
	fn family_completeness_and_independence()
	{
		let mut families = QueueFamilies {
			graphics: 0,
			present: 0,
			transfer: NULL_QUEUE,
		};

		assert!(!families.is_complete());

		families.transfer = 0;
		assert!(families.is_complete());
		assert!(!families.has_independent_transfer());

		families.transfer = 2;
		assert!(families.has_independent_transfer());
	}
}
