use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

const SOURCES: &[&str] = &[
	"AST/AST.c",
	"Core/Util.c",
	"Core/SourceManager.c",
	"Core/Diagnostics.c",
	"Lex/Token.c",
	"Lex/Lexer.c",
	"Parse/Parser.c",
	"Sema/ResolveOverloads.c",
	"Codegen/TypeRegistry.c",
	"Codegen/TypeMetadata.c",
	"Codegen/TypeUtil.c",
	"Codegen/ASTDump.c",
	"Import/ModuleLoader.c",
	"Embed.c",
	"Codegen/LLVMSimd.c",
	"Codegen/LLVMCBackend.c",
	"Codegen/LLVMModuleBuilder.c",
	"Codegen/LLVMAot.c",
	"Codegen/LLVMJit.c",
];

fn main() {
	println!("cargo:rerun-if-env-changed=LLVM_DIR");
	println!("cargo:rerun-if-env-changed=LLVM_PREFIX");

	let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
	let repo = manifest
		.parent()
		.and_then(Path::parent)
		.unwrap()
		.to_path_buf();
	let strata = repo.join("Submodules/strata");
	let src = strata.join("src");

	println!("cargo:rerun-if-changed={}", src.display());
	println!(
		"cargo:rerun-if-changed={}",
		strata.join("include").display()
	);

	let llvm = find_llvm();

	let mut build = cc::Build::new();

	build
		.std("c11")
		.include(strata.join("include"))
		.include(&src)
		.include(llvm.join("include"))
		.define("STRATA_STATIC", None)
		.define("STRATA_HAS_LLVM", "1")
		.warnings(false)
		.flag_if_supported("-ffunction-sections")
		.flag_if_supported("-fdata-sections");

	for source in SOURCES {
		build.file(src.join(source));
	}

	build.compile("strata");

	let lib = llvm.join("lib");

	println!("cargo:rustc-link-search=native={}", lib.display());
	println!("cargo:rustc-link-lib=dylib=LLVM");
	println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib.display());
	println!("cargo:llvm_lib={}", lib.display());
}

fn find_llvm() -> PathBuf {
	for var in ["LLVM_PREFIX", "LLVM_DIR"] {
		if let Some(dir) = env::var_os(var).filter(|dir| !dir.is_empty()) {
			let dir = PathBuf::from(dir);

			if dir.join("include/llvm-c").exists() {
				return dir;
			}

			if let Some(root) = dir
				.ancestors()
				.nth(3)
				.filter(|root| root.join("include/llvm-c").exists())
			{
				return root.to_path_buf();
			}
		}
	}

	if let Ok(output) = Command::new("llvm-config").arg("--prefix").output() {
		let prefix = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());

		if prefix.join("include/llvm-c").exists() {
			return prefix;
		}
	}

	for candidate in [
		"/opt/homebrew/opt/llvm",
		"/usr/local/opt/llvm",
		"/usr/lib/llvm",
		"/usr",
	] {
		let candidate = PathBuf::from(candidate);

		if candidate.join("include/llvm-c").exists() {
			return candidate;
		}
	}

	panic!("could not find an LLVM installation: set LLVM_PREFIX");
}
