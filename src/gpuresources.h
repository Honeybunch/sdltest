#pragma once

#include <stdint.h>

typedef struct VkBuffer_T *VkBuffer;
typedef struct VkDevice_T *VkDevice;
typedef struct VkFence_T *VkFence;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;

typedef struct VmaAllocator_T *VmaAllocator;
typedef struct VmaAllocation_T *VmaAllocation;
typedef struct VmaPool_T *VmaPool;

typedef struct VkImageCreateInfo VkImageCreateInfo;
typedef struct VmaAllocationCreateInfo VmaAllocationCreateInfo;

typedef struct cpumesh cpumesh;

typedef struct gpubuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
} gpubuffer;

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

typedef struct gputexture {
  gpubuffer host;
  gpuimage device;
  VkImageView view;
  uint32_t width;
  uint32_t height;
  uint32_t mip_levels;
  uint32_t format;
} gputexture;

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, int32_t buf_usage, gpubuffer *out);
void destroy_gpubuffer(VmaAllocator allocator, const gpubuffer *buffer);

int32_t create_gpumesh(VkDevice device, VmaAllocator allocator,
                       const cpumesh *src_mesh, gpumesh *dst_mesh);
void destroy_gpumesh(VkDevice device, VmaAllocator allocator,
                     const gpumesh *mesh);

int32_t create_gpuimage(VmaAllocator alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,

                        gpuimage *i);
void destroy_gpuimage(VmaAllocator allocator, const gpuimage *image);

int32_t load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                     VmaPool up_pool, VmaPool tex_pool, gputexture *t);
void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t);