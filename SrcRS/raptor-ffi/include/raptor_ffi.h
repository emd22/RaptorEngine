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

RxBuffer* rx_buffer_new(void);
void rx_buffer_retain(RxBuffer* buffer);
void rx_buffer_release(RxBuffer* buffer, const RxGpuAllocator* allocator);
int32_t rx_buffer_create(RxBuffer* buffer, const RxGpuAllocator* allocator, uint32_t buffer_type, uint64_t size,
                         uint32_t memory_usage, uint16_t flags);
typedef struct RxBufferResource RxBufferResource;
RxBufferResource* rx_buffer_detach(RxBuffer* buffer);
void rx_buffer_resource_destroy(RxBufferResource* resource, const RxGpuAllocator* allocator);
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

typedef struct RxDescriptorEntry
{
    uint32_t binding;
    uint32_t stages;
    uint32_t kind;
    uint64_t sampler;
    const RxImage* image;
    const RxBuffer* buffer;
    uint64_t offset;
    uint64_t range;
} RxDescriptorEntry;

typedef struct RxDescriptorSet
{
    uint64_t set;
    uint32_t id;
    uint32_t layout_id;
    uint32_t buffer_count;
    uint8_t has_dynamic_offsets;
    uint8_t built;
} RxDescriptorSet;

typedef struct RxDescriptorCache RxDescriptorCache;


typedef struct RxDsLayoutCache RxDsLayoutCache;

typedef struct RxDsLayoutEntry
{
    uint32_t binding;
    int32_t descriptor_type;
    uint32_t stages;
    uint32_t count;
} RxDsLayoutEntry;


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
    const char* name;
    uint32_t shader_type;
    uint32_t input_location_mask;
} RxShaderProgram;

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

#define RX_INVALID_INDEX 0xFFFFFFFFu

uint64_t rx_pipeline_hash_init(void);
uint64_t rx_pipeline_blend_hash(const void* states, size_t count);
uint64_t rx_pipeline_pass_hash(const RxHashPair* pairs, size_t count);
uint64_t rx_pipeline_layout_hash(const RxHashPair* sets, size_t set_count, const RxHashPair* push_constants,
                                 size_t push_count);

size_t rx_vertex_filter_attributes(void* attributes, size_t count, uint32_t mask);
int32_t rx_check_descriptors(const RxReflectionEntry* reflection, size_t reflection_count,
                             const RxDescriptorSlot* slots, size_t slot_count, RxDescriptorSlot* out_missing);



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

typedef struct RxRenderStage
{
    uint64_t render_pass;
    uint32_t width;
    uint32_t height;
    uint32_t offset_x;
    uint32_t offset_y;
} RxRenderStage;

typedef struct RxTargetConfig
{
    uint16_t format;
    uint16_t image_type;
    uint32_t usage;
    uint32_t aspect;
    int32_t samples;
    int32_t load_op;
    int32_t store_op;
    int32_t stencil_load_op;
    int32_t stencil_store_op;
    int32_t initial_layout;
    int32_t final_layout;
    uint32_t width;
    uint32_t height;
    uint8_t render_pass_only;
} RxTargetConfig;

#define RX_NO_TARGET UINT32_MAX

RxRenderStage* rx_render_stage_new(void);
void rx_render_stage_destroy(RxRenderStage* stage, const RxGpuDevice* device, const RxGpuAllocator* allocator);
uint32_t rx_render_stage_add_target(RxRenderStage* stage, const RxTargetConfig* config, const RxImage* reference);
uint32_t rx_render_stage_target_count(const RxRenderStage* stage);
const RxImage* rx_render_stage_target_image(const RxRenderStage* stage, uint32_t index);
uint32_t rx_render_stage_find_target(const RxRenderStage* stage, uint16_t format, int32_t sub_index);
size_t rx_render_stage_color_target_formats(const RxRenderStage* stage, uint16_t* out, size_t capacity);
void rx_render_stage_descriptions(RxRenderStage* stage, const void** out_descriptions, size_t* out_count);
int32_t rx_render_stage_build(RxRenderStage* stage, const RxGpuDevice* device, const RxGpuAllocator* allocator,
                              uint32_t width, uint32_t height, const uint64_t* final_views, size_t final_view_count,
                              uint32_t final_width, uint32_t final_height, uint8_t recreate);
void rx_render_stage_begin(const RxRenderStage* stage, const RxGpuDevice* device, void* cmd, uint32_t image_index,
                           int32_t render_x, int32_t render_y, uint32_t render_width, uint32_t render_height,
                           int32_t draw_x, int32_t draw_y, uint32_t draw_width, uint32_t draw_height);
void rx_render_stage_end(const RxRenderStage* stage, const RxGpuDevice* device, void* cmd);

RxDescriptorCache* rx_descriptor_cache_new(void);
void rx_descriptor_cache_destroy(RxDescriptorCache* cache, const RxGpuDevice* device, const RxGpuAllocator* allocator);
int32_t rx_descriptor_cache_request(RxDescriptorCache* cache, const RxDsLayoutCache* layouts, const RxGpuDevice* device,
                                    const RxDescriptorEntry* entries, size_t count, uint32_t* out_id,
                                    RxDescriptorSet** out_set);
RxDescriptorSet* rx_descriptor_cache_find(RxDescriptorCache* cache, uint32_t id);
void rx_descriptor_cache_free(RxDescriptorCache* cache, const RxGpuDevice* device, const RxGpuAllocator* allocator,
                              uint32_t id);
