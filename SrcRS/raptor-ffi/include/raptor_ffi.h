#pragma once

#include <stdbool.h>
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

void rx_blockout_keep_in_place(const float* rotation, const float* old_center, const float* new_center, float* out);
size_t rx_blockout_move_face(const RxBrushPlane* planes, size_t count, const float* normal, float distance,
                             float min_thickness, RxBrushPlane* out, size_t capacity);
size_t rx_blockout_edit_face_texture(const RxBrushPlane* planes, size_t count, const float* normal, uint32_t edit,
                                     const float* amount, RxBrushPlane* out, size_t capacity);
uint8_t rx_blockout_clip(const RxBrushPlane* planes, size_t count, const float* to_local, const float* rotation,
                         const float* position, const float* point_a, const float* point_b, const float* face_normal,
                         RxBrushPlane* out_kept, RxBrushPlane* out_split, size_t capacity, size_t* out_counts,
                         float* out_split_position);
size_t rx_blockout_world_box(const float* min, const float* max, RxBrushPlane* out, size_t capacity,
                             float* out_position);
uint8_t rx_blockout_same_planes(const RxBrushPlane* a, size_t a_count, const RxBrushPlane* b, size_t b_count);
uint32_t rx_blockout_save_shape(const RxBrushPlane* planes, size_t count);

enum RxBlockoutShape
{
    RX_BLOCKOUT_SHAPE_BOX = 1,
    RX_BLOCKOUT_SHAPE_TEXTURES = 2,
};
void rx_script_point_to_world(const float* matrix, const float* point, float* out);
void rx_script_direction_to_world(const float* matrix, const float* direction, float* out);
void rx_script_ray_to_plane(const float* origin, const float* direction, const float* point, const float* normal,
                            float* out);
void rx_script_register_extern(const char* name, const void* function);
const void* rx_script_find_extern(const char* name);
uint32_t rx_script_load(const char* path);
void rx_script_free(uint32_t id);
void rx_script_reload(uint32_t id);
void rx_script_reload_all(void);
bool rx_script_has_errors(uint32_t id);
void* rx_script_context(uint32_t id);
const void* rx_script_function(uint32_t id, const char* name);
void rx_script_shutdown(void);

uint32_t rx_rand32(void);
float rx_random_unit(void);
float rx_random_signed_unit(void);
float rx_random_range(float low, float high);

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

typedef struct RxUploadInfo
{
    uint32_t image_type;
    uint32_t width;
    uint32_t height;
    uint16_t format;
    uint32_t mip_level;
    uint32_t mip_count;
    const uint8_t* data;
    size_t size;
} RxUploadInfo;

typedef void (*RxDeleteBufferFn)(RxBufferResource* resource);

int32_t rx_image_create_from_data(RxImage* image, const RxGpuDevice* device, const RxGpuAllocator* allocator,
                                  void* cmd, uint32_t queue_family, RxDeleteBufferFn delete_buffer,
                                  const RxUploadInfo* info, uint8_t is_target);
int32_t rx_image_upload_chain(RxImage* image, const RxGpuDevice* device, const RxGpuAllocator* allocator, void* cmd,
                              uint32_t queue_family, RxDeleteBufferFn delete_buffer, const RxUploadInfo* info);
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
size_t rx_blockout_make_planes(const RxLevelBlock* block, RxBrushPlane* out, size_t capacity);
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

typedef void (*RxTicketCallback)(void* user, void* argument);
typedef void (*RxTicketDestroy)(void* user);

typedef struct RxUploadContext RxUploadContext;

typedef struct RxAssetJobs
{
    void* user;
    int32_t (*load)(void* user, void* job);
    uint8_t (*upload)(void* user, void* job);
    void (*finish)(void* user, void* job, int32_t status);
    void (*log)(void* user, int32_t level, int32_t category, const char* message, size_t length);
} RxAssetJobs;

RxUploadContext* rx_upload_context_new(const RxGpuDevice* device, const RxGpuAllocator* allocator,
                                       const RxFrameLoop* frame_loop, uint32_t family);
void rx_upload_context_free(RxUploadContext* context);
void* rx_upload_cmd(const RxUploadContext* context);
uint32_t rx_upload_family(const RxUploadContext* context);
uint64_t rx_upload_transfer_semaphore(const RxUploadContext* context);
uint64_t rx_upload_transfer_count(const RxUploadContext* context);
int32_t rx_upload_immediate(const RxUploadContext* context, void (*record)(void* user, void* cmd), void* user);
int32_t rx_upload_begin(const RxUploadContext* context);
int32_t rx_upload_end(const RxUploadContext* context);
void rx_upload_wait(const RxUploadContext* context);
void rx_upload_forget_frames(const RxUploadContext* context);

typedef struct RxImageJobDesc
{
    const RxTicket* ticket;
    const RxImage* image;
    void* asset;
    uint16_t format;
    uint32_t image_type;
    uint8_t is_target;
} RxImageJobDesc;

