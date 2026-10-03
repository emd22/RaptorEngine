use crate::{Log, LogLevel};

const MAX_MACRO_NAME_LENGTH: usize = 255;

/// Max depth for `PERMIF` scopes
const MAX_NESTING_DEPTH: u32 = 64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage
{
	Vertex = 0,
	Pixel = 1,
	Compute = 2,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u16)]
pub enum ReflectionType
{
	StructuredBuffer = 0,
	CBuffer = 1,
	Texture = 2,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ReflectionEntry
{
	pub kind: ReflectionType,
	pub set: u8,
	pub binding: u8,
}

#[derive(Clone, Copy, Debug)]
pub struct Macro<'a>
{
	pub name: &'a [u8],
	pub value: Option<&'a [u8]>,
}

#[derive(Debug, Default)]
pub struct Output
{
	pub programs: [Vec<u8>; 3],
	pub reflection: [Vec<ReflectionEntry>; 3],
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Directive
{
	None,
	If,
	IfNot,
	Else,
	EndIf,
}

const DIRECTIVES: [(&[u8], Directive); 4] = [
	(b"PERMIF", Directive::If),
	(b"PERMNOT", Directive::IfNot),
	(b"PERMELSE", Directive::Else),
	(b"PERMEND", Directive::EndIf),
];

#[derive(Clone, Copy)]
enum PPFunction
{
	Program,
	Reflect,
	ParamTest,
	Texture2D,
	DataTexture2D,
	StructBuffer,
	CBuffer,
}

struct PPEntry
{
	name: &'static [u8],
	function: PPFunction,
	keeps_text: bool,
}

// Heh PP functions
/// Functions built into the preprocessor. Pretty much intrinsics, but heavily used with reflection.
const PP_FUNCTIONS: [PPEntry; 10] = [
	PPEntry {
		name: b"F_PROGRAM",
		function: PPFunction::Program,
		keeps_text: false,
	},
	PPEntry {
		name: b"F_REFLECT",
		function: PPFunction::Reflect,
		keeps_text: false,
	},
	PPEntry {
		name: b"F_PARAMTEST",
		function: PPFunction::ParamTest,
		keeps_text: false,
	},
	PPEntry {
		name: b"F_Texture2D",
		function: PPFunction::Texture2D,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_TextureCubeArray",
		function: PPFunction::Texture2D,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_DataTexture2D",
		function: PPFunction::DataTexture2D,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_ShadowTexture2D",
		function: PPFunction::Texture2D,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_StructBuffer",
		function: PPFunction::StructBuffer,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_RWStructBuffer",
		function: PPFunction::StructBuffer,
		keeps_text: true,
	},
	PPEntry {
		name: b"F_CBuffer",
		function: PPFunction::CBuffer,
		keeps_text: true,
	},
];

fn is_identifier_char(ch: u8) -> bool
{
	ch.is_ascii_alphanumeric() || ch == b'_'
}

fn trim(mut bytes: &[u8]) -> &[u8]
{
	while let [first, rest @ ..] = bytes {
		if !first.is_ascii_whitespace() {
			break;
		}
		bytes = rest;
	}
	while let [rest @ .., last] = bytes {
		if !last.is_ascii_whitespace() {
			break;
		}
		bytes = rest;
	}
	bytes
}

fn parse_int(text: &[u8]) -> i32
{
	let mut rest = trim(text);

	let negative = match rest {
		[b'-', tail @ ..] => {
			rest = tail;
			true
		}
		[b'+', tail @ ..] => {
			rest = tail;
			false
		}
		_ => false,
	};

	// Convert the string value to an int
	let mut value: i64 = 0;

	for &ch in rest.iter().take_while(|ch| ch.is_ascii_digit()) {
		// Add each digit to the result. We multiply the current value by 10 to shift it over, and
		// then add the digit. e.g. 1234 becomes 12340 when multiplying by 10, and then we
		// can add the next digit '5' to get 12345.
		value = value
			.saturating_mul(10)
			.saturating_add(i64::from(ch - b'0'));
	}

	(if negative { -value } else { value }) as i32
}

struct Preproc<'a>
{
	data: &'a [u8],
	index: usize,
	line: u32,
	macros: &'a [Macro<'a>],
	log: &'a mut dyn Log,
	output: Output,
	current: Stage,
	broadcast: bool,
	depth: u32,
}

impl Preproc<'_>
{
	fn get_at(&self, offset: usize) -> u8
	{
		self.data.get(self.index + offset).copied().unwrap_or(0)
	}

