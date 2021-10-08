#include "vkdbg.h"

#include <volk.h>

void queue_begin_label(VkQueue queue, const char *label, float4 color) {
  if (vkQueueBeginDebugUtilsLabelEXT) {
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {color[0], color[1], color[2], color[3]},
    };
    vkQueueBeginDebugUtilsLabelEXT(queue, &info);
  }
}

void queue_end_label(VkQueue queue) {
  if (vkQueueEndDebugUtilsLabelEXT) {
    vkQueueEndDebugUtilsLabelEXT(queue);
  }
}

void cmd_begin_label(VkCommandBuffer cmd, const char *label, float4 color) {
  if (vkCmdBeginDebugUtilsLabelEXT) {
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {color[0], color[1], color[2], color[3]},
    };
    vkCmdBeginDebugUtilsLabelEXT(cmd, &info);
  }
}

void cmd_end_label(VkCommandBuffer cmd) {
  if (vkCmdBeginDebugUtilsLabelEXT) {
    vkCmdEndDebugUtilsLabelEXT(cmd);
  }
}