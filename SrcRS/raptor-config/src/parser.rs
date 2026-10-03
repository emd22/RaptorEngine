use crate::host::{CATEGORY_CORE, Host, LogLevel};
use crate::model::{Entry, Kind, Parsed, Primitive};
use crate::token::{Numeric, TokenKind, classify_numeric, to_float, to_int};
use crate::tokenizer::{Token, TokenStream};

const MAX_DEPTH: u32 = 64;

struct Cursor<'a> {
	stream: &'a TokenStream,
	host: &'a mut dyn Host,
	index: usize,
	depth: u32,
	has_errors: bool,
}

impl Cursor<'_> {
	fn is_at_end(&self) -> bool {
		self.index >= self.stream.tokens.len()
	}

	fn peek(&self, offset: usize) -> Option<&Token> {
		self.stream.tokens.get(self.index + offset)
	}

	fn kind(&self, offset: usize) -> TokenKind {
		self.peek(offset).map_or(TokenKind::Unknown, |t| t.kind)
	}

	fn text(&self) -> &[u8] {
		self.peek(0).map_or(&[], |t| self.stream.text(t))
	}

	fn next_token(&mut self) {
		if !self.is_at_end() {
			self.index += 1;
		}
	}

	fn log(&mut self, level: LogLevel, message: &[u8]) {
		self.host.log(level, CATEGORY_CORE, message);
	}

	fn fail(&mut self, message: String) {
		self.log(LogLevel::Error, message.as_bytes());
		self.has_errors = true;
	}

	fn eat(&mut self, kind: TokenKind) -> bool {
		let found = self.kind(0);
		if found != kind {
			self.fail(format!(
				"Config({}): Expected '{}' but found '{}'",
				self.index,
				kind.name(),
				found.name()
			));
			return false;
		}

		self.next_token();
		true
	}

	/// Accepts any of the provided token types in `kinds`
	fn eat_any_of(&mut self, kinds: &[TokenKind]) -> bool {
		let found = self.kind(0);

		if kinds.contains(&found) {
			self.next_token();
			return true;
		}

		self.fail(format!(
			"Config({}): unexpected token type '{}'",
			self.index,
			found.name()
		));
		false
	}

	fn skip_to_next_entry(&mut self, in_struct: bool) {
		while !self.is_at_end() {
			let kind = self.kind(0);

			if in_struct && kind == TokenKind::RBrace {
				return;
			}

			let can_start_entry = matches!(
				kind,
				TokenKind::Identifier | TokenKind::Integer | TokenKind::Dollar
			);

			if can_start_entry && self.kind(1) == TokenKind::Equals {
				return;
			}

			self.next_token();
		}
	}
}

fn value_kind_of(cursor: &Cursor) -> Kind {
	if cursor.kind(0) == TokenKind::String {
		return Kind::String;
	}

	match classify_numeric(cursor.text()) {
		Numeric::Integer => Kind::Int,
		Numeric::Fractional => Kind::Float,
		Numeric::NaN => Kind::None,
	}
}

fn unescape(text: &[u8]) -> Vec<u8> {
	let mut out = Vec::with_capacity(text.len());
	let mut bytes = text.iter().copied().peekable();

	while let Some(ch) = bytes.next() {
		if ch == b'\\' && matches!(bytes.peek(), Some(b'"' | b'\\')) {
			out.push(bytes.next().unwrap_or(ch));
		} else {
			out.push(ch);
		}
	}

	out
}

fn find_entry<'e>(entries: &'e [Entry], name: &[u8]) -> Option<&'e Entry> {
	entries.iter().find(|entry| entry.name == name)
}

fn parse_reference(cursor: &mut Cursor, entries: &[Entry], value: &mut Primitive) -> bool {
	let mut identifier = cursor.text().to_vec();
	if !cursor.eat(TokenKind::Identifier) {
		return false;
	}

	let mut found = find_entry(entries, &identifier);

	while let Some(entry) = found {
		if cursor.kind(0) != TokenKind::Dot {
			break;
		}

		cursor.next_token();

		identifier = cursor.text().to_vec();
		if !cursor.eat(TokenKind::Identifier) {
			return false;
		}

		found = find_entry(&entry.members, &identifier);
	}

	let Some(entry) = found else {
		cursor.fail(format!(
			"Config({}): could not resolve reference",
			cursor.index
		));
		return false;
	};

	*value = entry.value.duplicated();
	true
}

