#include "gputexture.h"

#include <SDL2/SDL_image.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include <assert.h>
#include <stdio.h>

static VkResult create_image(VmaAllocator alloc,
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

void load_texture(VkDevice device, VmaAllocator alloc, const char *filename,
                  gputexture *t) {
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

  VkFence uploaded = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    err = vkCreateFence(device, &create_info, NULL, &uploaded);
    assert(err == VK_SUCCESS);
  }

  uint32_t img_width = opt_img->w;
  uint32_t img_height = opt_img->h;

  size_t host_buffer_size = img_width * img_height * (sizeof(uint8_t) * 4);

  gpubuffer host_buffer = {0};
  {
    VkBufferCreateInfo buffer_create_info = {0};
    buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_create_info.size = host_buffer_size;
    buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo alloc_create_info = {0};
    alloc_create_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
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
    err = create_image(alloc, &img_info, &alloc_info, &device_image);
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

  t->uploaded = uploaded;
  t->host = host_buffer;
  t->device = device_image;
  t->format = VK_FORMAT_R8G8B8A8_SRGB;
  t->width = opt_img->w;
  t->height = opt_img->h;
  t->mip_levels = mip_levels;
  t->view = view;

  SDL_FreeSurface(opt_img);
}

void destroy_texture(VkDevice device, VmaAllocator alloc, const gputexture *t) {
  vkDestroyFence(device, t->uploaded, NULL);
  vmaDestroyBuffer(alloc, t->host.buffer, t->host.alloc);
  vmaDestroyImage(alloc, t->device.image, t->device.alloc);
  vkDestroyImageView(device, t->view, NULL);
}