	fn get(&self) -> u8
	{
		self.get_at(0)
	}

	fn at_end(&self) -> bool
	{
		self.index >= self.data.len()
	}

	fn previous(&self) -> u8
	{
		match self.index {
			0 => 0,
			index => self.data.get(index - 1).copied().unwrap_or(0),
		}
	}

	fn next_char(&mut self)
	{
		if self.get() == b'\n' {
			self.line += 1;
		}
		self.index += 1;
	}

	fn skip(&mut self, count: usize)
	{
		for _ in 0..count {
			self.next_char();
		}
	}

	fn next_if_equal(&mut self, ch: u8)
	{
		if !self.at_end() && self.get() == ch {
			self.next_char();
		}
	}

	fn matches(&self, text: &[u8]) -> bool
	{
		self.data
			.get(self.index..)
			.is_some_and(|rest| rest.starts_with(text))
	}

	fn error(&mut self, message: &str)
	{
		self.log.log(LogLevel::Error, message);
	}

	fn write_byte(&mut self, ch: u8)
	{
		if self.broadcast {
			self.output.programs[Stage::Vertex as usize].push(ch);
			self.output.programs[Stage::Pixel as usize].push(ch);
		} else {
			self.output.programs[self.current as usize].push(ch);
		}
	}

	fn write_str(&mut self, text: &str)
	{
		for &ch in text.as_bytes() {
			self.write_byte(ch);
		}
	}

	fn peek_directive(&self) -> Directive
	{
		if is_identifier_char(self.previous()) {
			return Directive::None;
		}

		for (name, directive) in DIRECTIVES {
			if self.matches(name) && !is_identifier_char(self.get_at(name.len())) {
				return directive;
			}
		}

		Directive::None
	}

	fn skip_directive_name(&mut self, directive: Directive)
	{
		if let Some((name, _)) = DIRECTIVES.iter().find(|(_, d)| *d == directive) {
			self.skip(name.len());
		}
	}

	fn skip_horizontal_whitespace(&mut self)
	{
		while !self.at_end() && matches!(self.get(), b' ' | b'\t') {
			self.next_char();
		}
	}

	fn skip_line_end(&mut self)
	{
		self.skip_horizontal_whitespace();
		self.next_if_equal(b'\r');
		self.next_if_equal(b'\n');
	}

	fn skip_directive_tail(&mut self)
	{
		self.skip_horizontal_whitespace();

		if !self.at_end() && self.get() == b'(' {
			self.next_char();
			self.skip_horizontal_whitespace();
			self.next_if_equal(b')');
		}

		self.next_if_equal(b';');
		self.skip_line_end();
	}

	fn at_line_comment(&self) -> bool
	{
		self.matches(b"//")
	}

	fn at_block_comment(&self) -> bool
	{
		self.matches(b"/*")
	}

	fn skip_line_comment(&mut self)
	{
		while !self.at_end() && self.get() != b'\n' {
			self.next_char();
		}
	}

	fn block_comment_length(&self) -> usize
	{
		let rest = &self.data[self.index..];
		rest[2..]
			.windows(2)
			.position(|pair| pair == b"*/")
			.map_or(rest.len(), |position| position + 4)
	}

	fn copy_block_comment(&mut self)
	{
		for _ in 0..self.block_comment_length() {
			let ch = self.get();
			self.write_byte(ch);
			self.next_char();
		}
	}

	fn skip_block_comment(&mut self)
	{
		let length = self.block_comment_length();
		self.skip(length);
	}

	fn write_until_directive(&mut self)
	{
		while !self.at_end() {
			if self.peek_directive() != Directive::None {
				break;
			}

			if self.at_line_comment() {
				self.skip_line_comment();
				continue;
			}

			if self.at_block_comment() {
				self.copy_block_comment();
				continue;
			}

			if self.parse_function_call() {
				continue;
			}

			let ch = self.get();
			self.write_byte(ch);
			self.next_char();
		}
	}

	fn skip_conditional(&mut self, stop_at_else: bool) -> bool
	{
		let mut depth = 1;

		while !self.at_end() {
			if self.at_line_comment() {
				self.skip_line_comment();
				continue;
			}

			if self.at_block_comment() {
				self.skip_block_comment();
				continue;
			}

			let directive = self.peek_directive();

			match directive {
				Directive::If | Directive::IfNot => {
					self.skip_directive_name(directive);
					depth += 1;
					continue;
				}
				Directive::Else => {
					self.skip_directive_name(directive);

					if depth == 1 && stop_at_else {
						self.skip_directive_tail();
						return true;
					}
					continue;
				}
				Directive::EndIf => {
					self.skip_directive_name(directive);

					depth -= 1;
					if depth == 0 {
						self.skip_directive_tail();
						return false;
					}
					continue;
				}
				Directive::None => {}
			}

			self.next_char();
		}

		self.error("Preproc: Reached the end of the file without a matching PERMEND");
		false
	}

