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
    RX_LOG_DEBUG = 4,
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

enum RxProbeCaptureLimits
{
    RX_PROBE_FACES = 6,
    RX_PROBE_SH_COEFFS = 9,
    RX_PROBE_SH_FLOATS = 36,
    RX_PROBE_MOMENT_FLOATS = 3072,
    RX_PROBE_DEPTH_SIZE = 16,
};

#define RX_PROBE_DEPTH_MAX_DISTANCE 50.0f

typedef struct RxProbeCapture RxProbeCapture;

int32_t rx_dfg_lut(uint32_t size, uint16_t* out);

RxProbeCapture* rx_probe_capture_new(uint32_t size, const float* inv_projection, const float* face_inv_view_projection);
int32_t rx_probe_capture_project(const RxProbeCapture* capture, const uint16_t* const* colors,
                                 const float* const* depths, float* out_sh, float* out_moments);
void rx_probe_capture_free(RxProbeCapture* capture);
void rx_probe_sky_gradient(const float* sky, const float* ground, float* out_sh);

typedef struct RxGpuInstance RxGpuInstance;
typedef struct RxGpuDevice RxGpuDevice;

typedef void (*RxVoidFn)(void);
typedef RxVoidFn (*RxGetInstanceProcAddr)(void* instance, const char* name);

typedef struct RxGpuInstanceConfig
{
    const char* app_name;
    uint32_t api_version;
    const char* const* extensions;
    size_t extension_count;
    const char* const* layers;
    size_t layer_count;
    uint8_t enumerate_portability;
    uint8_t debug_messenger;
} RxGpuInstanceConfig;

typedef struct RxGpuDeviceInfo
{
    void* physical;
    void* device;
    void* graphics_queue;
    void* present_queue;
    void* transfer_queue;
    uint32_t graphics_family;
    uint32_t present_family;
    uint32_t transfer_family;
    float max_sampler_anisotropy;
    uint8_t supports_cube_arrays;
} RxGpuDeviceInfo;

RxGpuInstance* rx_gpu_instance_create(RxGetInstanceProcAddr get_instance_proc_addr, const RxGpuInstanceConfig* config,
                                      const RxLogSink* log);
void* rx_gpu_instance_handle(const RxGpuInstance* instance);
void rx_gpu_instance_free(RxGpuInstance* instance);

RxGpuDevice* rx_gpu_device_create(const RxGpuInstance* instance, uint64_t surface);
const RxGpuDeviceInfo* rx_gpu_device_info(const RxGpuDevice* device);
int32_t rx_gpu_device_surface_format(const RxGpuDevice* device, int32_t* format, int32_t* color_space);
void rx_gpu_device_wait_idle(const RxGpuDevice* device);
void rx_gpu_device_free(RxGpuDevice* device);

typedef struct RxGpuAllocator RxGpuAllocator;
typedef struct RxGpuAllocation RxGpuAllocation;

typedef enum RxMemoryUsage
{
    RX_MEMORY_AUTO = 0,
    RX_MEMORY_AUTO_PREFER_DEVICE = 1,
    RX_MEMORY_GPU_ONLY = 2,
    RX_MEMORY_CPU_ONLY = 3,
    RX_MEMORY_CPU_TO_GPU = 4,
    RX_MEMORY_GPU_TO_CPU = 5,
} RxMemoryUsage;

RxGpuAllocator* rx_gpu_allocator_create(const RxGpuInstance* instance, const RxGpuDevice* device);
void rx_gpu_allocator_free(RxGpuAllocator* allocator);

void rx_gpu_buffer_destroy(const RxGpuAllocator* allocator, uint64_t buffer, RxGpuAllocation* allocation);
int32_t rx_gpu_allocation_map(const RxGpuAllocator* allocator, RxGpuAllocation* allocation, void** out_mapped);
void rx_gpu_allocation_unmap(const RxGpuAllocator* allocator, RxGpuAllocation* allocation);
int32_t rx_gpu_allocation_flush(const RxGpuAllocator* allocator, const RxGpuAllocation* allocation, uint64_t offset,
                                uint64_t size);
