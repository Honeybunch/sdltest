#include "gpuresources.h"

#include "allocator.h"
#include "cpuresources.h"

#include <SDL2/SDL_image.h>
#include <cgltf.h>
#include <ktx.h>
#include <optick_capi.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include <assert.h>
#include <stddef.h>
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

gpuconstbuffer create_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                                     const VkAllocationCallbacks *vk_alloc,
                                     uint64_t size) {
  gpubuffer host_buffer = {0};
  VkResult err =
      create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);

  gpubuffer device_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  VkSemaphore sem = VK_NULL_HANDLE;
  {
    VkSemaphoreCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    err = vkCreateSemaphore(device, &create_info, vk_alloc, &sem);
    assert(err == VK_SUCCESS);
  }

  gpuconstbuffer cb = {
      .size = size,
      .host = host_buffer,
      .gpu = device_buffer,
      .updated = sem,
  };
  return cb;
}

void destroy_gpuconstbuffer(VkDevice device, VmaAllocator allocator,
                            const VkAllocationCallbacks *vk_alloc,
                            gpuconstbuffer cb) {
  destroy_gpubuffer(allocator, &cb.host);
  destroy_gpubuffer(allocator, &cb.gpu);
  vkDestroySemaphore(device, cb.updated, vk_alloc);
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
      size_t index_offset = indices->offset + view->offset;
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
      size_t attr_size = accessor->stride * accessor->count;

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

int32_t create_gpuimage(VmaAllocator vma_alloc,
                        const VkImageCreateInfo *img_create_info,
                        const VmaAllocationCreateInfo *alloc_create_info,
                        gpuimage *i) {
  VkResult err = VK_SUCCESS;
  gpuimage img = {0};

  VmaAllocationInfo alloc_info = {0};
  err = vmaCreateImage(vma_alloc, img_create_info, alloc_create_info,
                       &img.image, &img.alloc, &alloc_info);
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

SDL_Surface *parse_and_transform_image(const uint8_t *data, size_t size) {
  SDL_RWops *ops = SDL_RWFromMem((void *)data, size);
  SDL_Surface *img = IMG_Load_RW(ops, 0);
  assert(img);

  SDL_PixelFormat *opt_fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);

  SDL_Surface *opt_img = SDL_ConvertSurface(img, opt_fmt, 0);
  SDL_FreeSurface(img);

  return opt_img;
}

static VkImageType get_ktx2_image_type(const ktxTexture2 *t) {
  return (VkImageType)(t->numDimensions - 1);
}

static VkImageViewType get_ktx2_image_view_type(const ktxTexture2 *t) {
  VkImageType img_type = get_ktx2_image_type(t);

  bool cube = t->isCubemap;
  bool array = t->isArray;

  if (img_type == VK_IMAGE_TYPE_1D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_1D;
    }
  } else if (img_type == VK_IMAGE_TYPE_2D) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    } else {
      return VK_IMAGE_VIEW_TYPE_2D;
    }

  } else if (img_type == VK_IMAGE_TYPE_3D) {
    // No such thing as a 3D array
    return VK_IMAGE_VIEW_TYPE_3D;
  } else if (cube) {
    if (array) {
      return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_CUBE;
  }

  assert(0);
  return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

typedef struct ktx2_cb_data {
  VkBufferImageCopy *region; // Specify destination region in final image.
  VkDeviceSize offset;       // Offset of current level in staging buffer
  uint32_t num_faces;
  uint32_t num_layers;
} ktx2_cb_data;

static ktx_error_code_e ktx2_optimal_tiling_callback(
    int32_t mip_level, int32_t face, int32_t width, int32_t height,
    int32_t depth, uint64_t face_lod_size, void *pixels, void *userdata) {
  ktx2_cb_data *ud = (ktx2_cb_data *)userdata;
  (void)pixels;

  ud->region->bufferOffset = ud->offset;
  ud->offset += face_lod_size;
  // These 2 are expressed in texels.
  ud->region->bufferRowLength = 0;
  ud->region->bufferImageHeight = 0;
  ud->region->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ud->region->imageSubresource.mipLevel = mip_level;
  ud->region->imageSubresource.baseArrayLayer = face;
  ud->region->imageSubresource.layerCount = ud->num_layers * ud->num_faces;
  ud->region->imageOffset.x = 0;
  ud->region->imageOffset.y = 0;
  ud->region->imageOffset.z = 0;
  ud->region->imageExtent.width = width;
  ud->region->imageExtent.height = height;
  ud->region->imageExtent.depth = depth;

  ud->region += 1;

  return KTX_SUCCESS;
}

gputexture load_ktx2_texture(VkDevice device, VmaAllocator vma_alloc,
                             allocator *tmp_alloc,
                             const VkAllocationCallbacks *vk_alloc,
                             const char *file_path, VmaPool up_pool,
                             VmaPool tex_pool) {
  gputexture t = {0};

  uint8_t *mem = NULL;
  size_t size = 0;
  {
    SDL_RWops *file = SDL_RWFromFile(file_path, "rb");
    if (file == NULL) {
      assert(0);
      return t;
    }

    size = (size_t)file->size(file);
    mem = hb_alloc(*tmp_alloc, size);
    assert(mem);

    // Read file into memory
    if (file->read(file, mem, size, 1) == 0) {
      file->close(file);
      assert(0);
      return t;
    }
    file->close(file);
  }

  ktxTextureCreateFlags flags = KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT;
  ktxTexture2 *ktx = NULL;
  uint32_t component_count = 0;
  {
    ktx_error_code_e err = ktxTexture2_CreateFromMemory(mem, size, flags, &ktx);
    if (err != KTX_SUCCESS) {
      assert(0);
      return t;
    }

    bool needs_transcoding = ktxTexture2_NeedsTranscoding(ktx);
    if (needs_transcoding) {
      // TODO: pre-calculate the best format for the platform
      err = ktxTexture2_TranscodeBasis(ktx, KTX_TTF_BC7_RGBA, 0);
      if (err != KTX_SUCCESS) {
        assert(0);
        return t;
      }
    }
  }

  VkResult err = VK_SUCCESS;

  size_t host_buffer_size = ktx->dataSize;
  uint32_t width = ktx->baseWidth;
  uint32_t height = ktx->baseHeight;
  uint32_t depth = ktx->baseDepth;
  uint32_t layers = ktx->numLayers;
  uint32_t mip_levels = ktx->numLevels;
  VkFormat format = (VkFormat)ktx->vkFormat;
  bool gen_mips = ktx->generateMipmaps;

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
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    if (err != VK_SUCCESS) {
      assert(0);
      return t;
    }
  }

  gpuimage device_image = {0};
  {
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // If we need to generate mips we'll need to mark the image as being able to
    // be copied from
    if (gen_mips) {
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkImageCreateInfo img_info = {0};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = get_ktx2_image_type(ktx);
    img_info.format = format;
    img_info.extent = (VkExtent3D){width, height, depth};
    img_info.mipLevels = mip_levels;
    img_info.arrayLayers = layers;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = usage;
    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pool = tex_pool;
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
    if (err != VK_SUCCESS) {
      assert(0);
      return t;
    }
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    err = vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);
    if (err != VK_SUCCESS) {
      assert(0);
      return t;
    }

    memcpy(data, ktx->pData, host_buffer_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  // Create Image View
  VkImageView view = VK_NULL_HANDLE;
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = device_image.image;
    create_info.viewType = get_ktx2_image_view_type(ktx);
    create_info.format = format;
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, layers};
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    if (err != VK_SUCCESS) {
      assert(0);
      return t;
    }
  };

  uint32_t region_count = mip_levels;
  if (gen_mips) {
    region_count = 1;
  }
  assert(region_count < MAX_REGION_COUNT);

  t.host = host_buffer;
  t.device = device_image;
  t.format = format;
  t.width = width;
  t.height = height;
  t.mip_levels = mip_levels;
  t.gen_mips = gen_mips;
  t.layer_count = layers;
  t.view = view;
  t.region_count = region_count;

  // Gather Copy Regions
  {
    ktx2_cb_data cb_data = {
        .num_faces = ktx->numFaces,
        .num_layers = ktx->numLayers,
        .region = t.regions,
    };

    ktxTexture_IterateLevels(ktx, ktx2_optimal_tiling_callback, &cb_data);
  }

  return t;
}

