#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaPool_T *VmaPool;

typedef struct VmaAllocationCreateInfo VmaAllocationCreateInfo;

typedef struct allocator allocator;
typedef struct cpumesh cpumesh;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct cputexture cputexture;
typedef struct cgltf_texture cgltf_texture;
typedef struct cgltf_material cgltf_material;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;

typedef struct gpuconstbuffer {
  size_t size;
  gpubuffer host;
  gpubuffer gpu;
  VkSemaphore updated;
} gpuconstbuffer;

typedef struct gpumesh {
  size_t idx_count;
  size_t vtx_count;
  int32_t idx_type;
  size_t size;
  size_t idx_size;
  size_t vtx_size;
  gpubuffer host;
  gpubuffer gpu;
} gpumesh;

typedef struct gpuimage {
  VkImage image;
  VmaAllocation alloc;
} gpuimage;

#define MAX_REGION_COUNT 16

typedef struct gputexture {
  gpubuffer host;
  gpuimage device;
  VkImageView view;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  bool gen_mips;
  uint32_t layer_count;
  uint32_t format;
  uint32_t region_count;
  VkBufferImageCopy regions[MAX_REGION_COUNT];
} gputexture;

typedef struct gpupipeline {
  uint32_t pipeline_id;
  uint32_t pipeline_count;
  uint32_t *pipeline_flags;
  VkPipeline *pipelines;
} gpupipeline;

#define MAX_MATERIAL_TEXTURES 8

/*
  TODO: Fiugre out how to best represent materials
  For now I'm imagining that a material will be able to be implemented
  mostly with 1 uniform buffer for parameters and up to 8 textures for
  other resource bindings.
*/
typedef struct gpumaterial {
  // All material parameters go into one uniform buffer
  // The uniform buffer takes up location 0
  size_t consts_size;
  gpubuffer host_consts;
  gpubuffer gpu_consts;

  // textures take up locations 1-8 in the descriptor set
  uint32_t texture_count;
  gputexture *textures[MAX_MATERIAL_TEXTURES];
} gpumaterial;

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, int32_t buf_usage, gpubuffer *out);
void destroy_gpubuffer(VmaAllocator allocator, const gpubuffer *buffer);

gpuconstbuffer create_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                                     const VkAllocationCallbacks *vk_alloc,
                                     uint64_t size);
void destroy_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                            const VkAllocationCallbacks *vk_alloc,
                            gpuconstbuffer cb);

int32_t create_gpumesh(VkDevice device, VmaAllocator allocator,
                       const cpumesh *src_mesh, gpumesh *dst_mesh);
int32_t create_gpumesh_cgltf(VkDevice device, VmaAllocator allocator,
                             const cgltf_mesh *src_mesh, gpumesh *dst_mesh);
void destroy_gpumesh(VkDevice device, VmaAllocator allocator,
                     const gpumesh *mesh);

int32_t create_gpuimage(VmaAllocator vma_alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,

                        gpuimage *i);
void destroy_gpuimage(VmaAllocator allocator, const gpuimage *image);

gputexture load_ktx2_texture(VkDevice device, VmaAllocator vma_alloc,
                             allocator *tmp_alloc,
                             const VkAllocationCallbacks *vk_alloc,
                             const char *file_path, VmaPool up_pool,
                             VmaPool tex_pool);

int32_t load_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const char *filename, VmaPool up_pool, VmaPool tex_pool,
                     gputexture *t);
int32_t create_texture(VkDevice device, VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
                       const cputexture *tex, VmaPool up_pool, VmaPool tex_pool,
                       gputexture *t);
int32_t create_gputexture_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                const VkAllocationCallbacks *vk_alloc,
                                const cgltf_texture *gltf, const uint8_t *bin,
                                VmaPool up_pool, VmaPool tex_pool,
                                gputexture *t);
void destroy_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const gputexture *t);

int32_t create_gfx_pipeline(VkDevice device,
                            const VkAllocationCallbacks *vk_alloc,
                            VkPipelineCache cache, uint32_t perm_count,
                            VkGraphicsPipelineCreateInfo *create_info_base,
                            gpupipeline **p);
int32_t create_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc,
    VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    uint32_t perm_count, VkRayTracingPipelineCreateInfoKHR *create_info_base,
    gpupipeline **p);
void destroy_gpupipeline(VkDevice device, const VkAllocationCallbacks *vk_alloc,
                         const gpupipeline *p);

int32_t create_gpumaterial_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                 const VkAllocationCallbacks *vk_alloc,
                                 const cgltf_material *gltf, const uint8_t *bin,
                                 gpumaterial *m);
void destroy_material(VkDevice device, VmaAllocator vma_alloc,
                      const VkAllocationCallbacks *vk_alloc,
                      const gpumaterial *m);