int32_t rx_descriptor_cache_rebuild_all(RxDescriptorCache* cache, const RxDsLayoutCache* layouts,
                                        const RxGpuDevice* device);
void rx_descriptor_set_bind(const RxDescriptorSet* set, const RxGpuDevice* device, void* cmd, int32_t bind_point,
                            uint64_t layout, uint32_t first_set, const uint32_t* offsets, size_t offset_count);

typedef struct RxGpuProfiler RxGpuProfiler;

uint32_t rx_gpu_marker_count(void);
RxGpuProfiler* rx_gpu_profiler_new(const RxGpuDevice* device, uint32_t graphics_family, uint32_t frames_in_flight);
void rx_gpu_profiler_destroy(RxGpuProfiler* profiler, const RxGpuDevice* device);
void rx_gpu_profiler_read_results(RxGpuProfiler* profiler, const RxGpuDevice* device, uint32_t frame_index,
                                  double delta_seconds);
void rx_gpu_profiler_begin_frame(RxGpuProfiler* profiler, const RxGpuDevice* device, void* cmd, uint32_t frame_index);
void rx_gpu_profiler_mark(RxGpuProfiler* profiler, const RxGpuDevice* device, void* cmd, uint32_t marker);
double rx_gpu_profiler_average_ms(const RxGpuProfiler* profiler, uint32_t marker);
double rx_gpu_profiler_total_ms(const RxGpuProfiler* profiler);

typedef struct RxDebugDraw RxDebugDraw;

typedef struct RxDebugDrawCommand
{
    float combined_matrix[16];
    uint32_t color;
    uint32_t shape;
} RxDebugDrawCommand;

RxDebugDraw* rx_debug_draw_new(void);
void rx_debug_draw_free(RxDebugDraw* draw);
int32_t rx_debug_draw_shape(RxDebugDraw* draw, uint32_t shape, const float* matrix, uint32_t color);
int32_t rx_debug_draw_line(RxDebugDraw* draw, const float* from, const float* to, uint32_t color);
int32_t rx_debug_draw_box(RxDebugDraw* draw, uint32_t shape, const float* center, const float* half_extent,
                          const float* rotation, uint32_t color);
int32_t rx_debug_draw_aabb(RxDebugDraw* draw, uint32_t shape, const float* min, const float* max, uint32_t color);
uint32_t rx_debug_draw_count(const RxDebugDraw* draw);
void rx_debug_draw_commands(RxDebugDraw* draw, const float* camera, const RxDebugDrawCommand** out_commands,
                            size_t* out_count);
void rx_debug_draw_clear(RxDebugDraw* draw);

typedef struct RxTextState RxTextState;

typedef struct RxTextInstance
{
    float position[2];
    float size[2];
    float uv_min[2];
    float uv_max[2];
} RxTextInstance;

#define RX_TEXT_NO_ROOM UINT32_MAX

uint32_t rx_text_glyph_width(void);
uint32_t rx_text_glyph_height(void);
uint32_t rx_text_max_glyphs(void);
RxTextState* rx_text_state_new(void);
void rx_text_state_free(RxTextState* state);
void rx_text_begin_frame_if_needed(RxTextState* state, uint32_t frame_number);
void rx_text_cursor(const RxTextState* state, float* out_x, float* out_y);
void rx_text_move_cursor_down(RxTextState* state, float amount);
uint32_t rx_text_reserve(RxTextState* state, uint32_t count);
size_t rx_text_layout(const char* text, float scale, float origin_x, float origin_y, uint32_t window_width,
                      uint32_t window_height, uint32_t atlas_width, uint32_t atlas_height,
                      RxTextInstance* out_instances, size_t capacity, float* out_line_height);
void rx_text_image_instance(float x, float y, float width, float height, uint32_t window_width,
                            uint32_t window_height, RxTextInstance* out_instance);
void rx_text_ortho(float width, float height, float near_plane, float far_plane, float* out);

typedef struct RxShadowAtlas RxShadowAtlas;

typedef struct RxShadowRegion
{
    uint32_t offset_x;
    uint32_t offset_y;
    uint32_t width;
    uint32_t height;
} RxShadowRegion;

#define RX_SHADOW_NO_TILE UINT32_MAX

uint32_t rx_shadow_atlas_width(void);
uint32_t rx_shadow_atlas_height(void);
uint32_t rx_shadow_atlas_directional_size(void);
uint32_t rx_shadow_atlas_spot_tile_size(void);
uint32_t rx_shadow_atlas_max_spot_tiles(void);
RxShadowAtlas* rx_shadow_atlas_new(void);
void rx_shadow_atlas_free(RxShadowAtlas* atlas);
uint32_t rx_shadow_atlas_allocate_spot_tile(RxShadowAtlas* atlas);
void rx_shadow_atlas_free_spot_tile(RxShadowAtlas* atlas, uint32_t tile);
void rx_shadow_atlas_invalidate(RxShadowAtlas* atlas);
uint32_t rx_shadow_atlas_generation(const RxShadowAtlas* atlas);
uint8_t rx_shadow_atlas_is_initialized(const RxShadowAtlas* atlas);
void rx_shadow_atlas_begin_region(RxShadowAtlas* atlas, const RxShadowRegion* region,
                                  RxShadowRegion* out_render_area);
void rx_shadow_atlas_directional_region(RxShadowRegion* out);
uint8_t rx_shadow_atlas_spot_tile_region(uint32_t tile, RxShadowRegion* out);
void rx_shadow_atlas_region_uv_transform(const RxShadowRegion* region, float* out);

