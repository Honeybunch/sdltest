#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <mimalloc.h>

#include <stdbool.h>
#include <stdint.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include "allocator.h"
#include "camera.h"
#include "config.h"

#include "demo.h"
#include "profiling.h"
#include "shadercommon.h"
#include "simd.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifndef FINAL
#define VALIDATION
#endif

#define WIDTH 1600
#define HEIGHT 900

#define _PI 3.14159265358f
#define _2PI _PI * 2.0f

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

#ifdef VALIDATION
#ifndef __ANDROID__
static VkBool32
vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                  void *pUserData) {
  (void)messageTypes;
  (void)pUserData;

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    SDL_LogVerbose(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity &
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  } else {
    SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "%s", pCallbackData->pMessage);
  }

  return false;
}
#endif
#endif

static void *vk_alloc_fn(void *pUserData, size_t size, size_t alignment,
                         VkSystemAllocationScope scope) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  (void)scope;
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  void *ptr = mi_heap_malloc_aligned(heap, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void *vk_realloc_fn(void *pUserData, void *pOriginal, size_t size,
                           size_t alignment, VkSystemAllocationScope scope) {
  (void)scope;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  mi_heap_t *heap = (mi_heap_t *)pUserData;
  TracyCFreeN(pOriginal, "Vulkan");
  void *ptr = mi_heap_realloc_aligned(heap, pOriginal, size, alignment);
  TracyCAllocN(ptr, size, "Vulkan");
  TracyCZoneEnd(ctx);
  return ptr;
}

static void vk_free_fn(void *pUserData, void *pMemory) {
  (void)pUserData;
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(pMemory, "Vulkan");
  mi_free(pMemory);
  TracyCZoneEnd(ctx);
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

int32_t SDL_main(int32_t argc, char *argv[]) {
  static const float qtr_pi = 0.7853981625f;

  {
    const char *app_info = HB_APP_INFO_STR;
    size_t app_info_len = strlen(app_info);
    TracyCAppInfo(app_info, app_info_len);
  }

  // Create Temporary Arena Allocator
  static const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
  arena_allocator arena = create_arena_allocator(arena_alloc_size);

  mi_heap_t *vk_heap = mi_heap_new();
  VkAllocationCallbacks vk_alloc = create_vulkan_allocator(vk_heap);
  standard_allocator std_alloc = create_standard_allocator("std_alloc");

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
      .fov = qtr_pi * 2,
      .near = 0.01f,
      .far = 100.0f,
  };

  VkResult err = volkInitialize();
  assert(err == VK_SUCCESS);

  {
    int32_t res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    assert(res == 0);

    int32_t flags = IMG_INIT_PNG;
    res = IMG_Init(flags);
    assert(res & IMG_INIT_PNG);

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
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
        VkLayerProperties *instance_layers = hb_alloc_nm_tp(
            arena.alloc, instance_layer_count, VkLayerProperties);
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
#ifndef __ANDROID__
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
#endif
#endif

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "SDL Test";
    app_info.applicationVersion = VK_MAKE_VERSION(
        HB_GAME_VERSION_MAJOR, HB_GAME_VERSION_MINOR, HB_GAME_VERSION_PATCH);
    app_info.pEngineName = HB_ENGINE_NAME;
    app_info.engineVersion =
        VK_MAKE_VERSION(HB_ENGINE_VERSION_MAJOR, HB_ENGINE_VERSION_MINOR,
                        HB_ENGINE_VERSION_PATCH);
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

#ifdef VALIDATION
  VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
// Load debug callback
#ifndef __ANDROID__
  {
    VkDebugUtilsMessengerCreateInfoEXT ext_info = {0};
    ext_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ext_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT;
    ext_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT;
    ext_info.pfnUserCallback = vk_debug_callback;
    err = vkCreateDebugUtilsMessengerEXT(instance, &ext_info, vk_alloc_ptr,
                                         &debug_utils_messenger);
    assert(err == VK_SUCCESS);
  }
#endif
#endif

  demo d = {0};
  bool success = demo_init(window, instance, std_alloc.alloc, arena.alloc,
                           vk_alloc_ptr, &d);
  assert(success);

  SkyData sky_data = {
      .sun_dir = {0, -1, 0},
      .turbidity = 3,
      .albedo = 1,
  };

  CommonCameraData camera_data = {
      0,
  };

  // Main loop
  bool running = true;

  bool showImGui = true;
  bool showSkyWindow = true;
  bool showDemoWindow = false;
  bool showMetricsWindow = false;

  uint64_t time = 0;
  uint64_t last_time = SDL_GetPerformanceCounter();
  uint64_t delta_time = 0;
  float time_seconds = 0.0f;
  float delta_time_ms = 0.0f;
  float delta_time_seconds = 0.0f;

  // Controlled by ImGui and fed to the sky system
  float timeOfDay = _PI;
  float sunY = cosf(_PI + timeOfDay);
  float sunX = sinf(_PI + timeOfDay);

  while (running) {
    TracyCFrameMarkStart("Frame");
    TracyCZoneN(trcy_ctx, "Frame", true);
    TracyCZoneColor(trcy_ctx, TracyCategoryColorCore);

    // Use SDL High Performance Counter to get timing info
    time = SDL_GetPerformanceCounter();
    delta_time = time - last_time;
    delta_time_seconds =
        (float)((double)delta_time / (double)SDL_GetPerformanceFrequency());
    time_seconds =
        (float)((double)time / (double)SDL_GetPerformanceFrequency());
    delta_time_ms = delta_time_ms * 1000.0f;
    last_time = time;

    // TODO: Handle events more gracefully
    // Mutliple events (or none) could happen in one frame but we only process
    // the latest one

    // while (SDL_PollEvent(&e))
    {
      TracyCZoneN(ctx, "Handle Events", true);
      TracyCZoneColor(ctx, TracyCategoryColorInput);

      SDL_Event e = {0};
      {
        TracyCZoneN(sdl_ctx, "SDL_PollEvent", true);
        SDL_PollEvent(&e);
        TracyCZoneEnd(sdl_ctx);
      }
      if (e.type == SDL_QUIT) {
        running = false;
        TracyCZoneEnd(ctx);
        break;
      }
      demo_process_event(&d, &e);

      editor_camera_control(delta_time_seconds, &e, &controller, &main_cam);

      if (e.type == SDL_KEYDOWN) {
        const SDL_Keysym *keysym = &e.key.keysym;
        SDL_Scancode scancode = keysym->scancode;
        if (scancode == SDL_SCANCODE_GRAVE) {
          showImGui = !showImGui;
        }
      }

      TracyCZoneEnd(ctx);
    }

    ImVec2 display_size;
    display_size.x = WIDTH;
    display_size.y = HEIGHT;
    d.ig_io->DisplaySize = display_size;
    d.ig_io->DeltaTime = delta_time_seconds;
    igNewFrame();

    // ImGui Test

    if (showImGui) {
      TracyCZoneN(ctx, "UI Test", true);
      TracyCZoneColor(ctx, TracyCategoryColorUI);

      if (igBeginMainMenuBar()) {
        if (igBeginMenu("Sky", true)) {
          showSkyWindow = !showSkyWindow;
          igEndMenu();
        }
        if (igBeginMenu("Metrics", true)) {
          showMetricsWindow = !showMetricsWindow;
          igEndMenu();
        }
        if (igBeginMenu("Demo", true)) {
          showDemoWindow = !showDemoWindow;
          igEndMenu();
        }
        igEndMainMenuBar();
      }

      if (showSkyWindow && igBegin("Sky Control", &showSkyWindow, 0)) {
        if (igSliderFloat("Time of Day", &timeOfDay, 0.0f, _2PI, "%.3f", 0)) {
          sunY = cosf(_PI + timeOfDay);
          sunX = sinf(_PI + timeOfDay);
        }
        igText("Sun Y: %.3f", sunY);
        igText("Sun X: %.3f", sunX);
        igEnd();
      }

      if (showDemoWindow) {
        igShowDemoWindow(&showDemoWindow);
      }
      if (showMetricsWindow) {
        igShowMetricsWindow(&showMetricsWindow);
      }

      TracyCZoneEnd(ctx);
    }

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

    // Change sun position
    sky_data.sun_dir = (float3){sunX, sunY, 0};

    // Update view camera constant buffer
    {
      TracyCZoneN(trcy_camera_ctx, "Update Camera Const Buffer", true);
      camera_data.vp = vp;
      // TODO: camera_data.inv_vp = inv_vp;
      camera_data.view_pos = main_cam.transform.position;

      VmaAllocator vma_alloc = d.vma_alloc;
      VkBuffer camera_host = d.camera_const_buffer.host.buffer;
      VmaAllocation camera_host_alloc = d.camera_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, camera_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }

      memcpy(data, &camera_data, sizeof(CommonCameraData));
      vmaUnmapMemory(vma_alloc, camera_host_alloc);

      demo_upload_const_buffer(&d, &d.camera_const_buffer);

      TracyCZoneEnd(trcy_camera_ctx);
    }

    // Update view light constant buffer
    {
      TracyCZoneN(trcy_light_ctx, "Update Light Const Buffer", true);

      CommonLightData light_data = {
          .light_dir = -sky_data.sun_dir,
      };

      VmaAllocator vma_alloc = d.vma_alloc;
      VkBuffer light_host = d.light_const_buffer.host.buffer;
      VmaAllocation light_host_alloc = d.light_const_buffer.host.alloc;

      uint8_t *data = NULL;
      err = vmaMapMemory(vma_alloc, light_host_alloc, (void **)&data);
      if (err != VK_SUCCESS) {
        assert(0);
        return false;
      }
      // HACK: just pluck the light direction from the push constants for now
      memcpy(data, &light_data, sizeof(CommonLightData));
      vmaUnmapMemory(vma_alloc, light_host_alloc);

      demo_upload_const_buffer(&d, &d.light_const_buffer);

      TracyCZoneEnd(trcy_light_ctx);
    }

    // Update sky constant buffer
    {
      TracyCZoneN(trcy_sky_ctx, "Update Sky", true);

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
      TracyCZoneEnd(trcy_sky_ctx);
    }

    demo_render_frame(&d, &vp, &sky_vp);

    // Reset the arena allocator
    reset_arena(arena, true); // Just allow it to grow for now

    TracyCZoneEnd(trcy_ctx);
    TracyCFrameMarkEnd("Frame");
  }

  SDL_DestroyWindow(window);
  window = NULL;

  IMG_Quit();
  SDL_Quit();

  demo_destroy(&d);

#ifdef VALIDATION
#ifndef __ANDROID__
  vkDestroyDebugUtilsMessengerEXT(instance, debug_utils_messenger,
                                  vk_alloc_ptr);
  debug_utils_messenger = VK_NULL_HANDLE;
#endif
#endif

  vkDestroyInstance(instance, vk_alloc_ptr);
  instance = VK_NULL_HANDLE;

  destroy_arena_allocator(arena);
  destroy_standard_allocator(std_alloc);
  mi_heap_delete(vk_heap);

  return 0;
}