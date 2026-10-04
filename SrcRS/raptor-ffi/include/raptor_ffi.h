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

typedef struct RxBuffer
{
    uint64_t buffer;
    uint64_t size;
    void* mapped;
    uint32_t buffer_type;
    uint16_t flags;
} RxBuffer;

RxBuffer* rx_buffer_create(const RxGpuAllocator* allocator, uint32_t buffer_type, uint64_t size,
                           uint32_t memory_usage, uint16_t flags, int32_t* out_status);
void rx_buffer_destroy(RxBuffer* buffer, const RxGpuAllocator* allocator);
int32_t rx_buffer_map(RxBuffer* buffer, const RxGpuAllocator* allocator);
void rx_buffer_unmap(RxBuffer* buffer, const RxGpuAllocator* allocator);
int32_t rx_buffer_upload(RxBuffer* buffer, const RxGpuAllocator* allocator, const void* data, uint64_t size);
int32_t rx_buffer_flush(const RxBuffer* buffer, const RxGpuAllocator* allocator, uint64_t offset, uint64_t size);
int32_t rx_buffer_invalidate(const RxBuffer* buffer, const RxGpuAllocator* allocator);
void rx_gpu_cmd_copy_buffer(const RxGpuDevice* device, void* cmd, uint64_t src, uint64_t dst, uint64_t size);

typedef struct RxImage
{
    uint64_t image;
    uint64_t view;
    int32_t layout;
    uint32_t aspect;
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    uint32_t mip_level;
    uint16_t format;
    uint16_t image_type;
} RxImage;

RxImage* rx_image_new(void);
void rx_image_retain(RxImage* image);
void rx_image_release(RxImage* image, const RxGpuDevice* device, const RxGpuAllocator* allocator);
int32_t rx_image_create(RxImage* image, const RxGpuDevice* device, const RxGpuAllocator* allocator,
                        const RxImageDesc* desc);