typedef struct RxShaderLibrary RxShaderLibrary;

enum RxShaderLoad
{
    RX_SHADER_LOAD_OK = 0,
    RX_SHADER_LOAD_COMPILE_FAILED = 1,
    RX_SHADER_LOAD_COMPILER_UNAVAILABLE = 2,
};

RxShaderLibrary* rx_shader_library_new(const char* directory);
void rx_shader_library_free(RxShaderLibrary* library, const RxGpuDevice* device);
RxShaderProgram* rx_shader_library_get_program(RxShaderLibrary* library, const RxGpuDevice* device, const char* name,
                                               uint32_t stage_bits, const RxShaderMacroRef* macros,
                                               size_t macro_count, const RxLogSink* log, int32_t* out_status);

typedef struct RxSkeleton
{
    uint32_t joint_count;
    uint32_t pose_hash;
    float pose_radius;
    uint32_t reserved;
    float pose_center[4];
    const float* local;
    const float* world;
    const float* skinning;
    const uint32_t* parents;
} RxSkeleton;

typedef struct RxPlayback
{
    uint32_t animation;
    float time;
    float speed;
    uint8_t on_end;
} RxPlayback;

typedef struct RxRestPose
{
    float translation[4];
    float rotation[4];
    float scale[4];
} RxRestPose;

enum RxAnimationEnd
{
    RX_ANIMATION_END_POP = 0,
    RX_ANIMATION_END_HOLD = 1,
    RX_ANIMATION_END_LOOP = 2,
};

#define RX_NO_ANIMATION 0xFFFFFFFFu
#define RX_NO_BONE 0xFFFFFFFFu

RxSkeleton* rx_skeleton_new(uint32_t joint_count, const float* inverse_bind, const uint32_t* parents,
                            const RxRestPose* rest_pose, const float* root_transforms, const char* const* names);
void rx_skeleton_free(RxSkeleton* skeleton);
RxSkeleton* rx_skeleton_create_instance(const RxSkeleton* source);
uint32_t rx_skeleton_find_animation(const RxSkeleton* skeleton, const char* name);
void rx_skeleton_set_rest_animation(RxSkeleton* skeleton, uint32_t animation, float speed);
uint8_t rx_skeleton_push_animation(RxSkeleton* skeleton, uint32_t animation, uint8_t on_end, float speed);
uint8_t rx_skeleton_stack_is_full(const RxSkeleton* skeleton);
void rx_skeleton_pop_animation(RxSkeleton* skeleton);
void rx_skeleton_clear_animation_stack(RxSkeleton* skeleton);
uint8_t rx_skeleton_active_playback(const RxSkeleton* skeleton, RxPlayback* out);
void rx_skeleton_set_external_pose(RxSkeleton* skeleton, uint8_t enabled);
void rx_skeleton_evaluate_pose(RxSkeleton* skeleton, uint32_t animation, float time);
void rx_skeleton_pose_from_driven_bones(RxSkeleton* skeleton, const float* driven_world, const uint8_t* is_driven);
void rx_skeleton_advance(RxSkeleton* skeleton, float delta_time);
uint32_t rx_skeleton_find_bone(const RxSkeleton* skeleton, const char* name);
const char* rx_skeleton_bone_name(const RxSkeleton* skeleton, uint32_t bone);
uint32_t rx_skeleton_max_animation_stack(void);

typedef struct RxPipelineCache RxPipelineCache;
typedef struct RxPipelineDesc RxPipelineDesc;

typedef struct RxPipelineSetRef
{
    uint32_t index;
    RxDescriptorSet* set;
    uint64_t layout;
} RxPipelineSetRef;

typedef struct RxPipelineSlot
{
    uint64_t pipeline;
    uint64_t layout;
    uint32_t default_cull_mode;
    uint32_t handle;
    uint32_t name;
    uint32_t set_count;
    const RxPipelineSetRef* sets;
    const char* debug_name;
    uint8_t is_compute;
    uint8_t built;
} RxPipelineSlot;

typedef struct RxPipelineContext
{
    const RxGpuDevice* device;
    RxDescriptorCache* descriptor_cache;
    const RxDsLayoutCache* ds_layouts;
    RxShaderLibrary* library;
    const RxLogSink* log;
} RxPipelineContext;

enum RxPipelineStatus
{
    RX_PIPELINE_OK = 0,
    RX_PIPELINE_DESCRIPTOR_MISMATCH = 1,
    RX_PIPELINE_VULKAN_ERROR = 2,
    RX_PIPELINE_SHADER_COMPILE_FAILED = 3,
    RX_PIPELINE_SHADER_COMPILER_UNAVAILABLE = 4,
    RX_PIPELINE_BLEND_TARGET = 5,
};

typedef uint8_t (*RxPipelineTemplateFn)(void* user, uint32_t pass, uint32_t features, RxPipelineDesc* desc);
typedef void (*RxPipelineDeclareFn)(void* user, RxPipelineDesc* desc);
typedef void (*RxPipelineFreeFn)(void* user);

