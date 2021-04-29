#include "gpuresources.h"

#include "cpuresources.h"

#include <SDL2/SDL_image.h>
#include <cgltf.h>
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

  size_t index_size = src_mesh->index_size;
  size_t geom_size = src_mesh->geom_size;

  size_t size = index_size + geom_size;

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

  // Actually copy cube data to cpu local buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(allocator, host_buffer.alloc, (void **)&data);

    // Copy Data
    memcpy(data, src_mesh->indices, size);

    vmaUnmapMemory(allocator, host_buffer.alloc);
  }

  *dst_mesh = (gpumesh){src_mesh->index_count, src_mesh->vertex_count,
                        VK_INDEX_TYPE_UINT16,  size,
                        src_mesh->index_size,  src_mesh->geom_size,
                        host_buffer,           device_buffer};

  return err;
}

int32_t create_gpumesh_cgltf(VkDevice device, VmaAllocator allocator,
                             const cgltf_mesh *src_mesh, gpumesh *dst_mesh) {
  assert(src_mesh->primitives_count == 1);
  cgltf_primitive *prim = &src_mesh->primitives[0];

  cgltf_accessor *indices = prim->indices;

  uint32_t index_count = indices->count;
  uint32_t vertex_count = prim->attributes[0].data->count;

  size_t index_size = indices->buffer_view->size;
  size_t geom_size = 0;
  for (uint32_t i = 0; i < prim->attributes_count; ++i) {
    cgltf_accessor *attr = prim->attributes[i].data;
    geom_size += attr->buffer_view->size;
  }

  size_t size = index_size + geom_size;

  gpubuffer host_buffer = {0};
  VkResult err =
      create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);

  gpubuffer device_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  // Actually copy cube data to cpu local buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(allocator, host_buffer.alloc, (void **)&data);

    size_t offset = 0;
    // Copy Index Data
    {
      cgltf_buffer_view *view = indices->buffer_view;
      size_t index_offset = view->offset;
      size_t index_size = view->size;

      void *index_data = ((uint8_t *)view->buffer->data) + index_offset;
      memcpy(data, index_data, index_size);
      offset += index_size;
    }

    // Reorder attributes
    uint32_t *attr_order = alloca(sizeof(uint32_t) * prim->attributes_count);
    for (uint32_t i = 0; i < prim->attributes_count; ++i) {
      cgltf_attribute_type attr_type = prim->attributes[i].type;
      if (attr_type == cgltf_attribute_type_position) {
        attr_order[0] = i;
      } else if (attr_type == cgltf_attribute_type_normal) {
        attr_order[1] = i;
      } else if (attr_type == cgltf_attribute_type_texcoord) {
        attr_order[2] = i;
      }
    }

    for (uint32_t i = 0; i < prim->attributes_count; ++i) {
      uint32_t attr_idx = attr_order[i];
      cgltf_attribute *attr = &prim->attributes[attr_idx];
      cgltf_accessor *accessor = attr->data;
      cgltf_buffer_view *view = accessor->buffer_view;

      size_t attr_offset = view->offset + accessor->offset;
      size_t attr_size = accessor->count * accessor->stride;

      void *attr_data = ((uint8_t *)view->buffer->data) + attr_offset;
      memcpy(data + offset, attr_data, attr_size);
      offset += attr_size;
    }

    vmaUnmapMemory(allocator, host_buffer.alloc);
  }

  *dst_mesh =
      (gpumesh){index_count, vertex_count, VK_INDEX_TYPE_UINT16, size,
                index_size,  geom_size,    host_buffer,          device_buffer};
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

SDL_Surface *load_and_transform_image(const char *filename) {
  SDL_Surface *img = IMG_Load(filename);
  assert(img);

  SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  SDL_Surface *opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
  SDL_FreeSurface(img);

  return opt_img;
}

int32_t load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                     VmaPool up_pool, VmaPool tex_pool, gputexture *t) {
  assert(filename);
  assert(t);

  SDL_Surface *img = load_and_transform_image(filename);

  VkResult err = VK_SUCCESS;

  uint32_t img_width = img->w;
  uint32_t img_height = img->h;

  size_t host_buffer_size = img->pitch * img_height;

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

    memcpy_s(data, host_buffer_size, img->pixels, host_buffer_size);

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
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = mip_levels;
  t->layer_count = 1;
  t->view = view;

  SDL_FreeSurface(img);

  return err;
}

