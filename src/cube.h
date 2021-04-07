#pragma once

typedef struct cpumesh cpumesh;

// Assumes space for entire cube has been allocated
size_t cube_alloc_size();
void create_cube(cpumesh *cube);