int32_t rx_gpu_allocation_invalidate(const RxGpuAllocator* allocator, const RxGpuAllocation* allocation,
                                     uint64_t offset, uint64_t size);

int32_t rx_gpu_fence_create(const RxGpuDevice* device, uint8_t signaled, uint64_t* out);
int32_t rx_gpu_fence_wait(const RxGpuDevice* device, uint64_t fence, uint64_t timeout);
int32_t rx_gpu_fence_reset(const RxGpuDevice* device, uint64_t fence);
void rx_gpu_fence_destroy(const RxGpuDevice* device, uint64_t fence);
int32_t rx_gpu_semaphore_create(const RxGpuDevice* device, uint8_t timeline, uint64_t* out);
void rx_gpu_semaphore_destroy(const RxGpuDevice* device, uint64_t semaphore);

int32_t rx_gpu_command_pool_create(const RxGpuDevice* device, uint32_t queue_family, uint64_t* out);
void rx_gpu_command_pool_reset(const RxGpuDevice* device, uint64_t pool);
void rx_gpu_command_pool_destroy(const RxGpuDevice* device, uint64_t pool);
int32_t rx_gpu_command_buffer_allocate(const RxGpuDevice* device, uint64_t pool, void** out);
void rx_gpu_command_buffer_free(const RxGpuDevice* device, uint64_t pool, void* buffer);
int32_t rx_gpu_command_buffer_begin(const RxGpuDevice* device, void* buffer);
int32_t rx_gpu_command_buffer_end(const RxGpuDevice* device, void* buffer);
void rx_gpu_command_buffer_reset(const RxGpuDevice* device, void* buffer);

typedef enum RxImageFormat
{
    RX_IMAGE_FORMAT_NONE = 0,
    RX_IMAGE_FORMAT_BGRA8_UNORM = 1,
    RX_IMAGE_FORMAT_BGRA8_SRGB = 2,
    RX_IMAGE_FORMAT_RGBA8_SRGB = 3,
    RX_IMAGE_FORMAT_RGBA8_UNORM = 4,
    RX_IMAGE_FORMAT_RG32_FLOAT = 5,
    RX_IMAGE_FORMAT_RG16_UNORM = 6,
    RX_IMAGE_FORMAT_RGBA16_FLOAT = 7,
    RX_IMAGE_FORMAT_RGB32_FLOAT = 8,
    RX_IMAGE_FORMAT_D16_UNORM_S8_UINT = 9,
    RX_IMAGE_FORMAT_D32_FLOAT = 10,
    RX_IMAGE_FORMAT_D32_FLOAT_S8_UINT = 11,
    RX_IMAGE_FORMAT_R32_SFLOAT = 12,
    RX_IMAGE_FORMAT_R32_SINT = 13,
    RX_IMAGE_FORMAT_R32_UINT = 14,
    RX_IMAGE_FORMAT_R8_UINT = 15,
    RX_IMAGE_FORMAT_R8_UNORM = 16,
} RxImageFormat;

typedef enum RxImageType
{
    RX_IMAGE_TYPE_FLAT = 0,
    RX_IMAGE_TYPE_CUBEMAP = 1,
    RX_IMAGE_TYPE_CUBEMAP_ARRAY = 2,
} RxImageType;

typedef enum RxBufferType
{
    RX_BUFFER_NONE = 0,
    RX_BUFFER_STORAGE = 1,
    RX_BUFFER_STORAGE_WITH_OFFSET = 2,
    RX_BUFFER_UNIFORM = 3,
    RX_BUFFER_UNIFORM_WITH_OFFSET = 4,
    RX_BUFFER_TRANSFER = 5,
    RX_BUFFER_VERTEX = 6,
    RX_BUFFER_INDEX = 7,
} RxBufferType;

enum RxBufferFlags
{
    RX_BUFFER_FLAG_PERSISTENT_MAPPED = 1,
    RX_BUFFER_FLAG_TRANSFER_RECEIVER = 2,
};

