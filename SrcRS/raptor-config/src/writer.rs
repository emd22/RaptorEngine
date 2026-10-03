use crate::host::{CATEGORY_CORE, Host, LogLevel};
use crate::model::{Entry, Kind, Primitive};

const NAME_BREAKERS: &[u8] = b"=()[]{}<>+-*/$.,;:?!&\"#\\";

fn is_valid_name_byte(ch: u8) -> bool {
	!ch.is_ascii_whitespace() && ch != 0 && !NAME_BREAKERS.contains(&ch)
}

fn format_float(value: f32, host: &mut dyn Host) -> String {
	if !value.is_finite() {
		host.log(
			LogLevel::Error,
			CATEGORY_CORE,
			format!("Config: {value} cannot be written to a config file, writing 0.0 instead")
				.as_bytes(),
		);
		return "0.000000".to_string();
	}

	let fixed = format!("{:.6}", f64::from(value));

	if fixed
		.parse::<f32>()
		.is_ok_and(|parsed| parsed.to_bits() == value.to_bits())
	{
		return fixed;
	}

	let shortest = format!("{value}");

	if shortest.contains('.') {
		shortest
	} else {
		format!("{shortest}.0")
	}
}

/// Deals with escaped characters inside strings
fn push_escaped(out: &mut Vec<u8>, bytes: &[u8]) {
	out.push(b'"');
	for &ch in bytes {
		if ch == b'"' || ch == b'\\' {
			out.push(b'\\');
		}
		out.push(ch);
	}
	out.push(b'"');
}

pub fn format_primitive(primitive: &Primitive, out: &mut Vec<u8>, host: &mut dyn Host) {
	match primitive.kind {
		Kind::Int => out.extend_from_slice(primitive.int_value.to_string().as_bytes()),
		Kind::Float => out.extend_from_slice(format_float(primitive.float_value, host).as_bytes()),
		Kind::String => push_escaped(out, primitive.string_value.as_deref().unwrap_or(&[])),
		Kind::None | Kind::Struct => out.extend_from_slice(b"\"\""),
	}
}

fn valid_name(name: &[u8], host: &mut dyn Host) -> Vec<u8> {
	if !name.is_empty() && name.iter().all(|&ch| is_valid_name_byte(ch)) {
		return name.to_vec();
	}

	let mut fixed: Vec<u8> = name
		.iter()
		.map(|&ch| if is_valid_name_byte(ch) { ch } else { b'_' })
		.collect();
	if fixed.is_empty() {
		fixed.push(b'_');
	}

	let mut message = b"Config: '".to_vec();
	message.extend_from_slice(name);
	message.extend_from_slice(b"' is not a valid entry name, writing '");
	message.extend_from_slice(&fixed);
	message.extend_from_slice(b"' instead");
	host.log(LogLevel::Warning, CATEGORY_CORE, &message);

	fixed
}

fn push_indent(out: &mut Vec<u8>, indent: u32) {
	out.extend(std::iter::repeat_n(b'\t', indent as usize));
}

pub fn format_entry(entry: &Entry, indent: u32, out: &mut Vec<u8>, host: &mut dyn Host) {
	if entry.value.kind == Kind::Struct
		|| (entry.value.kind == Kind::None && !entry.is_array && !entry.is_dot_reference)
	{
		out.extend_from_slice(b"{\n");

		for member in &entry.members {
			push_indent(out, indent + 1);
			out.extend_from_slice(&valid_name(&member.name, host));
			out.extend_from_slice(b" = ");
			format_entry(member, indent + 1, out, host);
			out.push(b'\n');
		}

		push_indent(out, indent);
		out.push(b'}');
	} else if entry.is_array {
		out.extend_from_slice(b"[ ");

		for (index, value) in entry.array.iter().enumerate() {
			if index > 0 {
				out.extend_from_slice(b", ");
			}
			format_primitive(value, out, host);
		}

		out.extend_from_slice(b" ]");
	} else if entry.is_dot_reference {
		out.extend_from_slice(entry.value.string_value.as_deref().unwrap_or(&[]));
	} else {
		format_primitive(&entry.value, out, host);
	}
}

pub fn format_file(entries: &[Entry], host: &mut dyn Host) -> Vec<u8> {
	let mut out = Vec::new();

	for entry in entries {
		out.extend_from_slice(&valid_name(&entry.name, host));
		out.extend_from_slice(b" = ");
		format_entry(entry, 0, &mut out, host);
		out.push(b'\n');
	}

	out
}