RxPipelineCache* rx_pipeline_cache_new(uint32_t num_static, uint32_t max_dynamic);
void rx_pipeline_cache_free(RxPipelineCache* cache, const RxGpuDevice* device);
void rx_pipeline_cache_set_static_name(const RxPipelineCache* cache, uint32_t handle, const char* name);
RxPipelineSlot* rx_pipeline_cache_slot(const RxPipelineCache* cache, uint32_t handle);
uint8_t rx_pipeline_cache_has_pending(const RxPipelineCache* cache);
uint8_t rx_pipeline_cache_is_main_thread(const RxPipelineCache* cache);
void rx_pipeline_cache_register_template(const RxPipelineCache* cache, uint32_t pass, RxPipelineTemplateFn call,
                                         void* user, RxPipelineFreeFn free);
int32_t rx_pipeline_cache_create(const RxPipelineCache* cache, const RxPipelineContext* context, RxPipelineDesc* desc,
                                 uint32_t* out_handle, int32_t* out_vk_result);
int32_t rx_pipeline_cache_build_pending(const RxPipelineCache* cache, const RxPipelineContext* context,
                                        int32_t* out_vk_result);
int32_t rx_pipeline_cache_build_static(const RxPipelineCache* cache, const RxPipelineContext* context,
                                       uint32_t handle, RxPipelineDesc* desc, int32_t* out_vk_result);
uint32_t rx_pipeline_cache_find_variant(const RxPipelineCache* cache, uint32_t pass, uint32_t features);
uint32_t rx_pipeline_cache_find_variant_in_pass(const RxPipelineCache* cache, uint32_t handle, uint32_t pass);
int32_t rx_pipeline_cache_get_or_create_variant(const RxPipelineCache* cache, const RxPipelineContext* context,
                                                uint32_t pass, uint32_t features, uint32_t* out_handle,
                                                int32_t* out_vk_result);
int32_t rx_pipeline_cache_get_or_create_variant_in_pass(const RxPipelineCache* cache,
                                                        const RxPipelineContext* context, uint32_t handle,
                                                        uint32_t pass, uint32_t* out_handle, int32_t* out_vk_result);
uint8_t rx_pipeline_cache_pass_pipeline(const RxPipelineCache* cache, uint32_t pass, size_t index, uint32_t* out);
void rx_pipeline_cache_add_buffer_offset(const RxPipelineCache* cache, uint32_t set, uint32_t offset);
uint8_t rx_pipeline_cache_bind_sets(const RxPipelineCache* cache, const RxGpuDevice* device, const RxPipelineSlot* slot,
                                    void* cmd, int32_t bind_point);

RxPipelineDesc* rx_pipeline_desc_new(void);
void rx_pipeline_desc_free(RxPipelineDesc* desc);
void rx_pipeline_desc_set_debug_name(RxPipelineDesc* desc, const char* name);
void rx_pipeline_desc_set_shader(RxPipelineDesc* desc, uint32_t index, const char* name, const RxShaderMacroRef* macros,
                                 size_t macro_count);
void rx_pipeline_desc_add_macro(RxPipelineDesc* desc, const char* name, const char* value);
void rx_pipeline_desc_set_vertex_type(RxPipelineDesc* desc, uint32_t vertex_type);
void rx_pipeline_desc_set_no_vertices(RxPipelineDesc* desc, uint8_t value);
void rx_pipeline_desc_set_stage(RxPipelineDesc* desc, RxRenderStage* stage);
void rx_pipeline_desc_set_cull_mode(RxPipelineDesc* desc, uint32_t cull_mode);
void rx_pipeline_desc_set_front_face(RxPipelineDesc* desc, int32_t front_face);
void rx_pipeline_desc_set_depth_compare_op(RxPipelineDesc* desc, int32_t op);
void rx_pipeline_desc_set_depth_test(RxPipelineDesc* desc, uint8_t value);
void rx_pipeline_desc_set_depth_write(RxPipelineDesc* desc, uint8_t value);
void rx_pipeline_desc_set_render_lines(RxPipelineDesc* desc, uint8_t value);
void rx_pipeline_desc_add_blend(RxPipelineDesc* desc, uint32_t target_index, const RxBlendAttachment* blend);
void rx_pipeline_desc_add_push_constants(RxPipelineDesc* desc, uint32_t size, uint32_t stages);
void rx_pipeline_desc_add_entry(RxPipelineDesc* desc, uint32_t set, const RxDescriptorEntry* entry);
void rx_pipeline_desc_set_features(RxPipelineDesc* desc, uint32_t features);
void rx_pipeline_desc_set_declare(RxPipelineDesc* desc, RxPipelineDeclareFn call, void* user, RxPipelineFreeFn free);

typedef struct RxLevel RxLevel;

typedef struct RxLevelSun
{
    uint32_t present;
    uint32_t enabled;
    uint32_t has_color;
    uint32_t has_intensity;
    float position[3];
    int32_t color[3];
    float intensity;
} RxLevelSun;

typedef struct RxLevelLight
{
    const char* name;
    size_t name_length;
    int64_t kind;
    uint32_t has_position;
    uint32_t has_color;
    uint32_t has_intensity;
    uint32_t has_direction;
    uint32_t has_rotation;
    uint32_t shadows;
    float position[3];
    int32_t color[3];
    float intensity;
    float radius;
    float direction[3];
    float rotation[4];
    float inner_degrees;
    float outer_degrees;
} RxLevelLight;

typedef struct RxLevelCamera
{
    uint32_t present;
    uint32_t has_aperture;
    uint32_t has_shutter;
    uint32_t has_iso;
    uint32_t has_exposure_ev;
    float aperture;
    float shutter;
    float iso;
    float exposure_ev;
} RxLevelCamera;