int32_t load_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const char *filename, VmaPool up_pool, VmaPool tex_pool,
                     gputexture *t) {
  OPTICK_C_PUSH(optick_e, "load_texture", OptickAPI_Category_None);
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
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }

  uint32_t mip_levels = floor(log2(SDL_max(img_width, img_height))) + 1;

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
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
    assert(err == VK_SUCCESS);
  }

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

    memcpy(data, img->pixels, host_buffer_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
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
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    assert(err == VK_SUCCESS);
  };

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = mip_levels;
  t->gen_mips = mip_levels > 1;
  t->layer_count = 1;
  t->view = view;
  t->region_count = 1;
  t->regions[0] = (VkBufferImageCopy){
      .imageExtent =
          {
              .width = img_width,
              .height = img_height,
              .depth = 1,
          },
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
          },
  };

  SDL_FreeSurface(img);

  OptickAPI_PopEvent(optick_e);

  return err;
}

int32_t create_gputexture_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                const VkAllocationCallbacks *vk_alloc,
                                const cgltf_texture *gltf, const uint8_t *bin,
                                VmaPool up_pool, VmaPool tex_pool,
                                gputexture *t) {
  cgltf_buffer_view *image_view = gltf->image->buffer_view;
  cgltf_buffer *image_data = image_view->buffer;
  const uint8_t *data = (uint8_t *)(image_view->buffer) + image_view->offset;

  if (image_data->uri == NULL) {
    data = bin + image_view->offset;
  }

  size_t size = image_view->size;

  SDL_Surface *image = parse_and_transform_image(data, size);
  uint32_t image_width = image->w;
  uint32_t image_height = image->h;
  uint8_t *image_pixels = image->pixels;
  size_t image_size = image->pitch * image_height;

  texture_mip mip = {
      image_width,
      image_height,
      1,
      image_pixels,
  };

  texture_layer layer = {
      image_width,
      image_height,
      1,
      &mip,
  };
  cputexture cpu_tex = {
      1, 1, &layer, image_size, image_pixels,
  };
  return create_texture(device, vma_alloc, vk_alloc, &cpu_tex, up_pool,
                        tex_pool, t);
}

