#pragma once
#include "cpumesh.h"

size_t plane_alloc_size(uint32_t subdiv);
void create_plane(uint32_t subdiv, cpumesh_buffer *plane);