void* rx_image_job_from_file(const RxImageJobDesc* desc, const char* path);
void* rx_image_job_from_memory(const RxImageJobDesc* desc, const uint8_t* data, size_t size);
void* rx_image_job_from_chain(const RxImageJobDesc* desc, const uint8_t* data, size_t size, uint32_t width,
                              uint32_t height, uint32_t mip_level, uint32_t mip_count);

RxAssetScheduler* rx_asset_scheduler_new_native(const RxUploadContext* context, const RxAssetJobs* jobs,
                                                uint32_t workers);

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

uint8_t rx_asset_is_ktx_memory(const uint8_t* data, size_t size);
uint8_t rx_asset_is_ktx_file(const char* path);
size_t rx_ktx_chain_size(const RxKtx* ktx, uint32_t first, uint32_t count, uint32_t* out_count);
void rx_ktx_chain_copy(const RxKtx* ktx, uint32_t first, uint32_t count, uint8_t* dst);
void rx_ktx_mip_dimensions(const RxKtx* ktx, uint32_t level, uint32_t* out);

typedef struct RxNullImages RxNullImages;

#define RX_NULL_IMAGE_FLAT_NORMAL_KEY 0xFFFFFFFFu

RxNullImages* rx_null_images_new(void);
void rx_null_images_free(RxNullImages* images);
void* rx_null_images_get(const RxNullImages* images, uint32_t key);
void* rx_null_images_insert(const RxNullImages* images, uint32_t key, void* image);
void rx_null_images_clear(const RxNullImages* images);
void rx_null_image_pixel(size_t stride, uint8_t* out);
void rx_flat_normal_pixel(uint8_t* out);

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

typedef struct RxLocomotion
{
    float user_force[3];
    float bob_counter;
    uint32_t bob_reverse;
} RxLocomotion;

void rx_locomotion_step(RxLocomotion* loco, double delta_time, const float* direction, const float* offset,
                        uint8_t sprinting, float speed_multiplier, float* force);
uint8_t rx_locomotion_released(const RxLocomotion* loco);
void rx_locomotion_bob(RxLocomotion* loco, double delta_time);
void rx_locomotion_head_bob(const RxLocomotion* loco, float strength_x, float strength_y, float* x, float* y);
float rx_fov_step(float fov, uint8_t sprinting, uint8_t moving, double delta_time);

typedef struct RxPackedVertices RxPackedVertices;

typedef struct RxPackInput
{
    const float* positions;
    size_t position_floats;
    const float* normals;
    size_t normal_floats;
    const float* uvs;
    size_t uv_floats;
    const float* tangents;
    size_t tangent_floats;
    size_t tangent_stride;
    float handedness;
    const float* bone_weights;
    size_t bone_weight_floats;
    const uint32_t* bone_ids;
    size_t bone_id_values;
    uint8_t negative_x;
    uint8_t mirror_basis;
} RxPackInput;

RxPackedVertices* rx_mesh_pack_vertices(const RxPackInput* input);
void rx_mesh_packed_free(RxPackedVertices* packed);
const uint8_t* rx_mesh_packed_data(const RxPackedVertices* packed);
uint32_t rx_mesh_packed_count(const RxPackedVertices* packed);
uint32_t rx_mesh_packed_info(const RxPackedVertices* packed, uint32_t* out_flags);
uint8_t rx_mesh_recalculate_normals(uint8_t* vertices, size_t vertex_count, size_t stride, const uint32_t* indices,
                                    size_t index_count);

typedef struct RxMeshRecord RxMeshRecord;

RxMeshRecord* rx_mesh_record_new(void);
void rx_mesh_record_free(RxMeshRecord* mesh);
uint8_t rx_mesh_record_pack(RxMeshRecord* mesh, const RxPackInput* input);
void rx_mesh_record_set_vertices(RxMeshRecord* mesh, uint32_t vertex_type, const uint8_t* bytes, size_t size);
void rx_mesh_record_set_indices(RxMeshRecord* mesh, const uint32_t* indices, size_t count);
uint32_t rx_mesh_record_ensure_normals(RxMeshRecord* mesh);
const uint8_t* rx_mesh_record_vertex_bytes(const RxMeshRecord* mesh, size_t* out_size);
const uint32_t* rx_mesh_record_indices(const RxMeshRecord* mesh, size_t* out_count);
uint32_t rx_mesh_record_vertex_count(const RxMeshRecord* mesh);
uint32_t rx_mesh_record_vertex_type(const RxMeshRecord* mesh);
uint8_t rx_mesh_record_is_skinned(const RxMeshRecord* mesh);
uint8_t rx_mesh_record_is_ready(const RxMeshRecord* mesh);
void rx_mesh_record_set_ready(const RxMeshRecord* mesh, uint8_t ready);
uint8_t rx_mesh_record_is_reference(const RxMeshRecord* mesh);
uint8_t rx_mesh_record_keeps_in_memory(const RxMeshRecord* mesh);
void rx_mesh_record_set_keep_in_memory(RxMeshRecord* mesh, uint8_t value);
void rx_mesh_record_positions(const RxMeshRecord* mesh, float* out);
uint8_t rx_mesh_record_bounds(const RxMeshRecord* mesh, float* out_min, float* out_max);
void rx_mesh_record_clear_local(RxMeshRecord* mesh);
void rx_mesh_record_clear_vertices(RxMeshRecord* mesh);

