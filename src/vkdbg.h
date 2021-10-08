#pragma once

#include "simd.h"

typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;

void queue_begin_label(VkQueue queue, const char *label, float4 color);
void queue_end_label(VkQueue queue);

void cmd_begin_label(VkCommandBuffer cmd, const char *label, float4 color);
void cmd_end_label(VkCommandBuffer cmd);