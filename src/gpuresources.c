#include "gpuresources.h"

#include "cpuresources.h"

#include <SDL2/SDL_image.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include <assert.h>
#include <stdio.h>

int32_t create_gpubuffer(VmaAllocator allocator, uint64_t size,
                         int32_t mem_usage, int32_t buf_usage, gpubuffer *out) {
  VkResult err = VK_SUCCESS;
  VkBuffer buffer = {0};
  VmaAllocation alloc = {0};
  VmaAllocationInfo alloc_info = {0};
  {
    VkMemoryRequirements mem_reqs = {size, 16, 0};
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = mem_usage;
    VkBufferCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size;
    create_info.usage = buf_usage;
    err = vmaCreateBuffer(allocator, &create_info, &alloc_create_info, &buffer,
                          &alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }
  *out = (gpubuffer){buffer, alloc};

  return err;
}

void destroy_gpubuffer(VmaAllocator allocator, const gpubuffer *buffer) {
  vmaDestroyBuffer(allocator, buffer->buffer, buffer->alloc);
}

int32_t create_gpumesh(VkDevice device, VmaAllocator allocator,
                       const cpumesh *src_mesh, gpumesh *dst_mesh) {
  VkResult err = VK_SUCCESS;

  size_t idx_size = src_mesh->index_size;
  size_t geom_size = src_mesh->geom_size;

  size_t size = idx_size + geom_size;

  gpubuffer host_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);

  gpubuffer device_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  *dst_mesh = (gpumesh){src_mesh->index_count, src_mesh->vertex_count,
                        VK_INDEX_TYPE_UINT16,  size,
                        src_mesh->index_size,  src_mesh->geom_size,
                        host_buffer,           device_buffer};

  return err;
}

void destroy_gpumesh(VkDevice device, VmaAllocator allocator,
                     const gpumesh *mesh) {
  destroy_gpubuffer(allocator, &mesh->host);
  destroy_gpubuffer(allocator, &mesh->gpu);
}

int32_t create_gpuimage(VmaAllocator alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,
                        gpuimage *i) {
  VkResult err = VK_SUCCESS;
  gpuimage img = {0};

  VmaAllocationInfo alloc_info = {0};
  err = vmaCreateImage(alloc, img_create_info, alloc_create_info, &img.image,
                       &img.alloc, &alloc_info);
  assert(err == VK_SUCCESS);

  if (err == VK_SUCCESS) {
    *i = img;
  }
  return err;
}

void destroy_gpuimage(VmaAllocator allocator, const gpuimage *image) {
  vmaDestroyImage(allocator, image->image, image->alloc);
}

int32_t load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                     VmaPool up_pool, VmaPool tex_pool, gputexture *t) {
  assert(filename);
  assert(t);

  SDL_Surface *opt_img = NULL;
  {
    SDL_Surface *img = IMG_Load(filename);
    assert(img);

    SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

    opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
    SDL_FreeSurface(img);
    SDL_FreeFormat(opt_fmt);
  }
  assert(opt_img);

  VkResult err = VK_SUCCESS;

  uint32_t img_width = opt_img->w;
  uint32_t img_height = opt_img->h;

  size_t host_buffer_size = opt_img->pitch * img_height;

  gpubuffer host_buffer = {0};
  {
    VkBufferCreateInfo buffer_create_info = {0};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = host_buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_create_info.pool = up_pool;
    VmaAllocationInfo alloc_info = {0};
    err = vmaCreateBuffer(alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }

  uint32_t mip_levels = floor(log2(max(img_width, img_height))) + 1;

  gpuimage device_image = {0};
  {
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // If we need to generate mips we'll need to mark the image as being able to
    // be copied from
    if (mip_levels > 1) {
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo img_info = {0};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D; // Assuming for now
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.extent = (VkExtent3D){img_width, img_height, 1};
    img_info.mipLevels = mip_levels;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(alloc, host_buffer.alloc, (void **)&data);

    memcpy_s(data, host_buffer_size, opt_img->pixels, host_buffer_size);

    vmaUnmapMemory(alloc, host_buffer.alloc);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1};
    err = vkCreateImageView(device, &create_info, NULL, &view);
    assert(err == VK_SUCCESS);
  };

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = opt_img->w;
  t->height = opt_img->h;
  t->mip_levels = mip_levels;
  t->view = view;

  SDL_FreeSurface(opt_img);

  return err;
}

void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t) {
  destroy_gpubuffer(alloc, &t->host);
  destroy_gpuimage(alloc, &t->device);
  vkDestroyImageView(device, t->view, NULL);
}