#include "demo.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <mimalloc.h>
#include <optick_capi.h>
#include <stddef.h>

#include <volk.h>

#include <vk_mem_alloc.h>

#include "cpuresources.h"
#include "pipelines.h"
#include "shadercommon.h"
#include "simd.h"
#include "skydome.h"

#define MAX_EXT_COUNT 16

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

static VkPhysicalDevice select_gpu(VkInstance instance, allocator tmp_alloc) {
  uint32_t gpu_count = 0;
  VkResult err = vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
  assert(err == VK_SUCCESS);

  VkPhysicalDevice *physical_devices =
      hb_alloc_nm_tp(tmp_alloc, gpu_count, VkPhysicalDevice);
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
  VkPhysicalDevice gpu = physical_devices[gpu_idx];
  hb_free(tmp_alloc, physical_devices);
  return gpu;
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

static void demo_render_scene(scene *s, VkCommandBuffer cmd,
                              VkPipelineLayout layout, VkDescriptorSet view_set,
                              VkDescriptorSet object_set,
                              VkDescriptorSet material_set, const float4x4 *vp,
                              demo *d) {
  OPTICK_C_PUSH(optick_e, "demo_render_scene", OptickAPI_Category_Rendering);
  for (uint32_t i = 0; i < s->entity_count; ++i) {
    uint64_t components = s->components[i];
    scene_transform *scene_transform = &s->transforms[i];
    scene_static_mesh *static_mesh = &s->static_meshes[i];

    if (components & COMPONENT_TYPE_STATIC_MESH) {
      transform *t = &scene_transform->t;

      // Hack to fuck with the scale of the object
      // t->scale = (float3){0.01f, -0.01f, 0.01f};
      // t->scale = (float3){100.0f, -100.0f, 100.0f};
      t->scale = (float3){1.0f, -1.0f, 1.0f};

      CommonObjectData object_data = {0};

      transform_to_matrix(&object_data.m, t);
      mulmf44(vp, &object_data.m, &object_data.mvp);

      // HACK: Update object's constant buffer here
      {
        OPTICK_C_PUSH(update_object_event, "Update Object Const Buffer",
                      OptickAPI_Category_Rendering);

        VmaAllocator vma_alloc = d->vma_alloc;
        VkBuffer object_host = d->object_const_buffer.host.buffer;
        VmaAllocation object_host_alloc = d->object_const_buffer.host.alloc;

        uint8_t *data = NULL;
        VkResult err =
            vmaMapMemory(vma_alloc, object_host_alloc, (void **)&data);
        if (err != VK_SUCCESS) {
          assert(0);
          return;
        }
        memcpy(data, &object_data, sizeof(CommonObjectData));
        vmaUnmapMemory(vma_alloc, object_host_alloc);

        demo_upload_const_buffer(d, &d->object_const_buffer);

        OptickAPI_PopEvent(update_object_event);
      }

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                              1, &material_set, 0, NULL);

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              1, &object_set, 0, NULL);

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 2,
                              1, &view_set, 0, NULL);

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

bool demo_init(SDL_Window *window, VkInstance instance, allocator std_alloc,
               allocator tmp_alloc, const VkAllocationCallbacks *vk_alloc,
               demo *d) {
  OPTICK_C_PUSH(optick_e, "demo_init", OptickAPI_Category_None);
  VkResult err = VK_SUCCESS;

  // Get the GPU we want to run on
  VkPhysicalDevice gpu = select_gpu(instance, tmp_alloc);
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
    VkBool32 *supports_present =
        hb_alloc_nm_tp(tmp_alloc, queue_family_count, VkBool32);
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
    hb_free(tmp_alloc, supports_present);

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

  uint32_t width = 0;
  uint32_t height = 0;
  {
    int32_t w = 0;
    int32_t h = 0;
    SDL_GetWindowSize(window, &w, &h);
    width = (uint32_t)w;
    height = (uint32_t)h;
  }

  // Create Swapchain
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;

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
        hb_alloc_nm_tp(tmp_alloc, format_count, VkSurfaceFormatKHR);
    err = vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count,
                                               surface_formats);
    assert(err == VK_SUCCESS);
    VkSurfaceFormatKHR surface_format =
        pick_surface_format(surface_formats, format_count);
    swapchain_image_format = surface_format.format;
    swapchain_color_space = surface_format.colorSpace;
    hb_free(tmp_alloc, surface_formats);
    surface_formats = NULL;

    VkSurfaceCapabilitiesKHR surf_caps;
    err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surf_caps);
    assert(err == VK_SUCCESS);

    uint32_t present_mode_count = 0;
    err = vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface,
                                                    &present_mode_count, NULL);
    assert(err == VK_SUCCESS);
    VkPresentModeKHR *present_modes =
        hb_alloc_nm_tp(tmp_alloc, present_mode_count, VkPresentModeKHR);
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
    hb_free(tmp_alloc, present_modes);
    present_modes = NULL;

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

  // Create ImGui Render Pass
  VkRenderPass imgui_pass = VK_NULL_HANDLE;
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

    VkAttachmentDescription attachments[1] = {color_attachment};

    VkAttachmentReference color_attachment_ref = {
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference attachment_refs[1] = {color_attachment_ref};

    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = attachment_refs;

    VkSubpassDependency subpass_dep = {0};
    subpass_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpass_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = attachments;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.pDependencies = &subpass_dep;
    err = vkCreateRenderPass(device, &create_info, vk_alloc, &imgui_pass);
    assert(err == VK_SUCCESS);
  }

  // Create Pipeline Cache
  VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
  {
    OPTICK_C_PUSH(pipe_cache_e, "init pipeline cache", OptickAPI_Category_None);
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
    OptickAPI_PopEvent(pipe_cache_e);
  }

  VkPushConstantRange sky_const_range = {
      VK_SHADER_STAGE_ALL_GRAPHICS,
      0,
      sizeof(SkyPushConstants),
  };

  VkPushConstantRange imgui_const_range = {
      VK_SHADER_STAGE_ALL_GRAPHICS,
      0,
      sizeof(ImGuiPushConstants),
  };

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
    create_info.pPushConstantRanges = &sky_const_range;

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

  // Create Common Object DescriptorSet Layout
  VkDescriptorSetLayout gltf_object_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[1] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 1;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_object_set_layout);
    assert(err == VK_SUCCESS);
  }

  // Create Common Per-View DescriptorSet Layout
  VkDescriptorSetLayout gltf_view_set_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 2;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &gltf_view_set_layout);
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Descriptor Set Layout
  VkDescriptorSetLayout gltf_material_set_layout = VK_NULL_HANDLE;
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
                                      &gltf_material_set_layout);
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Pipeline Layout
  VkPipelineLayout gltf_pipe_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayout layouts[] = {
        gltf_material_set_layout,
        gltf_object_set_layout,
        gltf_view_set_layout,
    };
    const uint32_t layout_count =
        sizeof(layouts) / sizeof(VkDescriptorSetLayout);

    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = layout_count;
    create_info.pSetLayouts = layouts;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &gltf_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create GLTF Pipeline
  gpupipeline *gltf_pipeline = NULL;
  err = create_gltf_pipeline(device, vk_alloc, tmp_alloc, pipeline_cache,
                             render_pass, width, height, gltf_pipe_layout,
                             &gltf_pipeline);
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
  //    device, vk_alloc, tmp_alloc, pipeline_cache,
  //    vkCreateRayTracingPipelinesKHR, render_pass, width, height,
  //    gltf_rt_pipe_layout, &gltf_rt_pipeline);
  // assert(err == VK_SUCCESS);

  // Create ImGui Descriptor Set Layout
  VkDescriptorSetLayout imgui_layout = VK_NULL_HANDLE;
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = 2;
    create_info.pBindings = bindings;
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &imgui_layout);
    assert(err == VK_SUCCESS);
  }

  // Create ImGui Pipeline Layout
  VkPipelineLayout imgui_pipe_layout = VK_NULL_HANDLE;
  {
    VkPipelineLayoutCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.setLayoutCount = 1;
    create_info.pSetLayouts = &imgui_layout;
    create_info.pushConstantRangeCount = 1;
    create_info.pPushConstantRanges = &imgui_const_range;

    err = vkCreatePipelineLayout(device, &create_info, vk_alloc,
                                 &imgui_pipe_layout);
    assert(err == VK_SUCCESS);
  }

  // Create ImGui pipeline
  VkPipeline imgui_pipeline = VK_NULL_HANDLE;
  err =
      create_imgui_pipeline(device, vk_alloc, pipeline_cache, imgui_pass, width,
                            height, imgui_pipe_layout, &imgui_pipeline);
  assert(err == VK_SUCCESS);

  // Create a pool for host memory uploads
  VmaPool upload_mem_pool = VK_NULL_HANDLE;
  {
    OPTICK_C_PUSH(vma_pool_e, "init vma upload pool", OptickAPI_Category_None);
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

    OptickAPI_PopEvent(vma_pool_e);
  }

  // Create a pool for texture memory
  VmaPool texture_mem_pool = VK_NULL_HANDLE;
  {
    OPTICK_C_PUSH(vma_pool_e, "init vma texture pool", OptickAPI_Category_None);
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

    // block size to fit a 4k R8G8B8A8 uncompressed texture
    uint64_t block_size = (uint64_t)(4096.0 * 4096.0 * 4.0);

    VmaPoolCreateInfo create_info = {0};
    create_info.memoryTypeIndex = mem_type_idx;
    create_info.blockSize = block_size;
    create_info.minBlockCount = 10;
    err = vmaCreatePool(vma_alloc, &create_info, &texture_mem_pool);
    assert(err == VK_SUCCESS);
    OptickAPI_PopEvent(vma_pool_e);
  }

  // Create Skydome Mesh
  gpumesh skydome = {0};
  {
    cpumesh *skydome_cpu = create_skydome(&tmp_alloc);

    err = create_gpumesh(device, vma_alloc, skydome_cpu, &skydome);
    assert(err == VK_SUCCESS);
  }

  // Create Uniform buffer for sky data
  gpuconstbuffer sky_const_buffer =
      create_gpuconstbuffer(device, vma_alloc, vk_alloc, sizeof(SkyData));

  // Create Uniform buffer for object data
  gpuconstbuffer object_const_buffer = create_gpuconstbuffer(
      device, vma_alloc, vk_alloc, sizeof(CommonObjectData));

  // Create Uniform buffer for camera data
  gpuconstbuffer camera_const_buffer = create_gpuconstbuffer(
      device, vma_alloc, vk_alloc, sizeof(CommonCameraData));

  // Create Uniform buffer for light data
  gpuconstbuffer light_const_buffer = create_gpuconstbuffer(
      device, vma_alloc, vk_alloc, sizeof(CommonLightData));

  // Load scene
  scene *scene = NULL;
  load_scene(device, vk_alloc, tmp_alloc, vma_alloc, upload_mem_pool,
             texture_mem_pool, "./assets/scenes/Lantern.glb", &scene);

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
  d->imgui_pass = imgui_pass;
  d->pipeline_cache = pipeline_cache;
  d->sampler = sampler;
  d->skydome_layout = skydome_layout;
  d->skydome_pipe_layout = skydome_pipe_layout;
  d->skydome_pipeline = skydome_pipeline;
  d->sky_const_buffer = sky_const_buffer;
  d->object_const_buffer = object_const_buffer;
  d->camera_const_buffer = camera_const_buffer;
  d->light_const_buffer = light_const_buffer;
  d->gltf_material_set_layout = gltf_material_set_layout;
  d->gltf_object_set_layout = gltf_object_set_layout;
  d->gltf_view_set_layout = gltf_view_set_layout;
  d->gltf_pipe_layout = gltf_pipe_layout;
  d->gltf_pipeline = gltf_pipeline;
  d->gltf_rt_layout = gltf_rt_layout;
  d->gltf_rt_pipe_layout = gltf_rt_pipe_layout;
  // d->gltf_rt_pipeline = gltf_rt_pipeline;
  d->imgui_layout = imgui_layout;
  d->imgui_pipe_layout = imgui_pipe_layout;
  d->imgui_pipeline = imgui_pipeline;
  d->upload_mem_pool = upload_mem_pool;
  d->texture_mem_pool = texture_mem_pool;
  d->skydome_gpu = skydome;
  d->scene = scene;
  d->screenshot_image = screenshot_image;
  d->screenshot_fence = screenshot_fence;
  d->frame_idx = 0;

  demo_upload_mesh(d, &d->skydome_gpu);
  demo_upload_scene(d, d->scene);

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
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}};
    const uint32_t pool_sizes_count =
        sizeof(pool_sizes) / sizeof(VkDescriptorPoolSize);

    VkDescriptorPoolCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.maxSets = 6;
    create_info.poolSizeCount = pool_sizes_count;
    create_info.pPoolSizes = pool_sizes;

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

    alloc_info.pSetLayouts = &skydome_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->skydome_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &gltf_material_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->gltf_material_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &gltf_object_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->gltf_object_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }

    alloc_info.pSetLayouts = &gltf_view_set_layout;
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      alloc_info.descriptorPool = d->descriptor_pools[i];
      err = vkAllocateDescriptorSets(device, &alloc_info,
                                     &d->gltf_view_descriptor_sets[i]);
      assert(err == VK_SUCCESS);
    }
  }

  // Write textures to descriptor set
  {
    VkDescriptorBufferInfo skydome_info = {sky_const_buffer.gpu.buffer, 0,
                                           sky_const_buffer.size};
    VkDescriptorImageInfo duck_info = {
        NULL, scene->textures[0].view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo object_info = {object_const_buffer.gpu.buffer, 0,
                                          object_const_buffer.size};
    VkDescriptorBufferInfo camera_info = {camera_const_buffer.gpu.buffer, 0,
                                          camera_const_buffer.size};
    VkDescriptorBufferInfo light_info = {light_const_buffer.gpu.buffer, 0,
                                         light_const_buffer.size};
    VkWriteDescriptorSet writes[7] = {
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
        // Future permutations of the gltf pipeline may support these
        // For now we have to write *something* even if we know they're not used
        // (yet)
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &duck_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo = &duck_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &object_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &camera_info,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &light_info,
        },
    };
    for (uint32_t i = 0; i < FRAME_LATENCY; ++i) {
      VkDescriptorSet gltf_material_set = d->gltf_material_descriptor_sets[i];
      VkDescriptorSet gltf_object_set = d->gltf_object_descriptor_sets[i];
      VkDescriptorSet gltf_view_set = d->gltf_view_descriptor_sets[i];
      VkDescriptorSet skydome_set = d->skydome_descriptor_sets[i];

      writes[0].dstSet = skydome_set;

      writes[1].dstSet = gltf_material_set;
      writes[2].dstSet = gltf_material_set;
      writes[3].dstSet = gltf_material_set;

      writes[4].dstSet = gltf_object_set;

      writes[5].dstSet = gltf_view_set;
      writes[6].dstSet = gltf_view_set;

      vkUpdateDescriptorSets(device, 7, writes, 0, NULL);
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

void demo_destroy(demo *d) {
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

    destroy_gpumesh(device, vma_alloc, &d->imgui_gpu[i]);
  }

  destroy_gpuimage(vma_alloc, &d->depth_buffers);

  hb_free(d->std_alloc, d->imgui_mesh_data);

  destroy_scene(device, vma_alloc, vk_alloc, d->scene);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->sky_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->object_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->camera_const_buffer);
  destroy_gpuconstbuffer(device, vma_alloc, vk_alloc, d->light_const_buffer);
  destroy_gpumesh(device, vma_alloc, &d->skydome_gpu);

  vkDestroyFence(device, d->screenshot_fence, vk_alloc);
  destroy_gpuimage(vma_alloc, &d->screenshot_image);

  vmaDestroyPool(vma_alloc, d->upload_mem_pool);
  vmaDestroyPool(vma_alloc, d->texture_mem_pool);

  hb_free(d->std_alloc, d->queue_props);
  vkDestroySampler(device, d->sampler, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->skydome_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->skydome_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->skydome_pipeline, vk_alloc);

  vkDestroyDescriptorSetLayout(device, d->gltf_rt_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_rt_pipe_layout, vk_alloc);
  // destroy_gpupipeline(device, vk_alloc, d->gltf_rt_pipeline);

  vkDestroyDescriptorSetLayout(device, d->gltf_material_set_layout, vk_alloc);
  vkDestroyDescriptorSetLayout(device, d->gltf_object_set_layout, vk_alloc);
  vkDestroyDescriptorSetLayout(device, d->gltf_view_set_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->gltf_pipe_layout, vk_alloc);
  destroy_gpupipeline(device, vk_alloc, d->gltf_pipeline);

  vkDestroyDescriptorSetLayout(device, d->imgui_layout, vk_alloc);
  vkDestroyPipelineLayout(device, d->imgui_pipe_layout, vk_alloc);
  vkDestroyPipeline(device, d->imgui_pipeline, vk_alloc);

  vkDestroyPipelineCache(device, d->pipeline_cache, vk_alloc);
  vkDestroyRenderPass(device, d->render_pass, vk_alloc);
  vkDestroyRenderPass(device, d->imgui_pass, vk_alloc);
  vkDestroySwapchainKHR(device, d->swapchain, vk_alloc);
  vkDestroySurfaceKHR(d->instance, d->surface,
                      NULL); // Surface is created by SDL
  vmaDestroyAllocator(vma_alloc);
  vkDestroyDevice(device, vk_alloc);
  *d = (demo){0};

  igDestroyContext(d->ig_ctx);

  OptickAPI_PopEvent(optick_e);
}

