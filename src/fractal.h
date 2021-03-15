#pragma once

#include <stdint.h>

typedef struct VkDevice_T *VkDevice;
typedef struct VkPipelineCache_T *VkPipelineCache;
typedef struct VkRenderPass_T *VkRenderPass;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;

uint32_t create_fractal_pipeline(VkDevice device, VkPipelineCache cache,
                                 VkRenderPass pass, uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe);
