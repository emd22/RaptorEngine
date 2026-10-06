pub mod blood;
pub mod config;
pub mod exposure;
pub mod fps;
pub mod game;
pub mod host;
pub mod ragdoll;
pub mod title;

pub use config::GameConfig;
pub use game::Game;
pub use host::Host;

#[cfg(test)]
mod mock;

#[cfg(test)]
mod tests {
	use std::time::Instant;

	use raptor_core::Key;

	use crate::GameConfig;
	use crate::game::Game;
	use crate::host::weapon_input;
	use crate::mock::MockHost;
	use crate::ragdoll::{Impact, MAX_DUMMIES};

	fn tick(game: &mut Game, host: &mut MockHost) {
		let mut last = Instant::now();

		game.tick(host, &mut last);
	}

	#[test]
	fn a_normal_frame_renders_and_composes() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		game.create_game(&mut host).unwrap();
		tick(&mut game, &mut host);

		assert_eq!(host.rendered, 1);
		assert_eq!(host.composed, 1);
		assert_eq!(host.titles, vec!["Raptor Engine - map_test.prx".to_owned()]);
		assert!(host.text.iter().any(|(line, _)| line.starts_with("FPS=")));
		assert_eq!(host.crosshair.len(), 1);
		assert_eq!(host.crosshair[0], ([448.0, 318.0], 3.0));

		tick(&mut game, &mut host);

		assert_eq!(host.titles.len(), 1);
	}

	#[test]
	fn a_frame_the_swapchain_rejects_draws_nothing() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.begin_frame_ok = false;
		tick(&mut game, &mut host);

		assert_eq!(host.rendered, 0);
		assert_eq!(host.composed, 0);
		assert!(game.is_running());
	}

	#[test]
	fn shift_grave_quits_and_frees_the_mouse() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.locked = true;
		host.down.insert(Key::KeyLshift);
		host.pressed.insert(Key::KeyGrave);
		tick(&mut game, &mut host);

		assert!(!game.is_running());
		assert!(!host.locked);
	}

	#[test]
	fn clicking_captures_the_mouse_without_firing() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.pressed.insert(Key::MouseLeft);
		tick(&mut game, &mut host);

		assert!(host.locked);
		assert_eq!(
			host.weapon_flags.last().unwrap() & weapon_input::FIRE_PRESSED,
			0
		);

		tick(&mut game, &mut host);

		assert_ne!(
			host.weapon_flags.last().unwrap() & weapon_input::FIRE_PRESSED,
			0
		);
	}

	#[test]
	fn slash_enters_the_command_console_and_runs_a_cvar_command() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.pressed.insert(Key::KeySlash);
		tick(&mut game, &mut host);
		assert!(game.in_command_mode());

		host.pressed.clear();
		host.typed = "$game_test_cvar\n".chars().collect();

		for _ in 0..16 {
			tick(&mut game, &mut host);
		}

		assert!(host.text.iter().any(|(line, _)| line == "=not defined"));
	}

	#[test]
	fn ragdolls_wait_for_the_template_then_drop_and_evict() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		for _ in 0..MAX_DUMMIES + 2 {
			game.request_ragdoll_drop(&mut host);
		}

		assert_eq!(host.template_requests, 1);

		tick(&mut game, &mut host);
		assert!(host.spawned.is_empty());

		host.template_ready = true;
		tick(&mut game, &mut host);

		assert_eq!(host.spawned.len(), MAX_DUMMIES + 2);
		assert_eq!(host.destroyed.len(), 2);
	}

	#[test]
	fn a_hard_ragdoll_impact_leaves_blood_once_per_cooldown() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.template_ready = true;
		game.request_ragdoll_drop(&mut host);
		tick(&mut game, &mut host);

		let impact = Impact {
			serial: 100,
			point: [0.0; 3],
			normal: [0.0, 1.0, 0.0],
			speed: 6.0,
		};

		host.impacts = vec![impact, impact];
		tick(&mut game, &mut host);

		assert!(host.blood.len() >= 3);

		let first = host.blood.len();

		host.impacts = vec![impact];
		tick(&mut game, &mut host);

		assert_eq!(host.blood.len(), first);
	}

	#[test]
	fn the_blockout_saves_to_its_own_path() {
		let mut game = Game::new(GameConfig::default());
		let mut host = MockHost::new();

		host.down.insert(Key::KeyLmeta);
		host.pressed.insert(Key::KeyS);
		tick(&mut game, &mut host);

		assert_eq!(
			host.saved_blockouts,
			vec!["RaptorData/Data/blockouts/map_test.prx".to_owned()]
		);
	}
}