typedef struct RxImageDesc
{
    uint32_t image_type;
    uint32_t width;
    uint32_t height;
    uint32_t mips;
    uint32_t format;
    int32_t tiling;
    uint32_t usage;
    uint32_t aspect;
    uint32_t cube_count;
    int32_t initial_layout;
    uint8_t is_target;
} RxImageDesc;

int32_t rx_image_format_to_vk(uint16_t format);
uint32_t rx_image_format_pixel_stride(uint16_t format);
uint32_t rx_image_format_aspect_mask(uint16_t format);
uint32_t rx_image_format_usage(uint16_t format);
uint8_t rx_image_format_is_depth(uint16_t format);
uint8_t rx_image_format_is_stencil(uint16_t format);
uint8_t rx_image_format_is_srgb(uint16_t format);
int32_t rx_image_type_properties(uint32_t image_type, uint32_t cube_count, int32_t* view_type, uint32_t* layer_count);
void rx_image_mip_dimensions(uint32_t width, uint32_t height, uint32_t level, uint32_t* out_width, uint32_t* out_height);

uint32_t rx_buffer_type_usage(uint32_t buffer_type);
int32_t rx_buffer_type_descriptor_type(uint32_t buffer_type);
const char* rx_buffer_type_name(uint32_t buffer_type);

int32_t rx_gpu_buffer_create_typed(const RxGpuAllocator* allocator, uint32_t buffer_type, uint64_t size,
                                   uint32_t memory_usage, uint16_t flags, uint64_t* out_buffer,
                                   RxGpuAllocation** out_allocation, void** out_mapped);
void rx_gpu_cmd_copy_buffer(const RxGpuDevice* device, void* cmd, uint64_t src, uint64_t dst, uint64_t size);

int32_t rx_gpu_image_create_full(const RxGpuDevice* device, const RxGpuAllocator* allocator, const RxImageDesc* desc,
                                 uint64_t* out_image, uint64_t* out_view, RxGpuAllocation** out_allocation);
void rx_gpu_image_destroy_full(const RxGpuDevice* device, const RxGpuAllocator* allocator, uint64_t image,
                               uint64_t view, RxGpuAllocation* allocation);
int32_t rx_gpu_cmd_copy_buffer_to_mips(const RxGpuDevice* device, void* cmd, uint64_t buffer, uint64_t image,
                                       uint16_t format, uint32_t width, uint32_t height, uint32_t mips,
                                       uint32_t aspect);
void rx_gpu_cmd_copy_buffer_to_region(const RxGpuDevice* device, void* cmd, uint64_t buffer, uint64_t image,
                                      uint32_t mip, uint32_t width, uint32_t height, int32_t offset_x,
                                      int32_t offset_y);

typedef struct RxSamplerCache RxSamplerCache;

typedef struct RxSamplerProps
{
    uint8_t min_filter;
    uint8_t mag_filter;
    uint8_t mip_filter;
    uint8_t address_mode;
    uint8_t border_color;
    uint8_t compare_op;
    uint8_t max_anisotropy;
    float min_lod;
    float max_lod;
} RxSamplerProps;

void rx_gpu_cmd_image_layout_transition(const RxGpuDevice* device, void* cmd, uint64_t image, uint32_t aspect,
                                        int32_t old_layout, int32_t new_layout, uint32_t base_mip, uint32_t levels,
                                        uint32_t cmd_queue_family);
int32_t rx_gpu_cmd_image_transfer_release(const RxGpuDevice* device, void* cmd, uint64_t image, uint32_t aspect,
                                          uint32_t mips);
int32_t rx_gpu_cmd_image_graphics_acquire(const RxGpuDevice* device, void* cmd, uint64_t image, uint32_t aspect,
                                          uint32_t mips);