void demo_upload_const_buffer(demo *d, const gpuconstbuffer *buffer) {
  uint32_t buffer_idx = d->const_buffer_upload_count;
  assert(d->const_buffer_upload_count + 1 < CONST_BUFFER_UPLOAD_QUEUE_SIZE);
  d->const_buffer_upload_queue[buffer_idx] = *buffer;
  d->const_buffer_upload_count++;
}

void demo_upload_mesh(demo *d, const gpumesh *mesh) {
  uint32_t mesh_idx = d->mesh_upload_count;
  assert(d->mesh_upload_count + 1 < MESH_UPLOAD_QUEUE_SIZE);
  d->mesh_upload_queue[mesh_idx] = *mesh;
  d->mesh_upload_count++;
}

void demo_upload_texture(demo *d, const gputexture *tex) {
  uint32_t tex_idx = d->texture_upload_count;
  assert(d->texture_upload_count + 1 < TEXTURE_UPLOAD_QUEUE_SIZE);
  d->texture_upload_queue[tex_idx] = *tex;
  d->texture_upload_count++;
}

void demo_upload_scene(demo *d, const scene *s) {
  for (uint32_t i = 0; i < s->mesh_count; ++i) {
    demo_upload_mesh(d, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; ++i) {
    demo_upload_texture(d, &s->textures[i]);
  }
}

void demo_render_frame(demo *d, const float4x4 *vp, const float4x4 *sky_vp) {
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

      // Render main geometry pass
      {
        VkFramebuffer framebuffer = d->swapchain_framebuffers[frame_idx];

        // Main Geometry Pass
        {
          const float width = d->swap_width;
          const float height = d->swap_height;

          // Set Render Pass
          {
            VkClearValue clear_values[2] = {
                {.color = {.float32 = {0, 1, 1, 1}}},
                {.depthStencil = {.depth = 0.0f, .stencil = 0.0f}},
            };

            VkRenderPassBeginInfo pass_info = {0};
            pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            pass_info.renderPass = d->render_pass;
            pass_info.framebuffer = framebuffer;
            pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
            pass_info.clearValueCount = 2;
            pass_info.pClearValues = clear_values;

            vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                                 VK_SUBPASS_CONTENTS_INLINE);
          }

          VkViewport viewport = {0, height, width, -height, 0, 1};
          VkRect2D scissor = {{0, 0}, {width, height}};
          vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
          vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

          // Draw Fullscreen Fractal
          // vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
          //                    d->fractal_pipeline);
          // vkCmdDraw(graphics_buffer, 3, 1, 0, 0);

          // Draw Scene
          {
            OPTICK_C_GPU_PUSH(optick_gpu_e, "Duck",
                              OptickAPI_Category_GPU_Scene);
            // HACK: Known desired permutations
            uint32_t perm = GLTF_PERM_NONE;
            VkPipelineLayout pipe_layout = d->gltf_pipe_layout;
            VkPipeline pipe = d->gltf_pipeline->pipelines[perm];

            vkCmdBindPipeline(graphics_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipe);

            demo_render_scene(d->scene, graphics_buffer, pipe_layout,
                              d->gltf_view_descriptor_sets[frame_idx],
                              d->gltf_object_descriptor_sets[frame_idx],
                              d->gltf_material_descriptor_sets[frame_idx], vp,
                              d);
            OptickAPI_PopGPUEvent(optick_gpu_e);
          }

          // Draw Skydome
          {
            OPTICK_C_GPU_PUSH(optick_gpu_e, "Skydome",
                              OptickAPI_Category_GPU_Scene);
            // Another hack to fiddle with the matrix we send to the shader for
            // the skydome
            SkyPushConstants sky_consts = {.vp = *sky_vp};
            vkCmdPushConstants(graphics_buffer, d->skydome_pipe_layout,
                               VK_SHADER_STAGE_ALL_GRAPHICS, 0,
                               sizeof(SkyPushConstants),
                               (const void *)&sky_consts);

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

        // ImGui Render Pass
        {
          // ImGui Internal Render
          {
            OPTICK_C_PUSH(optick_e, "ImGui Internal", OptickAPI_Category_UI);
            igRender();
            OptickAPI_PopEvent(optick_e);
          }

          // (Re)Create and upload ImGui geometry buffer
          {
            OPTICK_C_PUSH(optick_e, "ImGui CPU", OptickAPI_Category_UI);

            // If imgui_gpu is empty, this is still safe to call
            destroy_gpumesh(device, d->vma_alloc, &d->imgui_gpu[frame_idx]);

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

                create_gpumesh(device, d->vma_alloc, &imgui_cpu,
                               &d->imgui_gpu[frame_idx]);

                // Copy to gpu
                {
                  VkBufferCopy region = {
                      .srcOffset = 0,
                      .dstOffset = 0,
                      .size = d->imgui_gpu[frame_idx].size,
                  };
                  vkCmdCopyBuffer(
                      graphics_buffer, d->imgui_gpu[frame_idx].host.buffer,
                      d->imgui_gpu[frame_idx].gpu.buffer, 1, &region);
                }
              }
            }

            OptickAPI_PopEvent(optick_e);
          };

          // Record ImGui render commands
          {
            OPTICK_C_GPU_PUSH(optick_gpu_e, "ImGui", OptickAPI_Category_GPU_UI);

            const float width = d->ig_io->DisplaySize.x;
            const float height = d->ig_io->DisplaySize.y;

            // TODO: Can't re-use same framebuffer since the imgui pipeline
            // doesn't need the depth buffer
            /*
            // Set Render Pass
            {
              VkFramebuffer framebuffer = d->swapchain_framebuffers[frame_idx];

              VkClearValue clear_values[1] = {
                  {.color = {.float32 = {0, 1, 1, 1}}},
              };

              VkRenderPassBeginInfo pass_info = {0};
              pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
              pass_info.renderPass = d->imgui_pass;
              pass_info.framebuffer = framebuffer;
              pass_info.renderArea = (VkRect2D){{0, 0}, {width, height}};
              pass_info.clearValueCount = 1;
              pass_info.pClearValues = clear_values;

              vkCmdBeginRenderPass(graphics_buffer, &pass_info,
                                   VK_SUBPASS_CONTENTS_INLINE);
            }

            // Draw ImGui
            {
              vkCmdBindPipeline(graphics_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                d->imgui_pipeline);

              VkViewport viewport = {0, height, width, -height, 0, 1};
              VkRect2D scissor = {{0, 0}, {width, height}};
              vkCmdSetViewport(graphics_buffer, 0, 1, &viewport);
              vkCmdSetScissor(graphics_buffer, 0, 1, &scissor);

              // vkCmdPushConstants(graphics_buffer, d->simple_pipe_layout,
              //                   VK_SHADER_STAGE_ALL_GRAPHICS, 0,
              //                   sizeof(ImGuiPushConstants),
              //                   (const void *)&d->push_constants);
            }

            vkCmdEndRenderPass(graphics_buffer);
            */

            OptickAPI_PopGPUEvent(optick_gpu_e);
          }
        }
      }

      OptickAPI_SetGpuContext(optick_prev_gpu_ctx);

      err = vkEndCommandBuffer(graphics_buffer);
      assert(err == VK_SUCCESS);
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

bool demo_screenshot(demo *d, allocator std_alloc, uint8_t **screenshot_bytes,
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