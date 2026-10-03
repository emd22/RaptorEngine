#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RxLogLevel
{
    RX_LOG_PRINT = 0,
    RX_LOG_INFO = 1,
    RX_LOG_WARNING = 2,
    RX_LOG_ERROR = 3,
};

enum RxValueKind
{
    RX_KIND_NONE = 0,
    RX_KIND_INT = 1,
    RX_KIND_FLOAT = 2,
    RX_KIND_STRING = 3,
    RX_KIND_STRUCT = 4,
};

typedef struct RxHost
{
    void* user;
    int32_t (*read_include)(void* user, const char* path, const char* extension, uint8_t** data, size_t* length);
    void (*release_include)(void* user, uint8_t* data);
    void (*log)(void* user, int32_t level, int32_t category, const char* message, size_t length);
} RxHost;

typedef struct RxPrimitive
{
    int64_t int_value;
    const char* string_value;
    size_t string_length;
    float float_value;
    uint8_t kind;
} RxPrimitive;

typedef struct RxEntry
{
    const char* name;
    size_t name_length;
    RxPrimitive value;
    const struct RxEntry* members;
    size_t member_count;
    const RxPrimitive* array;
    size_t array_count;
    uint8_t is_array;
    uint8_t is_dot_reference;
} RxEntry;

typedef struct RxConfig RxConfig;
typedef struct RxText RxText;

RxConfig* rx_config_parse(const uint8_t* data, size_t length, const char* prelude_path, const char* include_extension,
                          const RxHost* host);
int32_t rx_config_has_errors(const RxConfig* config);
size_t rx_config_entry_count(const RxConfig* config);
const RxEntry* rx_config_entries(const RxConfig* config);
void rx_config_free(RxConfig* config);

typedef struct RxLogSink
{
    void* user;
    void (*log)(void* user, int32_t level, int32_t category, const char* message, size_t length);
} RxLogSink;

RxText* rx_config_format_file(const RxEntry* entries, size_t count, const RxLogSink* log);
RxText* rx_config_format_entry(const RxEntry* entry, uint32_t indent, const RxLogSink* log);
RxText* rx_config_format_primitive(const RxPrimitive* primitive, const RxLogSink* log);
const char* rx_text_data(const RxText* text, size_t* length);
void rx_text_free(RxText* text);

typedef struct RxShaderMacro
{
    const char* name;
    const char* value;
} RxShaderMacro;

typedef struct RxReflectionEntry
{
    uint16_t type;
    uint8_t set;
    uint8_t binding;
} RxReflectionEntry;

typedef struct RxPreprocResult RxPreprocResult;

enum RxShaderStage
{
    RX_STAGE_VERTEX = 0,
    RX_STAGE_PIXEL = 1,
    RX_STAGE_COMPUTE = 2,
};

RxPreprocResult* rx_preproc_process(const uint8_t* data, size_t length, const RxShaderMacro* macros, size_t macro_count,
                                    const RxLogSink* log);
const uint8_t* rx_preproc_program(const RxPreprocResult* result, uint32_t stage, size_t* length);
const RxReflectionEntry* rx_preproc_reflection(const RxPreprocResult* result, uint32_t stage, size_t* count);
void rx_preproc_free(RxPreprocResult* result);

typedef struct RxFaceOptions
{
    float scale;
    float uv_min[2];
    float uv_max[2];
} RxFaceOptions;

typedef struct RxCubeOptions
{
    RxFaceOptions left;
    RxFaceOptions right;
    RxFaceOptions top;
    RxFaceOptions bottom;
    RxFaceOptions front;
    RxFaceOptions back;
    uint8_t align_uvs;
} RxCubeOptions;

typedef struct RxMesh RxMesh;

RxMesh* rx_mesh_icosphere(int32_t resolution);
RxMesh* rx_mesh_cube(const RxCubeOptions* options);
RxMesh* rx_mesh_wireframe_box(void);
RxMesh* rx_mesh_line(void);
RxMesh* rx_mesh_quad(float scale_x, float scale_y);
const float* rx_mesh_positions(const RxMesh* mesh, size_t* count);
const float* rx_mesh_normals(const RxMesh* mesh, size_t* count);
const float* rx_mesh_tangents(const RxMesh* mesh, size_t* count);
const float* rx_mesh_texcoords(const RxMesh* mesh, size_t* count);
const uint32_t* rx_mesh_indices(const RxMesh* mesh, size_t* count);
void rx_mesh_free(RxMesh* mesh);

typedef struct RxBrushPlane
{
    float normal[3];
    float distance;
    float offset[2];
    float scale[2];
    float rotation;
} RxBrushPlane;

typedef struct RxBrushFace
{
    uint32_t plane_index;
    const float* vertices;
    size_t vertex_count;
} RxBrushFace;

typedef struct RxBrushView
{
    const RxBrushPlane* planes;
    size_t plane_count;
    const RxBrushFace* faces;
    size_t face_count;
    const float* vertices;
    size_t vertex_count;
    float bounds_min[3];
    float bounds_max[3];
} RxBrushView;

typedef struct RxBrushResult RxBrushResult;
typedef struct RxBrushSplit RxBrushSplit;

RxBrushResult* rx_brush_from_box(const float* min, const float* max);
RxBrushResult* rx_brush_rebuild(const RxBrushPlane* planes, size_t count);
int32_t rx_brush_result_ok(const RxBrushResult* result);
const RxBrushView* rx_brush_result_view(const RxBrushResult* result);
void rx_brush_result_free(RxBrushResult* result);

int32_t rx_brush_is_box(const RxBrushView* view);
int32_t rx_brush_contains_point(const RxBrushView* view, const float* point, float tolerance);
int32_t rx_brush_find_plane(const RxBrushView* view, const float* normal);
float rx_brush_support(const RxBrushView* view, const float* direction);
void rx_brush_face_center(const RxBrushView* view, uint32_t plane_index, float* out);
int32_t rx_brush_raycast(const RxBrushView* view, const float* origin, const float* direction, float* out_distance,
                         uint32_t* out_plane_index);
int32_t rx_brush_has_default_textures(const RxBrushView* view);
void rx_brush_default_texture_offset(const RxBrushView* view, uint32_t plane_index, float* out_offset);
void rx_brush_world_aligned_offset(const float* normal, const float* origin, float* out_offset);
RxBrushSplit* rx_brush_split(const RxBrushView* view, const float* normal, float distance, const float* origin);
const RxBrushPlane* rx_brush_split_planes(const RxBrushSplit* split, int32_t front, size_t* count);
void rx_brush_split_free(RxBrushSplit* split);
RxMesh* rx_brush_generate_mesh(const RxBrushView* view);

#ifdef __cplusplus
}
#endif
