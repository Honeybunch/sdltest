#include "mesh.h"

#include "mesh_frag.h"
#include "mesh_vert.h"
#include "shadercommon.h"

#include <assert.h>

#include "volk.h"

uint32_t create_mesh_pipeline(VkDevice device, VkPipelineCache cache,
                              VkRenderPass pass, uint32_t w, uint32_t h,
                              VkPipelineLayout *layout, VkPipeline *pipe) {
  VkResult err = VK_SUCCESS;
  return err;
}