void rx_gpu_cmd_buffer_compute_to_fragment(const RxGpuDevice* device, void* cmd, uint64_t buffer, uint64_t size);
void rx_gpu_cmd_buffer_fragment_to_compute(const RxGpuDevice* device, void* cmd, uint64_t buffer, uint64_t size);

RxSamplerCache* rx_gpu_sampler_cache_create(void);
const void* rx_gpu_sampler_cache_request(const RxSamplerCache* cache, const RxGpuDevice* device,
                                         const RxSamplerProps* props);
void rx_gpu_sampler_cache_free(RxSamplerCache* cache, const RxGpuDevice* device);

typedef struct RxSwapchainResult
{
    uint64_t handle;
    uint16_t format;
    int32_t color_space;
} RxSwapchainResult;

int32_t rx_gpu_swapchain_create(const RxGpuDevice* device, uint64_t surface, uint32_t width, uint32_t height,
                                uint64_t old, RxSwapchainResult* out);
uint32_t rx_gpu_swapchain_images(const RxGpuDevice* device, uint64_t swapchain, uint64_t* out, uint32_t capacity);
void rx_gpu_swapchain_destroy(const RxGpuDevice* device, uint64_t swapchain);
int32_t rx_gpu_swapchain_acquire(const RxGpuDevice* device, uint64_t swapchain, uint64_t timeout, uint64_t semaphore,
                                 uint32_t* out_index);
int32_t rx_gpu_swapchain_present(const RxGpuDevice* device, void* queue, uint64_t swapchain, uint64_t wait_semaphore,
                                 uint32_t image_index);
int32_t rx_gpu_color_view_create(const RxGpuDevice* device, uint64_t image, uint16_t format, uint64_t* out);
void rx_gpu_view_destroy(const RxGpuDevice* device, uint64_t view);

enum RxDescriptorKind
{
    RX_DESCRIPTOR_IMAGE = 1,
    RX_DESCRIPTOR_BUFFER = 2,
};

typedef struct RxDescriptorPoolSize
{
    int32_t descriptor_type;
    uint32_t count;
} RxDescriptorPoolSize;

typedef struct RxDescriptorWrite
{
    uint32_t binding;
    uint32_t kind;
    uint64_t sampler;
    uint64_t view;
    uint64_t buffer;
    uint64_t offset;
    uint64_t range;
    uint32_t buffer_type;
} RxDescriptorWrite;

int32_t rx_gpu_descriptor_pool_create(const RxGpuDevice* device, const RxDescriptorPoolSize* sizes, size_t count,
                                      uint32_t max_sets, uint8_t free_sets, uint64_t* out);
void rx_gpu_descriptor_pool_destroy(const RxGpuDevice* device, uint64_t pool);
int32_t rx_gpu_descriptor_set_allocate(const RxGpuDevice* device, uint64_t pool, uint64_t layout, uint64_t* out);
void rx_gpu_descriptor_set_free(const RxGpuDevice* device, uint64_t pool, uint64_t set);
int32_t rx_gpu_descriptor_set_update(const RxGpuDevice* device, uint64_t set, const RxDescriptorWrite* writes,
                                     size_t count);
void rx_gpu_cmd_bind_descriptor_sets(const RxGpuDevice* device, void* cmd, int32_t bind_point, uint64_t layout,
                                     uint32_t first_set, const uint64_t* sets, size_t set_count,
                                     const uint32_t* offsets, size_t offset_count);

typedef struct RxDsLayoutCache RxDsLayoutCache;

typedef struct RxDsLayoutEntry
{
    uint32_t binding;
    int32_t descriptor_type;
    uint32_t stages;
    uint32_t count;
} RxDsLayoutEntry;

int32_t rx_gpu_render_pass_create(const RxGpuDevice* device, const void* descriptions, const uint8_t* is_depth,
                                  size_t count, uint64_t* out);
void rx_gpu_render_pass_destroy(const RxGpuDevice* device, uint64_t pass);
void rx_gpu_cmd_begin_render_pass(const RxGpuDevice* device, void* cmd, uint64_t pass, uint64_t framebuffer, int32_t x,
                                  int32_t y, uint32_t width, uint32_t height, const void* clear_values,
                                  size_t clear_count);
