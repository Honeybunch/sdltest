#pragma once
#include "cpumesh.h"

static const uint16_t cube_indices[] = {
    0,  1,  2,  2,  3,  1,  // Front
    4,  5,  6,  6,  7,  5,  // Back
    8,  9,  10, 10, 11, 9,  // Right
    12, 13, 14, 14, 15, 13, // Left
    16, 17, 18, 18, 19, 17, // Top
    20, 21, 22, 22, 23, 21, // Bottom
};

static const float3 cube_positions[] = {
    // front
    {-1.0f, -1.0f, +1.0f}, // point blue
    {+1.0f, -1.0f, +1.0f}, // point magenta
    {-1.0f, +1.0f, +1.0f}, // point cyan
    {+1.0f, +1.0f, +1.0f}, // point white
    // back
    {+1.0f, -1.0f, -1.0f}, // point red
    {-1.0f, -1.0f, -1.0f}, // point black
    {+1.0f, +1.0f, -1.0f}, // point yellow
    {-1.0f, +1.0f, -1.0f}, // point green
    // right
    {+1.0f, -1.0f, +1.0f}, // point magenta
    {+1.0f, -1.0f, -1.0f}, // point red
    {+1.0f, +1.0f, +1.0f}, // point white
    {+1.0f, +1.0f, -1.0f}, // point yellow
    // left
    {-1.0f, -1.0f, -1.0f}, // point black
    {-1.0f, -1.0f, +1.0f}, // point blue
    {-1.0f, +1.0f, -1.0f}, // point green
    {-1.0f, +1.0f, +1.0f}, // point cyan
    // top
    {-1.0f, +1.0f, +1.0f}, // point cyan
    {+1.0f, +1.0f, +1.0f}, // point white
    {-1.0f, +1.0f, -1.0f}, // point green
    {+1.0f, +1.0f, -1.0f}, // point yellow
    // bottom
    {-1.0f, -1.0f, -1.0f}, // point black
    {+1.0f, -1.0f, -1.0f}, // point red
    {-1.0f, -1.0f, +1.0f}, // point blue
    {+1.0f, -1.0f, +1.0f}  // point magenta
};

static const float3 cube_colors[] = {
    // front
    {0.0f, 0.0f, 1.0f}, // blue
    {1.0f, 0.0f, 1.0f}, // magenta
    {0.0f, 1.0f, 1.0f}, // cyan
    {1.0f, 1.0f, 1.0f}, // white
    // back
    {1.0f, 0.0f, 0.0f}, // red
    {0.0f, 0.0f, 0.0f}, // black
    {1.0f, 1.0f, 0.0f}, // yellow
    {0.0f, 1.0f, 0.0f}, // green
    // right
    {1.0f, 0.0f, 1.0f}, // magenta
    {1.0f, 0.0f, 0.0f}, // red
    {1.0f, 1.0f, 1.0f}, // white
    {1.0f, 1.0f, 0.0f}, // yellow
    // left
    {0.0f, 0.0f, 0.0f}, // black
    {0.0f, 0.0f, 1.0f}, // blue
    {0.0f, 1.0f, 0.0f}, // green
    {0.0f, 1.0f, 1.0f}, // cyan
    // top
    {0.0f, 1.0f, 1.0f}, // cyan
    {1.0f, 1.0f, 1.0f}, // white
    {0.0f, 1.0f, 0.0f}, // green
    {1.0f, 1.0f, 0.0f}, // yellow
    // bottom
    {0.0f, 0.0f, 0.0f}, // black
    {1.0f, 0.0f, 0.0f}, // red
    {0.0f, 0.0f, 1.0f}, // blue
    {1.0f, 0.0f, 1.0f}  // magenta
};

static const float3 cube_normals[] = {
    // front
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    {+0.0f, +0.0f, +1.0f}, // forward
    // back
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    {+0.0f, +0.0f, -1.0f}, // backbard
    // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    {+1.0f, +0.0f, +0.0f}, // right
    // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    {-1.0f, +0.0f, +0.0f}, // left
    // top
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    {+0.0f, +1.0f, +0.0f}, // up
    // bottom
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}, // down
    {+0.0f, -1.0f, +0.0f}  // down
};

static cpumesh cube_cpu = {
    .index_size = sizeof(cube_indices),
    .geom_size =
        sizeof(cube_positions) + sizeof(cube_colors) + sizeof(cube_normals),
    .index_count = sizeof(cube_indices) / sizeof(uint16_t),
    .vertex_count = sizeof(cube_positions) / sizeof(float3),
    .indices = cube_indices,
    .positions = cube_positions,
    .colors = cube_colors,
    .normals = cube_normals,
};