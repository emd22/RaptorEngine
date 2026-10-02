use std::rc::Rc;

use crate::host::{CATEGORY_SCRIPT, Host, LogLevel};
use crate::token::{Numeric, TokenKind, classify_numeric};

const SINGLE_CHAR_OPERATORS: &[u8] = b"=()[]{}<>+-*/$.,;:?!&";
const DOUBLE_CHAR_OPERATORS: &[u8] = b"=!<>";
const MAX_INCLUDE_PATH: usize = 512;
const MAX_INCLUDE_DEPTH: u32 = 32;

#[derive(Clone, Copy, Debug)]
pub struct Token {
	pub kind: TokenKind,
	pub source: usize,
	pub start: usize,
	pub len: usize,
}

#[derive(Default)]
pub struct TokenStream {
	pub tokens: Vec<Token>,
	pub sources: Vec<Rc<[u8]>>,
}

impl TokenStream {
	pub fn text(&self, token: &Token) -> &[u8] {
		let source = &self.sources[token.source];
		let start = token.start.min(source.len());
		let end = token.start.saturating_add(token.len).min(source.len());
		&source[start..end]
	}
}

struct Pending {
	start: usize,
	len: usize,
}

struct Lexer {
	source: usize,
	buffer: Rc<[u8]>,
	pos: usize,
	end: usize,
}

impl Lexer {
	fn at(&self, index: usize) -> u8 {
		self.buffer.get(index).copied().unwrap_or(0)
	}

	fn pending_text(&self, pending: &Pending) -> &[u8] {
		let start = pending.start.min(self.buffer.len());

		// Use saturating add here as we don't want to overflow
		let end = pending
			.start
			.saturating_add(pending.len)
			.min(self.buffer.len());

		&self.buffer[start..end]
	}
}

pub struct Tokenizer<'h> {
	host: &'h mut dyn Host,
	extension: Vec<u8>,
	stream: TokenStream,
	include_depth: u32,
}

impl<'h> Tokenizer<'h> {
	pub fn new(host: &'h mut dyn Host, extension: &[u8]) -> Self {
		Self {
			host,
			extension: extension.to_vec(),
			stream: TokenStream::default(),
			include_depth: 0,
		}
	}

	pub fn finish(self) -> TokenStream {
		self.stream
	}

	pub fn tokenize(&mut self, data: &[u8]) {
		let buffer: Rc<[u8]> = Rc::from(data);
		self.tokenize_buffer(buffer);
	}

	pub fn include_file(&mut self, path: &[u8]) {
		if self.include_depth >= MAX_INCLUDE_DEPTH {
			let message = format!(
				"Include depth limit reached at '{}'",
				String::from_utf8_lossy(path)
			);
			self.host
				.log(LogLevel::Error, CATEGORY_SCRIPT, message.as_bytes());
			return;
		}

		let Some(data) = self.host.read_include(path, &self.extension) else {
			let mut message = b"Could not open include file '".to_vec();
			message.extend_from_slice(path);
			message.push(b'\'');
			self.host.log(LogLevel::Error, CATEGORY_SCRIPT, &message);
			return;
		};

		self.include_depth += 1;
		self.tokenize(&data);
		self.include_depth -= 1;
	}

	fn tokenize_buffer(&mut self, buffer: Rc<[u8]>) {
		let source = self.stream.sources.len();
		self.stream.sources.push(buffer.clone());

		let end = buffer.iter().position(|&b| b == 0).unwrap_or(buffer.len());
		let mut lexer = Lexer {
			source,
			buffer,
			pos: 0,
			end,
		};

		let mut current = Pending { start: 0, len: 0 };

		while lexer.pos < lexer.end {
			let ch = lexer.at(lexer.pos);

			if ch == b'/' && lexer.pos + 1 < lexer.end && lexer.at(lexer.pos + 1) == b'/' {
				self.submit(&lexer, &mut current);

				while lexer.pos < lexer.end && lexer.at(lexer.pos) != b'\n' {
					lexer.pos += 1;
				}

				current.start = lexer.pos;
				continue;
			}

			if ch == b'/' && lexer.pos + 1 < lexer.end && lexer.at(lexer.pos + 1) == b'*' {
				self.submit(&lexer, &mut current);

				lexer.pos += 2;
				while lexer.pos < lexer.end
					&& !(lexer.at(lexer.pos) == b'*' && lexer.at(lexer.pos + 1) == b'/')
				{
					lexer.pos += 1;
				}
				lexer.pos = (lexer.pos + 2).min(lexer.end);

				current.start = lexer.pos;
				continue;
			}

			if ch == b'"' {
				self.submit(&lexer, &mut current);

				lexer.pos += 1;
				let start = lexer.pos;

				while lexer.pos < lexer.end && lexer.at(lexer.pos) != b'"' {
					lexer.pos += 1;
				}

				self.stream.tokens.push(Token {
					kind: TokenKind::String,
					source: lexer.source,
					start,
					len: lexer.pos - start,
				});

				if lexer.pos < lexer.end {
					lexer.pos += 1;
				}

				current.start = lexer.pos;
				continue;
			}

			if ch == b'#' {
				self.submit(&lexer, &mut current);

				lexer.pos += 1;
				self.try_read_internal_call(&mut lexer);

				current.start = lexer.pos;
				continue;
			}

			if matches!(ch, b' ' | b'\t' | b'\n' | b'\r') {
				self.submit(&lexer, &mut current);

				lexer.pos += 1;
				current.start = lexer.pos;
				continue;
			}

			if self.check_operators(&mut lexer, &mut current, ch) {
				continue;
			}

			lexer.pos += 1;
			current.len += 1;
		}

		self.submit(&lexer, &mut current);
	}