typedef struct RxLevelPlane
{
    float normal[3];
    float distance;
    uint32_t has_texture;
    float offset[2];
    float scale[2];
    float rotation;
} RxLevelPlane;

typedef struct RxLevelBlock
{
    const char* name;
    size_t name_length;
    float position[3];
    uint32_t brush_kind;
    float box_min[3];
    float box_max[3];
    const RxLevelPlane* planes;
    uint32_t plane_count;
    uint32_t has_textures;
    uint32_t rotation_kind;
    float rotation[4];
    uint32_t locked;
    uint32_t has_material;
    int32_t material;
    uint32_t probe_volume;
    uint32_t reflection_probe;
    uint32_t bleeds;
    uint32_t dynamic;
} RxLevelBlock;

enum RxLevelBrushKind
{
    RX_LEVEL_BRUSH_INVALID = 0,
    RX_LEVEL_BRUSH_BOX = 1,
    RX_LEVEL_BRUSH_PLANES = 2,
};

enum RxLevelRotationKind
{
    RX_LEVEL_ROTATION_IDENTITY = 0,
    RX_LEVEL_ROTATION_EULER = 1,
    RX_LEVEL_ROTATION_QUAT = 2,
};

enum RxLevelLightKind
{
    RX_LEVEL_LIGHT_POINT = 0,
    RX_LEVEL_LIGHT_SPOT = 1,
};

typedef struct RxLevelWriteSun
{
    uint32_t present;
    uint32_t enabled;
    float position[3];
    int32_t color[4];
    float intensity;
} RxLevelWriteSun;

typedef struct RxLevelWriteLight
{
    const char* name;
    size_t name_length;
    uint32_t spot;
    uint32_t shadows;
    float position[3];
    int32_t color[4];
    float intensity;
    float radius;
    float direction[3];
    float inner_degrees;
    float outer_degrees;
} RxLevelWriteLight;

typedef struct RxLevelWritePlane
{
    float normal[3];
    float distance;
    float offset[2];
    float scale[2];
    float rotation;
} RxLevelWritePlane;

typedef struct RxLevelWriteBlock
{
    const char* name;
    size_t name_length;
    float position[3];
    uint32_t brush_kind;
    float box_extents[6];
    const RxLevelWritePlane* planes;
    uint32_t plane_count;
    uint32_t textures;
    float rotation[4];
    uint32_t locked;
    uint32_t has_material;
    int32_t material;
    uint32_t probe_volume;
    uint32_t reflection_probe;
    uint32_t bleeds;
    uint32_t dynamic;
} RxLevelWriteBlock;

typedef struct RxLevelWrite
{
    RxLevelWriteSun sun;
    const RxLevelWriteLight* lights;
    uint32_t light_count;
    float camera[4];
    const RxLevelWriteBlock* blocks;
    uint32_t block_count;
} RxLevelWrite;

RxLevel* rx_level_parse(const uint8_t* data, size_t length, const char* prelude_path, const RxHost* host);
void rx_level_free(RxLevel* level);
uint8_t rx_level_has_errors(const RxLevel* level);
uint8_t rx_level_has_blocks(const RxLevel* level);
const RxLevelSun* rx_level_sun(const RxLevel* level);
const RxLevelCamera* rx_level_camera(const RxLevel* level);
uint32_t rx_level_light_count(const RxLevel* level);
const RxLevelLight* rx_level_light(const RxLevel* level, uint32_t index);
uint32_t rx_level_block_count(const RxLevel* level);
const RxLevelBlock* rx_level_block(const RxLevel* level, uint32_t index);
uint8_t rx_level_save(const char* path, const RxLevelWrite* write, const RxLogSink* log);

typedef struct RxWorldFile RxWorldFile;

typedef struct RxWorldCollider
{
    const char* name;
    size_t name_length;
    float position[3];
    float rotation[4];
    uint32_t dynamic;
    uint32_t has_box;
    float box_size[3];
} RxWorldCollider;

typedef struct RxWorldObject
{
    const char* name;
    size_t name_length;
    const char* mesh;
    const char* collider;
    int64_t layer;
    uint32_t has_shadows;
    uint32_t shadows;
    uint32_t has_position;
    uint32_t has_rotation;
    uint32_t has_scale;
    uint32_t has_layer;
    uint32_t unlit;
    uint32_t no_cull;
    float scale;
    float position[3];
    float rotation[4];
} RxWorldObject;

RxWorldFile* rx_world_file_parse(const uint8_t* data, size_t length, const char* prelude_path, const RxHost* host);
void rx_world_file_free(RxWorldFile* world);
uint8_t rx_world_file_has_errors(const RxWorldFile* world);
uint8_t rx_world_file_has_meta(const RxWorldFile* world);
const char* rx_world_file_name(const RxWorldFile* world);
uint32_t rx_world_file_collider_count(const RxWorldFile* world);
const RxWorldCollider* rx_world_file_collider(const RxWorldFile* world, uint32_t index);
uint32_t rx_world_file_object_count(const RxWorldFile* world);
const RxWorldObject* rx_world_file_object(const RxWorldFile* world, uint32_t index);

typedef struct RxWorldGrid RxWorldGrid;

typedef struct RxTileSlots
{
    const uint32_t* objects;
    uint32_t object_slots;
    const uint32_t* lights;
    uint32_t light_slots;
} RxTileSlots;

typedef struct RxTileBounds
{
    float min[3];
    float max[3];
} RxTileBounds;

