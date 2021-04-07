#pragma once

typedef struct cpumesh_buffer cpumesh_buffer;

// Assumes space for entire cube has been allocated
size_t cube_alloc_size();
void create_cube(cpumesh_buffer *cube);