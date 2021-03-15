#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include "fractal.h"
#include "mesh.h"
#include "shadercommon.h"
#include "simd.h"

#include "cube.h"
#include "gpumesh.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifndef FINAL
#define VALIDATION
#endif
#define FRAME_LATENCY 3
#define MESH_UPLOAD_QUEUE_SIZE 16

#define WIDTH 1600
#define HEIGHT 900

typedef struct demo {
  VkInstance instance;

  VkPhysicalDevice gpu;
  VkPhysicalDeviceProperties gpu_props;
  uint32_t queue_family_count;
  VkQueueFamilyProperties *queue_props;
  VkPhysicalDeviceFeatures gpu_features;

  VkSurfaceKHR surface;
  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;
  bool separate_present_queue;

  VkDevice device;
  VkQueue present_queue;
  VkQueue graphics_queue;

  VmaAllocator allocator;

  VkFormat swapchain_image_format;
  VkSwapchainKHR swapchain;
  uint32_t swapchain_image_count;
  uint32_t swap_width;
  uint32_t swap_height;

  VkRenderPass render_pass;
  VkPipelineCache pipeline_cache;

  VkPipelineLayout pipeline_layout;
  VkPipeline fractal_pipeline;
  VkPipeline mesh_pipeline;

  VkImage swapchain_images[FRAME_LATENCY];
  VkImageView swapchain_image_views[FRAME_LATENCY];
  VkFramebuffer swapchain_framebuffers[FRAME_LATENCY];

  VkCommandPool command_pools[FRAME_LATENCY];
  VkCommandBuffer upload_buffers[FRAME_LATENCY];
  VkCommandBuffer graphics_buffers[FRAME_LATENCY];

  // For allowing the currently processed frame to access
  // resources being uploaded this frame
  VkSemaphore upload_complete_sems[FRAME_LATENCY];
  VkSemaphore img_acquired_sems[FRAME_LATENCY];
  VkSemaphore swapchain_image_sems[FRAME_LATENCY];
  VkSemaphore render_complete_sems[FRAME_LATENCY];

  uint32_t frame_idx;
  uint32_t swap_img_idx;
  VkFence fences[FRAME_LATENCY];

  gpumesh cube_gpu;

  uint32_t mesh_upload_count;
  gpumesh mesh_upload_queue[MESH_UPLOAD_QUEUE_SIZE];

  PushConstants push_constants;
} demo;

static bool check_layer(const char *check_name, uint32_t layer_count,
                        VkLayerProperties *layers) {
  bool found = false;
  for (uint32_t i = 0; i < layer_count; i++) {
    if (!strcmp(check_name, layers[i].layerName)) {
      found = true;
      break;
    }
  }
  return found;
}

static VkPhysicalDevice select_gpu(VkInstance instance) {
  uint32_t gpu_count = 0;
  VkResult err = vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
  assert(err == VK_SUCCESS);

  VkPhysicalDevice *physical_devices =
      alloca(sizeof(VkPhysicalDevice) * gpu_count);
  err = vkEnumeratePhysicalDevices(instance, &gpu_count, physical_devices);
  assert(err == VK_SUCCESS);

  /* Try to auto select most suitable device */
  int32_t gpu_idx = -1;
  {
    uint32_t count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU + 1];
    memset(count_device_type, 0, sizeof(count_device_type));

    VkPhysicalDeviceProperties physicalDeviceProperties;
    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(physical_devices[i],
                                    &physicalDeviceProperties);
      assert(physicalDeviceProperties.deviceType <=
             VK_PHYSICAL_DEVICE_TYPE_CPU);
      count_device_type[physicalDeviceProperties.deviceType]++;
    }

    VkPhysicalDeviceType search_for_device_type =
        VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_CPU]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_CPU;
    } else if (count_device_type[VK_PHYSICAL_DEVICE_TYPE_OTHER]) {
      search_for_device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    }

    for (uint32_t i = 0; i < gpu_count; i++) {
      vkGetPhysicalDeviceProperties(physical_devices[i],
                                    &physicalDeviceProperties);
      if (physicalDeviceProperties.deviceType == search_for_device_type) {
        gpu_idx = i;
        break;
      }
    }
  }
  assert(gpu_idx >= 0);
  return physical_devices[gpu_idx];
}

