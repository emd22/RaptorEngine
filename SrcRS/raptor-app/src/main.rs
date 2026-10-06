mod cpp_host;

use std::path::PathBuf;
use std::process::ExitCode;

use raptor_ffi as _;
use raptor_game::{Game, GameConfig};
use raptor_script as _;

fn find_config() -> PathBuf
{
	let relative = PathBuf::from("Config/Main.conf");

	let exe_dir = std::env::current_exe()
		.ok()
		.and_then(|exe| exe.parent().map(PathBuf::from));

	exe_dir
		.into_iter()
		.chain([raptor_core::paths::base_dir().clone()])
		.map(|dir| dir.join(&relative))
		.find(|candidate| candidate.exists())
		.unwrap_or(relative)
}

fn main() -> ExitCode
{
	let config = GameConfig::load(find_config());
	let args: Vec<String> = std::env::args().collect();

	let mut host = cpp_host::CppHost::new();

	match Game::run(config, &mut host, args) {
		Ok(()) => ExitCode::SUCCESS,
		Err(error) => {
			raptor_core::log_error!("{error}");
			ExitCode::FAILURE
		}
	}
}