#define RX_MAX_VARIANT_MACROS 3

typedef struct RxGeometryVariant
{
    uint32_t features;
    uint32_t vertex_type;
    uint32_t macro_count;
    const char* macros[RX_MAX_VARIANT_MACROS];
    const char* suffix;
} RxGeometryVariant;

void rx_forward_geometry_variant(uint32_t features, RxGeometryVariant* out);
uint8_t rx_forward_light_grid(uint32_t width, uint32_t height, uint32_t tile_size, uint32_t max_columns,
                              uint32_t max_rows, uint32_t* out_columns, uint32_t* out_rows);

uint8_t rx_gpu_cmd_bind_pipeline_cached(const RxGpuDevice* device, void* cmd, uint64_t* bound, int32_t bind_point,
                                        uint64_t pipeline, uint8_t is_compute, uint32_t default_cull_mode);
void rx_gpu_cmd_set_double_sided(const RxGpuDevice* device, void* cmd, uint8_t is_compute, uint32_t default_cull_mode,
                                 uint8_t double_sided);

void rx_forward_ssao_blur_push(uint32_t width, uint32_t height, void* out);
typedef struct RxSlotSet RxSlotSet;

#define RX_SLOTS_NOT_FOUND 0xFFFFFFFFu

RxSlotSet* rx_slots_new(uint32_t max_bits, uint8_t all_set);
RxSlotSet* rx_slots_clone(const RxSlotSet* slots);
void rx_slots_free(RxSlotSet* slots);
uint8_t rx_slots_get(const RxSlotSet* slots, uint32_t index);
void rx_slots_set(RxSlotSet* slots, uint32_t index);
void rx_slots_unset(RxSlotSet* slots, uint32_t index);
void rx_slots_clear_all(RxSlotSet* slots);
uint32_t rx_slots_find_next_free(const RxSlotSet* slots, uint32_t start);
uint32_t rx_slots_find_next_set(const RxSlotSet* slots, uint32_t start);
uint32_t rx_slots_find_free_group(const RxSlotSet* slots, uint32_t size);
uint32_t rx_slots_reserve_instances(RxSlotSet* slots, uint32_t current, uint32_t instances, bool* moved);
uint64_t rx_slots_capacity(const RxSlotSet* slots);
const uint64_t* rx_slots_words(const RxSlotSet* slots);

typedef struct RxCullInputs
{
    uint8_t cullable;
    uint8_t has_mesh;
    uint8_t world_layer;
    uint8_t skinned;
    uint8_t is_instance;
    uint8_t physics_enabled;
    uint32_t instance_slots_in_use;
} RxCullInputs;

uint8_t rx_object_can_be_frustum_culled(const RxCullInputs* inputs, const float* bounds_min, const float* bounds_max);
void rx_object_merge_child_bounds(float* parent_min, float* parent_max, const float* parent_world, const float* child_min,
                                  const float* child_max, const float* child_world);
uint8_t rx_object_contains_point(const float* bounds_min, const float* bounds_max, const float* world_matrix,
                                 const float* point);
float rx_object_raycast_bounds(const float* bounds_min, const float* bounds_max, const float* world_matrix,
                               const float* origin, const float* direction, float* out_face);
float rx_object_direction_scale(const float* bounds_min, const float* bounds_max, float scale, const float* direction);

uint32_t rx_forward_material_features(uint8_t has_normal_or_orm, uint8_t skinned, uint8_t unlit);

void rx_world_far_to_near(const float* centers, size_t count, const float* camera, uint32_t* out);
void rx_world_clamp_tile_range(uint32_t* min, uint32_t* max, uint32_t width, uint32_t height);

typedef struct RxCameraCore
{
    float view[16];
    float projection[16];
    float inv_view[16];
    float inv_projection[16];
    float camera_matrix[16];
    float weapon_camera_matrix[16];
    float weapon_projection[16];
    float position[4];
    float direction[4];
    float target[4];
    float angle_x;
    float angle_y;
    float z_near;
    float z_far;
    float fov_rad;
    float aspect;
    float weapon_fov;
    float width;
    float height;
    uint32_t kind;
    uint32_t update_transform;
    uint32_t update_projection;
    uint32_t look_at_target;
} __attribute__((aligned(16))) RxCameraCore;