static VkDevice create_device(VkPhysicalDevice gpu,
                              uint32_t graphics_queue_family_index,
                              uint32_t present_queue_family_index,
                              uint32_t ext_count,
                              const char *const *ext_names) {
  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[2];
  queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queues[0].pNext = NULL;
  queues[0].queueFamilyIndex = graphics_queue_family_index;
  queues[0].queueCount = 1;
  queues[0].pQueuePriorities = queue_priorities;
  queues[0].flags = 0;

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
  create_info.pNext = NULL, create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = queues;
  create_info.enabledExtensionCount = ext_count;
  create_info.ppEnabledExtensionNames = ext_names;
  create_info.pEnabledFeatures =
      NULL; // If specific features are required, pass them in here

  if (present_queue_family_index != graphics_queue_family_index) {
    queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[1].pNext = NULL;
    queues[1].queueFamilyIndex = present_queue_family_index;
    queues[1].queueCount = 1;
    queues[1].pQueuePriorities = queue_priorities;
    queues[1].flags = 0;
    create_info.queueCreateInfoCount = 2;
  }

  VkDevice device = VK_NULL_HANDLE;
  VkResult err = vkCreateDevice(gpu, &create_info, NULL, &device);
  assert(err == VK_SUCCESS);

  return device;
}

static VkSurfaceFormatKHR
pick_surface_format(VkSurfaceFormatKHR *surface_formats,
                    uint32_t format_count) {
  // Prefer non-SRGB formats...
  for (uint32_t i = 0; i < format_count; i++) {
    const VkFormat format = surface_formats[i].format;

    if (format == VK_FORMAT_R8G8B8A8_UNORM ||
        format == VK_FORMAT_B8G8R8A8_UNORM ||
        format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
        format == VK_FORMAT_A2R10G10B10_UNORM_PACK32 ||
        format == VK_FORMAT_R16G16B16A16_SFLOAT) {
      return surface_formats[i];
    }
  }

  assert(format_count >= 1);
  return surface_formats[0];
}

static VkResult create_gpubuffer(VmaAllocator allocator, VkDeviceSize size,
                                 VmaMemoryUsage mem_usage,
                                 VkBufferUsageFlags buf_usage, gpubuffer *out) {
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

static VkResult create_mesh(VkDevice device, VmaAllocator allocator,
                            const cpumesh *src_mesh, gpumesh *dst_mesh) {
  VkResult err = VK_SUCCESS;

  size_t size = src_mesh->size;

  gpubuffer host_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_CPU_TO_GPU,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &host_buffer);
  assert(err == VK_SUCCESS);

  gpubuffer device_buffer = {0};
  err = create_gpubuffer(allocator, size, VMA_MEMORY_USAGE_GPU_ONLY,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         &device_buffer);
  assert(err == VK_SUCCESS);

  VkFence fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    err = vkCreateFence(device, &create_info, NULL, &fence);
    assert(err == VK_SUCCESS);
  }

  *dst_mesh = (gpumesh){fence, host_buffer, device_buffer, size};

  return err;
}

static void destroy_gpubuffer(VmaAllocator allocator, const gpubuffer *buffer) {
  vmaDestroyBuffer(allocator, buffer->buffer, buffer->alloc);
}

static void destroy_mesh(VkDevice device, VmaAllocator allocator,
                         const gpumesh *mesh) {
  destroy_gpubuffer(allocator, &mesh->geom_host);
  destroy_gpubuffer(allocator, &mesh->geom_gpu);
  vkDestroyFence(device, mesh->uploaded, NULL);
}

static void demo_upload_mesh(demo *d, const gpumesh *mesh) {
  uint32_t mesh_idx = d->mesh_upload_count;
  assert(d->mesh_upload_count + 1 < MESH_UPLOAD_QUEUE_SIZE);
  d->mesh_upload_queue[mesh_idx] = *mesh;
  d->mesh_upload_count++;
}