fn parse_value(cursor: &mut Cursor, entries: &[Entry], value: &mut Primitive) -> bool {
	let kind = cursor.kind(0);

	if kind == TokenKind::Dollar {
		cursor.eat(TokenKind::Dollar);
		return parse_reference(cursor, entries, value);
	}

	if kind == TokenKind::Identifier {
		let mut message = b"Could not find reference to constant ".to_vec();
		message.extend_from_slice(cursor.text());
		message.push(b'!');
		cursor.log(LogLevel::Error, &message);
		cursor.has_errors = true;
	}

	if kind == TokenKind::Minus {
		cursor.eat(TokenKind::Minus);

		if cursor.depth >= MAX_DEPTH {
			cursor.fail(format!("Config({}): nesting too deep", cursor.index));
			return false;
		}

		let mut operand = Primitive::default();
		cursor.depth += 1;
		let ok = parse_value(cursor, entries, &mut operand);
		cursor.depth -= 1;

		if !ok {
			return false;
		}

		match operand.kind {
			Kind::Int => *value = Primitive::int(operand.int_value.wrapping_neg()),
			Kind::Float => *value = Primitive::float(-operand.float_value),
			_ => {
				cursor.fail(format!(
					"Config({}): cannot negate a non-numeric value",
					cursor.index
				));
				return false;
			}
		}

		return true;
	}

	match value_kind_of(cursor) {
		Kind::None | Kind::Struct => {
			cursor.fail(format!(
				"Config({}): expected a value but found '{}'",
				cursor.index,
				kind.name()
			));
			if !matches!(
				kind,
				TokenKind::Unknown | TokenKind::RBrace | TokenKind::RBracket
			) {
				cursor.next_token();
			}
			return false;
		}
		Kind::String => *value = Primitive::string(&unescape(cursor.text())),
		Kind::Int => *value = Primitive::int(to_int(cursor.text())),
		Kind::Float => *value = Primitive::float(to_float(cursor.text())),
	}

	cursor.next_token();
	true
}

fn parse_entry(
	cursor: &mut Cursor,
	entries: &[Entry],
	parent_member_count: Option<usize>,
	entry: &mut Entry,
) -> bool {
	let token_kind = cursor.kind(0);

	entry.name = match parent_member_count {
		Some(count) if token_kind == TokenKind::Dollar => count.to_string().into_bytes(),
		_ => cursor.text().to_vec(),
	};

	if !cursor.eat_any_of(&[TokenKind::Identifier, TokenKind::Integer, TokenKind::Dollar]) {
		return false;
	}
	if !cursor.eat(TokenKind::Equals) {
		return false;
	}

	if cursor.kind(0) == TokenKind::LBrace {
		if cursor.depth >= MAX_DEPTH {
			cursor.fail(format!("Config({}): nesting too deep", cursor.index));
			return false;
		}

		cursor.eat(TokenKind::LBrace);

		entry.value = Primitive::structure();

		cursor.depth += 1;

		while !cursor.is_at_end() && cursor.kind(0) != TokenKind::RBrace {
			let start_index = cursor.index;

			let mut member = Entry::default();
			if parse_entry(cursor, entries, Some(entry.members.len()), &mut member) {
				entry.add_member(member);
				continue;
			}

			cursor.skip_to_next_entry(true);
			if cursor.index == start_index
				&& !cursor.is_at_end()
				&& cursor.kind(0) != TokenKind::RBrace
			{
				cursor.next_token();
			}
		}

		cursor.depth -= 1;

		return cursor.eat(TokenKind::RBrace);
	}

	if cursor.kind(0) == TokenKind::LBracket {
		cursor.eat(TokenKind::LBracket);

		entry.is_array = true;

		entry.value.kind = value_kind_of(cursor);

		while !cursor.is_at_end() && cursor.kind(0) != TokenKind::RBracket {
			let mut value = Primitive::default();
			if !parse_value(cursor, entries, &mut value) {
				return false;
			}
			entry.array.push(value);

			if cursor.kind(0) == TokenKind::RBracket {
				break;
			}

			if !cursor.eat(TokenKind::Comma) {
				return false;
			}
		}

		return cursor.eat(TokenKind::RBracket);
	}

	parse_value(cursor, entries, &mut entry.value)
}

pub fn parse(stream: &TokenStream, host: &mut dyn Host) -> Parsed {
	let mut cursor = Cursor {
		stream,
		host,
		index: 0,
		depth: 0,
		has_errors: false,
	};

	let mut entries: Vec<Entry> = Vec::new();

	while !cursor.is_at_end() {
		let start_index = cursor.index;

		let mut entry = Entry::default();
		if parse_entry(&mut cursor, &entries, None, &mut entry) {
			entries.push(entry);
			continue;
		}

		cursor.skip_to_next_entry(false);
		if cursor.index == start_index && !cursor.is_at_end() {
			cursor.next_token();
		}
	}

	if cursor.has_errors {
		cursor.log(
			LogLevel::Warning,
			b"Config file had errors; malformed entries were skipped",
		);
	}

	Parsed {
		entries,
		has_errors: cursor.has_errors,
	}
}