RxCameraCore* rx_camera_core_new(uint8_t orthographic);
RxCameraCore* rx_camera_core_clone(const RxCameraCore* core);
void rx_camera_core_assign(RxCameraCore* dst, const RxCameraCore* src);
void rx_camera_core_free(RxCameraCore* core);
void rx_camera_update(RxCameraCore* core);
void rx_camera_update_projection(RxCameraCore* core);
void rx_camera_update_camera_matrix(RxCameraCore* core);
void rx_camera_look_along(RxCameraCore* core, const float* position, const float* direction, const float* up);
void rx_camera_rotate(RxCameraCore* core, float angle_x, float angle_y);
void rx_camera_move_by(RxCameraCore* core, const float* offset);
void rx_camera_move_to(RxCameraCore* core, const float* position);
void rx_camera_set_planes(RxCameraCore* core, float near_plane, float far_plane);
void rx_camera_set_bounds(RxCameraCore* core, float width, float height);
void rx_camera_resolve_view_to_texels(float* eye, float* target, const float* world_up, float width, float resolution);

float rx_light_inv_radius_sq(float radius);
void rx_light_clamp_cone(float max_outer, float* inner, float* outer);
void rx_light_spot_bounds(const float* position, const float* direction, float outer_angle, float radius,
                          float* out_min, float* out_max);
float rx_light_spot_solid_angle(float inner, float outer);
float rx_light_intensity_from_lumens(float lumens, float inner, float outer);
void rx_light_spot_falloff(float inner, float outer, float* out_cos_outer, float* out_angle_scale);
void rx_light_spot_shadow_matrix(const float* position, const float* direction, float outer_angle, float radius,
                                 float fov_padding, float max_half_fov, float near_plane, float* out);

void rx_quat_from_axis_angle(const float* axis, float angle, float* out);
void rx_quat_from_euler(const float* angles, float* out);
void rx_quat_euler_angles(const float* rotation, float* out);
void rx_quat_mul(const float* left, const float* right, float* out);
void rx_quat_slerp(const float* from, const float* to, float step, float* out);
void rx_quat_nlerp(const float* from, const float* to, float time, float* out);
void rx_quat_rotate_by_axis(const float* rotation, const float* axis, float angle, float* out);
void rx_quat_from_direction(const float* direction, float* out);

#define RX_NO_BODY 0xFFFFFFFFu

typedef struct RxPhysicsWorld RxPhysicsWorld;

typedef struct RxCharacterSpec
{
    float standing_height;
    float radius;
    float mass;
    float max_strength;
    float max_slope_angle;
} RxCharacterSpec;

typedef struct RxBodyProps
{
    float convex_radius;
    float friction;
    float restitution;
    float density;
} RxBodyProps;

typedef struct RxRayResult
{
    uint32_t body;
    float point[3];
    float normal[3];
} RxRayResult;

enum RxPhysicsStatus
{
    RX_PHYSICS_OK = 0,
    RX_PHYSICS_SHAPE_ERROR = 1,
    RX_PHYSICS_TOO_FEW_POINTS = 2,
    RX_PHYSICS_NOT_TRIANGLES = 3,
    RX_PHYSICS_NO_ROOM = 4,
    RX_PHYSICS_BODY_EXISTS = 5,
};

RxPhysicsWorld* rx_physics_new(void);
void rx_physics_free(RxPhysicsWorld* world);
const char* rx_physics_last_error(const RxPhysicsWorld* world);
int32_t rx_physics_create_box_body(RxPhysicsWorld* world, uint32_t previous_body, const float* dimensions,
                                   uint8_t dynamic, const RxBodyProps* props, uint32_t* out_body,
                                   float* out_dimensions);
int32_t rx_physics_create_hull_body(RxPhysicsWorld* world, uint32_t previous_body, const float* points, size_t count,
                                    uint8_t dynamic, const RxBodyProps* props, uint32_t* out_body,
                                    float* out_dimensions);
int32_t rx_physics_create_mesh_body(RxPhysicsWorld* world, uint32_t existing, const float* positions,
                                    size_t vertex_count, const uint32_t* indices, size_t index_count, uint8_t dynamic,
                                    const RxBodyProps* props, uint32_t* out_body);
void rx_physics_add_to_world(RxPhysicsWorld* world, uint32_t body);
void rx_physics_remove_from_world(RxPhysicsWorld* world, uint32_t body);
void rx_physics_destroy_body(RxPhysicsWorld* world, uint32_t body);
void rx_physics_teleport(RxPhysicsWorld* world, uint32_t body, const float* position, const float* rotation);
void rx_physics_position_rotation(const RxPhysicsWorld* world, uint32_t body, float* position, float* rotation);
uint8_t rx_physics_raycast(const RxPhysicsWorld* world, const float* origin, const float* direction, uint32_t ignore,
                           RxRayResult* out);
size_t rx_physics_raycast_objects(const RxPhysicsWorld* world, const float* origin, const float* direction,
                                  uint32_t* out, size_t capacity);
