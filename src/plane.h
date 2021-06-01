#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct cpumesh cpumesh;

size_t plane_alloc_size(uint32_t subdiv);
void create_plane(uint32_t subdiv, cpumesh *plane);