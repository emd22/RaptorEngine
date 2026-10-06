use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main()
{
	println!("cargo:rerun-if-env-changed=RAPTOR_SKIP_CPP");

	if env::var_os("RAPTOR_SKIP_CPP").is_some() {
		return;
	}

	let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
	let repo = manifest.parent().and_then(Path::parent).unwrap().to_path_buf();
	let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

	let target_dir = out_dir
		.ancestors()
		.nth(4)
		.expect("OUT_DIR is not inside a cargo target directory")
		.to_path_buf();

	let config = env::var("RAPTOR_CMAKE_CONFIG").unwrap_or_else(|_| {
		match env::var("PROFILE").as_deref() {
			Ok("release") => "RelWithDebInfo".to_owned(),
			_ => "Debug".to_owned(),
		}
	});

	let build_dir = target_dir.join("cmake").join(&config);

	for path in ["Src", "CMakeLists.txt"] {
		println!("cargo:rerun-if-changed={}", repo.join(path).display());
	}

	if let Some(lib) = env::var_os("DEP_STRATA_LLVM_LIB") {
		println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{}", PathBuf::from(lib).display());
	}

	for var in ["VULKAN_SDK", "RAPTOR_CMAKE_CONFIG", "LLVM_DIR"] {
		println!("cargo:rerun-if-env-changed={var}");
	}

	let vulkan_sdk = find_vulkan_sdk();

	configure(&repo, &build_dir, &config, vulkan_sdk.as_deref());
	build(&build_dir, &config, vulkan_sdk.as_deref());
	link(&build_dir, &config);
}

fn find_vulkan_sdk() -> Option<PathBuf>
{
	if let Some(sdk) = env::var_os("VULKAN_SDK").filter(|sdk| !sdk.is_empty()) {
		return Some(PathBuf::from(sdk));
	}

	let home = PathBuf::from(env::var_os("HOME")?);
	let mut sdks: Vec<_> = fs::read_dir(home.join("VulkanSDK"))
		.ok()?
		.filter_map(Result::ok)
		.map(|entry| entry.path().join("macOS"))
		.filter(|sdk| sdk.join("lib").exists())
		.collect();

	sdks.sort();

	sdks.pop()
}

fn command(program: &str, vulkan_sdk: Option<&Path>) -> Command
{
	let mut command = Command::new(program);

	if let Some(sdk) = vulkan_sdk {
		command.env("VULKAN_SDK", sdk);
	}

	command
}

fn run(mut command: Command, what: &str)
{
	let status = command
		.status()
		.unwrap_or_else(|error| panic!("could not run {what}: {error}"));

	assert!(status.success(), "{what} failed ({status})");
}

fn configure(repo: &Path, build_dir: &Path, config: &str, vulkan_sdk: Option<&Path>)
{
	let mut cmake = command("cmake", vulkan_sdk);

	cmake.arg("-S").arg(repo).arg("-B").arg(build_dir);

	if !build_dir.join("CMakeCache.txt").exists() && Command::new("ninja").arg("--version").output().is_ok() {
		cmake.args(["-G", "Ninja"]);
	}

	cmake.arg(format!("-DCMAKE_BUILD_TYPE={config}"));
	cmake.arg(format!("-DFX_BASE_DIR={}", repo.display()));

	if cfg!(target_os = "macos") {
		cmake.arg("-DUSE_MOLTENVK=ON");
	}

	run(cmake, "cmake configure");
}

fn build(build_dir: &Path, config: &str, vulkan_sdk: Option<&Path>)
{
	let mut cmake = command("cmake", vulkan_sdk);

	cmake
		.arg("--build")
		.arg(build_dir)
		.args(["--target", "raptor_cpp", "--config", config, "--parallel"]);

	run(cmake, "cmake build");
}

fn link(build_dir: &Path, config: &str)
{
	let spec_path = build_dir.join(format!("raptor_cpp.{config}.link"));
	let spec = fs::read_to_string(&spec_path)
		.unwrap_or_else(|error| panic!("could not read {}: {error}", spec_path.display()));

	let macos = cfg!(target_os = "macos");
	let msvc = env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc");

	let arg = |value: String| println!("cargo:rustc-link-arg-bins={value}");

	let mut framework_name = false;
	let mut rpaths: Vec<String> = Vec::new();

	for line in spec.lines() {
		let Some((kind, value)) = line.split_once(' ') else {
			continue;
		};

		match kind {
			"ARCHIVE" => {
				if macos {
					arg(format!("-Wl,-force_load,{value}"));
				} else if msvc {
					arg(format!("/WHOLEARCHIVE:{value}"));
				} else {
					arg("-Wl,--whole-archive".to_owned());
					arg(value.to_owned());
					arg("-Wl,--no-whole-archive".to_owned());
				}
			}
			"DIR" => {
				if msvc {
					arg(format!("/LIBPATH:{value}"));
				} else {
					arg(format!("-L{value}"));
				}
			}
			"RPATH" => {
				if !msvc && !rpaths.iter().any(|seen| seen == value) {
					rpaths.push(value.to_owned());
					arg(format!("-Wl,-rpath,{value}"));
				}
			}
			"LIB" => {
				for token in value.split_whitespace() {
					if framework_name {
						arg(token.to_owned());
						framework_name = false;
					} else {
						framework_name = token == "-framework" || token == "-weak_framework";
						arg(library_arg(token, msvc));
					}
				}
			}
			_ => {}
		}
	}
}

fn library_arg(token: &str, msvc: bool) -> String
{
	let is_flag = token.starts_with('-') || token.starts_with('/');
	let is_path = token.contains('/') || token.contains('\\');
	let has_extension = Path::new(token).extension().is_some();

	if is_flag || is_path || (has_extension && !token.contains(".1")) {
		token.to_owned()
	} else if msvc {
		format!("{token}.lib")
	} else {
		format!("-l{token}")
	}
}
