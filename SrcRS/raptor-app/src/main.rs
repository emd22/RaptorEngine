use std::ffi::CString;
use std::os::raw::{c_char, c_int};

use raptor_ffi as _;

unsafe extern "C" {
	fn RaptorMain(argc: c_int, argv: *mut *mut c_char) -> c_int;
}

fn main()
{
	let args: Vec<CString> = std::env::args_os()
		.map(|arg| CString::new(arg.to_string_lossy().into_owned()).unwrap_or_default())
		.collect();

	let mut argv: Vec<*mut c_char> = args.iter().map(|arg| arg.as_ptr().cast_mut()).collect();
	argv.push(std::ptr::null_mut());

	// SAFETY: `argv` is a null terminated array of `args.len()` NUL terminated strings that outlive
	// the call.
	let code = unsafe { RaptorMain(args.len() as c_int, argv.as_mut_ptr()) };

	std::process::exit(code);
}
