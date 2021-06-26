#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <mimalloc.h>
#include <optick_capi.h>
#include <stdbool.h>
#include <stdint.h>
#include <volk.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#include <vk_mem_alloc.h>

#include "allocator.h"
#include "camera.h"
#include "config.h"
#include "cpuresources.h"
#include "cube.h"
#include "gpuresources.h"
#include "pattern.h"
#include "pipelines.h"
#include "plane.h"
#include "scene.h"
#include "shadercommon.h"
#include "simd.h"
#include "skydome.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifndef FINAL
#define VALIDATION
#endif
#define FRAME_LATENCY 3
#define CONST_BUFFER_UPLOAD_QUEUE_SIZE 16
#define MESH_UPLOAD_QUEUE_SIZE 16
#define TEXTURE_UPLOAD_QUEUE_SIZE 16

#define WIDTH 1600
#define HEIGHT 900

typedef struct demo {
  allocator std_alloc;
  allocator tmp_alloc;

  const VkAllocationCallbacks *vk_alloc;
  VmaAllocator vma_alloc;

  VkInstance instance;

  VkPhysicalDevice gpu;
  VkPhysicalDeviceProperties gpu_props;
  uint32_t queue_family_count;
  VkQueueFamilyProperties *queue_props;
  VkPhysicalDeviceFeatures gpu_features;
  VkPhysicalDeviceMemoryProperties gpu_mem_props;

  VkSurfaceKHR surface;
  uint32_t graphics_queue_family_index;
  uint32_t present_queue_family_index;
  bool separate_present_queue;

  VkDevice device;
  VkQueue present_queue;
  VkQueue graphics_queue;

  VkFormat swapchain_image_format;
  VkSwapchainKHR swapchain;
  uint32_t swapchain_image_count;
  uint32_t swap_width;
  uint32_t swap_height;

  VkRenderPass render_pass;
  VkPipelineCache pipeline_cache;

  VkPipelineLayout simple_pipe_layout;
  VkPipeline fractal_pipeline;
  VkPipeline color_mesh_pipeline;

  VkSampler sampler;

  VkDescriptorSetLayout material_layout;
  VkPipelineLayout material_pipe_layout;
  VkPipeline uv_mesh_pipeline;

  VkDescriptorSetLayout skydome_layout;
  VkPipelineLayout skydome_pipe_layout;
  VkPipeline skydome_pipeline;
  gpuconstbuffer sky_const_buffer;

  VkDescriptorSetLayout gltf_layout;
  VkPipelineLayout gltf_pipe_layout;
  gpupipeline *gltf_pipeline;

  VkDescriptorSetLayout gltf_rt_layout;
  VkPipelineLayout gltf_rt_pipe_layout;
  gpupipeline *gltf_rt_pipeline;

  VkImage swapchain_images[FRAME_LATENCY];
  VkImageView swapchain_image_views[FRAME_LATENCY];
  VkFramebuffer swapchain_framebuffers[FRAME_LATENCY];

  gpuimage depth_buffers; // Implemented as an image array; one image for each
                          // latency frame
  VkImageView depth_buffer_views[FRAME_LATENCY];

  VkCommandPool command_pools[FRAME_LATENCY];
  VkCommandBuffer upload_buffers[FRAME_LATENCY];
  VkCommandBuffer graphics_buffers[FRAME_LATENCY];
  VkCommandBuffer screenshot_buffers[FRAME_LATENCY];

  // For allowing the currently processed frame to access
  // resources being uploaded this frame
  VkSemaphore upload_complete_sems[FRAME_LATENCY];
  VkSemaphore img_acquired_sems[FRAME_LATENCY];
  VkSemaphore swapchain_image_sems[FRAME_LATENCY];
  VkSemaphore render_complete_sems[FRAME_LATENCY];

  uint32_t frame_idx;
  uint32_t swap_img_idx;
  VkFence fences[FRAME_LATENCY];

  VmaPool upload_mem_pool;
  VmaPool texture_mem_pool;

  gpumesh cube_gpu;
  gpumesh plane_gpu;
  gpumesh skydome_gpu;

  uint8_t *imgui_mesh_data;
  gpumesh imgui_gpu;

  gputexture albedo;
  gputexture displacement;
  gputexture normal;
  gputexture roughness;
  gputexture pattern;

  scene *duck;

  gpuimage screenshot_image;
  VkFence screenshot_fence;

  VkDescriptorPool descriptor_pools[FRAME_LATENCY];
  VkDescriptorSet skydome_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet mesh_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet gltf_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet imgui_descriptor_sets[FRAME_LATENCY];

  uint32_t const_buffer_upload_count;
  gpuconstbuffer const_buffer_upload_queue[CONST_BUFFER_UPLOAD_QUEUE_SIZE];

  uint32_t mesh_upload_count;
  gpumesh mesh_upload_queue[MESH_UPLOAD_QUEUE_SIZE];

  uint32_t texture_upload_count;
  gputexture texture_upload_queue[TEXTURE_UPLOAD_QUEUE_SIZE];

  PushConstants push_constants;

  ImGuiContext *ig_ctx;
  ImGuiIO *ig_io;
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
                              const VkAllocationCallbacks *vk_alloc,
                              const char *const *ext_names) {
  float queue_priorities[1] = {0.0};
  VkDeviceQueueCreateInfo queues[2];
  queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queues[0].pNext = NULL;
  queues[0].queueFamilyIndex = graphics_queue_family_index;
  queues[0].queueCount = 1;
  queues[0].pQueuePriorities = queue_priorities;
  queues[0].flags = 0;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipe_feature = {
      .sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
      .rayTracingPipeline = VK_TRUE,
  };

  VkDeviceCreateInfo create_info = {0};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
  create_info.pNext = (const void *)&rt_pipe_feature;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = queues;
  create_info.enabledExtensionCount = ext_count;
  create_info.ppEnabledExtensionNames = ext_names;

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
  VkResult err = vkCreateDevice(gpu, &create_info, vk_alloc, &device);
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

static void demo_upload_const_buffer(demo *d, const gpuconstbuffer *buffer) {
  uint32_t buffer_idx = d->const_buffer_upload_count;
  assert(d->const_buffer_upload_count + 1 < CONST_BUFFER_UPLOAD_QUEUE_SIZE);
  d->const_buffer_upload_queue[buffer_idx] = *buffer;
  d->const_buffer_upload_count++;
}

static void demo_upload_mesh(demo *d, const gpumesh *mesh) {
  uint32_t mesh_idx = d->mesh_upload_count;
  assert(d->mesh_upload_count + 1 < MESH_UPLOAD_QUEUE_SIZE);
  d->mesh_upload_queue[mesh_idx] = *mesh;
  d->mesh_upload_count++;
}

static void demo_upload_texture(demo *d, const gputexture *tex) {
  uint32_t tex_idx = d->texture_upload_count;
  assert(d->texture_upload_count + 1 < TEXTURE_UPLOAD_QUEUE_SIZE);
  d->texture_upload_queue[tex_idx] = *tex;
  d->texture_upload_count++;
}

static void demo_upload_scene(demo *d, const scene *s) {
  for (uint32_t i = 0; i < s->mesh_count; ++i) {
    demo_upload_mesh(d, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; ++i) {
    demo_upload_texture(d, &s->textures[i]);
  }
}

static void demo_update_skydata(demo *d, gpuconstbuffer cb) {
  VkDescriptorBufferInfo buffer_info = {.buffer = cb.gpu.buffer,
                                        .range = cb.size};

  VkWriteDescriptorSet const_buffer_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .pBufferInfo = &buffer_info,
  };

  vkUpdateDescriptorSets(d->device, 1, &const_buffer_write, 0, NULL);
}

static bool demo_init_imgui(demo *d) {
  ImGuiContext *ctx = igCreateContext(NULL);
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, NULL);

  // TODO: Create and upload imgui atlas texture

  d->ig_ctx = ctx;
  d->ig_io = io;

  return true;
}

