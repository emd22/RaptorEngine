use raptor_world::random;

#[unsafe(no_mangle)]
pub extern "C" fn rx_rand32() -> u32
{
	random::fast_rand32()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_random_unit() -> f32
{
	random::unit()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_random_signed_unit() -> f32
{
	random::signed_unit()
}

#[unsafe(no_mangle)]
pub extern "C" fn rx_random_range(low: f32, high: f32) -> f32
{
	random::range(low, high)
}