typedef struct RxGridInfo
{
    uint32_t grid_size[2];
    float tile_size[2];
    float position_offset[3];
    uint32_t tile_count;
    uint32_t view_tile;
} RxGridInfo;

enum RxObjectUpdate
{
    RX_OBJECT_NOT_PLACED = 0,
    RX_OBJECT_UNCHANGED = 1,
    RX_OBJECT_MOVED = 2,
    RX_OBJECT_PLACED = 3,
};

#define RX_WORLD_GRID_GLOBAL_TILE 0xFFFFFFFEu
#define RX_WORLD_GRID_NULL_TILE 0xFFFFFFFFu

RxWorldGrid* rx_world_grid_new(const RxLogSink* log);
void rx_world_grid_free(RxWorldGrid* grid);
void rx_world_grid_create(RxWorldGrid* grid, uint32_t width, uint32_t height);
void rx_world_grid_info(const RxWorldGrid* grid, RxGridInfo* out);
uint32_t rx_world_grid_world_to_tile(const RxWorldGrid* grid, float x, float y, float z);
void rx_world_grid_tile_to_xy(const RxWorldGrid* grid, uint32_t tile, uint32_t* out);
uint32_t rx_world_grid_tile_from_xy(const RxWorldGrid* grid, uint32_t x, uint32_t y);
void rx_world_grid_tile_world_center(const RxWorldGrid* grid, uint32_t x, uint32_t y, float* out);
void rx_world_grid_tile_bounds(const RxWorldGrid* grid, uint32_t tile, RxTileBounds* out);
uint8_t rx_world_grid_tile_slots(const RxWorldGrid* grid, uint32_t tile, RxTileSlots* out);
void rx_world_grid_add_object(RxWorldGrid* grid, uint32_t id, const float* min, const float* max, uint8_t cullable);
uint32_t rx_world_grid_update_object(RxWorldGrid* grid, uint32_t id, const float* min, const float* max,
                                     uint8_t cullable);
void rx_world_grid_remove_object(RxWorldGrid* grid, uint32_t id);
uint32_t rx_world_grid_object_tile(const RxWorldGrid* grid, uint32_t id);
void rx_world_grid_add_light(RxWorldGrid* grid, uint32_t id, const float* min, const float* max, uint8_t cullable);
void rx_world_grid_update_light(RxWorldGrid* grid, uint32_t id, const float* min, const float* max, uint8_t cullable);
void rx_world_grid_remove_light(RxWorldGrid* grid, uint32_t id);
uint32_t rx_world_grid_light_tile(const RxWorldGrid* grid, uint32_t id);
void rx_world_grid_set_view_tile(RxWorldGrid* grid, uint32_t tile);
const uint32_t* rx_world_grid_nearby_objects(RxWorldGrid* grid, uint32_t* count);

typedef struct RxGltf RxGltf;

typedef struct RxGltfNode
{
    uint32_t mesh;
    int32_t skin;
} RxGltfNode;

typedef struct RxGltfPrimitive
{
    const uint32_t* indices;
    size_t index_count;
    const float* positions;
    size_t position_floats;
    const float* normals;
    size_t normal_floats;
    const float* uvs;
    size_t uv_floats;
    const float* tangents;
    size_t tangent_floats;
    const float* weights;
    size_t weight_floats;
    const uint32_t* joints;
    size_t joint_values;
    uint32_t has_indices;
    int32_t material;
} RxGltfPrimitive;

typedef struct RxGltfTexture
{
    int32_t image;
    int32_t source_image;
} RxGltfTexture;

enum RxGltfAlphaMode
{
    RX_GLTF_ALPHA_OPAQUE = 0,
    RX_GLTF_ALPHA_MASK = 1,
    RX_GLTF_ALPHA_BLEND = 2,
};

typedef struct RxGltfMaterial
{
    const char* name;
    uint32_t alpha_mode;
    uint32_t double_sided;
    uint32_t unlit;
    uint32_t has_specular_glossiness;
    uint32_t has_packed_occlusion;
    float packed_occlusion_strength;
    float diffuse_factor[4];
    float specular_factor[3];
    float glossiness_factor;
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    RxGltfTexture specular_glossiness_texture;
    RxGltfTexture diffuse_texture;
    RxGltfTexture metallic_roughness_texture;
    RxGltfTexture base_color_texture;
    RxGltfTexture normal_texture;
} RxGltfMaterial;

typedef struct RxGltfImage
{
    const char* name;
    const uint8_t* bytes;
    size_t size;
} RxGltfImage;

RxGltf* rx_gltf_load_file(const char* path, const RxLogSink* log);
RxGltf* rx_gltf_load_memory(const uint8_t* data, size_t size, const RxLogSink* log);
void rx_gltf_free(RxGltf* gltf);
uint32_t rx_gltf_node_count(const RxGltf* gltf);
const RxGltfNode* rx_gltf_node(const RxGltf* gltf, uint32_t index);
uint32_t rx_gltf_mesh_count(const RxGltf* gltf);
uint32_t rx_gltf_skin_count(const RxGltf* gltf);
uint32_t rx_gltf_primitive_count(const RxGltf* gltf, uint32_t mesh);
const RxGltfPrimitive* rx_gltf_primitive(const RxGltf* gltf, uint32_t mesh, uint32_t primitive);
const RxGltfMaterial* rx_gltf_material(const RxGltf* gltf, uint32_t index);
uint8_t rx_gltf_image(const RxGltf* gltf, uint32_t index, RxGltfImage* out);
const char* rx_gltf_image_name(const RxGltf* gltf, uint32_t index);
RxSkeleton* rx_gltf_skeleton_new(const RxGltf* gltf, uint32_t skin);

