#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <mimalloc.h>
#include <optick_capi.h>
#include <stdbool.h>
#include <stdint.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include "allocator.h"
#include "camera.h"
#include "config.h"

#include "demo.h"
#include "shadercommon.h"
#include "simd.h"

#define MAX_LAYER_COUNT 16
#define MAX_EXT_COUNT 16

#ifndef FINAL
#define VALIDATION
#endif

#define WIDTH 1600
#define HEIGHT 900

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
    // Optick will consider the caputre un-dumped and will attempt to call
    // this again
    if (g_taking_screenshot) {
      return false;
    }
    OptickAPI_AttachSummary("Engine", "SDLTest");
    OptickAPI_AttachSummary("Author", "Honeybunch");
    OptickAPI_AttachSummary("Game Version", HB_GAME_VERSION);
    OptickAPI_AttachSummary("Engine Version", HB_ENGINE_VERSION);
    // OptickAPI_AttachSummary("Configuration", HB_CONFIG);
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
  static const size_t arena_alloc_size = 1024 * 1024 * 512; // 512 MB
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
    {
      assert(ext_count + 1 < MAX_EXT_COUNT);
      ext_names[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
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

  CommonCameraData camera_data = {
      0,
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
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = false;
        break;
      }
      demo_process_event(&d, &e);
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

    // Update view camera constant buffer
    {
      OPTICK_C_PUSH(update_camera_event, "Update Light Const Buffer",
                    OptickAPI_Category_Rendering);
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

      OptickAPI_PopEvent(update_camera_event);
    }

    // Update view light constant buffer
    {
      OPTICK_C_PUSH(update_light_event, "Update Light Const Buffer",
                    OptickAPI_Category_Rendering);

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

      OptickAPI_PopEvent(update_light_event);
    }

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