void rx_physics_raycast_face_of_box(const RxPhysicsWorld* world, uint32_t body, const float* origin,
                                    const float* direction, float* out);

void rx_physics_body_bounds(const RxPhysicsWorld* world, uint32_t body, float* out_min, float* out_max);
uint8_t rx_physics_is_dynamic(const RxPhysicsWorld* world, uint32_t body);
uint8_t rx_physics_is_active(const RxPhysicsWorld* world, uint32_t body);
void rx_physics_activate(RxPhysicsWorld* world, uint32_t body);
void rx_physics_deactivate(RxPhysicsWorld* world, uint32_t body);
void rx_physics_push(RxPhysicsWorld* world, uint32_t body, const float* impulse);
void rx_physics_point_to_local(const RxPhysicsWorld* world, uint32_t body, const float* point, float* out);
uint8_t rx_physics_hold(RxPhysicsWorld* world, uint32_t body, const float* local_point, const float* target,
                        float stiffness, float max_speed, float angular_damping, float* out_held);


typedef struct RxImpact
{
    float point[3];
    float normal[3];
    float speed;
    uint32_t ragdoll_serial;
} RxImpact;

void rx_physics_update(RxPhysicsWorld* world);
void rx_physics_optimize(RxPhysicsWorld* world);
void rx_physics_set_paused(RxPhysicsWorld* world, uint8_t paused);
uint8_t rx_physics_is_paused(const RxPhysicsWorld* world);
float rx_physics_time_step(void);

void rx_physics_set_min_ragdoll_impact_speed(const RxPhysicsWorld* world, float speed);
size_t rx_physics_drain_impacts(const RxPhysicsWorld* world, RxImpact* out, size_t capacity);
uint64_t rx_impacts_ragdoll_user_data(uint32_t serial);
uint32_t rx_impacts_ragdoll_serial(uint64_t user_data);

typedef struct RxCharacter RxCharacter;
typedef struct RxRagdoll RxRagdoll;

RxCharacter* rx_character_new(RxPhysicsWorld* world, const RxCharacterSpec* spec);
void rx_character_free(RxCharacter* character, RxPhysicsWorld* world);
void rx_character_teleport(const RxCharacter* character, RxPhysicsWorld* world, const float* position);
void rx_character_apply_movement(RxCharacter* character, const float* movement);
void rx_character_set_collision_enabled(RxCharacter* character, RxPhysicsWorld* world, uint8_t enabled);
void rx_character_set_gravity_disabled(RxCharacter* character, uint8_t disabled);
uint8_t rx_character_is_grounded(const RxCharacter* character);
void rx_character_position(const RxCharacter* character, const RxPhysicsWorld* world, float* out);
void rx_character_linear_velocity(const RxCharacter* character, const RxPhysicsWorld* world, float* out);
void rx_character_update(RxCharacter* character, RxPhysicsWorld* world, float delta_time);
size_t rx_character_ray_bodies(const RxCharacter* character, const RxPhysicsWorld* world, const float* direction,
                               uint32_t* out, size_t capacity);

RxRagdoll* rx_ragdoll_new(RxPhysicsWorld* world, RxSkeleton* skeleton, const float* object_world,
                          const float* object_world_inverse, uint32_t serial, uint64_t user_data,
                          const RxLogSink* log, int32_t log_category);
void rx_ragdoll_free(RxRagdoll* ragdoll, RxPhysicsWorld* world, RxSkeleton* skeleton);
uint32_t rx_ragdoll_serial(const RxRagdoll* ragdoll);
void rx_ragdoll_activate(const RxRagdoll* ragdoll, RxPhysicsWorld* world, RxSkeleton* skeleton, const float* velocity);
void rx_ragdoll_write_to_skeleton(RxRagdoll* ragdoll, const RxPhysicsWorld* world, RxSkeleton* skeleton);
size_t rx_ragdoll_debug_boxes(const RxRagdoll* ragdoll, const RxPhysicsWorld* world, float* out, size_t capacity);

#define RX_PROBE_MAX_VOLUMES 8
#define RX_PROBE_MAX_PROBES 4096
#define RX_PROBE_MAX_GRID_POINTS (1u << 20)
#define RX_PROBE_CACHE_FILE_VERSION 12
#define RX_PROBE_ATLAS_WIDTH 3072

enum RxProbeFileStatus
{
    RX_PROBE_FILE_OK = 0,
    RX_PROBE_FILE_OUT_OF_DATE = 1,
    RX_PROBE_FILE_INVALID = 2,
};

enum RxSurfaceCandidateStatus
{
    RX_SURFACE_FITS = 0,
    RX_SURFACE_TOO_MANY_POINTS = 1,
    RX_SURFACE_TOO_MANY_PROBES = 2,
};