void rx_gpu_cmd_end_render_pass(const RxGpuDevice* device, void* cmd);
int32_t rx_gpu_framebuffer_create(const RxGpuDevice* device, uint64_t pass, const uint64_t* views, size_t count,
                                  uint32_t width, uint32_t height, uint64_t* out);
void rx_gpu_framebuffer_destroy(const RxGpuDevice* device, uint64_t framebuffer);

int32_t rx_gpu_ds_layout_create(const RxGpuDevice* device, const RxDsLayoutEntry* entries, size_t count,
                                uint64_t* out);
uint32_t rx_gpu_ds_layout_id(const RxDsLayoutEntry* entries, size_t count);
RxDsLayoutCache* rx_gpu_ds_layout_cache_create(void);
int32_t rx_gpu_ds_layout_cache_request(const RxDsLayoutCache* cache, const RxGpuDevice* device,
                                       const RxDsLayoutEntry* entries, size_t count, uint32_t* out_id,
                                       uint64_t* out_layout);
uint64_t rx_gpu_ds_layout_cache_get(const RxDsLayoutCache* cache, uint32_t id);
void rx_gpu_ds_layout_cache_free(const RxDsLayoutCache* cache, const RxGpuDevice* device, uint32_t id);
void rx_gpu_ds_layout_cache_destroy(RxDsLayoutCache* cache, const RxGpuDevice* device);

typedef struct RxPushConstantDef
{
    uint32_t size;
    uint32_t stages;
} RxPushConstantDef;

typedef struct RxGraphicsPipelineDesc
{
    const uint32_t* stage_flags;
    const uint64_t* stage_modules;
    size_t stage_count;
    const void* vertex_binding;
    const void* vertex_attributes;
    size_t vertex_attribute_count;
    const void* color_blend;
    size_t color_blend_count;
    const int32_t* attachment_formats;
    size_t attachment_count;
    uint32_t cull_mode;
    int32_t front_face;
    int32_t polygon_mode;
    int32_t depth_compare_op;
    uint8_t render_lines;
    uint8_t disable_depth_test;
    uint8_t disable_depth_write;
    uint64_t layout;
    uint64_t render_pass;
} RxGraphicsPipelineDesc;

typedef struct RxShaderMacroRef
{
    const char* name;
    const char* value;
} RxShaderMacroRef;

int32_t rx_gpu_pipeline_layout_create(const RxGpuDevice* device, const uint64_t* set_layouts, size_t set_count,
                                      const RxPushConstantDef* defs, size_t def_count, uint64_t* out);
void rx_gpu_pipeline_layout_destroy(const RxGpuDevice* device, uint64_t layout);
int32_t rx_gpu_shader_module_create(const RxGpuDevice* device, const uint32_t* code, size_t word_count, uint64_t* out);
void rx_gpu_shader_module_destroy(const RxGpuDevice* device, uint64_t module);
int32_t rx_gpu_graphics_pipeline_create(const RxGpuDevice* device, const RxGraphicsPipelineDesc* desc, uint64_t* out);
int32_t rx_gpu_compute_pipeline_create(const RxGpuDevice* device, uint64_t module, uint64_t layout, uint64_t* out);
void rx_gpu_pipeline_destroy(const RxGpuDevice* device, uint64_t pipeline);
void rx_gpu_cmd_bind_pipeline(const RxGpuDevice* device, void* cmd, int32_t bind_point, uint64_t pipeline);
void rx_gpu_cmd_set_cull_mode(const RxGpuDevice* device, void* cmd, uint32_t mode);

uint64_t rx_shader_hash_macros(const RxShaderMacroRef* macros, size_t count, uint64_t seed);
uint64_t rx_shader_id(uint32_t kind, const RxShaderMacroRef* macros, size_t count);
uint32_t rx_spirv_input_location_mask(const uint32_t* words, size_t count);

#ifdef __cplusplus
}
#endif