typedef struct RxAssetScheduler RxAssetScheduler;
typedef struct RxTicket RxTicket;

enum RxLoadStatus
{
    RX_LOAD_STATUS_NONE = 0,
    RX_LOAD_STATUS_SUCCESS = 1,
    RX_LOAD_STATUS_ERROR = 2,
};

typedef struct RxAssetBackend
{
    void* user;
    int32_t (*load)(void* user, void* job);
    void (*begin_upload)(void* user);
    uint8_t (*upload)(void* user, void* job);
    void (*end_upload)(void* user);
    void (*finish)(void* user, void* job, int32_t status);
    uint32_t (*frame)(void* user);
    void (*wait_for_uploads)(void* user);
    void (*destroy)(void* user, void* resource);
    void (*log)(void* user, int32_t level, int32_t category, const char* message, size_t length);
} RxAssetBackend;

typedef void (*RxTicketCallback)(void* user, void* argument);
typedef void (*RxTicketDestroy)(void* user);

RxAssetScheduler* rx_asset_scheduler_new(const RxAssetBackend* backend, uint32_t workers);
void rx_asset_scheduler_free(RxAssetScheduler* scheduler);
void rx_asset_scheduler_submit(const RxAssetScheduler* scheduler, void* job);
void rx_asset_scheduler_delete_resource(const RxAssetScheduler* scheduler, void* resource, uint32_t frame_spacing);
void rx_asset_scheduler_signal(const RxAssetScheduler* scheduler);
void rx_asset_scheduler_stop(const RxAssetScheduler* scheduler);
void rx_asset_scheduler_flush_deletions(const RxAssetScheduler* scheduler);

const RxTicket* rx_ticket_new(void);
void rx_ticket_retain(const RxTicket* ticket);
void rx_ticket_release(const RxTicket* ticket);
uint8_t rx_ticket_is_loaded(const RxTicket* ticket);
void rx_ticket_wait_finished(const RxTicket* ticket);
void rx_ticket_wait_uploaded(const RxTicket* ticket);
void rx_ticket_signal_finished(const RxTicket* ticket);
void rx_ticket_signal_uploaded(const RxTicket* ticket);
void rx_ticket_mark_loaded(const RxTicket* ticket);
void rx_ticket_on_loaded(const RxTicket* ticket, void* asset, RxTicketCallback callback, void* user,
                         RxTicketDestroy destroy);
void rx_ticket_on_error(const RxTicket* ticket, RxTicketCallback callback, void* user, RxTicketDestroy destroy);
void rx_ticket_complete(const RxTicket* ticket, void* asset, uint8_t run_callbacks);
void rx_ticket_fail(const RxTicket* ticket, uint8_t run_error_callback);

typedef struct RxPlacementBoxes RxPlacementBoxes;
typedef struct RxPlacement RxPlacement;

#define RX_PLACEMENT_NO_PROBE UINT32_MAX

RxPlacementBoxes* rx_placement_boxes_new(void);
void rx_placement_boxes_free(RxPlacementBoxes* boxes);
uint8_t rx_placement_boxes_add(RxPlacementBoxes* boxes, const float* local_min, const float* local_max,
                               const float* world_min, const float* world_max, const float* local_to_world,
                               const float* world_to_local, uint8_t axis_aligned, const float* planes,
                               uint32_t plane_count);
void rx_placement_boxes_extend(RxPlacementBoxes* boxes, const float* min, const float* max);
uint32_t rx_placement_boxes_count(const RxPlacementBoxes* boxes);
uint8_t rx_placement_boxes_is_empty(const RxPlacementBoxes* boxes);
void rx_placement_boxes_bounds(const RxPlacementBoxes* boxes, float* out_min, float* out_max);
uint32_t rx_probe_count_needed(const RxPlacementBoxes* boxes, const float* volume_min, const float* cell_size,
                               const uint32_t* grid_dims, uint32_t fill);
RxPlacement* rx_probe_place_volume(const RxPlacementBoxes* boxes, const float* volume_min, const float* volume_size,
                                   const uint32_t* grid_dims, uint32_t fill, uint32_t probe_budget);
void rx_placement_free(RxPlacement* placement);
void rx_placement_counts(const RxPlacement* placement, uint32_t* out);
void rx_placement_positions(const RxPlacement* placement, float* out);
void rx_placement_grid(const RxPlacement* placement, uint32_t* out);
void rx_probe_grid_for_spacing(const float* region_size, float spacing, uint8_t cell_centred, uint32_t* out_dims);
void rx_probe_layout_grid(const float* region_min, const float* region_max, const uint32_t* grid_dims,
                          uint8_t cell_centred, float* out_min, float* out_size);
void rx_probe_grid_cell_size(const float* volume_size, const uint32_t* grid_dims, float* out_size);
uint8_t rx_probe_position_valid(const RxPlacementBoxes* boxes, const float* grid_position, const float* position);
uint8_t rx_probe_find_valid_position(const RxPlacementBoxes* boxes, const float* grid_position, const float* preferred,
                                     const float* max_relocation, float* out_position);

typedef struct RxKtx RxKtx;

typedef struct RxKtxInfo
{
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t level_count;
} RxKtxInfo;

typedef struct RxDecodedImage RxDecodedImage;

typedef struct RxDecodedInfo
{
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    size_t size;
} RxDecodedInfo;