void rx_image_wrap_external(RxImage* image, uint64_t vk_image, uint32_t width, uint32_t height, uint16_t format);
int32_t rx_image_recreate_color_view(RxImage* image, const RxGpuDevice* device);
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
int32_t rx_gpu_swapchain_present(const RxGpuDevice* device, uint64_t swapchain, uint64_t wait_semaphore,
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

typedef struct RxDescriptorPool
{
    uint64_t pool;
    uint32_t set_capacity;
    uint32_t sets_used;
} RxDescriptorPool;

typedef struct RxDescriptorIdEntry
{
    uint32_t binding;
    uint32_t kind;
    uint64_t handle;
} RxDescriptorIdEntry;

RxDescriptorPool* rx_descriptor_pool_new(const RxGpuDevice* device, const RxDescriptorPoolSize* sizes, size_t count,
                                         uint32_t max_sets, uint8_t free_sets, int32_t* out_status);
int32_t rx_descriptor_pool_recreate(RxDescriptorPool* pool, const RxGpuDevice* device);
void rx_descriptor_pool_destroy(RxDescriptorPool* pool, const RxGpuDevice* device);
int32_t rx_descriptor_pool_allocate_set(RxDescriptorPool* pool, const RxGpuDevice* device, uint64_t layout,
                                        uint64_t* out);
void rx_descriptor_pool_free_set(const RxDescriptorPool* pool, const RxGpuDevice* device, uint64_t set);
uint32_t rx_descriptor_id(const RxDescriptorIdEntry* entries, size_t count);
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

typedef struct RxPipelineLayout
{
    uint64_t layout;
} RxPipelineLayout;

typedef struct RxPipeline
{
    uint64_t pipeline;
    int32_t bind_point;
    uint32_t default_cull_mode;
    uint8_t is_compute;
} RxPipeline;

RxPipelineLayout* rx_pipeline_layout_new(const RxGpuDevice* device, const uint64_t* set_layouts, size_t set_count,
                                         const RxPushConstantDef* defs, size_t def_count, int32_t* out_status);
void rx_pipeline_layout_retain(RxPipelineLayout* layout);
void rx_pipeline_layout_release(RxPipelineLayout* layout, const RxGpuDevice* device);
typedef struct RxShaderProgram
{
    uint64_t module;
    const RxReflectionEntry* reflection;
    size_t reflection_count;
    void* user;
    uint32_t shader_type;
    uint32_t input_location_mask;
} RxShaderProgram;

RxShaderProgram* rx_shader_program_new(const RxGpuDevice* device, const uint32_t* code, size_t word_count,
                                       const RxReflectionEntry* reflection, size_t reflection_count,
                                       uint32_t shader_type, void* user, int32_t* out_status);
void rx_shader_program_retain(RxShaderProgram* program);
void rx_shader_program_release(RxShaderProgram* program, const RxGpuDevice* device);
RxPipeline* rx_pipeline_create_graphics(const RxGpuDevice* device, const RxGraphicsPipelineDesc* desc,
                                        int32_t* out_status);
RxPipeline* rx_pipeline_create_compute(const RxGpuDevice* device, uint64_t module, uint64_t layout,
                                       int32_t* out_status);
void rx_pipeline_destroy(RxPipeline* pipeline, const RxGpuDevice* device);
void rx_gpu_cmd_bind_pipeline(const RxGpuDevice* device, void* cmd, int32_t bind_point, uint64_t pipeline);
void rx_gpu_cmd_set_cull_mode(const RxGpuDevice* device, void* cmd, uint32_t mode);

uint64_t rx_shader_hash_macros(const RxShaderMacroRef* macros, size_t count, uint64_t seed);
uint64_t rx_shader_id(uint32_t kind, const RxShaderMacroRef* macros, size_t count);
uint32_t rx_spirv_input_location_mask(const uint32_t* words, size_t count);

enum RxVertexLayout
{
    RX_VERTEX_SLIM_SIZE = 12,
    RX_VERTEX_DEFAULT_SIZE = 48,
    RX_VERTEX_SKINNED_SIZE = 80,
    RX_VERTEX_NORMAL_OFFSET = 12,
    RX_VERTEX_UV_OFFSET = 24,
    RX_VERTEX_TANGENT_OFFSET = 32,
    RX_VERTEX_BONE_IDS_OFFSET = 48,
    RX_VERTEX_BONE_WEIGHTS_OFFSET = 64,
};

typedef struct RxBlendAttachment
{
    uint8_t enabled;
    uint32_t write_mask;
    int32_t color_op;
    int32_t alpha_op;
    int32_t src_color;
    int32_t dst_color;
    int32_t src_alpha;
    int32_t dst_alpha;
    uint32_t target_index;
} RxBlendAttachment;

size_t rx_vertex_description(uint32_t vertex_type, void* out_binding, void* out_attributes, size_t capacity);
int32_t rx_blend_states(const RxBlendAttachment* attachments, size_t attachment_count, uint32_t count, void* out);

enum RxShaderType
{
    RX_SHADER_VERTEX = 1,
    RX_SHADER_PIXEL = 2,
    RX_SHADER_COMPUTE = 4,
};

uint32_t rx_shader_stage_flags(uint32_t bits);
int32_t rx_shader_bind_point(uint32_t bits);
const char* rx_shader_type_name(uint32_t bits);
int32_t rx_reflection_descriptor_type(uint16_t type);
uint8_t rx_reflection_requires_offset(uint16_t type);
const char* rx_reflection_name(uint16_t type);

size_t rx_vk_result_name(int32_t result, char* buffer, size_t capacity);
int32_t rx_gpu_set_object_name(const RxGpuDevice* device, int32_t object_type, uint64_t handle, const char* name);

typedef struct RxPipelineRegistry RxPipelineRegistry;

typedef struct RxPipelineKey
{
    uint64_t macro_hash;
    uint64_t blend_hash;
    uint64_t pass_hash;
    uint64_t layout_hash;
    uint32_t shader;
    uint32_t vertex_type;
    uint32_t cull_mode;
    int32_t winding_order;
    int32_t polygon_mode;
    int32_t depth_compare_op;
    uint8_t is_compute;
    uint8_t has_vertex_input;
    uint8_t depth_test;
    uint8_t depth_write;
    uint8_t render_lines;
    uint8_t reserved[3];
} RxPipelineKey;

enum RxKeyRegistration
{
    RX_KEY_REGISTERED = 0,
    RX_KEY_IDENTICAL = 1,
    RX_KEY_COLLISION = 2,
};

typedef struct RxHashPair
{
    uint32_t first;
    uint32_t second;
} RxHashPair;

enum RxDescriptorEntryKind
{
    RX_ENTRY_NONE = 0,
    RX_ENTRY_IMAGE = 1,
    RX_ENTRY_BUFFER = 2,
};

typedef struct RxDescriptorSlot
{
    uint32_t set;
    uint32_t binding;
    uint32_t kind;
} RxDescriptorSlot;

typedef struct RxAttachmentInfo
{
    int32_t format;
    uint32_t samples;
    int32_t load_op;
    int32_t store_op;
    int32_t stencil_load_op;
    int32_t stencil_store_op;
    int32_t initial_layout;
    int32_t final_layout;
} RxAttachmentInfo;

typedef struct RxClearTarget
{
    uint32_t aspect;
    int32_t load_op;
    uint8_t render_pass_only;
} RxClearTarget;

#define RX_INVALID_INDEX 0xFFFFFFFFu

RxPipelineRegistry* rx_pipeline_registry_create(uint32_t num_static, uint32_t max_dynamic);
void rx_pipeline_registry_free(RxPipelineRegistry* registry);
uint32_t rx_pipeline_registry_allocate(const RxPipelineRegistry* registry);
uint8_t rx_pipeline_registry_has_pending(const RxPipelineRegistry* registry);
size_t rx_pipeline_registry_take_pending(const RxPipelineRegistry* registry, uint32_t* out, size_t capacity);
int32_t rx_pipeline_registry_register_key(const RxPipelineRegistry* registry, uint32_t handle, const RxPipelineKey* key,
                                          uint32_t* other, uint64_t* hash);
uint32_t rx_pipeline_registry_find(const RxPipelineRegistry* registry, const RxPipelineKey* key);
int32_t rx_pipeline_registry_get_key(const RxPipelineRegistry* registry, uint32_t handle, RxPipelineKey* out);
void rx_pipeline_registry_register_variant(const RxPipelineRegistry* registry, uint32_t pass, uint32_t features,
                                           uint32_t handle);
uint32_t rx_pipeline_registry_find_variant(const RxPipelineRegistry* registry, uint32_t pass, uint32_t features);
int32_t rx_pipeline_registry_variant_info(const RxPipelineRegistry* registry, uint32_t handle, uint32_t* out_pass,
                                          uint32_t* out_features);
int32_t rx_pipeline_registry_pass_pipeline(const RxPipelineRegistry* registry, uint32_t pass, size_t index,
                                           uint32_t* out);

uint64_t rx_pipeline_key_hash(const RxPipelineKey* key);
uint64_t rx_pipeline_hash_init(void);
uint64_t rx_pipeline_blend_hash(const void* states, size_t count);
uint64_t rx_pipeline_pass_hash(const RxHashPair* pairs, size_t count);
uint64_t rx_pipeline_layout_hash(const RxHashPair* sets, size_t set_count, const RxHashPair* push_constants,
                                 size_t push_count);

size_t rx_vertex_filter_attributes(void* attributes, size_t count, uint32_t mask);
int32_t rx_check_descriptors(const RxReflectionEntry* reflection, size_t reflection_count,
                             const RxDescriptorSlot* slots, size_t slot_count, RxDescriptorSlot* out_missing);

void rx_attachment_description(const RxAttachmentInfo* info, void* out);
size_t rx_clear_values(const RxClearTarget* targets, size_t count, void* out, size_t capacity);
int32_t rx_find_format_index(const uint16_t* formats, size_t count, uint16_t format, int32_t sub_index);
uint8_t rx_formats_compatible(const uint16_t* a, size_t a_count, const uint16_t* b, size_t b_count);
void rx_gpu_cmd_set_viewport_scissor(const RxGpuDevice* device, void* cmd, int32_t x, int32_t y, uint32_t width,
                                     uint32_t height);


typedef struct RxSubmitWait
{
    uint64_t semaphore;
    uint32_t stages;
    uint64_t value;
} RxSubmitWait;

typedef struct RxSubmitSignal
{
    uint64_t semaphore;
    uint64_t value;
} RxSubmitSignal;

enum RxQueue
{
    RX_QUEUE_GRAPHICS = 0,
    RX_QUEUE_PRESENT = 1,
    RX_QUEUE_TRANSFER = 2,
};

int32_t rx_gpu_queue_submit(const RxGpuDevice* device, uint32_t queue, const RxSubmitWait* waits, size_t wait_count,
                            void* const* commands, size_t command_count, const RxSubmitSignal* signals,
                            size_t signal_count, uint64_t fence);
int32_t rx_gpu_queue_wait_idle(const RxGpuDevice* device, uint32_t queue);

typedef struct RxFrameLoop
{
    uint32_t frame_number;
    uint32_t elapsed;
    uint32_t image_index;
} RxFrameLoop;

RxFrameLoop* rx_frame_loop_new(const RxGpuDevice* device, uint32_t frames_in_flight, uint32_t image_count,
                               int32_t* out_status);
int32_t rx_frame_loop_begin(const RxFrameLoop* frame_loop, const RxGpuDevice* device);
int32_t rx_frame_loop_acquire(const RxFrameLoop* frame_loop, const RxGpuDevice* device, uint64_t swapchain);
int32_t rx_frame_loop_submit_and_present(const RxFrameLoop* frame_loop, const RxGpuDevice* device,
                                         uint64_t swapchain, void* commands, uint64_t transfer,
                                         uint64_t transfer_value, int32_t* out_present);
void rx_frame_loop_end_frame(const RxFrameLoop* frame_loop);
void rx_frame_loop_destroy(RxFrameLoop* frame_loop, const RxGpuDevice* device);

#ifdef __cplusplus
}
#endif
