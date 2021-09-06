#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#undef VK_NO_PROTOTYPES

#include "allocator.h"
#include "gpuresources.h"
#include "scene.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#define FRAME_LATENCY 3
#define CONST_BUFFER_UPLOAD_QUEUE_SIZE 16
#define MESH_UPLOAD_QUEUE_SIZE 16
#define TEXTURE_UPLOAD_QUEUE_SIZE 16

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
  VkRenderPass imgui_pass;

  VkPipelineCache pipeline_cache;

  VkSampler sampler;

  VkDescriptorSetLayout skydome_layout;
  VkPipelineLayout skydome_pipe_layout;
  VkPipeline skydome_pipeline;
  gpuconstbuffer sky_const_buffer;

  gpuconstbuffer object_const_buffer;
  gpuconstbuffer camera_const_buffer;
  gpuconstbuffer light_const_buffer;

  VkDescriptorSetLayout gltf_material_set_layout;
  VkDescriptorSetLayout gltf_object_set_layout;
  VkDescriptorSetLayout gltf_view_set_layout;
  VkPipelineLayout gltf_pipe_layout;
  gpupipeline *gltf_pipeline;

  VkDescriptorSetLayout gltf_rt_layout;
  VkPipelineLayout gltf_rt_pipe_layout;
  gpupipeline *gltf_rt_pipeline;

  VkDescriptorSetLayout imgui_layout;
  VkPipelineLayout imgui_pipe_layout;
  VkPipeline imgui_pipeline;

  VkImage swapchain_images[FRAME_LATENCY];
  VkImageView swapchain_image_views[FRAME_LATENCY];
  VkFramebuffer main_pass_framebuffers[FRAME_LATENCY];
  VkFramebuffer ui_pass_framebuffers[FRAME_LATENCY];

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

  gpumesh skydome_gpu;

  uint8_t *imgui_mesh_data;
  gpumesh imgui_gpu[FRAME_LATENCY];

  scene *scene;

  gpuimage screenshot_image;
  VkFence screenshot_fence;

  VkDescriptorPool descriptor_pools[FRAME_LATENCY];
  VkDescriptorSet skydome_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet gltf_material_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet gltf_object_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet gltf_view_descriptor_sets[FRAME_LATENCY];
  VkDescriptorSet imgui_descriptor_sets[FRAME_LATENCY];

  uint32_t const_buffer_upload_count;
  gpuconstbuffer const_buffer_upload_queue[CONST_BUFFER_UPLOAD_QUEUE_SIZE];

  uint32_t mesh_upload_count;
  gpumesh mesh_upload_queue[MESH_UPLOAD_QUEUE_SIZE];

  uint32_t texture_upload_count;
  gputexture texture_upload_queue[TEXTURE_UPLOAD_QUEUE_SIZE];

  ImGuiContext *ig_ctx;
  ImGuiIO *ig_io;
} demo;

typedef struct SDL_Window SDL_Window;

bool demo_init(SDL_Window *window, VkInstance instance, allocator std_alloc,
               allocator tmp_alloc, const VkAllocationCallbacks *vk_alloc,
               demo *d);
void demo_destroy(demo *d);

void demo_upload_const_buffer(demo *d, const gpuconstbuffer *buffer);
void demo_upload_mesh(demo *d, const gpumesh *mesh);
void demo_upload_texture(demo *d, const gputexture *tex);
void demo_upload_scene(demo *d, const scene *s);

void demo_render_frame(demo *d, const float4x4 *vp, const float4x4 *sky_vp);

bool demo_screenshot(demo *d, allocator std_alloc, uint8_t **screenshot_bytes,
                     uint32_t *screenshot_size);