enum RxImageSaveFormat
{
    RX_IMAGE_SAVE_JPEG = 0,
    RX_IMAGE_SAVE_PNG = 1,
};

RxKtx* rx_ktx_open_file(const char* path, const RxLogSink* log);
RxKtx* rx_ktx_open_memory(const uint8_t* data, size_t size, const RxLogSink* log);
void rx_ktx_free(RxKtx* ktx);
void rx_ktx_info(const RxKtx* ktx, RxKtxInfo* out);
const uint8_t* rx_ktx_level(const RxKtx* ktx, uint32_t level, size_t* size);

RxDecodedImage* rx_image_decode_file(const char* path, uint32_t channels);
RxDecodedImage* rx_image_decode_memory(const uint8_t* data, size_t size, uint32_t channels);
void rx_image_decoded_free(RxDecodedImage* image);
void rx_image_decoded_info(const RxDecodedImage* image, RxDecodedInfo* out);
const uint8_t* rx_image_decoded_data(const RxDecodedImage* image);
uint8_t rx_image_probe_memory(const uint8_t* data, size_t size, uint32_t* width, uint32_t* height);
uint8_t rx_image_save(const char* path, uint32_t format, const uint8_t* rgba, size_t size, uint32_t width,
                      uint32_t height, uint8_t flip_y, const RxLogSink* log);

typedef struct RxMaterialLibrary RxMaterialLibrary;

typedef struct RxMaterialDef
{
    const char* name;
    const char* diffuse;
    const char* normal;
    const char* orm;
} RxMaterialDef;

RxMaterialLibrary* rx_material_library_new(void);
void rx_material_library_free(RxMaterialLibrary* library);
uint8_t rx_material_library_parse(RxMaterialLibrary* library, const uint8_t* data, size_t length,
                                  const char* prelude_path, const RxHost* host);
uint32_t rx_material_library_def_count(const RxMaterialLibrary* library);
const RxMaterialDef* rx_material_library_def(const RxMaterialLibrary* library, uint32_t index);
void rx_material_library_register(RxMaterialLibrary* library, const char* name, uint32_t material);
uint32_t rx_material_library_count(const RxMaterialLibrary* library);
const char* rx_material_library_name(const RxMaterialLibrary* library, uint32_t index);
uint32_t rx_material_library_material(const RxMaterialLibrary* library, int32_t index);
int32_t rx_material_library_find(const RxMaterialLibrary* library, uint32_t material);

typedef struct RxWeaponScriptDef
{
    float damage;
    float range;
    float rounds_per_minute;
    int32_t pellets;
    int32_t fire_mode_flags;
    int32_t default_mode;
    int32_t burst_count;
    float burst_rounds_per_minute;
    float burst_cooldown;
    float hit_force;
    int32_t decals;
    float spread_hip;
    float spread_moving;
    float spread_air;
    float spread_per_shot;
    float spread_max;
    float spread_recovery;
    float recoil_pitch;
    float recoil_pitch_variance;
    float recoil_yaw;
    float recoil_ramp;
    float recoil_ramp_max;
    float recoil_recovery;
    float recoil_reset_time;
    float falloff_start;
    float falloff_end;
    float falloff_min;
    int32_t magazine_size;
    int32_t reserve_max;
    int32_t reserve_start;
    float reload_time;
    float reload_empty_time;
    int32_t auto_reload;
    float raise_time;
    float lower_time;
} RxWeaponScriptDef;

typedef struct RxWeaponDef
{
    const char* name;
    const char* script;
    const char* idle_anim;
    const char* fire_anim;
    const char* reload_anim;
    int32_t slot;
    float view_kick;
    RxWeaponScriptDef def;
} RxWeaponDef;

typedef struct RxWeaponDefOwner RxWeaponDefOwner;

#define RX_WEAPON_ERROR_PARSE 1
#define RX_WEAPON_ERROR_NO_SCRIPT 2

RxWeaponDefOwner* rx_weapon_def_parse(const uint8_t* data, size_t length, const char* prelude_path, const RxHost* host,
                                      int32_t* error);
const RxWeaponDef* rx_weapon_def_get(const RxWeaponDefOwner* weapon);
void rx_weapon_def_free(RxWeaponDefOwner* weapon);

typedef struct RxViewKick
{
    float value[4];
    float velocity[4];
    float bound[4];
} RxViewKick;

typedef struct RxRecoil
{
    float pending_pitch;
    float pending_yaw;
    float offset_pitch;
    float offset_yaw;
    float recovery;
    float idle_time;
} RxRecoil;

typedef struct RxViewSway
{
    float yaw;
    float pitch;
    float prev_yaw;
    float prev_pitch;
} RxViewSway;

void rx_view_kick_fire(RxViewKick* kick, float kick_degrees, float kickback, float random_yaw, float random_roll);
void rx_view_kick_update(RxViewKick* kick, float delta_time);
void rx_recoil_add(RxRecoil* recoil, float pitch, float yaw);
void rx_recoil_cancel(RxRecoil* recoil, float yaw, float pitch);
void rx_recoil_update(RxRecoil* recoil, float delta_time, float* delta_yaw, float* delta_pitch);
void rx_recoil_pitch_clamped(RxRecoil* recoil, float delta_pitch, float applied_pitch);
void rx_view_sway_update(RxViewSway* sway, float camera_yaw, float camera_pitch, float delta_time);

#ifdef __cplusplus
}
#endif
