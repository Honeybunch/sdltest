#include "fractal.h"

#include "fractal_frag.h"
#include "fractal_vert.h"
#include "shadercommon.h"

#include <assert.h>

#include "volk.h"

uint32_t create_fractal_pipeline(VkDevice device, VkPipelineCache cache,
                                 VkRenderPass pass, uint32_t w, uint32_t h,
                                 VkPipelineLayout layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;

  // Create Fullscreen Graphics Pipeline
  VkPipeline fractal_pipeline = VK_NULL_HANDLE;
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {0};
      create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      create_info.codeSize = sizeof(fractal_vert);
      create_info.pCode = (const uint32_t *)fractal_vert;
      err = vkCreateShaderModule(device, &create_info, NULL, &vert_mod);
      assert(err == VK_SUCCESS);

      create_info.codeSize = sizeof(fractal_frag);
      create_info.pCode = (const uint32_t *)fractal_frag;
      err = vkCreateShaderModule(device, &create_info, NULL, &frag_mod);
      assert(err == VK_SUCCESS);
    }

    VkPipelineShaderStageCreateInfo vert_stage = {0};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_mod;
    vert_stage.pName = "vert";
    VkPipelineShaderStageCreateInfo frag_stage = {0};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_mod;
    frag_stage.pName = "frag";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    VkPipelineVertexInputStateCreateInfo vert_input_state = {0};
    vert_input_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {0};
    input_assembly_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly_state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {0, h, w, -(float)h, 0, 1};
    VkRect2D scissor = {{0, 0}, {w, h}};

    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster_state = {0};
    raster_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_state.polygonMode = VK_POLYGON_MODE_FILL;
    raster_state.cullMode = VK_CULL_MODE_BACK_BIT;
    raster_state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_state.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample_state = {0};
    multisample_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    depth_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState attachment_state = {0};
    attachment_state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &attachment_state;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        sizeof(dyn_states) / sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    create_info.stageCount =
        sizeof(shader_stages) / sizeof(VkPipelineShaderStageCreateInfo);
    create_info.pStages = shader_stages;
    create_info.pVertexInputState = &vert_input_state;
    create_info.pInputAssemblyState = &input_assembly_state;
    create_info.pViewportState = &viewport_state;
    create_info.pRasterizationState = &raster_state;
    create_info.pMultisampleState = &multisample_state;
    create_info.pDepthStencilState = &depth_state;
    create_info.pColorBlendState = &color_blend_state;
    create_info.pDynamicState = &dynamic_state;
    create_info.layout = layout;
    create_info.renderPass = pass;
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, NULL,
                                    &fractal_pipeline);
    assert(err == VK_SUCCESS);

    // Can destroy shaders
    vkDestroyShaderModule(device, vert_mod, NULL);
    vkDestroyShaderModule(device, frag_mod, NULL);
  }

  *pipe = fractal_pipeline;

  return err;
}