int32_t load_skybox(VkDevice device, VmaAllocator alloc,
                    const char *folder_path, VmaPool up_pool, VmaPool tex_pool,
                    gputexture *t) {
  assert(folder_path);
  assert(t);

  const uint32_t skybox_side_count = 6;
  // ORDER MATTERS
  const char *file_names[6] = {"right",  "left",  "top",
                               "bottom", "front", "back"};

  // TODO: Use some sort of arena allocator

  // Load all images and count up how much space we need for them
  SDL_Surface *skybox_imgs[6] = {0};
  int32_t img_width = 0;
  int32_t img_height = 0;
  size_t host_buffer_size = 0;
  {
    const size_t max_file_name_len = 512;
    char *file_path = malloc(max_file_name_len);
    assert(file_path);

    for (uint32_t i = 0; i < skybox_side_count; ++i) {
      SDL_snprintf(file_path, max_file_name_len, "%s/%s.png", folder_path,
                   file_names[i]);

      SDL_Surface *img = load_and_transform_image(file_path);
      assert(img);

      host_buffer_size += (img->pitch * img->h);

      // Every face of the skybox needs a texture of the same size
      assert(img_width == 0 || img_width == img->w);
      assert(img_height == 0 || img_height == img->h);

      img_width = img->w;
      img_height = img->h;
      skybox_imgs[i] = img;
    }
    free(file_path);
  }

  VkResult err = VK_SUCCESS;

  // Allocate host buffer for image data
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

  // Allocate device image
  gpuimage device_image = {0};
  {
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo img_info = {0};
    img_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.extent = (VkExtent3D){img_width, img_height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = skybox_side_count;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    create_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, skybox_side_count};
    err = vkCreateImageView(device, &create_info, NULL, &view);
    assert(err == VK_SUCCESS);
  };

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(alloc, host_buffer.alloc, (void **)&data);

    uint32_t offset = 0;
    uint32_t img_size = 0;
    for (uint32_t i = 0; i < skybox_side_count; ++i) {
      SDL_Surface *img = skybox_imgs[i];
      img_size = (img->pitch * img->h);

      memcpy_s(data + offset, img_size, img->pixels, img_size);
      offset += img_size;
    }
    vmaUnmapMemory(alloc, host_buffer.alloc);
  }

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = 1;
  t->layer_count = skybox_side_count;
  t->view = view;

  // Free up sdl surfaces
  for (uint32_t i = 0; i < skybox_side_count; ++i) {
    SDL_FreeSurface(skybox_imgs[i]);
  }

  return err;
}

int32_t create_texture(VkDevice device, VmaAllocator alloc,
                       const cputexture *tex, VmaPool up_pool, VmaPool tex_pool,
                       gputexture *t) {
  VkResult err = VK_SUCCESS;

  VkDeviceSize host_buffer_size = tex->data_size;
  uint32_t layer_count = tex->layer_count;
  uint32_t mip_count = tex->mip_count;
  const texture_mip *tex_mip = &tex->layers[0].mips[0];
  uint32_t img_width = tex_mip->width;
  uint32_t img_height = tex_mip->height;

  // Allocate host buffer for image data
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

  uint32_t desired_mip_levels = floor(log2(max(img_width, img_height))) + 1;

  // Allocate device image
  gpuimage device_image = {0};
  {
    VkImageUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo img_info = {0};
    img_info.flags = 0;
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.extent = (VkExtent3D){img_width, img_height, 1};
    img_info.mipLevels = desired_mip_levels;
    img_info.arrayLayers = layer_count;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
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
        VK_IMAGE_ASPECT_COLOR_BIT, 0, desired_mip_levels, 0, layer_count};
    err = vkCreateImageView(device, &create_info, NULL, &view);
    assert(err == VK_SUCCESS);
  };

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(alloc, host_buffer.alloc, (void **)&data);

    uint64_t data_size = tex->data_size;
    memcpy_s(data, data_size, tex->data, data_size);

    vmaUnmapMemory(alloc, host_buffer.alloc);
  }

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = desired_mip_levels;
  t->layer_count = tex->layer_count;
  t->view = view;

  return err;
}

void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t) {
  destroy_gpubuffer(alloc, &t->host);
  destroy_gpuimage(alloc, &t->device);
  vkDestroyImageView(device, t->view, NULL);
}