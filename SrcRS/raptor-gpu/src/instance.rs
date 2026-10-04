use std::ffi::{CStr, CString, c_void};
use std::sync::Arc;

use ash::{Entry, ext, vk};

use crate::{Error, Level, Log, Result};

pub struct InstanceConfig
{
	pub app_name: CString,
	pub api_version: u32,
	pub extensions: Vec<CString>,
	pub layers: Vec<CString>,
	pub enumerate_portability: bool,
	pub debug_messenger: bool,
}

struct Messenger
{
	loader: ext::debug_utils::Instance,
	handle: vk::DebugUtilsMessengerEXT,
	user_data: *mut Arc<dyn Log>,
}

pub struct Instance
{
	entry: Entry,
	raw: ash::Instance,
	messenger: Option<Messenger>,
	debug_utils: bool,
	log: Arc<dyn Log>,
}

unsafe extern "system" fn debug_callback(
	severity: vk::DebugUtilsMessageSeverityFlagsEXT,
	_message_type: vk::DebugUtilsMessageTypeFlagsEXT,
	data: *const vk::DebugUtilsMessengerCallbackDataEXT<'_>,
	user_data: *mut c_void,
) -> vk::Bool32
{
	if data.is_null() || user_data.is_null() {
		return vk::FALSE;
	}

	// SAFETY: `user_data` is the boxed `Arc` that `Instance::create` registered, alive until the
	// messenger is destroyed, and the driver passes a valid callback data pointer.
	let (log, data) = unsafe { (&*user_data.cast::<Arc<dyn Log>>(), &*data) };

	if data.p_message.is_null() {
		return vk::FALSE;
	}

	// SAFETY: the driver passes a NUL-terminated message.
	let message = unsafe { CStr::from_ptr(data.p_message) }.to_string_lossy();
	let message = format!("VkValidator: {message}");

	if severity.contains(vk::DebugUtilsMessageSeverityFlagsEXT::ERROR) {
		log.log(Level::Error, &message);
	} else if severity.contains(vk::DebugUtilsMessageSeverityFlagsEXT::WARNING) {
		log.log(Level::Warning, &message);
	} else if !severity.contains(vk::DebugUtilsMessageSeverityFlagsEXT::INFO) {
		log.log(Level::Debug, &message);
	}

	vk::FALSE
}

impl Instance
{
	/// # Safety
	///
	/// `get_instance_proc_addr` must be the `vkGetInstanceProcAddr` of the Vulkan loader the rest
	/// of the process uses.
	pub unsafe fn create(
		get_instance_proc_addr: vk::PFN_vkGetInstanceProcAddr,
		config: &InstanceConfig,
		log: Arc<dyn Log>,
	) -> Result<Self>
	{
		// SAFETY: guaranteed by the caller.
		let entry = unsafe {
			Entry::from_static_fn(ash::StaticFn {
				get_instance_proc_addr,
			})
		};

		let app_info = vk::ApplicationInfo::default()
			.application_name(&config.app_name)
			.engine_name(&config.app_name)
			.api_version(config.api_version);

		let extensions: Vec<_> = config.extensions.iter().map(|name| name.as_ptr()).collect();
		let layers: Vec<_> = config.layers.iter().map(|name| name.as_ptr()).collect();

		let mut flags = vk::InstanceCreateFlags::empty();
		if config.enumerate_portability {
			flags |= vk::InstanceCreateFlags::ENUMERATE_PORTABILITY_KHR;
		}

		let create_info = vk::InstanceCreateInfo::default()
			.application_info(&app_info)
			.enabled_extension_names(&extensions)
			.enabled_layer_names(&layers)
			.flags(flags);

		// SAFETY: the create info and everything it points to outlive the call.
		let raw = unsafe { entry.create_instance(&create_info, None) }
			.map_err(|result| Error::vulkan("Could not create vulkan instance!", result))?;

		let debug_utils = config
			.extensions
			.iter()
			.any(|name| name.as_c_str() == ext::debug_utils::NAME);

		let mut instance = Self {
			entry,
			raw,
			messenger: None,
			debug_utils,
			log,
		};

		if config.debug_messenger {
			instance.messenger = Some(instance.create_messenger()?);
		}

		Ok(instance)
	}

	fn create_messenger(&self) -> Result<Messenger>
	{
		let loader = ext::debug_utils::Instance::new(&self.entry, &self.raw);
		let user_data = Box::into_raw(Box::new(self.log.clone()));

		let create_info = vk::DebugUtilsMessengerCreateInfoEXT::default()
			.message_severity(
				vk::DebugUtilsMessageSeverityFlagsEXT::ERROR
					| vk::DebugUtilsMessageSeverityFlagsEXT::WARNING
					| vk::DebugUtilsMessageSeverityFlagsEXT::INFO,
			)
			.message_type(
				vk::DebugUtilsMessageTypeFlagsEXT::GENERAL
					| vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION
					| vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE,
			)
			.pfn_user_callback(Some(debug_callback))
			.user_data(user_data.cast());

		// SAFETY: the instance is alive and `user_data` stays valid until the messenger is
		// destroyed.
		match unsafe { loader.create_debug_utils_messenger(&create_info, None) } {
			Ok(handle) => Ok(Messenger {
				loader,
				handle,
				user_data,
			}),
			Err(result) => {
				// SAFETY: the messenger was not created, so nothing else holds the pointer.
				drop(unsafe { Box::from_raw(user_data) });
				Err(Error::vulkan("Could not create debug messenger!", result))
			}
		}
	}

	pub fn handle(&self) -> vk::Instance
	{
		self.raw.handle()
	}

	pub fn raw(&self) -> &ash::Instance
	{
		&self.raw
	}

	pub fn entry(&self) -> &Entry
	{
		&self.entry
	}

	pub fn debug_utils_enabled(&self) -> bool
	{
		self.debug_utils
	}

	pub fn log(&self) -> &Arc<dyn Log>
	{
		&self.log
	}
}

impl Drop for Instance
{
	fn drop(&mut self)
	{
		if let Some(messenger) = self.messenger.take() {
			// SAFETY: the messenger belongs to this instance, and nothing can call back once it is
			// destroyed.
			unsafe {
				messenger
					.loader
					.destroy_debug_utils_messenger(messenger.handle, None);
				drop(Box::from_raw(messenger.user_data));
			}
		}

		// SAFETY: the owner destroys everything created from the instance first.
		unsafe { self.raw.destroy_instance(None) };
	}
}
