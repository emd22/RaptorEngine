use crate::cvar;

pub const MAX_ENTRY_CHARS: usize = 512;
const MAX_TOKEN_CHARS: usize = 1024;

pub trait CommandHost
{
	fn run_command(&mut self, name: &str) -> bool;
}

impl CommandHost for ()
{
	fn run_command(&mut self, _name: &str) -> bool
	{
		false
	}
}

#[derive(Default)]
pub struct Console
{
	pub output: String,
	entry: String,
}

pub fn tokenize(line: &str) -> Vec<String>
{
	line.split_whitespace()
		.map(|token| token.chars().take(MAX_TOKEN_CHARS).collect())
		.collect()
}

impl Console
{
	pub fn new() -> Console
	{
		Console::default()
	}

	pub fn entry(&self) -> &str
	{
		&self.entry
	}

	pub fn backspace(&mut self, clear_line: bool)
	{
		if clear_line {
			self.entry.clear();
		} else {
			self.entry.pop();
		}
	}

	pub fn push_char(&mut self, ch: char, host: &mut dyn CommandHost)
	{
		if ch == '\n' {
			self.submit(host);
		} else if self.entry.chars().count() < MAX_ENTRY_CHARS {
			self.entry.push(ch);
		}
	}

	pub fn submit(&mut self, host: &mut dyn CommandHost)
	{
		let line = std::mem::take(&mut self.entry);

		self.execute(&tokenize(&line), host);
	}

	pub fn execute(&mut self, tokens: &[String], host: &mut dyn CommandHost)
	{
		let Some(command) = tokens.first().filter(|token| !token.is_empty()) else {
			return;
		};

		let Some(name) = command.strip_prefix('$') else {
			self.output = if host.run_command(command) {
				"Executed".to_owned()
			} else {
				"Cmd not found".to_owned()
			};
			return;
		};

		let mut cvars = cvar::global()
			.write()
			.unwrap_or_else(|poisoned| poisoned.into_inner());

		match tokens.len() {
			1 => {
				self.output = cvars
					.get(name)
					.map_or_else(|| "not defined".to_owned(), |value| value.to_string());
			}
			2 => {
				self.output = match cvars.set_from_str(name, &tokens[1]) {
					Ok(()) => cvars.get(name).map(ToString::to_string).unwrap_or_default(),
					Err(cvar::CVarError::Undefined(_)) => "not defined".to_owned(),
					Err(_) => "invalid value".to_owned(),
				};
			}
			_ => {}
		}
	}
}

#[cfg(test)]
mod tests
{
	use super::*;

	struct Host(Vec<String>);

	impl CommandHost for Host
	{
		fn run_command(&mut self, name: &str) -> bool
		{
			self.0.push(name.to_owned());
			name == "known"
		}
	}

	fn type_line(console: &mut Console, host: &mut Host, line: &str)
	{
		for ch in line.chars() {
			console.push_char(ch, host);
		}
		console.push_char('\n', host);
	}

	#[test]
	fn cvar_get_and_set()
	{
		let mut host = Host(Vec::new());
		let mut console = Console::new();

		cvar::set_int("console_test_value", 5).unwrap();

		type_line(&mut console, &mut host, "$console_test_value");
		assert_eq!(console.output, "5");

		type_line(&mut console, &mut host, "$console_test_value 12");
		assert_eq!(console.output, "12");
		assert_eq!(cvar::int("console_test_value", 0), 12);

		type_line(&mut console, &mut host, "$console_test_value abc");
		assert_eq!(console.output, "invalid value");

		type_line(&mut console, &mut host, "$console_test_missing");
		assert_eq!(console.output, "not defined");
	}

	#[test]
	fn commands_go_to_the_host()
	{
		let mut host = Host(Vec::new());
		let mut console = Console::new();

		type_line(&mut console, &mut host, "known");
		assert_eq!(console.output, "Executed");

		type_line(&mut console, &mut host, "other");
		assert_eq!(console.output, "Cmd not found");
		assert_eq!(host.0, ["known", "other"]);
	}

	#[test]
	fn editing_the_entry()
	{
		let mut console = Console::new();

		console.push_char('a', &mut ());
		console.push_char('b', &mut ());
		console.backspace(false);
		assert_eq!(console.entry(), "a");
		console.backspace(true);
		assert_eq!(console.entry(), "");
	}
}