typedef struct RxProbeFileLayout
{
    char magic[4];
    uint32_t version;
    uint32_t sh_coeff_count;
    uint32_t depth_float_count;
    uint32_t max_volumes;
} RxProbeFileLayout;

typedef struct RxProbeFileHeader
{
    RxProbeFileLayout layout;
    uint32_t volume_count;
    uint32_t probe_count;
    uint32_t grid_point_count;
} RxProbeFileHeader;

typedef struct RxReflectionFileHeader
{
    char magic[4];
    uint32_t version;
    uint32_t size;
    uint32_t mips;
    uint32_t probe_count;
} RxReflectionFileHeader;

typedef struct RxProbeVolume
{
    float min_and_count[4];
    float inv_cell_size[4];
    uint32_t dims_and_first[4];
    float max_and_cell_volume[4];
} RxProbeVolume;

typedef struct RxProbeVolumeRange
{
    uint32_t first_probe;
    uint32_t probe_count;
} RxProbeVolumeRange;

typedef struct RxSurfaceCandidate
{
    float volume_min[3];
    float volume_size[3];
    uint32_t dims[3];
    uint32_t needed;
} RxSurfaceCandidate;

uint64_t rx_probe_file_size(uint32_t probe_count, uint32_t grid_point_count);
int32_t rx_probe_validate_file(const RxProbeFileHeader* header, uint64_t file_size, const RxProbeVolume* volumes,
                               const RxProbeVolumeRange* ranges, size_t volume_slots, const uint16_t* grid,
                               size_t grid_count, char* message, size_t message_capacity);
uint64_t rx_probe_reflection_file_size(uint32_t probe_count);
int32_t rx_probe_validate_reflection_file(const RxReflectionFileHeader* header, uint64_t file_size, char* message,
                                          size_t message_capacity);
void rx_probe_volume_data(const float* volume_min, const float* volume_size, const uint32_t* dims,
                          uint32_t first_point, RxProbeVolume* out);
void rx_probe_moments_row(const float* infos, size_t stride, size_t moments_offset, uint32_t probe_count,
                          uint32_t atlas_row, uint16_t* out);
void rx_probe_sample_capture_face(const float* face, uint32_t size, float ndc_x, float ndc_y, float* out);
void rx_probe_reflection_box(const float* local_min, const float* local_max, const float* local_to_world,
                             float* out_box_to_world, float* out_half_extent, float* out_volume);
void rx_probe_reflection_data(const float* box_to_world, const float* half_extent, const float* position,
                              float* out_world_to_box, float* out_position_and_count, float* out_fade);
void rx_probe_base_grid(const float* level_size, uint32_t* out_dims);
int32_t rx_probe_surface_candidate(const RxPlacementBoxes* boxes, const float* region_min, const float* region_max,
                                   float spacing, uint8_t cell_centred, uint32_t probe_budget,
                                   uint32_t point_budget, RxSurfaceCandidate* out);

typedef struct RxCasterState
{
    uint32_t id;
    uint8_t drawable;
    uint8_t has_pose;
    uint8_t has_bones;
    uint32_t pose_hash;
    uint64_t mesh;
    float world_matrix[16];
} RxCasterState;

size_t rx_world_visible_tiles(const RxWorldGrid* grid, const float* view_projection, uint32_t* out, size_t capacity);
uint32_t rx_world_spot_bake_hash(uint32_t atlas_generation, uint32_t tile, const float* shadow_matrix,
                                 const RxCasterState* casters, size_t caster_count);
uint8_t rx_world_skinned_reaches_sphere(const float* world_matrix, float scale, const float* pose_center,
                                        float pose_radius, float padding, const float* center, float radius);

typedef struct RxEntityCore
{
    float matrix[16];
    float position[4];
    float rotation[4];
    float rotation_origin[4];
    float scale;
    uint32_t transform_mode;
    uint32_t id;
    uint8_t flags;
    uint8_t submitted_frames;
} __attribute__((aligned(16))) RxEntityCore;

RxEntityCore* rx_entity_core_new(void);
void rx_entity_core_free(RxEntityCore* core);
void rx_entity_set_position(RxEntityCore* core, const float* position);
void rx_entity_set_rotation(RxEntityCore* core, const float* rotation);
void rx_entity_set_scale(RxEntityCore* core, float scale);
void rx_entity_set_rotation_origin(RxEntityCore* core, const float* origin);
void rx_entity_rotated_by_axis(const RxEntityCore* core, const float* axis, float angle, float* out);
void rx_entity_update_matrix(RxEntityCore* core);
void rx_entity_set_model_matrix(RxEntityCore* core, const float* matrix);
uint8_t rx_entity_submit_needed(RxEntityCore* core, uint32_t frame);
void rx_entity_mark_transform_out_of_date(RxEntityCore* core);
void rx_entity_mark_matrix_out_of_date(RxEntityCore* core);
uint8_t rx_entity_is_matrix_out_of_date(const RxEntityCore* core);
uint8_t rx_entity_is_physics_out_of_date(const RxEntityCore* core);
void rx_entity_set_physics_out_of_date(RxEntityCore* core, uint8_t value);

