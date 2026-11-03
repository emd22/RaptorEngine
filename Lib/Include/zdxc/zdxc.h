/*
 * zdxc: an HLSL 2021 to DXIL and SPIR-V compiler.
 *
 * The C API. Link libzdxc statically (zig build -Dlinkage=static, the
 * default) or dynamically (-Dlinkage=dynamic); it needs nothing besides
 * the platform's C library. See docs/c-api.md.
 *
 * A compilation takes a dxc command line and returns its outputs in
 * memory:
 *
 *     const char *args[] = {"-T", "ps_6_0", "-E", "PS"};
 *     zdxc_compile_info info = {
 *         .size = sizeof info,
 *         .source = text, .source_size = text_size,
 *         .source_name = "mesh.hlsl",
 *         .args = args, .arg_count = 4,
 *     };
 *     zdxc_result *r = zdxc_compile(&info);
 *     if (!r) { ... out of memory ... }
 *     size_t size;
 *     const char *diags = zdxc_result_output(r, ZDXC_OUTPUT_DIAGNOSTICS, &size);
 *     if (zdxc_result_status(r) == ZDXC_STATUS_OK) {
 *         const void *dxil = zdxc_result_output(r, ZDXC_OUTPUT_OBJECT, &size);
 *         ...
 *     }
 *     zdxc_result_free(r);
 *
 * zdxc never writes files: -Fo, -Fi and -Frs only name the outputs. It
 * keeps no global state, so any number of threads may compile at once.
 */
#ifndef ZDXC_H
#define ZDXC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZDXC_API_VERSION 1

typedef struct zdxc_result zdxc_result;

/* A compilation's outcome. The values are dxc's exit codes. */
typedef enum zdxc_status {
    ZDXC_STATUS_OK = 0,
    /* Bad options, or an input that cannot be read. */
    ZDXC_STATUS_FAILED = 1,
    /* Errors in the source. */
    ZDXC_STATUS_COMPILE_FAILED = 5
} zdxc_status;

typedef enum zdxc_output {
    /* The DXIL container or SPIR-V module. */
    ZDXC_OUTPUT_OBJECT = 0,
    /* Errors, warnings and notes, as the command line prints them. Present
     * after a failure and, with warnings, after a success. */
    ZDXC_OUTPUT_DIAGNOSTICS = 1,
    /* The preprocessed source (-P). */
    ZDXC_OUTPUT_PREPROCESSED = 2,
    /* A container holding the root signature (-Frs). */
    ZDXC_OUTPUT_ROOT_SIGNATURE = 3,
    /* Other text the command line prints (-help, --version). */
    ZDXC_OUTPUT_TEXT = 4
} zdxc_output;

/*
 * Reads a file: the input when no source text is given, and every
 * #include candidate (the including file's directory, then each -I
 * directory). On success, set *data and *size and return 0; the memory
 * must stay valid until zdxc_compile returns. Return nonzero if the file
 * does not exist, and zdxc tries the next candidate.
 */
typedef int (*zdxc_read_file_fn)(void *user, const char *path, const char **data, size_t *size);

typedef struct zdxc_compile_info {
    /* sizeof(zdxc_compile_info): lets later versions add fields. */
    uint32_t size;
    /* dxc command-line options, without the program name. A positional
     * argument names the input file. */
    const char *const *args;
    size_t arg_count;
    /* The main file's text (a trailing NUL is allowed), or NULL to read the
     * input file named in args with read_file. */
    const char *source;
    size_t source_size;
    /* The main file's name in diagnostics and for #include "...". NULL
     * means the input file in args, else "hlsl.hlsl". */
    const char *source_name;
    /* Reads files; NULL reads them from disk relative to the current
     * directory. */
    zdxc_read_file_fn read_file;
    void *user;
} zdxc_compile_info;

/* Compiles. Returns NULL only when out of memory (or for a NULL or too
 * small info); every other failure is in the result. */
zdxc_result *zdxc_compile(const zdxc_compile_info *info);

zdxc_status zdxc_result_status(const zdxc_result *result);

/* One output: its bytes, followed by a NUL that *size does not count, or
 * NULL (and *size 0) if the compilation did not produce it. size may be
 * NULL. Valid until zdxc_result_free. */
const void *zdxc_result_output(const zdxc_result *result, zdxc_output output, size_t *size);

/* The file name the options give an output (-Fo, -Fi, -Frs), or NULL. */
const char *zdxc_result_output_name(const zdxc_result *result, zdxc_output output);

void zdxc_result_free(zdxc_result *result);

/* zdxc's version, such as "0.1.0". */
const char *zdxc_version(void);

#ifdef __cplusplus
}
#endif

#endif /* ZDXC_H */
