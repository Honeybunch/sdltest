#include "cube.h"

#include <string.h>

size_t cube_alloc_size() {
  return sizeof(cpumesh_buffer) + cube_index_size + cube_geom_size;
}

void create_cube(cpumesh_buffer *cube) {
  size_t offset = sizeof(cpumesh_buffer);
  cube->indices = (uint16_t *)((uint8_t *)cube) + offset;
  offset += cube_index_size;
  cube->vertices = ((uint8_t *)cube) + offset;

  uint8_t *pos = ((uint8_t *)cube) + offset;
  offset += sizeof(cube_positions);
  uint8_t *col = ((uint8_t *)cube) + offset;
  offset += sizeof(cube_colors);
  uint8_t *norm = ((uint8_t *)cube) + offset;

  cube->index_size = cube_index_size;
  cube->geom_size = cube_geom_size;
  cube->index_count = cube_index_count;
  cube->vertex_count = cube_vertex_count;

  memcpy_s((void *)cube->indices, cube_index_size, cube_indices,
           cube_index_size);
  memcpy_s(pos, sizeof(cube_positions), cube_positions, sizeof(cube_positions));
  memcpy_s(col, sizeof(cube_colors), cube_colors, sizeof(cube_colors));
  memcpy_s(norm, sizeof(cube_normals), cube_normals, sizeof(cube_normals));
}