typedef struct RxObjectCore
{
    float bounds_min[4];
    float bounds_max[4];
    uint32_t tags;
    uint32_t material_id;
    uint32_t physics_id;
    uint32_t parent_id;
    uint32_t bone_buffer_base;
    uint32_t layer;
    uint16_t flags;
    uint16_t instance_slots;
    uint16_t instance_slots_in_use;
} __attribute__((aligned(16))) RxObjectCore;

typedef struct RxObjectStore RxObjectStore;

RxObjectStore* rx_object_store_new(uint32_t capacity);
void rx_object_store_free(RxObjectStore* store);
uint32_t rx_object_store_alloc(const RxObjectStore* store, uint32_t name_hash, RxEntityCore** out_entity,
                               RxObjectCore** out_object);
uint8_t rx_object_store_get(const RxObjectStore* store, uint32_t id, RxEntityCore** out_entity,
                            RxObjectCore** out_object);
uint8_t rx_object_store_is_used(const RxObjectStore* store, uint32_t id);
void rx_object_store_release(const RxObjectStore* store, uint32_t id);
void rx_object_store_set_name_hash(const RxObjectStore* store, uint32_t id, uint32_t hash);
uint32_t rx_object_store_find_by_name(const RxObjectStore* store, uint32_t hash);
size_t rx_object_store_used_ids(const RxObjectStore* store, uint32_t* out, size_t capacity);
size_t rx_object_store_ids_with_tags(const RxObjectStore* store, uint32_t tags, uint32_t* out, size_t capacity);
uint32_t rx_object_store_len(const RxObjectStore* store);
void rx_object_store_clear(const RxObjectStore* store);

typedef struct RxListBuilder RxListBuilder;

typedef struct RxCullStats
{
    uint32_t tested;
    uint32_t culled;
} RxCullStats;

RxListBuilder* rx_list_builder_new(const RxObjectStore* store, const float* view_projection, uint32_t shadow_pipeline);
void rx_list_builder_free(RxListBuilder* builder);
void rx_list_builder_add_tile(RxListBuilder* builder, const RxWorldGrid* grid, uint32_t tile, void* user,
                              uint32_t (*pipeline_for_material)(void* user, uint32_t material),
                              void (*add)(void* user, uint32_t pipeline, uint32_t id));
void rx_list_builder_stats(const RxListBuilder* builder, RxCullStats* out);

RxObjectCore* rx_object_core_new(void);
void rx_object_core_free(RxObjectCore* core);
uint8_t rx_object_is_probe_visible(const RxObjectCore* core);
const uint32_t* rx_object_children(const RxObjectCore* core, size_t* out_count);
void rx_object_add_child(RxObjectCore* core, uint32_t id);
void rx_object_clear_children(RxObjectCore* core);

typedef struct RxLightCore
{
    float shadow_matrix[16];
    uint32_t shadow_atlas_tile;
    uint32_t shadow_bake_hash;
    uint32_t shadow_padding[2];
    uint32_t color;
    float intensity;
    float radius;
    float inner_angle;
    float outer_angle;
    uint32_t light_id;
    uint32_t light_type;
    uint16_t flags;
    uint8_t enabled;
    uint8_t cast_shadows;
} __attribute__((aligned(16))) RxLightCore;

RxLightCore* rx_light_core_new(void);
void rx_light_core_free(RxLightCore* core);
uint8_t rx_light_is_cullable(const RxLightCore* core);

typedef struct RxLightGpuData
{
    float light_camera_matrix[16];
    float position[3];
    float radius;
    uint32_t color;
    uint32_t light_type;
    float intensity;
    float inv_radius_sq;
    float spot_direction[3];
    float spot_cos_outer;
    float linear_color[3];
    float spot_angle_scale;
    float shadow_atlas_rect[4];
} __attribute__((aligned(16))) RxLightGpuData;

typedef struct RxMaterialRecord RxMaterialRecord;

typedef struct RxMaterialProperties
{
    uint32_t flags;
    float alpha;
    float metallic_factor;
    float roughness_factor;
    float specular_factor[3];
    float glossiness_factor;
    float base_color_factor[3];
    float occlusion_strength;
} RxMaterialProperties;

typedef struct RxMaterialComponent
{
    uint8_t exists;
    uint8_t has_image;
    uint8_t loaded;
} RxMaterialComponent;