	fn token_kind(&self, lexer: &Lexer, pending: &Pending) -> TokenKind {
		match classify_numeric(lexer.pending_text(pending)) {
			Numeric::Integer => return TokenKind::Integer,
			Numeric::Fractional => return TokenKind::Float,
			Numeric::NaN => {}
		}

		if pending.len == 1 {
			match lexer.at(pending.start) {
				b'=' => return TokenKind::Equals,
				b'(' => return TokenKind::LParen,
				b')' => return TokenKind::RParen,
				b'[' => return TokenKind::LBracket,
				b']' => return TokenKind::RBracket,
				b'{' => return TokenKind::LBrace,
				b'}' => return TokenKind::RBrace,
				b'<' => return TokenKind::LessThan,
				b'>' => return TokenKind::GreaterThan,
				b'+' => return TokenKind::Plus,
				b'-' => return TokenKind::Minus,
				b'$' => return TokenKind::Dollar,
				b'*' => return TokenKind::Asterisk,
				b'&' => return TokenKind::Ampersand,
				b'.' => return TokenKind::Dot,
				b',' => return TokenKind::Comma,
				b';' => return TokenKind::Semicolon,
				b':' => return TokenKind::Colon,
				_ => {}
			}
		} else if pending.len == 2 && lexer.at(pending.start + 1) == b'=' {
			match lexer.at(pending.start) {
				b'=' => return TokenKind::Equality,
				b'!' => return TokenKind::NotEqual,
				b'<' => return TokenKind::LessEqual,
				b'>' => return TokenKind::GreaterEqual,
				_ => {}
			}
		}

		TokenKind::Identifier
	}

	fn submit(&mut self, lexer: &Lexer, pending: &mut Pending) {
		if pending.len == 0 {
			return;
		}

		let kind = self.token_kind(lexer, pending);
		self.stream.tokens.push(Token {
			kind,
			source: lexer.source,
			start: pending.start,
			len: pending.len,
		});

		pending.len = 0;
		pending.start = lexer.pos;
	}

	fn check_operators(&mut self, lexer: &mut Lexer, pending: &mut Pending, ch: u8) -> bool {
		if ch == b'.' && classify_numeric(lexer.pending_text(pending)) != Numeric::NaN {
			return false;
		}

		if !SINGLE_CHAR_OPERATORS.contains(&ch) {
			return false;
		}

		if DOUBLE_CHAR_OPERATORS.contains(&ch) && lexer.at(lexer.pos + 1) == b'=' {
			self.submit(lexer, pending);

			pending.len += 2;
			lexer.pos += 2;

			self.submit(lexer, pending);
			return true;
		}

		self.submit(lexer, pending);

		pending.len += 1;
		lexer.pos += 1;

		self.submit(lexer, pending);
		true
	}

	fn expect_string(lexer: &mut Lexer, expected: &[u8]) -> bool {
		let mut expected_index = 0;
		let mut pos = lexer.pos;

		while pos < lexer.end {
			let ch = lexer.at(pos);
			if ch == 0 {
				break;
			}

			if expected_index >= expected.len() {
				break;
			}

			if ch != expected[expected_index] {
				return false;
			}

			expected_index += 1;
			pos += 1;
		}

		lexer.pos = pos;
		true
	}

	fn read_quoted_string(&mut self, lexer: &mut Lexer) -> Vec<u8> {
		let mut pos = lexer.pos;

		if pos >= lexer.end {
			return Vec::new();
		}

		let mut ch;
		loop {
			ch = lexer.at(pos);
			if ch == b' ' || ch == b'\t' {
				pos += 1;
			} else {
				break;
			}
		}

		if ch != b'"' {
			self.host.log(LogLevel::Print, 0, b"Not a string!");
			return Vec::new();
		}

		pos += 1;

		let mut path = Vec::new();
		for _ in 0..MAX_INCLUDE_PATH {
			if pos >= lexer.end {
				break;
			}

			ch = lexer.at(pos);

			if ch == b'"' {
				pos += 1;
				break;
			}

			path.push(ch);
			pos += 1;
		}

		lexer.pos = pos;

		let terminator = path.iter().position(|&b| b == 0).unwrap_or(path.len());
		path.truncate(terminator);
		path
	}

	fn try_read_internal_call(&mut self, lexer: &mut Lexer) {
		if !Self::expect_string(lexer, b"include") {
			return;
		}

		let path = self.read_quoted_string(lexer);
		if path.is_empty() {
			self.host
				.log(LogLevel::Print, 0, b"Error reading include path!");
			return;
		}

		self.include_file(&path);
	}
}
