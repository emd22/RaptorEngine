use raptor_world::SlotSet;

pub type RxSlotSet = SlotSet;

#[unsafe(no_mangle)]
pub extern "C" fn rx_slots_new(max_bits: u32, all_set: bool) -> *mut RxSlotSet
{
	Box::into_raw(Box::new(SlotSet::new(max_bits, all_set)))
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_clone(slots: *const RxSlotSet) -> *mut RxSlotSet
{
	// SAFETY: guaranteed by the caller.
	Box::into_raw(Box::new(unsafe { &*slots }.clone()))
}

/// # Safety
///
/// `slots` must be null or come from `rx_slots_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_free(slots: *mut RxSlotSet)
{
	if !slots.is_null() {
		// SAFETY: guaranteed by the caller.
		drop(unsafe { Box::from_raw(slots) });
	}
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_get(slots: *const RxSlotSet, index: u32) -> bool
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.get(index)
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_set(slots: *mut RxSlotSet, index: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *slots }.set(index);
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_unset(slots: *mut RxSlotSet, index: u32)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *slots }.unset(index);
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_clear_all(slots: *mut RxSlotSet)
{
	// SAFETY: guaranteed by the caller.
	unsafe { &mut *slots }.clear_all();
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_find_next_free(slots: *const RxSlotSet, start: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.find_next_free(start)
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_find_next_set(slots: *const RxSlotSet, start: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.find_next_set(start)
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_find_free_group(slots: *const RxSlotSet, size: u32) -> u32
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.find_free_group(size)
}

/// # Safety
///
/// `slots` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_capacity(slots: *const RxSlotSet) -> u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.capacity()
}

/// The words the bits are kept in, `rx_slots_capacity() / 64` of them.
///
/// # Safety
///
/// `slots` must be live. The pointer is valid until the set is freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_words(slots: *const RxSlotSet) -> *const u64
{
	// SAFETY: guaranteed by the caller.
	unsafe { &*slots }.words().as_ptr()
}

/// Reserves `instances` slots after the object at `current`, moving the object if they are not
/// free. Returns the first slot of the run, or `u32::MAX` when there is no room, and sets `moved`.
///
/// # Safety
///
/// `slots` must be live and `moved` writable.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rx_slots_reserve_instances(
	slots: *mut RxSlotSet,
	current: u32,
	instances: u32,
	moved: *mut bool,
) -> u32
{
	// SAFETY: guaranteed by the caller.
	let result = unsafe { &mut *slots }.reserve_instances(current, instances);

	// SAFETY: guaranteed by the caller.
	unsafe { moved.write(result.is_some_and(|(_, moved)| moved)) };

	result.map_or(u32::MAX, |(start, _)| start)
}