int32_t create_texture(VkDevice device, VmaAllocator vma_alloc,
                       const VkAllocationCallbacks *vk_alloc,
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
    err = vmaCreateBuffer(vma_alloc, &buffer_create_info, &alloc_create_info,
                          &host_buffer.buffer, &host_buffer.alloc, &alloc_info);
    assert(err == VK_SUCCESS);
  }

  uint32_t desired_mip_levels = floor(log2(SDL_max(img_width, img_height))) + 1;

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
    err = create_gpuimage(vma_alloc, &img_info, &alloc_info, &device_image);
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
    err = vkCreateImageView(device, &create_info, vk_alloc, &view);
    assert(err == VK_SUCCESS);
  };

  // Copy data to host buffer
  {
    uint8_t *data = NULL;
    vmaMapMemory(vma_alloc, host_buffer.alloc, (void **)&data);

    uint64_t data_size = tex->data_size;
    memcpy(data, tex->data, data_size);

    vmaUnmapMemory(vma_alloc, host_buffer.alloc);
  }

  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = img_width;
  t->height = img_height;
  t->mip_levels = desired_mip_levels;
  t->gen_mips = desired_mip_levels > 1;
  t->layer_count = tex->layer_count;
  t->view = view;
  t->region_count = 1;
  t->regions[0] = (VkBufferImageCopy){
      .imageExtent =
          {
              .width = img_width,
              .height = img_height,
              .depth = 1,
          },
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = tex->layer_count,
          },
  };

  return err;
}

void destroy_texture(VkDevice device, VmaAllocator vma_alloc,
                     const VkAllocationCallbacks *vk_alloc,
                     const gputexture *t) {
  destroy_gpubuffer(vma_alloc, &t->host);
  destroy_gpuimage(vma_alloc, &t->device);
  vkDestroyImageView(device, t->view, vk_alloc);
}

static gpupipeline *alloc_gpupipeline(uint32_t perm_count) {
  size_t pipe_handles_size = sizeof(VkPipeline) * perm_count;
  size_t flags_size = sizeof(uint32_t) * perm_count;
  size_t pipeline_size = sizeof(gpupipeline);
  size_t alloc_size = pipeline_size + pipe_handles_size + flags_size;
  gpupipeline *p = (gpupipeline *)calloc(1, alloc_size);
  uint8_t *mem = (uint8_t *)p;
  assert(p);
  p->pipeline_count = perm_count;

  size_t offset = pipeline_size;

  p->pipeline_flags = (uint32_t *)(mem + offset);
  offset += flags_size;

  p->pipelines = (VkPipeline *)(mem + offset);
  offset += pipe_handles_size;

  return p;
}

