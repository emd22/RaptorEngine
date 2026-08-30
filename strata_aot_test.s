	.build_version macos, 27, 0
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_forty_two
	.p2align	2
_forty_two:
	.cfi_startproc
	mov	w0, #42
	ret
	.cfi_endproc

.subsections_via_symbols
