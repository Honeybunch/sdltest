#define HB_NO_PROFILING

#ifndef HB_NO_PROFILING

#ifdef HB_PROFILER_OPTICK
#include <optick_capi.h>

#define HB_PROF_STATE_TYPE OptickAPI_State
#define HB_PROF_STATE_STOPCAPTURE OptickAPI_State_StopCapture
#define HB_PROF_STATE_DUMPCAPTURE OptickAPI_State_DumpCapture

#define HB_PROF_CATEGORY_NONE OptickAPI_Category_None
#define HB_PROF_CATEGORY_WAIT OptickAPI_Category_Wait
#define HB_PROF_CATEGORY_RENDERING OptickAPI_Category_Rendering
#define HB_PROF_CATEGORY_CAMERA OptickAPI_Category_Camera
#define HB_PROF_CATEGORY_UI OptickAPI_Category_UI

#define HB_PROF_CATEGORY_GPU_SCENE OptickAPI_Category_GPU_Scene
#define HB_PROF_CATEGORY_GPU_UI OptickAPI_Category_GPU_UI

#define HB_PROF_FILE_TYPE_IMAGE OptickAPI_File_Image

#define HB_PROF_SET_ALLOCATOR(alloc_fn, free_fn, thread_init_cb)               \
  OptickAPI_SetAllocator(alloc_fn, free_fn, thread_init_cb)
#define HB_PROF_REGISTER_THREAD(name, name_len)                                \
  OptickAPI_RegisterThread(name, name_len)
#define HB_PROF_SET_STATE_CHANGED_CALLBACK(cb)                                 \
  OptickAPI_SetStateChangedCallback(cb)
#define HB_PROF_SHUTDOWN() OptickAPI_Shutdown()

#define HB_VK_FN_STRUCT_TYPE OptickAPI_VulkanFunctions
#define HB_PROF_GPU_INIT(device, physical_device, queues, queue_families,      \
                         queue_count, vk_fns)                                  \
  OptickAPI_GPUInitVulkan(device, physical_device, queues, queue_families,     \
                          queue_count, vk_fns)
#define HB_PROF_GPU_SHUTDOWN() OptickAPI_GPUShutdown()

#define HB_PROF_START_CAPTURE() OptickAPI_StartCapture()
#define HB_PROF_NEXT_FRAME() OptickAPI_NextFrame()

#define HB_PROF_PUSH(event, name, category) OPTICK_C_PUSH(event, name, category)
#define HB_PROF_POP(event) OptickAPI_PopEvent(event)
#define HB_PROF_ATTACH_SUMMARY(key, value) OptickAPI_AttachSummary(key, value)
#define HB_PROF_ATTACH_FILE(type, name, data, size)                            \
  OptickAPI_AttachFile(type, name, data, size)

#define HB_PROF_GPU_CONTEXT_TYPE OptickAPI_GPUContext
#define HB_PROF_GPU_SET_CONTEXT(ctx) OptickAPI_SetGpuContext(ctx)
#define HB_PROF_GPU_PUSH(event, name, category)                                \
  OPTICK_C_GPU_PUSH(event, name, category)
#define HB_PROF_GPU_POP(event) OptickAPI_PopGPUEvent(event)
#define HB_PROF_GPU_FLIP(swapchain) OptickAPI_GPUFlip(swapchain)

#elif HB_PROFILER_TRACY
#define TRACY_ENABLE
#include "TracyC.h"

#else
#error Expected one profiler
#endif

#else
#define HB_PROF_STATE_TYPE int
#define HB_PROF_STATE_STOPCAPTURE 0
#define HB_PROF_STATE_DUMPCAPTURE 0

#define HB_PROF_CATEGORY_NONE 0
#define HB_PROF_CATEGORY_WAIT 0
#define HB_PROF_CATEGORY_RENDERING 0
#define HB_PROF_CATEGORY_CAMERA 0

#define HB_PROF_CATEGORY_GPU_SCENE 0

#define HB_PROF_FILE_TYPE_IMAGE 0

#define HB_PROF_SET_ALLOCATOR(alloc_fn, free_fn, thread_init_cb)
#define HB_PROF_REGISTER_THREAD(name, name_len)
#define HB_PROF_SET_STATE_CHANGED_CALLBACK(cb)
#define HB_PROF_SHUTDOWN()

#define HB_VK_FN_STRUCT_TYPE int
#define HB_PROF_GPU_INIT(device, physical_device, queues, queue_families,      \
                         queue_count, vk_fns)
#define HB_PROF_GPU_SHUTDOWN()

#define HB_PROF_START_CAPTURE()
#define HB_PROF_NEXT_FRAME()

#define HB_PROF_PUSH(event, name, category)
#define HB_PROF_POP(event)
#define HB_PROF_ATTACH_SUMMARY(key, value)
#define HB_PROF_ATTACH_FILE(type, name, data, size)

#define HB_PROF_GPU_CONTEXT_TYPE int
#define HB_PROF_GPU_SET_CONTEXT(ctx) 0
#define HB_PROF_GPU_PUSH(event, name, category)
#define HB_PROF_GPU_POP(event)
#define HB_PROF_GPU_FLIP(swapchain)
#endif