int32_t create_gfx_pipeline(VkDevice device,
                            const VkAllocationCallbacks *vk_alloc,
                            VkPipelineCache cache, uint32_t perm_count,
                            VkGraphicsPipelineCreateInfo *create_info_base,
                            gpupipeline **p) {
  gpupipeline *pipe = alloc_gpupipeline(perm_count);
  VkResult err = VK_SUCCESS;

  VkGraphicsPipelineCreateInfo *pipe_create_info =
      (VkGraphicsPipelineCreateInfo *)alloca(
          sizeof(VkGraphicsPipelineCreateInfo) * perm_count);
  assert(pipe_create_info);

  uint32_t stage_count = create_info_base->stageCount;
  uint32_t perm_stage_count = perm_count * stage_count;

  // Every shader stage needs its own create info
  VkPipelineShaderStageCreateInfo *pipe_stage_info =
      (VkPipelineShaderStageCreateInfo *)alloca(
          sizeof(VkPipelineShaderStageCreateInfo) * perm_stage_count);

  VkSpecializationMapEntry map_entries[1] = {
      {0, 0, sizeof(uint32_t)},
  };

  VkSpecializationInfo *spec_info =
      (VkSpecializationInfo *)alloca(sizeof(VkSpecializationInfo) * perm_count);
  uint32_t *flags = (uint32_t *)alloca(sizeof(uint32_t) * perm_count);

  // Insert specialization info to every shader stage
  for (uint32_t i = 0; i < perm_count; ++i) {
    pipe_create_info[i] = *create_info_base;

    flags[i] = i;
    spec_info[i] = (VkSpecializationInfo){
        1,
        map_entries,
        sizeof(uint32_t),
        &flags[i],
    };

    uint32_t stage_idx = i * stage_count;
    for (uint32_t ii = 0; ii < stage_count; ++ii) {
      VkPipelineShaderStageCreateInfo *stage = &pipe_stage_info[stage_idx + ii];
      *stage = create_info_base->pStages[ii];
      stage->pSpecializationInfo = &spec_info[i];
    }
    pipe_create_info[i].pStages = &pipe_stage_info[stage_idx];
  }

  err = vkCreateGraphicsPipelines(device, cache, perm_count, pipe_create_info,
                                  vk_alloc, pipe->pipelines);
  assert(err == VK_SUCCESS);

  *p = pipe;
  return err;
}

int32_t create_rt_pipeline(
    VkDevice device, const VkAllocationCallbacks *vk_alloc,
    VkPipelineCache cache,
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelines,
    uint32_t perm_count, VkRayTracingPipelineCreateInfoKHR *create_info_base,
    gpupipeline **p) {
  gpupipeline *pipe = alloc_gpupipeline(perm_count);
  VkResult err = VK_SUCCESS;

  VkRayTracingPipelineCreateInfoKHR *pipe_create_info =
      (VkRayTracingPipelineCreateInfoKHR *)alloca(
          sizeof(VkRayTracingPipelineCreateInfoKHR) * perm_count);
  assert(pipe_create_info);

  uint32_t stage_count = create_info_base->stageCount;
  uint32_t perm_stage_count = perm_count * stage_count;

  // Every shader stage needs its own create info
  VkPipelineShaderStageCreateInfo *pipe_stage_info =
      (VkPipelineShaderStageCreateInfo *)alloca(
          sizeof(VkPipelineShaderStageCreateInfo) * perm_stage_count);

  VkSpecializationMapEntry map_entries[1] = {
      {0, 0, sizeof(uint32_t)},
  };

  VkSpecializationInfo *spec_info =
      (VkSpecializationInfo *)alloca(sizeof(VkSpecializationInfo) * perm_count);
  uint32_t *flags = (uint32_t *)alloca(sizeof(uint32_t) * perm_count);

  // Insert specialization info to every shader stage
  for (uint32_t i = 0; i < perm_count; ++i) {
    pipe_create_info[i] = *create_info_base;

    flags[i] = i;
    spec_info[i] = (VkSpecializationInfo){
        1,
        map_entries,
        sizeof(uint32_t),
        &flags[i],
    };

    uint32_t stage_idx = i * stage_count;
    for (uint32_t ii = 0; ii < stage_count; ++ii) {
      VkPipelineShaderStageCreateInfo *stage = &pipe_stage_info[stage_idx + ii];
      *stage = create_info_base->pStages[ii];
      stage->pSpecializationInfo = &spec_info[i];
    }
    pipe_create_info[i].pStages = &pipe_stage_info[stage_idx];
  }

  err =
      vkCreateRayTracingPipelines(device, VK_NULL_HANDLE, cache, perm_count,
                                  pipe_create_info, vk_alloc, pipe->pipelines);
  assert(err == VK_SUCCESS);

  *p = pipe;
  return err;
}

void destroy_gpupipeline(VkDevice device, const VkAllocationCallbacks *vk_alloc,
                         const gpupipeline *p) {
  for (uint32_t i = 0; i < p->pipeline_count; ++i) {
    vkDestroyPipeline(device, p->pipelines[i], vk_alloc);
  }
  free((void *)p);
}

int32_t create_gpumaterial_cgltf(VkDevice device, VmaAllocator vma_alloc,
                                 const VkAllocationCallbacks *vk_alloc,
                                 const cgltf_material *gltf, const uint8_t *bin,
                                 gpumaterial *m) {
  VkResult err = VK_SUCCESS;

  return err;
}
void destroy_material(VkDevice device, VmaAllocator vma_alloc,
                      const VkAllocationCallbacks *vk_alloc,
                      const gpumaterial *m) {}