static bool demo_init(SDL_Window *window, VkInstance instance, demo *d) {
  VkResult err = VK_SUCCESS;

  // Get the GPU we want to run on
  VkPhysicalDevice gpu = select_gpu(instance);
  if (gpu == VK_NULL_HANDLE) {
    return false;
  }

  // Check physical device properties
  VkPhysicalDeviceProperties gpu_props = {0};
  vkGetPhysicalDeviceProperties(gpu, &gpu_props);

  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, NULL);

  VkQueueFamilyProperties *queue_props =
      malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
  assert(queue_props);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count,
                                           queue_props);

  VkPhysicalDeviceFeatures gpu_features = {0};
  vkGetPhysicalDeviceFeatures(gpu, &gpu_features);

  // Create vulkan surface
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  SDL_Vulkan_CreateSurface(window, instance, &surface);

  uint32_t graphics_queue_family_index = UINT32_MAX;
  uint32_t present_queue_family_index = UINT32_MAX;
  {
    // Iterate over each queue to learn whether it supports presenting:
    VkBool32 *supports_present = alloca(queue_family_count * sizeof(VkBool32));
    for (uint32_t i = 0; i < queue_family_count; i++) {
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface,
                                           &supports_present[i]);
    }

    // Search for a graphics and a present queue in the array of queue
    // families, try to find one that supports both
    for (uint32_t i = 0; i < queue_family_count; i++) {
      if ((queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        if (graphics_queue_family_index == UINT32_MAX) {
          graphics_queue_family_index = i;
        }

        if (supports_present[i] == VK_TRUE) {
          graphics_queue_family_index = i;
          present_queue_family_index = i;
          break;
        }
      }
    }

    if (present_queue_family_index == UINT32_MAX) {
      // If didn't find a queue that supports both graphics and present, then
      // find a separate present queue.
      for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (supports_present[i] == VK_TRUE) {
          present_queue_family_index = i;
          break;
        }
      }
    }

    // Generate error if could not find both a graphics and a present queue
    if (graphics_queue_family_index == UINT32_MAX ||
        present_queue_family_index == UINT32_MAX) {
      return false;
    }
  }

  // Create Logical Device
  uint32_t device_ext_count = 0;
  const char *device_ext_names[MAX_EXT_COUNT] = {0};

  {
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  }

  VkDevice device = create_device(gpu, graphics_queue_family_index,
                                  present_queue_family_index, device_ext_count,
                                  device_ext_names);

  VkQueue graphics_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, graphics_queue_family_index, 0, &graphics_queue);

  VkQueue present_queue = VK_NULL_HANDLE;
  if (graphics_queue_family_index == present_queue_family_index) {
    present_queue = graphics_queue;
  } else {
    vkGetDeviceQueue(device, present_queue_family_index, 0, &present_queue);
  }

  // Create Allocator
  VmaAllocator allocator = {0};
  {
    VmaVulkanFunctions volk_functions = {0};
    volk_functions.vkGetPhysicalDeviceProperties =
        vkGetPhysicalDeviceProperties;
    volk_functions.vkGetPhysicalDeviceMemoryProperties =
        vkGetPhysicalDeviceMemoryProperties;
    volk_functions.vkAllocateMemory = vkAllocateMemory;
    volk_functions.vkFreeMemory = vkFreeMemory;
    volk_functions.vkMapMemory = vkMapMemory;
    volk_functions.vkUnmapMemory = vkUnmapMemory;
    volk_functions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
    volk_functions.vkInvalidateMappedMemoryRanges =
        vkInvalidateMappedMemoryRanges;
    volk_functions.vkBindBufferMemory = vkBindBufferMemory;
    volk_functions.vkBindImageMemory = vkBindImageMemory;
    volk_functions.vkGetBufferMemoryRequirements =
        vkGetBufferMemoryRequirements;
    volk_functions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
    volk_functions.vkCreateBuffer = vkCreateBuffer;
    volk_functions.vkDestroyBuffer = vkDestroyBuffer;
    volk_functions.vkCreateImage = vkCreateImage;
    volk_functions.vkDestroyImage = vkDestroyImage;
    volk_functions.vkCmdCopyBuffer = vkCmdCopyBuffer;

    VmaAllocatorCreateInfo create_info = {0};
    create_info.physicalDevice = gpu;
    create_info.device = device;
    create_info.pVulkanFunctions = &volk_functions;
    create_info.instance = instance;
    create_info.vulkanApiVersion = VK_API_VERSION_1_0;
    err = vmaCreateAllocator(&create_info, &allocator);
    assert(err == VK_SUCCESS);
  }

  // Create Swapchain
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t width = WIDTH;
  uint32_t height = HEIGHT;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  VkFormat swapchain_image_format = VK_FORMAT_UNDEFINED;
  VkColorSpaceKHR swapchain_color_space = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  uint32_t swap_img_count = FRAME_LATENCY;
  {
    uint32_t format_count = 0;
    err =
        vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
    assert(err == VK_SUCCESS);
    VkSurfaceFormatKHR *surface_formats =
        alloca(format_count * sizeof(VkSurfaceFormatKHR));
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count,
                                               surface_formats);
    assert(err == VK_SUCCESS);
    VkSurfaceFormatKHR surface_format =
        pick_surface_format(surface_formats, format_count);
    swapchain_image_format = surface_format.format;
    swapchain_color_space = surface_format.colorSpace;

    VkSurfaceCapabilitiesKHR surf_caps;
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surf_caps);
    assert(err == VK_SUCCESS);

    uint32_t present_mode_count = 0;
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface,
                                                    &present_mode_count, NULL);
    assert(err == VK_SUCCESS);
    VkPresentModeKHR *present_modes =
        alloca(present_mode_count * sizeof(VkPresentModeKHR));
    assert(present_modes);
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(
        gpu, surface, &present_mode_count, present_modes);
    assert(err == VK_SUCCESS);

    VkExtent2D swapchain_extent;
    // width and height are either both 0xFFFFFFFF, or both not 0xFFFFFFFF.
    if (surf_caps.currentExtent.width == 0xFFFFFFFF) {
      // If the surface size is undefined, the size is set to the size
      // of the images requested, which must fit within the minimum and
      // maximum values.
      swapchain_extent.width = width;
      swapchain_extent.height = height;

      if (swapchain_extent.width < surf_caps.minImageExtent.width) {
        swapchain_extent.width = surf_caps.minImageExtent.width;
      } else if (swapchain_extent.width > surf_caps.maxImageExtent.width) {
        swapchain_extent.width = surf_caps.maxImageExtent.width;
      }

      if (swapchain_extent.height < surf_caps.minImageExtent.height) {
        swapchain_extent.height = surf_caps.minImageExtent.height;
      } else if (swapchain_extent.height > surf_caps.maxImageExtent.height) {
        swapchain_extent.height = surf_caps.maxImageExtent.height;
      }
    } else {
      // If the surface size is defined, the swap chain size must match
      swapchain_extent = surf_caps.currentExtent;
      width = surf_caps.currentExtent.width;
      height = surf_caps.currentExtent.height;
    }

    // The FIFO present mode is guaranteed by the spec to be supported
    // and to have no tearing.  It's a great default present mode to use.
    VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;

    if (present_mode != swapchain_present_mode) {
      for (size_t i = 0; i < present_mode_count; ++i) {
        if (present_modes[i] == present_mode) {
          swapchain_present_mode = present_mode;
          break;
        }
      }
    }
    if (swapchain_present_mode != present_mode) {
      // The desired present mode was not found, just use the first one
      present_mode = present_modes[0];
    }

    // Determine the number of VkImages to use in the swap chain.
    // Application desires to acquire 3 images at a time for triple
    // buffering
    if (swap_img_count < surf_caps.minImageCount) {
      swap_img_count = surf_caps.minImageCount;
    }
    // If maxImageCount is 0, we can ask for as many images as we want;
    // otherwise we're limited to maxImageCount
    if ((surf_caps.maxImageCount > 0) &&
        (swap_img_count > surf_caps.maxImageCount)) {
      // Application must settle for fewer images than desired:
      swap_img_count = surf_caps.maxImageCount;
    }

    VkSurfaceTransformFlagsKHR pre_transform;
    if (surf_caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
      pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
      pre_transform = surf_caps.currentTransform;
    }

    // Find a supported composite alpha mode - one of these is guaranteed to be
    // set
    VkCompositeAlphaFlagBitsKHR composite_alpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    VkCompositeAlphaFlagBitsKHR composite_alpha_flags[4] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (uint32_t i = 0; i < 4; i++) {
      if (surf_caps.supportedCompositeAlpha & composite_alpha_flags[i]) {
        composite_alpha = composite_alpha_flags[i];
        break;
      }
    }

    VkSwapchainCreateInfoKHR create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = swap_img_count;
    create_info.imageFormat = swapchain_image_format;
    create_info.imageColorSpace = swapchain_color_space;
    create_info.imageExtent = swapchain_extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.compositeAlpha = composite_alpha;
    create_info.preTransform = pre_transform;
    create_info.presentMode = present_mode;

    vkCreateSwapchainKHR(device, &create_info, NULL, &swapchain);
  }

  // Create Render Pass
  VkRenderPass render_pass = VK_NULL_HANDLE;
  {
    VkAttachmentDescription attachment = {0};
    attachment.format = swapchain_image_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference attachment_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &attachment_ref;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    err = vkCreateRenderPass(device, &create_info, NULL, &render_pass);
    assert(err == VK_SUCCESS);
  }

  // Create Pipeline Cache
  VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
  {
    VkPipelineCacheCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    err = vkCreatePipelineCache(device, &create_info, NULL, &pipeline_cache);
    assert(err == VK_SUCCESS);
  }

  // Create Graphics Pipeline Layout
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  {
    VkPushConstantRange const_range = {
        VK_SHADER_STAGE_ALL_GRAPHICS,
        0,
        PUSH_CONSTANT_BYTES,
    };
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;
    err = vkCreatePipelineLayout(device, &create_info, NULL, &pipeline_layout);
    assert(err == VK_SUCCESS);
  }

  VkPipeline fractal_pipeline = VK_NULL_HANDLE;
  err = create_fractal_pipeline(device, pipeline_cache, render_pass, width,
                                height, pipeline_layout, &fractal_pipeline);
  assert(err == VK_SUCCESS);

  VkPipeline mesh_pipeline = VK_NULL_HANDLE;
  err = create_mesh_pipeline(device, pipeline_cache, render_pass, width, height,
                             pipeline_layout, &mesh_pipeline);
  assert(err == VK_SUCCESS);

  // Create Cube Mesh
  gpumesh cube = {0};
  {
    err = create_mesh(device, allocator, &cube_cpu, &cube);

    // Actually copy cube data to cpu local buffer
    {
      uint8_t *data = NULL;
      vmaMapMemory(allocator, cube.geom_host.alloc, (void **)&data);

      size_t offset = 0;
      // Copy Positions
      size_t size = sizeof(float3) * cube_cpu.vertex_count;
      memcpy(data + offset, cube_cpu.positions, size);
      offset += size;
      // Copy Colors
      memcpy(data + offset, cube_cpu.colors, size);
      offset += size;
      // Copy Normals
      memcpy(data + offset, cube_cpu.normals, size);
      offset += size;

      vmaUnmapMemory(allocator, cube.geom_host.alloc);
      data = NULL;
    }

    assert(err == VK_SUCCESS);
  }

  // Apply to output var
  d->instance = instance;
  d->gpu = gpu;
  d->allocator = allocator;
  d->gpu_props = gpu_props;
  d->queue_family_count = queue_family_count;
  d->queue_props = queue_props;
  d->gpu_features = gpu_features;
  d->surface = surface;
  d->graphics_queue_family_index = graphics_queue_family_index;
  d->present_queue_family_index = present_queue_family_index;
  d->separate_present_queue =
      (graphics_queue_family_index != present_queue_family_index);
  d->device = device;
  d->present_queue = present_queue;
  d->graphics_queue = graphics_queue;
  d->swapchain = swapchain;
  d->swapchain_image_count = swap_img_count;
  d->swap_width = width;
  d->swap_height = height;
  d->render_pass = render_pass;
  d->pipeline_cache = pipeline_cache;
  d->pipeline_layout = pipeline_layout;
  d->fractal_pipeline = fractal_pipeline;
  d->mesh_pipeline = mesh_pipeline;
  d->cube_gpu = cube;
  d->frame_idx = 0;

  demo_upload_mesh(d, &d->cube_gpu);

  // Create Semaphores
  {
    VkSemaphoreCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateSemaphore(device, &create_info, NULL,
                              &d->upload_complete_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, NULL,
                              &d->img_acquired_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, NULL,
                              &d->swapchain_image_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, NULL,
                              &d->render_complete_sems[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Get Swapchain Images
  {
    err = vkGetSwapchainImagesKHR(device, swapchain, &swap_img_count,
                                  d->swapchain_images);
    assert(err == VK_SUCCESS || err == VK_INCOMPLETE);
  }

  // Create Image Views
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = swapchain_image_format;
    create_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      create_info.image = d->swapchain_images[i];
      err = vkCreateImageView(device, &create_info, NULL,
                              &d->swapchain_image_views[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Framebuffers
  {
    VkFramebufferCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create_info.renderPass = render_pass;
    create_info.attachmentCount = 1;
    create_info.width = width;
    create_info.height = height;
    create_info.layers = 1;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      create_info.pAttachments = &d->swapchain_image_views[i];
      err = vkCreateFramebuffer(device, &create_info, NULL,
                                &d->swapchain_framebuffers[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Command Pools
  {
    VkCommandPoolCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.queueFamilyIndex = graphics_queue_family_index;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err =
          vkCreateCommandPool(device, &create_info, NULL, &d->command_pools[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Allocate Command Buffers
  {
    VkCommandBufferAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.commandPool = d->command_pools[i];
      err = vkAllocateCommandBuffers(device, &alloc_info,
                                     &d->graphics_buffers[i]);
      assert(err == VK_SUCCESS);
      err =
          vkAllocateCommandBuffers(device, &alloc_info, &d->upload_buffers[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Descriptor Set Pools
  // Create Descriptor Sets

  // Create Fences
  {
    VkFenceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateFence(device, &create_info, NULL, &d->fences[i]);
      assert(err == VK_SUCCESS);
    }
  }

  return true;
}

static void demo_render_frame(demo *d) {
  VkResult err = VK_SUCCESS;

  VkDevice device = d->device;
  VkSwapchainKHR swapchain = d->swapchain;
  uint32_t frame_idx = d->frame_idx;

  VkFence *fences = d->fences;

  VkQueue graphics_queue = d->graphics_queue;
  VkQueue present_queue = d->present_queue;

  VkSemaphore img_acquired_sem = d->img_acquired_sems[frame_idx];
  VkSemaphore render_complete_sem = d->render_complete_sems[frame_idx];

  // Ensure no more than FRAME_LATENCY renderings are outstanding
  vkWaitForFences(device, 1, &fences[frame_idx], VK_TRUE, UINT64_MAX);
  vkResetFences(device, 1, &fences[frame_idx]);

  // Acquire Image
  do {
    err = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, img_acquired_sem,
                                VK_NULL_HANDLE, &d->swap_img_idx);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // demo->swapchain is out of date (e.g. the window was resized) and
      // must be recreated:
      // resize(d);
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // demo->swapchain is not as optimal as it could be, but the platform's
      // presentation engine will still present the image correctly.
      break;
    } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
      // If the surface was lost we could re-create it.
      // But the surface is owned by SDL2
      assert(err == VK_SUCCESS);
    } else {
      assert(err == VK_SUCCESS);
    }
  } while (err != VK_SUCCESS);

  uint32_t swap_img_idx = d->swap_img_idx;

  // Render
  {
    VkCommandPool command_pool = d->command_pools[frame_idx];
    vkResetCommandPool(device, command_pool, 0);

    VkCommandBuffer upload_buffer = d->upload_buffers[frame_idx];
    VkCommandBuffer graphics_buffer = d->graphics_buffers[frame_idx];

    VkSemaphore upload_sem = VK_NULL_HANDLE;

    // Record
    {
      // Upload
      {
        // If the fence has not been signaled, it's elligible for upload
        VkResult status = vkGetFenceStatus(d->device, d->cube_gpu.uploaded);
        if (status == VK_NOT_READY) {
          VkCommandBufferBeginInfo begin_info = {0};
          begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
          err = vkBeginCommandBuffer(upload_buffer, &begin_info);
          assert(err == VK_SUCCESS);

          const gpumesh *mesh = &d->cube_gpu;
          VkBufferCopy region = {
              0,
              0,
              d->cube_gpu.size,
          };
          vkCmdCopyBuffer(upload_buffer, mesh->geom_host.buffer,
                          mesh->geom_gpu.buffer, 1, &region);

          err = vkEndCommandBuffer(upload_buffer);

          upload_sem = d->upload_complete_sems[frame_idx];
          assert(err == VK_SUCCESS);

          // Submit upload
          VkSubmitInfo submit_info = {0};
          submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
          submit_info.commandBufferCount = 1;
          submit_info.pCommandBuffers = &upload_buffer;
          submit_info.signalSemaphoreCount = 1;
          submit_info.pSignalSemaphores = &upload_sem;
          err = vkQueueSubmit(d->graphics_queue, 1, &submit_info,
                              d->cube_gpu.uploaded);
          assert(err == VK_SUCCESS);
        }
      }

      VkCommandBufferBeginInfo begin_info = {0};
      begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      err = vkBeginCommandBuffer(graphics_buffer, &begin_info);
      assert(err == VK_SUCCESS);

      // Transition Swapchain Image
      {
        VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (frame_idx >= FRAME_LATENCY) {
          old_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        VkImageMemoryBarrier barrier = {0};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = old_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = d->swapchain_images[frame_idx];
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(graphics_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, NULL, 0, NULL, 1, &barrier);
      }

      // Render Pass
      {
        VkRenderPass render_pass = d->render_pass;
        VkFramebuffer framebuffer = d->swapchain_framebuffers[frame_idx];

        VkClearValue clear_value = {.color = {.float32 = {0, 1, 1, 1}}};

        const float width = d->swap_width;
        const float height = d->swap_height;

        VkRenderPassBeginInfo pass_info = {0};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass_info.renderPass = render_pass;
        pass_info.framebuffer = framebuffer;
        pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
        pass_info.clearValueCount = 1;
        pass_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {0, height, width, -height, 0, 1};
        VkRect2D scissor = {{0, 0}, {width, height}};
        vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
        vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

        vkCmdPushConstants(
            graphics_buffer, d->pipeline_layout, VK_SHADER_STAGE_ALL_GRAPHICS,
            0, sizeof(PushConstants), (const void *)&d->push_constants);

        vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          d->fractal_pipeline);
        vkCmdDraw(graphics_buffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(graphics_buffer);
      }

      err = vkEndCommandBuffer(graphics_buffer);
      assert(err == VK_SUCCESS);
    }

    // Submit
    {
      uint32_t wait_sem_count = 0;
      VkSemaphore wait_sems[16] = {0};
      VkPipelineStageFlags wait_stage_flags[16] = {0};

      wait_sems[wait_sem_count] = img_acquired_sem;
      wait_stage_flags[wait_sem_count++] =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      if (upload_sem != VK_NULL_HANDLE) {
        wait_sems[wait_sem_count] = upload_sem;
        wait_stage_flags[wait_sem_count++] = VK_PIPELINE_STAGE_TRANSFER_BIT;
      }

      VkPipelineStageFlags pipe_stage_flags =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo submit_info = {0};
      submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit_info.waitSemaphoreCount = wait_sem_count;
      submit_info.pWaitSemaphores = wait_sems;
      submit_info.pWaitDstStageMask = wait_stage_flags;
      submit_info.commandBufferCount = 1;
      submit_info.pCommandBuffers = &graphics_buffer;
      submit_info.signalSemaphoreCount = 1;
      submit_info.pSignalSemaphores = &render_complete_sem;
      err = vkQueueSubmit(graphics_queue, 1, &submit_info, fences[frame_idx]);
      assert(err == VK_SUCCESS);
    }
  }

  // Present
  {
    VkSemaphore wait_sem = render_complete_sem;
    if (d->separate_present_queue) {
      VkSemaphore swapchain_sem = d->swapchain_image_sems[frame_idx];
      // If we are using separate queues, change image ownership to the
      // present queue before presenting, waiting for the draw complete
      // semaphore and signalling the ownership released semaphore when
      // finished
      VkSubmitInfo submit_info = {0};
      submit_info.waitSemaphoreCount = 1;
      submit_info.pWaitSemaphores = &d->render_complete_sems[frame_idx];
      submit_info.commandBufferCount = 1;
      // submit_info.pCommandBuffers =
      //    &d->swapchain_images[swap_img_idx].graphics_to_present_cmd;
      submit_info.signalSemaphoreCount = 1;
      submit_info.pSignalSemaphores = &swapchain_sem;
      err = vkQueueSubmit(present_queue, 1, &submit_info, VK_NULL_HANDLE);
      assert(err == VK_SUCCESS);

      wait_sem = swapchain_sem;
    }

    VkPresentInfoKHR present_info = {0};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &wait_sem;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &swap_img_idx;
    err = vkQueuePresentKHR(present_queue, &present_info);

    d->frame_idx = (frame_idx + 1) % FRAME_LATENCY;

    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
      // demo->swapchain is out of date (e.g. the window was resized) and
      // must be recreated:
      // resize(d);
    } else if (err == VK_SUBOPTIMAL_KHR) {
      // demo->swapchain is not as optimal as it could be, but the platform's
      // presentation engine will still present the image correctly.
    } else if (err == VK_ERROR_SURFACE_LOST_KHR) {
      // If the surface was lost we could re-create it.
      // But the surface is owned by SDL2
      assert(err == VK_SUCCESS);
    } else {
      assert(err == VK_SUCCESS);
    }
  }
}

static void demo_destroy(demo *d) {
  VkDevice device = d->device;

  vkDeviceWaitIdle(device);

  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    vkDestroyFence(device, d->fences[i], NULL);
    vkDestroySemaphore(device, d->upload_complete_sems[i], NULL);
    vkDestroySemaphore(device, d->render_complete_sems[i], NULL);
    vkDestroySemaphore(device, d->swapchain_image_sems[i], NULL);
    vkDestroySemaphore(device, d->img_acquired_sems[i], NULL);
    vkDestroyImageView(device, d->swapchain_image_views[i], NULL);
    vkDestroyFramebuffer(device, d->swapchain_framebuffers[i], NULL);
    vkDestroyCommandPool(device, d->command_pools[i], NULL);
  }

  destroy_mesh(d->device, d->allocator, &d->cube_gpu);

  free(d->queue_props);
  vkDestroyPipelineLayout(device, d->pipeline_layout, NULL);
  vkDestroyPipeline(device, d->mesh_pipeline, NULL);
  vkDestroyPipeline(device, d->fractal_pipeline, NULL);
  vkDestroyPipelineCache(device, d->pipeline_cache, NULL);
  vkDestroyRenderPass(device, d->render_pass, NULL);
  vkDestroySwapchainKHR(device, d->swapchain, NULL);
  vkDestroySurfaceKHR(d->instance, d->surface, NULL);
  vmaDestroyAllocator(d->allocator);
  vkDestroyDevice(d->device, NULL);
  *d = (demo){0};
}

int32_t SDL_main(int32_t argc, char *argv[]) {
  VkResult err = volkInitialize();
  assert(err == VK_SUCCESS);

  {
    int32_t res = SDL_Init(SDL_INIT_VIDEO);
    assert(res == 0);
  }

  SDL_Window *window = SDL_CreateWindow("SDL Test", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
                                        SDL_WINDOW_VULKAN);
  assert(window != NULL);

  // Create vulkan instance
  VkInstance instance = VK_NULL_HANDLE;
  {
    // Gather required layers
    uint32_t layer_count = 0;
    const char *layer_names[MAX_LAYER_COUNT] = {0};

    {
      uint32_t instance_layer_count = 0;
      err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
      assert(err == VK_SUCCESS);
      if (instance_layer_count > 0) {
        VkLayerProperties *instance_layers =
            alloca(sizeof(VkLayerProperties) * instance_layer_count);
        err = vkEnumerateInstanceLayerProperties(&instance_layer_count,
                                                 instance_layers);
        assert(err == VK_SUCCESS);
#ifdef VALIDATION
        {
          const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";

          bool validation_found = check_layer(
              validation_layer_name, instance_layer_count, instance_layers);
          if (validation_found) {
            assert(layer_count + 1 < MAX_LAYER_COUNT);
            layer_names[layer_count++] = validation_layer_name;
          }
        }
#endif
      }
    }

    // Query SDL for required extensions
    uint32_t ext_count = 0;
    const char *ext_names[MAX_EXT_COUNT] = {0};
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, NULL);

    assert(ext_count < MAX_EXT_COUNT);
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, ext_names);

// Add debug ext
#ifdef VALIDATION
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "SDL Test";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "SDL Test";
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    err = vkCreateInstance(&create_info, NULL, &instance);
    assert(err == VK_SUCCESS);
    volkLoadInstance(instance);
  }

  demo d = {0};
  bool success = demo_init(window, instance, &d);
  assert(success);

  // Main loop
  bool running = true;
  while (running) {
    SDL_Event e = {0};
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = false;
      }
    }

    // Pass time to shader
    {
      float time_ms = (float)SDL_GetTicks();
      float time_seconds = time_ms / 1000.0f;
      float time_ns = 0.0f;
      float time_us = 0.0f;
      d.push_constants.time = (float4){time_seconds, time_ms, time_ns, time_us};
      d.push_constants.resolution = (float2){d.swap_width, d.swap_height};
    }

    demo_render_frame(&d);
  }

  // Cleanup
  SDL_DestroyWindow(window);
  window = NULL;

  SDL_Quit();

  demo_destroy(&d);

  vkDestroyInstance(instance, NULL);
  instance = VK_NULL_HANDLE;

  return 0;
}