	fn emit_line_marker(&mut self)
	{
		let marker = format!("#line {}\n", self.line);
		self.write_str(&marker);
	}

	fn write_conditional(&mut self, in_true_branch: bool)
	{
		loop {
			self.emit_line_marker();

			self.write_until_directive();

			let directive = self.peek_directive();

			match directive {
				Directive::If | Directive::IfNot => {
					self.skip_directive_name(directive);
					self.parse_permutation(directive == Directive::IfNot);
				}
				Directive::Else => {
					self.skip_directive_name(directive);

					if !in_true_branch {
						self.error("Preproc: Multiple PERMELSE in the same conditional");
					}

					self.skip_conditional(false);
					return;
				}
				Directive::EndIf => {
					self.skip_directive_name(directive);
					self.skip_directive_tail();
					return;
				}
				Directive::None => {
					self.error("Preproc: Reached the end of the file without a matching PERMEND");
					return;
				}
			}
		}
	}

	fn parse_permutation(&mut self, is_negated: bool)
	{
		self.skip_horizontal_whitespace();

		if self.at_end() || self.get() != b'(' {
			self.error("Preproc: Missing '(' on permutation call");
			return;
		}

		self.next_char();

		let mut read_macro: Vec<u8> = Vec::new();
		let mut reported_overflow = false;

		while !self.at_end() && self.get() != b')' && self.get() != b'\n' {
			let ch = self.get();
			self.next_char();

			if ch == b'\r' {
				continue;
			}

			if read_macro.len() >= MAX_MACRO_NAME_LENGTH {
				if !reported_overflow {
					self.error("Preproc: Variable index is larger than the allocated buffer size");
					reported_overflow = true;
				}
				continue;
			}

			read_macro.push(ch);
		}

		if self.at_end() || self.get() != b')' {
			self.error("Preproc: Missing ')' on permutation call");
			return;
		}

		self.next_char();
		self.next_if_equal(b';');
		self.skip_line_end();

		let name = trim(&read_macro);

		let mut condition_is_true = self.macros.iter().any(|m| m.name == name);

		if is_negated {
			condition_is_true = !condition_is_true;
		}

		if self.depth >= MAX_NESTING_DEPTH {
			self.error("Preproc: Permutations are nested too deeply");
			self.skip_conditional(false);
			return;
		}

		self.depth += 1;
		if condition_is_true {
			self.write_conditional(true);
		} else if self.skip_conditional(true) {
			self.write_conditional(false);
		}
		self.depth -= 1;
	}

