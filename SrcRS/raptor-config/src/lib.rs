pub mod host;
pub mod model;
pub mod parser;
pub mod token;
pub mod tokenizer;

use host::Host;
use model::Parsed;
use tokenizer::Tokenizer;

pub fn parse(
	data: &[u8],
	prelude_path: Option<&[u8]>,
	include_extension: &[u8],
	host: &mut dyn Host,
) -> Parsed {
	let mut tokenizer = Tokenizer::new(host, include_extension);

	if let Some(path) = prelude_path {
		tokenizer.include_file(path);
	}
	tokenizer.tokenize(data);

	let stream = tokenizer.finish();
	parser::parse(&stream, host)
}