static bool demo_init(SDL_Window *window, VkInstance instance,
                      allocator std_alloc, allocator tmp_alloc,
                      const VkAllocationCallbacks *vk_alloc, demo *d) {
  OPTICK_C_PUSH(optick_e, "demo_init", OptickAPI_Category_None);
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
      hb_alloc_nm_tp(std_alloc, queue_family_count, VkQueueFamilyProperties);
  assert(queue_props);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count,
                                           queue_props);

  VkPhysicalDeviceFeatures gpu_features = {0};
  vkGetPhysicalDeviceFeatures(gpu, &gpu_features);

  VkPhysicalDeviceMemoryProperties gpu_mem_props;
  vkGetPhysicalDeviceMemoryProperties(gpu, &gpu_mem_props);

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

  // Need a swapchain
  {
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  }

  // TODO: Check for Raytracing Support
  /*
  {
    // Required for Spirv 1.4
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME;

    // Required for VK_KHR_ray_tracing_pipeline
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] = VK_KHR_SPIRV_1_4_EXTENSION_NAME;

    // Required for VK_KHR_acceleration_structure
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;

    // Required for raytracing
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
    assert(device_ext_count + 1 < MAX_EXT_COUNT);
    device_ext_names[device_ext_count++] =
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
  }
  */

  VkDevice device = create_device(gpu, graphics_queue_family_index,
                                  present_queue_family_index, device_ext_count,
                                  vk_alloc, device_ext_names);

  VkQueue graphics_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, graphics_queue_family_index, 0, &graphics_queue);

  VkQueue present_queue = VK_NULL_HANDLE;
  if (graphics_queue_family_index == present_queue_family_index) {
    present_queue = graphics_queue;
  } else {
    vkGetDeviceQueue(device, present_queue_family_index, 0, &present_queue);
  }

  // Create Allocator
  VmaAllocator vma_alloc = {0};
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
    err = vmaCreateAllocator(&create_info, &vma_alloc);
    assert(err == VK_SUCCESS);
  }

  // Create Swapchain
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t width = WIDTH;
  uint32_t height = HEIGHT;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
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

    vkCreateSwapchainKHR(device, &create_info, vk_alloc, &swapchain);
  }

  // Create Render Pass
  VkRenderPass render_pass = VK_NULL_HANDLE;
  {
    VkAttachmentDescription color_attachment = {0};
    color_attachment.format = swapchain_image_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment = {0};
    depth_attachment.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription attachments[2] = {color_attachment,
                                              depth_attachment};

    VkAttachmentReference color_attachment_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_attachment_ref = {
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference attachment_refs[1] = {color_attachment_ref};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = attachment_refs;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    VkSubpassDependency subpass_dep = {0};
    subpass_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 2;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.pDependencies = &subpass_dep;
    err = vkCreateRenderPass(device, &create_info, vk_alloc, &render_pass);
    assert(err == VK_SUCCESS);
  }

  // Create Pipeline Cache
  VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
  {
    size_t data_size = 0;
    void *data = NULL;

    // If an existing pipeline cache exists, load it
    SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "rb");
    if (cache_file != NULL) {
      data_size = (size_t)SDL_RWsize(cache_file);

      data = mi_malloc(data_size);

      SDL_RWread(cache_file, data, data_size, 1);

      SDL_RWclose(cache_file);
    }

    VkPipelineCacheCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    create_info.initialDataSize = data_size;
    create_info.pInitialData = data;
    err =
        vkCreatePipelineCache(device, &create_info, vk_alloc, &pipeline_cache);
    assert(err == VK_SUCCESS);

    if (data) {
      mi_free(data);
    }
  }

  VkPushConstantRange const_range = {
      VK_SHADER_STAGE_ALL_GRAPHICS,
      0,
      PUSH_CONSTANT_BYTES,
  };

  // Create Simple Pipeline Layout
  VkPipelineLayout simple_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;
    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &simple_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  VkPipeline fractal_pipeline = VK_NULL_HANDLE;
  err = create_fractal_pipeline(device, vk_alloc, pipeline_cache, render_pass,
                                width, height, simple_pipe_layout,
                                &fractal_pipeline);
  assert(err == VK_SUCCESS);

  VkPipeline color_mesh_pipeline = VK_NULL_HANDLE;
  err = create_color_mesh_pipeline(device, vk_alloc, pipeline_cache,
                                   render_pass, width, height,
                                   simple_pipe_layout, &color_mesh_pipeline);
  assert(err == VK_SUCCESS);

  // Create Immutable Sampler
  VkSampler sampler = VK_NULL_HANDLE;
  {
    VkSamplerCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.magFilter = VK_FILTER_LINEAR;
    create_info.minFilter = VK_FILTER_LINEAR;
    create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    create_info.anisotropyEnable = VK_FALSE;
    create_info.maxAnisotropy = 1.0f;
    create_info.maxLod = 14.0f; // Hack; known number of mips for 8k textures
    create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    err = vkCreateSampler(device, &create_info, vk_alloc, &sampler);
    assert(err == VK_SUCCESS);
  }

  // Create Material Descriptor Set Layout
  VkDescriptorSetLayout material_layout = VK_NULL_HANDLE;
  {
    // Note: binding 1 is for the displacement map, which is useful only in the
    // vertex stage
    VkDescriptorSetLayoutBinding bindings[5] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, &sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 5;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &material_layout);
    assert(err == VK_SUCCESS);
  }

  // Create Material Pipeline Layout
  VkPipelineLayout material_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &material_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &material_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create UV mesh pipeline
  VkPipeline uv_mesh_pipeline = VK_NULL_HANDLE;
  err = create_uv_mesh_pipeline(device, vk_alloc, pipeline_cache, render_pass,
                                width, height, material_pipe_layout,
                                &uv_mesh_pipeline);
  assert(err == VK_SUCCESS);

  // Create Skydome Descriptor Set Layout
  VkDescriptorSetLayout skydome_layout = VK_NULL_HANDLE;
  {
    // Note: binding 1 is for the displacement map, which is useful only in the
    // vertex stage
    VkDescriptorSetLayoutBinding bindings[1] = {
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &skydome_layout);
    assert(err == VK_SUCCESS);
  }

  // Create Skydome Pipeline Layout
  VkPipelineLayout skydome_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &skydome_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &skydome_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create Skydome Pipeline
  VkPipeline skydome_pipeline = VK_NULL_HANDLE;
  err = create_skydome_pipeline(device, vk_alloc, pipeline_cache, render_pass,
                                width, height, skydome_pipe_layout,
                                &skydome_pipeline);
  assert(err == VK_SUCCESS);

  // Create GLTF Descriptor Set Layout
  VkDescriptorSetLayout gltf_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[4] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 4;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_layout);
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Pipeline Layout
  VkPipelineLayout gltf_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &gltf_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &gltf_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Pipeline
  gpupipeline *gltf_pipeline = NULL;
  err = create_gltf_pipeline(device, vk_alloc, pipeline_cache, render_pass,
                             width, height, gltf_pipe_layout, &gltf_pipeline);
  assert(err == VK_SUCCESS);

  // Create GLTF RT Pipeline Layout
  // Create GLTF Descriptor Set Layout
  VkDescriptorSetLayout gltf_rt_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[3] = {
        {1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_RAYGEN_BIT_KHR},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 3;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_rt_layout);
    assert(err == VK_SUCCESS);
  }

  VkPipelineLayout gltf_rt_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &gltf_rt_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &gltf_rt_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // HACK: Get this function here...
  PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR =
      (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(
          device, "vkCreateRayTracingPipelinesKHR");

  // Create GLTF Ray Tracing Pipeline
  // gpupipeline *gltf_rt_pipeline = NULL;
  // err = create_gltf_rt_pipeline(
  //    device, vk_alloc, pipeline_cache, vkCreateRayTracingPipelinesKHR,
  //    render_pass, width, height, gltf_rt_pipe_layout, &gltf_rt_pipeline);
  // assert(err == VK_SUCCESS);

  // Create a pool for host memory uploads
  VmaPool upload_mem_pool = VK_NULL_HANDLE;
  {
    uint32_t mem_type_idx = 0xFFFFFFFF;
    // Find the desired memory type index
    for (uint32_t i = 0; i < gpu_mem_props.memoryTypeCount; ++i) {
      VkMemoryType type = gpu_mem_props.memoryTypes[i];
      if (type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        mem_type_idx = i;
        break;
      }
    }
    assert(mem_type_idx != 0xFFFFFFFF);

    VmaPoolCreateInfo create_info = {0};
    create_info.memoryTypeIndex = mem_type_idx;
    err = vmaCreatePool(vma_alloc, &create_info, &upload_mem_pool);
    assert(err == VK_SUCCESS);
  }

  // Create a pool for texture memory
  VmaPool texture_mem_pool = VK_NULL_HANDLE;
  {
    uint32_t mem_type_idx = 0xFFFFFFFF;
    // Find the desired memory type index
    for (uint32_t i = 0; i < gpu_mem_props.memoryTypeCount; ++i) {
      VkMemoryType type = gpu_mem_props.memoryTypes[i];
      if (type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        mem_type_idx = i;
        break;
      }
    }
    assert(mem_type_idx != 0xFFFFFFFF);

    // block size to fit an 8k R8G8B8A8 uncompressed texture w/ metadata
    uint64_t block_size = (uint64_t)(8192.0 * 8192.0 * 4.0 * 1.4);

    VmaPoolCreateInfo create_info = {0};
    create_info.memoryTypeIndex = mem_type_idx;
    create_info.blockSize = block_size;
    create_info.minBlockCount = 4; // We know we will have at least 4 textures
    err = vmaCreatePool(vma_alloc, &create_info, &texture_mem_pool);
    assert(err == VK_SUCCESS);
  }

  // Create Cube Mesh
  gpumesh cube = {0};
  {
    size_t cube_size = cube_alloc_size();
    cpumesh *cube_cpu = mi_malloc(cube_size);
    assert(cube_cpu);
    memset(cube_cpu, 0, cube_size);
    create_cube(cube_cpu);

    err = create_gpumesh(device, vma_alloc, cube_cpu, &cube);
    assert(err == VK_SUCCESS);

    mi_free(cube_cpu);
  }

  // Create Plane Mesh
  gpumesh plane = {0};
  {
    uint32_t plane_subdiv = 16;
    size_t plane_size = plane_alloc_size(plane_subdiv);
    cpumesh *plane_cpu = mi_malloc(plane_size);
    assert(plane_cpu);
    memset(plane_cpu, 0, plane_size);
    create_plane(plane_subdiv, plane_cpu);

    err = create_gpumesh(device, vma_alloc, plane_cpu, &plane);
    assert(err == VK_SUCCESS);

    mi_free(plane_cpu);
  }

  // Create Skydome Mesh
  gpumesh skydome = {0};
  {
    cpumesh *skydome_cpu = create_skydome(&tmp_alloc);

    err = create_gpumesh(device, vma_alloc, skydome_cpu, &skydome);
    assert(err == VK_SUCCESS);
  }

  // Load Textures
  gputexture albedo =
      load_ktx2_texture(device, vma_alloc, &tmp_alloc, vk_alloc,
                        "./assets/textures/shfsaida_2K_Albedo.ktx2",
                        upload_mem_pool, texture_mem_pool);

  gputexture displacement =
      load_ktx2_texture(device, vma_alloc, &tmp_alloc, vk_alloc,
                        "./assets/textures/shfsaida_2K_Displacement.ktx2",
                        upload_mem_pool, texture_mem_pool);

  gputexture normal =
      load_ktx2_texture(device, vma_alloc, &tmp_alloc, vk_alloc,
                        "./assets/textures/shfsaida_2K_Normal.ktx2",
                        upload_mem_pool, texture_mem_pool);

  gputexture roughness =
      load_ktx2_texture(device, vma_alloc, &tmp_alloc, vk_alloc,
                        "./assets/textures/shfsaida_2K_Roughness.ktx2",
                        upload_mem_pool, texture_mem_pool);

  // Create Uniform buffer for sky data
  gpuconstbuffer sky_const_buffer =
      create_gpuconstbuffer(device, vma_alloc, vk_alloc, sizeof(SkyData));

  // Create procedural texture
  gputexture pattern = {0};
  {
    cputexture *cpu_pattern = NULL;
    alloc_pattern(1024, 1024, &cpu_pattern);
    create_pattern(1024, 1024, cpu_pattern);

    create_texture(device, vma_alloc, vk_alloc, cpu_pattern, upload_mem_pool,
                   texture_mem_pool, &pattern);

    free(cpu_pattern);
  }

  // Load scene
  scene *duck = NULL;
  load_scene(device, vk_alloc, vma_alloc, upload_mem_pool, texture_mem_pool,
             "./assets/scenes/duck.glb", &duck);

  // Create resources for screenshots
  gpuimage screenshot_image = {0};
  {
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
        .pool = upload_mem_pool,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    };
    err =
        create_gpuimage(vma_alloc, &image_info, &alloc_info, &screenshot_image);
    assert(err == VK_SUCCESS);
  }

  VkFence screenshot_fence = VK_NULL_HANDLE;
  {
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    err = vkCreateFence(device, &create_info, vk_alloc, &screenshot_fence);
    assert(err == VK_SUCCESS);
  }

  // Apply to output var
  d->tmp_alloc = tmp_alloc;
  d->std_alloc = std_alloc;
  d->vk_alloc = vk_alloc;
  d->instance = instance;
  d->gpu = gpu;
  d->vma_alloc = vma_alloc;
  d->gpu_props = gpu_props;
  d->gpu_mem_props = gpu_mem_props;
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
  d->simple_pipe_layout = simple_pipe_layout;
  d->fractal_pipeline = fractal_pipeline;
  d->color_mesh_pipeline = color_mesh_pipeline;
  d->sampler = sampler;
  d->material_layout = material_layout;
  d->material_pipe_layout = material_pipe_layout;
  d->uv_mesh_pipeline = uv_mesh_pipeline;
  d->skydome_layout = skydome_layout;
  d->skydome_pipe_layout = skydome_pipe_layout;
  d->skydome_pipeline = skydome_pipeline;
  d->sky_const_buffer = sky_const_buffer;
  d->gltf_layout = gltf_layout;
  d->gltf_pipe_layout = gltf_pipe_layout;
  d->gltf_pipeline = gltf_pipeline;
  d->gltf_rt_layout = gltf_rt_layout;
  d->gltf_rt_pipe_layout = gltf_rt_pipe_layout;
  // d->gltf_rt_pipeline = gltf_rt_pipeline;
  d->upload_mem_pool = upload_mem_pool;
  d->texture_mem_pool = texture_mem_pool;
  d->cube_gpu = cube;
  d->plane_gpu = plane;
  d->skydome_gpu = skydome;
  d->albedo = albedo;
  d->displacement = displacement;
  d->normal = normal;
  d->roughness = roughness;
  d->pattern = pattern;
  d->duck = duck;
  d->screenshot_image = screenshot_image;
  d->screenshot_fence = screenshot_fence;
  d->frame_idx = 0;

  demo_upload_mesh(d, &d->cube_gpu);
  demo_upload_mesh(d, &d->plane_gpu);
  demo_upload_mesh(d, &d->skydome_gpu);
  demo_upload_texture(d, &d->albedo);
  demo_upload_texture(d, &d->displacement);
  demo_upload_texture(d, &d->normal);
  demo_upload_texture(d, &d->roughness);
  demo_upload_texture(d, &d->pattern);
  demo_upload_scene(d, d->duck);

  // Create Semaphores
  {
    VkSemaphoreCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->upload_complete_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->img_acquired_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->swapchain_image_sems[i]);
      assert(err == VK_SUCCESS);
      err = vkCreateSemaphore(device, &create_info, vk_alloc,
                              &d->render_complete_sems[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Get Swapchain Images
  {
    uint32_t img_count = 0;
    err = vkGetSwapchainImagesKHR(device, swapchain, &img_count, NULL);
    assert(err == VK_SUCCESS);

    // Device may really want us to have called vkGetSwapchainImagesKHR
    // For now just assert that making that call doesn't change our desired
    // swapchain images
    assert(swap_img_count == img_count);

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
      err = vkCreateImageView(device, &create_info, vk_alloc,
                              &d->swapchain_image_views[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Depth Buffers
  {
    VkImageCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    create_info.extent = (VkExtent3D){d->swap_width, d->swap_height, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = FRAME_LATENCY;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo alloc_info = {0};
    alloc_info.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.pUserData = (void *)("Depth Buffer Memory");
    err = create_gpuimage(vma_alloc, &create_info, &alloc_info,
                          &d->depth_buffers);
    assert(err == VK_SUCCESS);
  }

  // Create Depth Buffer Views
  {
    VkImageViewCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = d->depth_buffers.image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
    create_info.components = (VkComponentMapping){
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
    };
    create_info.subresourceRange = (VkImageSubresourceRange){
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      create_info.subresourceRange.baseArrayLayer = i;
      err = vkCreateImageView(device, &create_info, vk_alloc,
                              &d->depth_buffer_views[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Framebuffers
  {
    VkFramebufferCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create_info.renderPass = render_pass;
    create_info.attachmentCount = 2;
    create_info.width = width;
    create_info.height = height;
    create_info.layers = 1;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      VkImageView attachments[2] = {
          d->swapchain_image_views[i],
          d->depth_buffer_views[i],
      };

      create_info.pAttachments = attachments;
      err = vkCreateFramebuffer(device, &create_info, vk_alloc,
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
      err = vkCreateCommandPool(device, &create_info, vk_alloc,
                                &d->command_pools[i]);
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
      err = vkAllocateCommandBuffers(device, &alloc_info,
                                     &d->screenshot_buffers[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Descriptor Set Pools
  {
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8};
    VkDescriptorPoolCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.maxSets = 3;
    create_info.poolSizeCount = 1;
    create_info.pPoolSizes = &pool_size;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateDescriptorPool(device, &create_info, vk_alloc,
                                   &d->descriptor_pools[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Create Descriptor Sets
  {
    VkDescriptorSetAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &material_layout;

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->mesh_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &skydome_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->skydome_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &gltf_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->gltf_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Write textures to descriptor set
  {
    VkDescriptorImageInfo albedo_info = {
        NULL, albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo displacement_info = {
        NULL, displacement.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normal_info = {
        NULL, normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo roughness_info = {
        NULL, roughness.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo skydome_info = {sky_const_buffer.gpu.buffer, 0,
                                           sky_const_buffer.size};
    VkDescriptorImageInfo duck_info = {
        NULL, duck->textures[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[8] = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &albedo_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &displacement_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &normal_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &roughness_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &skydome_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &duck_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &normal_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &roughness_info,
        },
    };
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      VkDescriptorSet gltf_set = d->gltf_descriptor_sets[i];
      VkDescriptorSet mesh_set = d->mesh_descriptor_sets[i];
      VkDescriptorSet skydome_set = d->skydome_descriptor_sets[i];

      writes[0].dstSet = mesh_set;
      writes[1].dstSet = mesh_set;
      writes[2].dstSet = mesh_set;
      writes[3].dstSet = mesh_set;

      writes[4].dstSet = skydome_set;

      writes[5].dstSet = gltf_set;
      writes[6].dstSet = gltf_set;
      writes[7].dstSet = gltf_set;

      vkUpdateDescriptorSets(device, 8, writes, 0, NULL);
    }
  }

  // Create Fences
  {
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      err = vkCreateFence(device, &create_info, vk_alloc, &d->fences[i]);
      assert(err == VK_SUCCESS);
    }
  }

  if (!demo_init_imgui(d)) {
    OptickAPI_PopEvent(optick_e);
    return false;
  }

  OptickAPI_PopEvent(optick_e);

  return true;
}

static void demo_render_scene(scene *s, VkCommandBuffer cmd,
                              VkPipelineLayout layout,
                              PushConstants *push_constants,
                              const float4x4 *vp) {
  OPTICK_C_PUSH(optick_e, "demo_render_scene", OptickAPI_Category_Rendering);
  for (uint32_t i = 0; i < s->entity_count; ++i) {
    uint64_t components = s->components[i];
    scene_transform *scene_transform = &s->transforms[i];
    scene_static_mesh *static_mesh = &s->static_meshes[i];

    if (components & COMPONENT_TYPE_STATIC_MESH) {
      transform *t = &scene_transform->t;

      // Hack to fuck with the scale of the object
      t->scale = (float3){0.01f, -0.01f, 0.01f};

      float4x4 m = {.row0 = {0}};
      transform_to_matrix(&m, t);

      float4x4 mvp = {.row0 = {0}};
      mulmf44(vp, &m, &mvp);

      // Hack to change the object's transform
      push_constants->m = m;
      push_constants->mvp = mvp;
      vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                         sizeof(PushConstants), (const void *)push_constants);

      const gpumesh *mesh = static_mesh->mesh;
      uint32_t idx_count = mesh->idx_count;
      uint32_t vtx_count = mesh->vtx_count;
      VkBuffer buffer = mesh->gpu.buffer;

      vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT16);
      VkDeviceSize offset = mesh->idx_size;

      vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
      offset += vtx_count * sizeof(float) * 3;

      vkCmdBindVertexBuffers(cmd, 1, 1, &buffer, &offset);
      offset += vtx_count * sizeof(float) * 3;

      vkCmdBindVertexBuffers(cmd, 2, 1, &buffer, &offset);

      vkCmdDrawIndexed(cmd, idx_count, 1, 0, 0, 0);
    }
  }
  OptickAPI_PopEvent(optick_e);
}

static void demo_render_frame(demo *d, const float4x4 *vp,
                              const float4x4 *sky_vp) {
  OPTICK_C_PUSH(demo_render_frame_event, "demo_render_frame",
                OptickAPI_Category_Rendering)

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
  {
    OPTICK_C_PUSH(fence_wait_event, "demo_render_frame wait for fence",
                  OptickAPI_Category_Wait);
    vkWaitForFences(device, 1, &fences[frame_idx], VK_TRUE, UINT64_MAX);
    OptickAPI_PopEvent(fence_wait_event);

    vkResetFences(device, 1, &fences[frame_idx]);
  }

  // Acquire Image
  {
    OPTICK_C_PUSH(optick_e, "demo_render_frame acquire next image",
                  OptickAPI_Category_Rendering);
    do {
      err =
          vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, img_acquired_sem,
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
    OptickAPI_PopEvent(optick_e);
  }

  uint32_t swap_img_idx = d->swap_img_idx;

  // Render
  {
    OPTICK_C_PUSH(demo_render_frame_render_event, "demo_render_frame render",
                  OptickAPI_Category_Rendering);

    VkCommandPool command_pool = d->command_pools[frame_idx];
    vkResetCommandPool(device, command_pool, 0);

    VkCommandBuffer upload_buffer = d->upload_buffers[frame_idx];
    VkCommandBuffer graphics_buffer = d->graphics_buffers[frame_idx];

    VkSemaphore upload_sem = VK_NULL_HANDLE;

    // Record
    {
      OPTICK_C_PUSH(record_upload_event,
                    "demo_render_frame record upload commands",
                    OptickAPI_Category_Rendering);

      // Upload
      if (d->const_buffer_upload_count > 0 || d->mesh_upload_count > 0 ||
          d->texture_upload_count > 0) {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        err = vkBeginCommandBuffer(upload_buffer, &begin_info);
        assert(err == VK_SUCCESS);

        // Issue const buffer uploads
        {
          VkBufferCopy region = {0};
          for (uint32_t i = 0; i < d->const_buffer_upload_count; ++i) {
            gpuconstbuffer constbuffer = d->const_buffer_upload_queue[i];
            region = (VkBufferCopy){0, 0, constbuffer.size};
            vkCmdCopyBuffer(upload_buffer, constbuffer.host.buffer,
                            constbuffer.gpu.buffer, 1, &region);
          }
          d->const_buffer_upload_count = 0;
        }

        // Issue mesh uploads
        {
          VkBufferCopy region = {0};
          for (uint32_t i = 0; i < d->mesh_upload_count; ++i) {
            gpumesh mesh = d->mesh_upload_queue[i];
            region = (VkBufferCopy){0, 0, mesh.size};
            vkCmdCopyBuffer(upload_buffer, mesh.host.buffer, mesh.gpu.buffer, 1,
                            &region);
          }
          d->mesh_upload_count = 0;
        }

        // Issue texture uploads
        {

          VkImageMemoryBarrier barrier = {0};
          barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
          barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          barrier.subresourceRange.baseArrayLayer = 0;

          for (uint32_t i = 0; i < d->texture_upload_count; ++i) {
            gputexture tex = d->texture_upload_queue[i];

            VkImage image = tex.device.image;
            uint32_t img_width = tex.width;
            uint32_t img_height = tex.height;
            uint32_t mip_levels = tex.mip_levels;
            uint32_t layer_count = tex.layer_count;

            // Transition all mips to transfer dst
            {
              barrier.subresourceRange.baseMipLevel = 0;
              barrier.subresourceRange.levelCount = mip_levels;
              barrier.subresourceRange.layerCount = layer_count;
              barrier.srcAccessMask = 0;
              barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
              barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
              barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
              barrier.image = image;

              vkCmdPipelineBarrier(upload_buffer,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                                   0, NULL, 1, &barrier);

              // Afterwards, we're operating on single mips at a time no matter
              // what
              barrier.subresourceRange.levelCount = 1;
            }
            uint32_t region_count = tex.region_count;
            VkBufferImageCopy *regions = tex.regions;
            vkCmdCopyBufferToImage(upload_buffer, tex.host.buffer, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   region_count, regions);

            // Generate mipmaps
            if (tex.gen_mips) {
              uint32_t mip_width = img_width;
              uint32_t mip_height = img_height;

              for (uint32_t i = 1; i < mip_levels; ++i) {
                // Transition previous mip level to be transfer src
                {
                  barrier.subresourceRange.baseMipLevel = i - 1;
                  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                  vkCmdPipelineBarrier(upload_buffer,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                       NULL, 0, NULL, 1, &barrier);
                }

                // Copy to next mip
                VkImageBlit blit = {0};
                blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.srcOffsets[1] = (VkOffset3D){mip_width, mip_height, 1};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = layer_count;
                blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
                blit.dstOffsets[1] =
                    (VkOffset3D){mip_width > 1 ? mip_width / 2 : 1,
                                 mip_height > 1 ? mip_height / 2 : 1, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = layer_count;

                vkCmdBlitImage(upload_buffer, image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                               VK_FILTER_LINEAR);

                // Transition input mip to shader read only
                {
                  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                  barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                  vkCmdPipelineBarrier(upload_buffer,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                       0, NULL, 0, NULL, 1, &barrier);
                }

                if (mip_width > 1) {
                  mip_width /= 2;
                }
                if (mip_height > 1) {
                  mip_height /= 2;
                }
              }
            }
            // Transition last subresource(s) to shader read
            {
              if (tex.gen_mips) {
                barrier.subresourceRange.baseMipLevel = mip_levels - 1;
              } else {
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = mip_levels;
              }
              barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
              barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
              barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
              barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
              barrier.image = image;
              vkCmdPipelineBarrier(upload_buffer,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                   NULL, 0, NULL, 1, &barrier);
            }
          }
          d->texture_upload_count = 0;
        }

        // Issue Const Data Updates
        {
          // TODO: If sky data has changed only...
        }

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
        err = vkQueueSubmit(d->graphics_queue, 1, &submit_info, NULL);
        assert(err == VK_SUCCESS);

        OptickAPI_PopEvent(record_upload_event);
      }

      VkCommandBufferBeginInfo begin_info = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      err = vkBeginCommandBuffer(graphics_buffer, &begin_info);
      assert(err == VK_SUCCESS);

      OptickAPI_GPUContext optick_prev_gpu_ctx = OptickAPI_SetGpuContext(
          (OptickAPI_GPUContext){.cmdBuffer = graphics_buffer});

      // Transition Swapchain Image
      {
        OPTICK_C_GPU_PUSH(optick_gpu_e, "Transition Swapchain Image",
                          OptickAPI_Category_Rendering);

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
        OptickAPI_PopGPUEvent(optick_gpu_e);
      }

      // Render Pass
      {
        VkRenderPass render_pass = d->render_pass;
        VkFramebuffer framebuffer = d->swapchain_framebuffers[frame_idx];

        VkClearValue clear_values[2] = {
            {.color = {.float32 = {0, 1, 1, 1}}},
            {.depthStencil = {.depth = 0.0f, .stencil = 0.0f}},
        };

        const float width = d->swap_width;
        const float height = d->swap_height;

        VkRenderPassBeginInfo pass_info = {0};
        pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass_info.renderPass = render_pass;
        pass_info.framebuffer = framebuffer;
        pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
        pass_info.clearValueCount = 2;
        pass_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                             VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {0, height, width, -height, 0, 1};
        VkRect2D scissor = {{0, 0}, {width, height}};
        vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
        vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

        vkCmdPushConstants(graphics_buffer, d->simple_pipe_layout,
                           VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                           sizeof(PushConstants),
                           (const void *)&d->push_constants);

        // Draw Fullscreen Fractal
        // vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        //                    d->fractal_pipeline);
        // vkCmdDraw(graphics_buffer, 3, 1, 0, 0);

        float4x4 mvp = d->push_constants.mvp;

        // Draw Cube
        {
          OPTICK_C_GPU_PUSH(optick_gpu_e, "Cube", OptickAPI_Category_GPU_Scene);
          d->push_constants.mvp = mvp;
          vkCmdPushConstants(graphics_buffer, d->simple_pipe_layout,
                             VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                             sizeof(PushConstants),
                             (const void *)&d->push_constants);

          uint32_t idx_count = d->cube_gpu.idx_count;
          uint32_t vert_count = d->cube_gpu.vtx_count;

          vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->color_mesh_pipeline);

          VkBuffer b = d->cube_gpu.gpu.buffer;

          vkCmdBindIndexBuffer(graphics_buffer, b, 0, VK_INDEX_TYPE_UINT16);

          size_t idx_size =
              idx_count * sizeof(uint16_t) >> d->cube_gpu.idx_type;
          size_t pos_size = sizeof(float3) * vert_count;
          size_t colors_size = sizeof(float3) * vert_count;

          VkBuffer buffers[3] = {b, b, b};
          VkDeviceSize offsets[3] = {idx_size, idx_size + pos_size,
                                     idx_size + pos_size + colors_size};

          vkCmdBindVertexBuffers(graphics_buffer, 0, 3, buffers, offsets);
          vkCmdDrawIndexed(graphics_buffer, idx_count, 1, 0, 0, 0);
          OptickAPI_PopGPUEvent(optick_gpu_e);
        }

        // Draw Plane
        {
          OPTICK_C_GPU_PUSH(optick_gpu_e, "Plane",
                            OptickAPI_Category_GPU_Scene);
          // Hack to change the plane's transform
          d->push_constants.mvp = *vp;
          vkCmdPushConstants(graphics_buffer, d->simple_pipe_layout,
                             VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                             sizeof(PushConstants),
                             (const void *)&d->push_constants);

          vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->uv_mesh_pipeline);

          vkCmdBindDescriptorSets(graphics_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  d->material_pipe_layout, 0, 1,
                                  &d->mesh_descriptor_sets[frame_idx], 0, NULL);

          uint32_t idx_count = d->plane_gpu.idx_count;
          VkBuffer buffer = d->plane_gpu.gpu.buffer;
          VkDeviceSize offset = d->plane_gpu.idx_size;

          vkCmdBindIndexBuffer(graphics_buffer, buffer, 0,
                               VK_INDEX_TYPE_UINT16);
          vkCmdBindVertexBuffers(graphics_buffer, 0, 1, &buffer, &offset);
          vkCmdDrawIndexed(graphics_buffer, idx_count, 1, 0, 0, 0);
          OptickAPI_PopGPUEvent(optick_gpu_e);
        }

        // Draw Scene
        {
          OPTICK_C_GPU_PUSH(optick_gpu_e, "Duck", OptickAPI_Category_GPU_Scene);
          // HACK: Known desired permutations
          uint32_t perm = GLTF_PERM_NONE;
          VkPipelineLayout pipe_layout = d->gltf_pipe_layout;
          VkPipeline pipe = d->gltf_pipeline->pipelines[perm];

          vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipe);
          vkCmdBindDescriptorSets(
              graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout, 0,
              1, &d->gltf_descriptor_sets[frame_idx], 0, NULL);
          demo_render_scene(d->duck, graphics_buffer, pipe_layout,
                            &d->push_constants, vp);
          OptickAPI_PopGPUEvent(optick_gpu_e);
        }

        // Draw Skydome
        {
          OPTICK_C_GPU_PUSH(optick_gpu_e, "Skydome",
                            OptickAPI_Category_GPU_Scene);
          // Another hack to fiddle with the matrix we send to the shader for
          // the skydome
          d->push_constants.mvp = *sky_vp;
          vkCmdPushConstants(graphics_buffer, d->simple_pipe_layout,
                             VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                             sizeof(PushConstants),
                             (const void *)&d->push_constants);

          uint32_t idx_count = d->skydome_gpu.idx_count;
          uint32_t vert_count = d->skydome_gpu.vtx_count;

          vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            d->skydome_pipeline);

          vkCmdBindDescriptorSets(
              graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
              d->skydome_pipe_layout, 0, 1,
              &d->skydome_descriptor_sets[frame_idx], 0, NULL);

          VkBuffer b = d->skydome_gpu.gpu.buffer;

          size_t idx_size =
              idx_count * sizeof(uint16_t) >> d->skydome_gpu.idx_type;
          size_t pos_size = sizeof(float3) * vert_count;

          VkBuffer buffers[1] = {b};
          VkDeviceSize offsets[1] = {idx_size};

          vkCmdBindIndexBuffer(graphics_buffer, b, 0, VK_INDEX_TYPE_UINT16);
          vkCmdBindVertexBuffers(graphics_buffer, 0, 1, buffers, offsets);
          vkCmdDrawIndexed(graphics_buffer, idx_count, 1, 0, 0, 0);
          OptickAPI_PopGPUEvent(optick_gpu_e);
        }

        vkCmdEndRenderPass(graphics_buffer);
      }

      OptickAPI_SetGpuContext(optick_prev_gpu_ctx);

      err = vkEndCommandBuffer(graphics_buffer);
      assert(err == VK_SUCCESS);
    }

    // Render ImGui Pass
    {
      {
        OPTICK_C_PUSH(optick_e, "ImGui Internal", OptickAPI_Category_UI);
        igRender();
        OptickAPI_PopEvent(optick_e);
      }

      // TODO: (Re)Create and upload ImGui geometry buffer
      {
        OPTICK_C_PUSH(optick_e, "ImGui CPU", OptickAPI_Category_UI);

        // If imgui_gpu is empty, this is still safe to call
        destroy_gpumesh(device, d->vma_alloc, &d->imgui_gpu);

        const ImDrawData *draw_data = igGetDrawData();
        if (draw_data->Valid) {
          uint32_t idx_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
          uint32_t vtx_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);

          uint32_t imgui_size = idx_size + vtx_size;

          if (imgui_size > 0) {
            d->imgui_mesh_data =
                hb_realloc(d->std_alloc, d->imgui_mesh_data, imgui_size);

            uint8_t *idx_dst = d->imgui_mesh_data;
            uint8_t *vtx_dst = idx_dst + idx_size;

            // Organize all mesh data into a single cpu-side buffer
            for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
              const ImDrawList *cmd_list = draw_data->CmdLists[i];
              memcpy(vtx_dst, cmd_list->VtxBuffer.Data,
                     cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
              memcpy(idx_dst, cmd_list->IdxBuffer.Data,
                     cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
              vtx_dst += cmd_list->VtxBuffer.Size;
              idx_dst += cmd_list->IdxBuffer.Size;
            }
            idx_dst = d->imgui_mesh_data;
            vtx_dst = idx_dst + idx_size;

            cpumesh imgui_cpu = {.geom_size = imgui_size,
                                 .index_count = draw_data->TotalIdxCount,
                                 .index_size = idx_size,
                                 .indices = (uint16_t *)idx_dst,
                                 .vertex_count = draw_data->TotalVtxCount,
                                 .vertices = vtx_dst};

            create_gpumesh(device, d->vma_alloc, &imgui_cpu, &d->imgui_gpu);

            // TODO: Manually record mesh upload here
            {}
          }
        }

        OptickAPI_PopEvent(optick_e);
      };
      // TODO: Record and submit ImGui render commands
      {
        OPTICK_C_GPU_PUSH(optick_gpu_e, "ImGui", OptickAPI_Category_GPU_UI);
        OptickAPI_PopGPUEvent(optick_gpu_e);
      }
    }

    // Submit
    {
      OPTICK_C_PUSH(demo_render_frame_submit_event, "demo_render_frame submit",
                    OptickAPI_Category_Rendering);

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

      OptickAPI_PopEvent(demo_render_frame_submit_event);
    }

    OptickAPI_PopEvent(demo_render_frame_render_event);
  }

  // Present
  {
    OPTICK_C_PUSH(demo_render_frame_present_event, "demo_render_frame present",
                  OptickAPI_Category_Rendering);

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

    OptickAPI_GPUFlip(&swapchain);

    OptickAPI_PopEvent(demo_render_frame_present_event);
  }

  OptickAPI_PopEvent(demo_render_frame_event);
}

static bool demo_screenshot(demo *d, allocator std_alloc,
                            uint8_t **screenshot_bytes,
                            uint32_t *screenshot_size) {
  OPTICK_C_PUSH(optick_e, "demo_screenshot", OptickAPI_Category_Rendering);
  VkResult err = VK_SUCCESS;

  VkDevice device = d->device;
  uint32_t frame_idx = d->frame_idx;
  VkFence prev_frame_fence = d->fences[frame_idx];
  VmaAllocator vma_alloc = d->vma_alloc;
  gpuimage screenshot_image = d->screenshot_image;
  VkImage swap_image = d->swapchain_images[frame_idx];
  VkFence swap_fence = d->fences[frame_idx];

  VkQueue queue = d->graphics_queue;

  VkFence screenshot_fence = d->screenshot_fence;
  VkCommandBuffer screenshot_cmd = d->screenshot_buffers[frame_idx];

  /*
    Only need to wait for this fence if we know it hasn't been signaled.
    As such we don't need to reset the fence, that will be done by another
    waiter.
  */
  VkResult status = vkGetFenceStatus(device, swap_fence);
  if (status == VK_NOT_READY) {
    err = vkWaitForFences(device, 1, &swap_fence, VK_TRUE, ~0ULL);
    if (err != VK_SUCCESS) {
      OptickAPI_PopEvent(optick_e);
      assert(0);
      return false;
    }
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  err = vkBeginCommandBuffer(screenshot_cmd, &begin_info);
  if (err != VK_SUCCESS) {
    OptickAPI_PopEvent(optick_e);
    assert(0);
    return false;
  }

  // Issue necessary memory barriers
  VkImageMemoryBarrier barrier = {0};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange =
      (VkImageSubresourceRange){.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                .levelCount = 1,
                                .layerCount = 1};
  {
    // Transition swap image from Present to Transfer Src
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = swap_image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);

    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlagBits src_access = 0;
    // If screenshot_bytes points to something not-null that means we've
    // made a screenshot before and can assume the old layout
    if ((*screenshot_bytes) != NULL) {
      old_layout = VK_IMAGE_LAYOUT_GENERAL;
      src_access = VK_ACCESS_MEMORY_READ_BIT;
    }

    // Transition screenshot image from General (or Undefined) to Transfer Dst
    barrier.oldLayout = old_layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = screenshot_image.image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
  }

  // Copy the swapchain image to a GPU to CPU image of a known format
  VkImageCopy image_copy = {
      .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .layerCount = 1},
      .srcOffset = {0, 0, 0},
      .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .layerCount = 1},
      .dstOffset = {0, 0, 0},
      .extent = {d->swap_width, d->swap_height, 1},
  };
  vkCmdCopyImage(screenshot_cmd, swap_image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screenshot_image.image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

  // Issue necessary memory barriers back to original formats

  {
    // Transition swap image from to Transfer Src to Present
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.image = swap_image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);

    // Transition screenshot image from Transfer Dst to General
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    barrier.image = screenshot_image.image;

    vkCmdPipelineBarrier(screenshot_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
  }

  err = vkEndCommandBuffer(screenshot_cmd);
  if (err != VK_SUCCESS) {
    OptickAPI_PopEvent(optick_e);
    assert(0);
    return false;
  }

  VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &screenshot_cmd,
  };
  err = vkQueueSubmit(queue, 1, &submit_info, screenshot_fence);
  if (err != VK_SUCCESS) {
    OptickAPI_PopEvent(optick_e);
    assert(0);
    return false;
  }

  // Could move this to another place later on as it will take time for this
  // command to finish

  err = vkWaitForFences(device, 1, &screenshot_fence, VK_TRUE, ~0ULL);
  if (err != VK_SUCCESS) {
    OptickAPI_PopEvent(optick_e);
    assert(0);
    return false;
  }
  vkResetFences(device, 1, &screenshot_fence);

  VkImageSubresource sub_resource = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
  };
  VkSubresourceLayout sub_resource_layout = {0};
  vkGetImageSubresourceLayout(device, screenshot_image.image, &sub_resource,
                              &sub_resource_layout);

  uint8_t *screenshot_mem = NULL;
  err =
      vmaMapMemory(vma_alloc, screenshot_image.alloc, (void **)&screenshot_mem);
  if (err != VK_SUCCESS) {
    OptickAPI_PopEvent(optick_e);
    assert(0);
    return false;
  }

  VmaAllocationInfo alloc_info = {0};
  vmaGetAllocationInfo(vma_alloc, screenshot_image.alloc, &alloc_info);

  if (alloc_info.size > (*screenshot_size)) {
    (*screenshot_bytes) =
        hb_realloc(std_alloc, (*screenshot_bytes), alloc_info.size);
    (*screenshot_size) = alloc_info.size;
  }

  // Use SDL to transform raw bytes into a png bytestream
  {
    uint32_t rmask, gmask, bmask, amask;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0x0000ff00;
    gmask = 0x00ff0000;
    bmask = 0xff000000;
    amask = 0x000000ff;
#else // little endian, like x86
    rmask = 0x00ff0000;
    gmask = 0x0000ff00;
    bmask = 0x000000ff;
    amask = 0xff000000;
#endif
    // Note ^ We're assuming that the swapchain is BGR

    int32_t pitch = d->swap_width * 4;
    SDL_Surface *img = SDL_CreateRGBSurfaceFrom(
        (screenshot_mem + sub_resource_layout.offset), d->swap_width,
        d->swap_height, 32, pitch, rmask, gmask, bmask, amask);
    assert(img);

    SDL_RWops *ops =
        SDL_RWFromMem((void *)(*screenshot_bytes), sub_resource_layout.size);
    IMG_SavePNG_RW(img, ops, 0);

    SDL_FreeSurface(img);
  }

  vmaUnmapMemory(vma_alloc, screenshot_image.alloc);

  OptickAPI_PopEvent(optick_e);
  return true;
}

static void demo_destroy(demo *d) {
  OPTICK_C_PUSH(optick_e, "demo_destroy", OptickAPI_Category_None);

  VkDevice device = d->device;
  VmaAllocator vma_alloc = d->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = d->vk_alloc;

  vkDeviceWaitIdle(device);

  // Write out the pipeline cache
  {
    VkResult err = VK_SUCCESS;

    size_t cache_size = 0;
    err = vkGetPipelineCacheData(device, d->pipeline_cache, &cache_size, NULL);
    if (err == VK_SUCCESS) {
      void *cache = hb_alloc(d->tmp_alloc, cache_size);
      err =
          vkGetPipelineCacheData(device, d->pipeline_cache, &cache_size, cache);
      if (err == VK_SUCCESS) {

        SDL_RWops *cache_file = SDL_RWFromFile("./pipeline.cache", "wb");
        if (cache_file != NULL) {
          SDL_RWwrite(cache_file, cache, cache_size, 1);
          SDL_RWclose(cache_file);
        }
      }
    }
  }

  for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
    vkDestroyImageView(device, d->depth_buffer_views[i], vk_alloc);
    vkDestroyDescriptorPool(device, d->descriptor_pools[i], vk_alloc);
    vkDestroyFence(device, d->fences[i], vk_alloc);
    vkDestroySemaphore(device, d->upload_complete_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->render_complete_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->swapchain_image_sems[i], vk_alloc);
    vkDestroySemaphore(device, d->img_acquired_sems[i], vk_alloc);
    vkDestroyImageView(device, d->swapchain_image_views[i], vk_alloc);
    vkDestroyFramebuffer(device, d->swapchain_framebuffers[i], vk_alloc);
    vkDestroyCommandPool(device, d->command_pools[i], vk_alloc);
  }

  destroy_gpuimage(vma_alloc, &d->depth_buffers);

  hb_free(d->std_alloc, d->imgui_mesh_data);

  destroy_scene(device, vma_alloc, vk_alloc, d->duck);
  destroy_texture(device, vma_alloc, vk_alloc, &d->pattern);
  destroy_texture(device, vma_alloc, vk_alloc, &d->roughness);
  destroy_texture(device, vma_alloc, vk_alloc, &d->normal);
  destroy_texture(device, vma_alloc, vk_alloc, &d->displacement);
  destroy_texture(device, vma_alloc, vk_alloc, &d->albedo);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->sky_const_buffer);
  destroy_gpumesh(device, vma_alloc, &d->skydome_gpu);
  destroy_gpumesh(device, vma_alloc, &d->plane_gpu);
  destroy_gpumesh(device, vma_alloc, &d->cube_gpu);
  destroy_gpumesh(device, vma_alloc, &d->imgui_gpu);

  vkDestroyFence(device, d->screenshot_fence, vk_alloc);
  destroy_gpuimage(vma_alloc, &d->screenshot_image);

  vmaDestroyPool(vma_alloc, d->upload_mem_pool);
  vmaDestroyPool(vma_alloc, d->texture_mem_pool);

  hb_free(d->std_alloc, d->queue_props);
  vkDestroySampler(device, d->sampler, vk_alloc);

  vkDestroyPipeline(device, d->fractal_pipeline, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->skydome_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->skydome_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->skydome_pipeline, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->material_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->material_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->uv_mesh_pipeline, vk_alloc);

  vkDestroyPipelineLayout(device, d->simple_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->color_mesh_pipeline, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->gltf_rt_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_rt_pipe_layout, vk_alloc);
  // destroy_gpupipeline(device, vk_alloc, d->gltf_rt_pipeline);

  vkDestroyDescriptorSetLayout(device, d->gltf_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_pipe_layout, vk_alloc);
  destroy_gpupipeline(device, vk_alloc, d->gltf_pipeline);

  vkDestroyPipelineCache(device, d->pipeline_cache, vk_alloc);
  vkDestroyRenderPass(device, d->render_pass, vk_alloc);
  vkDestroySwapchainKHR(device, d->swapchain, vk_alloc);
  vkDestroySurfaceKHR(d->instance, d->surface,
                      NULL); // Surface is created by SDL
  vmaDestroyAllocator(vma_alloc);
  vkDestroyDevice(device, vk_alloc);
  *d = (demo){0};

  igDestroyContext(d->ig_ctx);

  OptickAPI_PopEvent(optick_e);
}

static void *vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                         VkSystemAllocationScope scope) {
  (void)scope;
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  return mi_heap_malloc_aligned(heap, size, alignment);
}

static void *vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                           size_t alignment, VkSystemAllocationScope scope) {
  (void)scope;
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  return mi_heap_realloc_aligned(heap, pOriginal, size, alignment);
}

static void vk_free_fn(void *pUserData, void *pMemory) {
  (void)pUserData;
  mi_free(pMemory);
}

static VkAllocationCallbacks create_vulkan_allocator(mi_heap_t *heap) {
  VkAllocationCallbacks ret = {
      .pUserData = heap,
      .pfnAllocation = vk_alloc_fn,
      .pfnReallocation = vk_realloc_fn,
      .pfnFree = vk_free_fn,
  };
  return ret;
}

void optick_init_thread_cb() {}

// The intent is that g_screenshot_bytes will be allocated on a heap by the
// renderer.
static uint8_t *g_screenshot_bytes = NULL;
static uint32_t g_screenshot_size = 0;
static bool g_taking_screenshot = false;

bool optick_state_changed_callback(OptickAPI_State state) {
  if (state == OptickAPI_State_StopCapture) {
    // Request that we take a screenshot and store it in g_screenshot_bytes
    g_taking_screenshot = true;
  } else if (state == OptickAPI_State_DumpCapture) {
    // Return false so optick knows that we *didn't* dump a capture
    // In this case because we're waiting for the renderer to get a screenshot
    // captured.
    // Optick will consider the caputre un-dumped and will attempt to call this
    // again
    if (g_taking_screenshot) {
      return false;
    }
    OptickAPI_AttachSummary("Engine", "SDLTest");
    OptickAPI_AttachSummary("Author", "Honeybunch");
    OptickAPI_AttachSummary("Version", HB_VERSION);
    OptickAPI_AttachSummary("Configuration", HB_CONFIG);
    OptickAPI_AttachSummary("Arch", HB_ARCH);
    // OptickAPI_AttachSummary("GPU Manufacturer", "TODO");
    // OptickAPI_AttachSummary("ISA Vulkan Version", "TODO");
    // OptickAPI_AttachSummary("GPU Driver Version", "TODO");

    OptickAPI_AttachFile(OptickAPI_File_Image, "Screenshot.png",
                         g_screenshot_bytes, g_screenshot_size);
  }
  return true;
}

int32_t SDL_main(int32_t argc, char *argv[]) {
  static const float qtr_pi = 0.7853981625f;

  OptickAPI_SetAllocator(mi_malloc, mi_free, optick_init_thread_cb);

  static const char thread_name[] = "Main Thread";
  OptickAPI_RegisterThread(thread_name, sizeof(thread_name));

  OptickAPI_SetStateChangedCallback(optick_state_changed_callback);

  OptickAPI_StartCapture();

  // Create Temporary Arena Allocator
  static const size_t arena_alloc_size = 1024 * 1024 * 1024; // 1 MB
  arena_allocator arena = create_arena_allocator(arena_alloc_size);

  mi_heap_t *vk_heap = mi_heap_new();
  VkAllocationCallbacks vk_alloc = create_vulkan_allocator(vk_heap);
  standard_allocator std_alloc = create_standard_allocator();

  const VkAllocationCallbacks *vk_alloc_ptr = &vk_alloc;

  assert(igDebugCheckVersionAndDataLayout(
      igGetVersion(), sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2),
      sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx)));

  editor_camera_controller controller = {0};
  controller.move_speed = 10.0f;
  controller.look_speed = 1.0f;

  camera main_cam = {
      .transform =
          {
              .position = {0, -1, 10},
              .scale = {1, 1, 1},
          },
      .aspect = (float)WIDTH / (float)HEIGHT,
      .fov = qtr_pi,
      .near = 0.01f,
      .far = 100.0f,
  };

  VkResult err = volkInitialize();
  assert(err == VK_SUCCESS);

  {
    int32_t res = SDL_Init(SDL_INIT_VIDEO);
    assert(res == 0);

    int32_t flags = IMG_INIT_PNG;
    res = IMG_Init(flags);
    assert(res & IMG_INIT_PNG);
  }

  SDL_Window *window = SDL_CreateWindow("SDL Test", SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT,
                                        SDL_WINDOW_VULKAN);
  if (window == NULL) {
    char msg[500] = {0};
    SDL_GetErrorMsg(msg, 500);
    assert(0);
  }

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
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = layer_names;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = ext_names;

    err = vkCreateInstance(&create_info, vk_alloc_ptr, &instance);
    assert(err == VK_SUCCESS);
    volkLoadInstance(instance);
  }

  demo d = {0};
  bool success = demo_init(window, instance, std_alloc.alloc, arena.alloc,
                           vk_alloc_ptr, &d);
  assert(success);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wincompatible-function-pointer-types"
#endif
  OptickAPI_VulkanFunctions optick_volk_funcs = {
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkCreateQueryPool = vkCreateQueryPool,
      .vkCreateCommandPool = vkCreateCommandPool,
      .vkAllocateCommandBuffers = vkAllocateCommandBuffers,
      .vkCreateFence = vkCreateFence,
      .vkCmdResetQueryPool = vkCmdResetQueryPool,
      .vkQueueSubmit = vkQueueSubmit,
      .vkWaitForFences = vkWaitForFences,
      .vkResetCommandBuffer = vkResetCommandBuffer,
      .vkCmdWriteTimestamp = vkCmdWriteTimestamp,
      .vkGetQueryPoolResults = vkGetQueryPoolResults,
      .vkBeginCommandBuffer = vkBeginCommandBuffer,
      .vkEndCommandBuffer = vkEndCommandBuffer,
      .vkResetFences = vkResetFences,
      .vkDestroyCommandPool = vkDestroyCommandPool,
      .vkDestroyQueryPool = vkDestroyQueryPool,
      .vkDestroyFence = vkDestroyFence,
      .vkFreeCommandBuffers = vkFreeCommandBuffers,
  };
#ifdef __clang__
#pragma clang diagnostic pop
#endif
  OptickAPI_GPUInitVulkan(&d.device, &d.gpu, &d.graphics_queue,
                          &d.graphics_queue_family_index, 1,
                          &optick_volk_funcs);

  transform cube_transform = {
      .position = {0, 2, 0},
      .scale = {1, 1, 1},
      .rotation = {0, 0, 0},
  };

  float4x4 cube_obj_mat = {.row0 = {0}};
  float4x4 cube_mvp = {.row0 = {0}};

  SkyData sky_data = {
      .sun_dir = {0, -1, 0},
      .turbidity = 3,
      .albedo = 1,
  };

  // Main loop
  bool running = true;
  float time_ms = 0;
  float time_seconds = 0;
  float time_ns = 0.0f; // Uncalculated and unused for now
  float time_us = 0.0f; // Uncalculated and unused for now

  float last_time_ms = 0;
  float delta_time_ms = 0;
  float delta_time_seconds = 0;
  while (running) {
    OptickAPI_NextFrame();

    ImVec2 display_size;
    display_size.x = WIDTH;
    display_size.y = HEIGHT;
    d.ig_io->DisplaySize = display_size;
    d.ig_io->DeltaTime = 1.0f / 60.0f; // TODO: Input real delta time
    igNewFrame();

    // ImGui Test
    igBegin("mainwindow", NULL, ImGuiWindowFlags_NoTitleBar);
    static float f = 0.0f;
    igText("Hello World!");
    igSliderFloat("float", &f, 0.0f, 1.0f, "%.3f", 0);
    igText("Application average %.3f ms/frame (%.1f FPS)",
           1000.0f / d.ig_io->Framerate, d.ig_io->Framerate);
    igEnd();
    igShowDemoWindow(NULL);

    // Crunch some numbers
    last_time_ms = time_ms;
    time_ms = (float)SDL_GetTicks();
    delta_time_ms = time_ms - last_time_ms;
    delta_time_seconds = delta_time_ms / 1000.0f;
    time_seconds = time_ms / 1000.0f;

    // TODO: Handle events more gracefully
    // Mutliple events (or none) could happen in one frame but we only process
    // the latest one
    SDL_Event e = {0};
    SDL_PollEvent(&e);
    if (e.type == SDL_QUIT) {
      running = false;
      break;
    }

    editor_camera_control(delta_time_seconds, &e, &controller, &main_cam);

    // Spin cube
    cube_transform.rotation[1] += 1.0f * delta_time_seconds;
    transform_to_matrix(&cube_obj_mat, &cube_transform);

    float4x4 view = {.row0 = {0}};
    camera_view(&main_cam, &view);

    float4x4 sky_view = {.row0 = {0}};
    camera_sky_view(&main_cam, &sky_view);

    float4x4 proj = {.row0 = {0}};
    camera_projection(&main_cam, &proj);

    float4x4 vp = {.row0 = {0}};
    mulmf44(&proj, &view, &vp);

    float4x4 sky_vp = {.row0 = {0}};
    mulmf44(&proj, &sky_view, &sky_vp);

    mulmf44(&vp, &cube_obj_mat, &cube_mvp);

    // Change sun position
    {
      float y = -cosf(time_seconds);
      float z = sinf(time_seconds);
      sky_data.sun_dir = (float3){0, y, z};
    }

    // Pass time to shader
    d.push_constants.time = (float4){time_seconds, time_ms, time_ns, time_us};
    d.push_constants.resolution = (float2){d.swap_width, d.swap_height};

    // Pass MVP to shader
    d.push_constants.mvp = cube_mvp;
    d.push_constants.m = cube_obj_mat;
    d.push_constants.view_pos = main_cam.transform.position;

    // Light data to shader
    d.push_constants.light_dir = -sky_data.sun_dir;

    // Update sky constant buffer
    {
      OPTICK_C_PUSH(update_sky_event, "Update Sky",
                    OptickAPI_Category_Rendering)

      VmaAllocator vma_alloc = d.vma_alloc;
      VkBuffer sky_host = d.sky_const_buffer.host.buffer;
      VmaAllocation sky_host_alloc = d.sky_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, sky_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }
      memcpy(data, &sky_data, sizeof(SkyData));
      vmaUnmapMemory(vma_alloc, sky_host_alloc);

      demo_upload_const_buffer(&d, &d.sky_const_buffer);
      OptickAPI_PopEvent(update_sky_event);
    }

    demo_render_frame(&d, &vp, &sky_vp);

    if (g_taking_screenshot) {
      g_taking_screenshot = !demo_screenshot(
          &d, std_alloc.alloc, &g_screenshot_bytes, &g_screenshot_size);
    }

    // Reset the arena allocator
    reset_arena(arena, true); // Just allow it to grow for now
  }

  SDL_DestroyWindow(window);
  window = NULL;

  IMG_Quit();
  SDL_Quit();

  OptickAPI_GPUShutdown();

  demo_destroy(&d);

  OptickAPI_Shutdown();

  if (g_screenshot_bytes != NULL) {
    hb_free(std_alloc.alloc, g_screenshot_bytes);
    g_screenshot_bytes = NULL;
    g_screenshot_size = 0;
  }

  vkDestroyInstance(instance, vk_alloc_ptr);
  instance = VK_NULL_HANDLE;

  destroy_arena_allocator(arena);
  destroy_standard_allocator(std_alloc);
  mi_heap_delete(vk_heap);

  return 0;
}