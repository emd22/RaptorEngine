#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum TokenKind {
	Unknown,
	Identifier,
	String,
	Integer,
	Float,
	Equals,
	LParen,
	RParen,
	LBracket,
	RBracket,
	LBrace,
	RBrace,
	LessThan,
	GreaterThan,
	Plus,
	Dollar,
	Minus,
	Asterisk,
	Question,
	Ampersand,
	Dot,
	Comma,
	Semicolon,
	Colon,
	Equality,
	NotEqual,
	LessEqual,
	GreaterEqual,
	DocComment,
}

impl TokenKind {
	pub fn name(self) -> &'static str {
		match self {
			TokenKind::Unknown => "Unknown",
			TokenKind::Identifier => "Identifier",
			TokenKind::String => "String",
			TokenKind::Integer => "Integer",
			TokenKind::Float => "Float",
			TokenKind::Equals => "Equals",
			TokenKind::LParen => "LParen",
			TokenKind::RParen => "RParen",
			TokenKind::LBracket => "LBracket",
			TokenKind::RBracket => "RBracket",
			TokenKind::LBrace => "LBrace",
			TokenKind::RBrace => "RBrace",
			TokenKind::LessThan => "LessThan",
			TokenKind::GreaterThan => "GreaterThan",
			TokenKind::Plus => "Plus",
			TokenKind::Dollar => "Dollar",
			TokenKind::Minus => "Minus",
			TokenKind::Asterisk => "Asterisk",
			TokenKind::Question => "Question",
			TokenKind::Ampersand => "Ampersand",
			TokenKind::Dot => "Dot",
			TokenKind::Comma => "Comma",
			TokenKind::Semicolon => "Semicolon",
			TokenKind::Colon => "Colon",
			TokenKind::Equality => "Equality",
			TokenKind::NotEqual => "NotEqual",
			TokenKind::LessEqual => "LessEqual",
			TokenKind::GreaterEqual => "GreaterEqual",
			TokenKind::DocComment => "DocComment",
		}
	}
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Numeric {
	NaN,
	Integer,
	Fractional,
}

pub fn classify_numeric(text: &[u8]) -> Numeric {
	let mut result = Numeric::NaN;

	for (i, &ch) in text.iter().enumerate() {
		if ch == b'.' && result != Numeric::NaN {
			result = Numeric::Fractional;
			continue;
		}

		if ch.is_ascii_digit() {
			if result == Numeric::NaN {
				result = Numeric::Integer;
			}
			continue;
		}

		if ch == b'f' && i == text.len() - 1 && result != Numeric::NaN {
			result = Numeric::Fractional;
			continue;
		}

		return Numeric::NaN;
	}

	result
}

/// The maximum amount of digits(including decimal) we could accept from a tokie
/// Converts a token to an integer value
pub fn to_int(text: &[u8]) -> i64 {
	let mut value: i64 = 0;
	for &ch in text.iter().take_while(|ch| ch.is_ascii_digit()) {
		let digit = i64::from(ch - b'0');
		value = match value.checked_mul(10).and_then(|v| v.checked_add(digit)) {
			Some(v) => v,
			None => return i64::MAX,
		};
	}
	value
}

/// Converts a token to a floating point value
pub fn to_float(text: &[u8]) -> f32 {
	let integer_digits = text.iter().take_while(|ch| ch.is_ascii_digit()).count();
	if integer_digits == 0 {
		return 0.0;
	}

	let mut end = integer_digits;
	if text.get(end) == Some(&b'.') {
		end += 1;
		end += text[end..]
			.iter()
			.take_while(|ch| ch.is_ascii_digit())
			.count();
	}

	std::str::from_utf8(&text[..end])
		.ok()
		.and_then(|s| s.parse::<f32>().ok())
		.unwrap_or(0.0)
}