RxMaterialRecord* rx_material_record_new(void);
void rx_material_record_free(RxMaterialRecord* material);
void rx_material_reset(RxMaterialRecord* material, uint32_t id);
void rx_material_copy_from(RxMaterialRecord* material, const RxMaterialRecord* other);
RxMaterialProperties* rx_material_properties(RxMaterialRecord* material);
uint32_t rx_material_id(const RxMaterialRecord* material);
void rx_material_set_id(RxMaterialRecord* material, uint32_t id);
uint32_t* rx_material_id_ptr(RxMaterialRecord* material);
int32_t rx_material_quality_level(const RxMaterialRecord* material);
void rx_material_lower_quality(RxMaterialRecord* material, int32_t level);
uint8_t rx_material_supports_skinning(const RxMaterialRecord* material);
void rx_material_set_supports_skinning(RxMaterialRecord* material, uint8_t value);
uint8_t rx_material_nearest_filtering(const RxMaterialRecord* material);
void rx_material_set_nearest_filtering(RxMaterialRecord* material, uint8_t value);
uint8_t rx_material_is_built(const RxMaterialRecord* material);
void rx_material_set_built(const RxMaterialRecord* material, uint8_t value);
uint8_t rx_material_is_ready_to_check(const RxMaterialRecord* material);
void rx_material_set_ready_to_check(const RxMaterialRecord* material, uint8_t value);
uint8_t rx_material_requires_sync(const RxMaterialRecord* material);
void rx_material_mark_synced(RxMaterialRecord* material);
void rx_material_set_unlit(RxMaterialRecord* material, uint8_t value);
void rx_material_set_alpha_mask(RxMaterialRecord* material, uint8_t value);
void rx_material_set_double_sided(RxMaterialRecord* material, uint8_t value);
void rx_material_set_alpha(RxMaterialRecord* material, float alpha);
void rx_material_set_metallic_roughness(RxMaterialRecord* material, float metallic, float roughness);
void rx_material_set_specular_glossiness(RxMaterialRecord* material, const float* specular, float glossiness);
void rx_material_set_base_color_factor(RxMaterialRecord* material, const float* color);
void rx_material_set_occlusion_strength(RxMaterialRecord* material, float strength);
uint8_t rx_material_is_transparent(const RxMaterialRecord* material);
uint32_t rx_material_pipeline_features(const RxMaterialRecord* material, uint8_t has_normal_or_orm);
uint8_t rx_material_evaluate_ready(RxMaterialRecord* material, const RxMaterialComponent* components, size_t count);

void rx_light_fill_gpu_data(const RxLightCore* light, const RxEntityCore* entity, const float* directional_matrix,
                            const float* directional_rect, const float* spot_rect, RxLightGpuData* out);

typedef struct RxPlayerState
{
    float position[4];
    float movement_direction[4];
    float camera_offset[4];
    float head_bob_strength[4];
    float speed_multiplier;
    float jump_force;
    uint8_t sprinting;
    uint8_t flags;
    float holster;
    float head_bob[2];
    RxLocomotion locomotion;
    RxViewKick kick;
    RxRecoil recoil;
    RxViewSway sway;
} __attribute__((aligned(16))) RxPlayerState;

RxPlayerState* rx_player_state_new(void);
void rx_player_state_free(RxPlayerState* state);
void rx_player_set_fly_mode(RxPlayerState* state, uint8_t value);
uint8_t rx_player_is_fly_mode(const RxPlayerState* state);
void rx_player_start_sway(RxPlayerState* state, const RxCameraCore* camera);
void rx_player_rotate_camera(RxPlayerState* state, RxCameraCore* camera, float yaw, float pitch);
void rx_player_rotate_head(RxPlayerState* state, RxCameraCore* camera, float yaw, float pitch);
void rx_player_jump(RxPlayerState* state, uint8_t grounded);
void rx_player_move_by(RxPlayerState* state, const float* by);
void rx_player_movement_force(RxPlayerState* state, double delta_time, const float* input, float* out);
void rx_player_update(RxPlayerState* state, RxCameraCore* camera, double delta_time, uint8_t grounded,
                      uint8_t head_bob_enabled);
void rx_player_view_model_pose(RxPlayerState* state, const RxCameraCore* camera, const float* velocity,
                               float delta_time, float* out_position, float* out_rotation);

#define RX_WINDOW_EVENT_QUIT 1u
#define RX_WINDOW_EVENT_RESIZED 2u

void rx_controls_begin_frame(void);
uint32_t rx_controls_pump_sdl(void);
uint8_t rx_controls_is_down(uint32_t code);
uint8_t rx_controls_is_up(uint32_t code);
uint8_t rx_controls_is_pressed(uint32_t code);
void rx_controls_reset_key(uint32_t code);
void rx_controls_post_button(uint32_t code, uint8_t down);
void rx_controls_post_mouse_motion(float x, float y);
void rx_controls_release_all(void);
void rx_controls_release_non_modifiers(void);
void rx_controls_mouse_delta(float* out);
void rx_controls_set_mouse_captured(uint8_t captured, float x, float y);
uint8_t rx_controls_mouse_captured(void);
void rx_controls_captured_mouse_position(float* out);
uint32_t rx_controls_typed_char(void);

#ifdef __cplusplus
}
#endif