	fn match_function(&self) -> Option<(&'static PPEntry, usize)>
	{
		if is_identifier_char(self.previous()) {
			return None;
		}

		for entry in &PP_FUNCTIONS {
			if !self.matches(entry.name) || is_identifier_char(self.get_at(entry.name.len())) {
				continue;
			}

			let mut offset = entry.name.len();
			while matches!(self.get_at(offset), b' ' | b'\t') {
				offset += 1;
			}

			if self.get_at(offset) == b'(' {
				return Some((entry, offset + 1));
			}
		}

		None
	}

	fn parse_function_call(&mut self) -> bool
	{
		let Some((entry, header_length)) = self.match_function() else {
			return false;
		};

		let origin = self.index;
		self.skip(header_length);

		let data = self.data;
		let mut params: Vec<&[u8]> = Vec::new();
		let mut param_start = self.index;
		let mut depth = 0;

		loop {
			if self.at_end() {
				self.error(
					"Preproc: Reached the end of the file inside a preprocessor function call",
				);
				return true;
			}

			match self.get() {
				b'(' => depth += 1,
				b')' if depth == 0 => break,
				b')' => depth -= 1,
				b',' if depth == 0 => {
					params.push(trim(&data[param_start..self.index]));
					self.next_char();
					param_start = self.index;
					continue;
				}
				_ => {}
			}

			self.next_char();
		}

		params.push(trim(&data[param_start..self.index]));
		self.next_char();

		self.next_if_equal(b';');
		self.next_if_equal(b'\r');
		self.next_if_equal(b'\n');

		if entry.keeps_text {
			for &ch in &data[origin..self.index] {
				self.write_byte(ch);
			}
		}

		self.run_function(entry.function, &params);
		true
	}

	fn require(&mut self, params: &[&[u8]], count: usize) -> bool
	{
		if params.len() < count {
			self.error("Not enough parameters found in preprocessor function!");
			return false;
		}
		true
	}

	fn add_reflection(&mut self, kind: ReflectionType, set: &[u8], binding: &[u8])
	{
		self.output.reflection[self.current as usize].push(ReflectionEntry {
			kind,
			set: parse_int(set) as u8,
			binding: parse_int(binding) as u8,
		});
	}

	fn set_stage(&mut self, stage: Stage)
	{
		self.broadcast = false;
		self.current = stage;
	}

	fn run_function(&mut self, function: PPFunction, params: &[&[u8]])
	{
		match function {
			PPFunction::Program => {
				if !self.require(params, 1) {
					return;
				}

				match params[0] {
					b"FPT_VERTEX" => self.set_stage(Stage::Vertex),
					b"FPT_PIXEL" => self.set_stage(Stage::Pixel),
					b"FPT_COMPUTE" => self.set_stage(Stage::Compute),
					b"FPT_ALL" => {
						self.broadcast = true;
						return;
					}
					_ => {}
				}

				let marker = format!("#line {}\n", self.line);
				self.output.programs[self.current as usize].extend_from_slice(marker.as_bytes());
			}
			PPFunction::Reflect => {
				if !self.require(params, 3) {
					return;
				}

				let kind = match params[0] {
					b"FR_CBUFFER" => ReflectionType::CBuffer,
					b"FR_SAMPLER2D" => ReflectionType::Texture,
					_ => ReflectionType::StructuredBuffer,
				};
				self.add_reflection(kind, params[2], params[1]);
			}
			PPFunction::ParamTest => {
				self.log.log(LogLevel::Print, "== PARAMTEST ==");
				for param in params {
					let line = format!("PARAMETER: '{}'", String::from_utf8_lossy(param));
					self.log.log(LogLevel::Print, &line);
				}
				self.log.log(LogLevel::Print, "=====");
			}
			PPFunction::Texture2D => {
				if self.require(params, 3) {
					self.add_reflection(ReflectionType::Texture, params[2], params[1]);
				}
			}
			PPFunction::DataTexture2D => {
				if self.require(params, 4) {
					self.add_reflection(ReflectionType::Texture, params[3], params[2]);
				}
			}
			PPFunction::StructBuffer => {
				if self.require(params, 4) {
					self.add_reflection(ReflectionType::StructuredBuffer, params[3], params[2]);
				}
			}
			PPFunction::CBuffer => {
				if self.require(params, 3) {
					self.add_reflection(ReflectionType::CBuffer, params[2], params[1]);
				}
			}
		}
	}

	fn prepend_macro_defines(&mut self)
	{
		let mut defines = Vec::new();

		for m in self.macros {
			let Some(value) = m.value else { continue };

			defines.extend_from_slice(b"#define ");
			defines.extend_from_slice(m.name);
			defines.push(b' ');
			defines.extend_from_slice(value);
			defines.push(b'\n');
		}

		if defines.is_empty() {
			return;
		}

		for program in &mut self.output.programs {
			if program.is_empty() {
				continue;
			}

			let mut combined = defines.clone();
			combined.append(program);
			*program = combined;
		}
	}
}

pub fn process(source: &[u8], macros: &[Macro], log: &mut dyn Log) -> Output
{
	let mut preproc = Preproc {
		data: source,
		index: 0,
		line: 1,
		macros,
		log,
		output: Output::default(),
		current: Stage::Vertex,
		broadcast: true,
		depth: 0,
	};

	while !preproc.at_end() {
		preproc.write_until_directive();

		let directive = preproc.peek_directive();

		match directive {
			Directive::If | Directive::IfNot => {
				preproc.skip_directive_name(directive);
				preproc.parse_permutation(directive == Directive::IfNot);
			}
			Directive::Else | Directive::EndIf => {
				let name = if directive == Directive::Else {
					"PERMELSE"
				} else {
					"PERMEND"
				};
				preproc.error(&format!("Preproc: {name} without a matching PERMIF"));
				preproc.skip_directive_name(directive);
				preproc.skip_directive_tail();
			}
			Directive::None => {}
		}
	}

	preproc.prepend_macro_defines();
	preproc.output
}
