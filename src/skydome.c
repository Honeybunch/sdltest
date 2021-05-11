#include "skydome.h"

#include "allocator.h"
#include "cpuresources.h"

#include <assert.h>
#include <string.h>

static const uint16_t skydome_indices[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 13, 15, 16,
    13, 16, 14, 14, 16, 9,  10, 17, 18, 10, 18, 11, 11, 18, 6,  7,  19, 20,
    7,  20, 8,  8,  20, 21, 4,  22, 23, 4,  23, 5,  5,  23, 0,  1,  24, 25,
    1,  25, 2,  2,  25, 12, 26, 12, 14, 26, 14, 27, 27, 14, 9,  28, 9,  11,
    28, 11, 29, 29, 11, 6,  30, 6,  8,  30, 8,  31, 31, 8,  21, 32, 3,  5,
    32, 5,  33, 33, 5,  0,  34, 0,  2,  34, 2,  35, 35, 2,  12, 27, 9,  28,
    29, 6,  30, 31, 21, 36, 33, 0,  34, 35, 12, 26,
};

static const float3 skydome_positions[] = {
    {0.27639f, -0.44722f, 0.85065f},   {0.16246f, -0.85065f, 0.49999f},
    {0.68819f, -0.52574f, 0.50000f},   {-0.72361f, -0.44722f, 0.52573f},
    {-0.42532f, -0.85065f, 0.30901f},  {-0.26287f, -0.52574f, 0.80901f},
    {-0.72361f, -0.44722f, -0.52573f}, {-0.42532f, -0.85065f, -0.30901f},
    {-0.85065f, -0.52574f, 0.00000f},  {0.27639f, -0.44722f, -0.85065f},
    {0.16246f, -0.85065f, -0.49999f},  {-0.26287f, -0.52574f, -0.80901f},
    {0.89443f, -0.44722f, 0.00000f},   {0.52573f, -0.85065f, 0.00000f},
    {0.68819f, -0.52574f, -0.50000f},  {0.00000f, -1.00000f, 0.00000f},
    {0.16246f, -0.85065f, -0.49999f},  {0.00000f, -1.00000f, 0.00000f},
    {-0.42532f, -0.85065f, -0.30901f}, {0.00000f, -1.00000f, 0.00000f},
    {-0.42532f, -0.85065f, 0.30901f},  {-0.72361f, -0.44722f, 0.52573f},
    {0.00000f, -1.00000f, 0.00000f},   {0.16246f, -0.85065f, 0.49999f},
    {0.00000f, -1.00000f, 0.00000f},   {0.52573f, -0.85065f, 0.00000f},
    {0.95106f, -0.00000f, -0.30901f},  {0.58779f, -0.00000f, -0.80902f},
    {0.00000f, -0.00000f, -1.00000f},  {-0.58779f, -0.00000f, -0.80902f},
    {-0.95106f, -0.00000f, -0.30901f}, {-0.95106f, -0.00000f, 0.30901f},
    {-0.58779f, -0.00000f, 0.80902f},  {0.00000f, -0.00000f, 1.00000f},
    {0.58779f, -0.00000f, 0.80902f},   {0.95106f, -0.00000f, 0.30901f},
    {-0.58779f, -0.00000f, 0.80902f},
};

static const float3 skydome_normals[] = {
    {-0.27640f, -0.44720f, -0.85060f}, {-0.16250f, -0.85060f, -0.50000f},
    {-0.68820f, -0.52570f, -0.50000f}, {0.72360f, -0.44720f, -0.52570f},
    {0.42530f, -0.85060f, -0.30900f},  {0.26290f, -0.52570f, -0.80900f},
    {0.72360f, -0.44720f, 0.52570f},   {0.42530f, -0.85060f, 0.30900f},
    {0.85060f, -0.52570f, 0.00000f},   {-0.27640f, -0.44720f, 0.85060f},
    {-0.16250f, -0.85060f, 0.50000f},  {0.26290f, -0.52570f, 0.80900f},
    {-0.89440f, -0.44720f, 0.00000f},  {-0.52570f, -0.85060f, 0.00000f},
    {-0.68820f, -0.52570f, 0.50000f},  {0.00000f, -1.00000f, 0.00000f},
    {-0.16250f, -0.85060f, 0.50000f},  {0.00000f, -1.00000f, 0.00000f},
    {0.42530f, -0.85060f, 0.30900f},   {0.00000f, -1.00000f, 0.00000f},
    {0.42530f, -0.85060f, -0.30900f},  {0.72360f, -0.44720f, -0.52570f},
    {0.00000f, -1.00000f, 0.00000f},   {-0.16250f, -0.85060f, -0.50000f},
    {0.00000f, -1.00000f, 0.00000f},   {-0.52570f, -0.85060f, 0.00000f},
    {-0.92250f, -0.22170f, 0.31600f},  {-0.58560f, -0.22170f, 0.77970f},
    {0.01540f, -0.22170f, 0.97500f},   {0.56060f, -0.22170f, 0.79780f},
    {0.93200f, -0.22170f, 0.28660f},   {0.93200f, -0.22170f, -0.28660f},
    {0.56060f, -0.22170f, -0.79780f},  {0.01540f, -0.22170f, -0.97500f},
    {-0.58560f, -0.22170f, -0.77970f}, {-0.92250f, -0.22170f, -0.31600f},
    {0.56060f, -0.22170f, -0.79780f},
};

static const float2 skydome_uvs[] = {
    {0.81818f, 0.31492f}, {0.77273f, 0.39365f}, {0.72727f, 0.31492f},
    {1.00000f, 0.31492f}, {0.95455f, 0.39365f}, {0.90909f, 0.31492f},
    {0.27273f, 0.31492f}, {0.22727f, 0.39365f}, {0.18182f, 0.31492f},
    {0.45454f, 0.31492f}, {0.40909f, 0.39365f}, {0.36364f, 0.31492f},
    {0.63636f, 0.31492f}, {0.59091f, 0.39365f}, {0.54545f, 0.31492f},
    {0.54545f, 0.47238f}, {0.50000f, 0.39365f}, {0.36364f, 0.47238f},
    {0.31818f, 0.39365f}, {0.18182f, 0.47238f}, {0.13636f, 0.39365f},
    {0.09091f, 0.31492f}, {0.90909f, 0.47238f}, {0.86364f, 0.39365f},
    {0.72727f, 0.47238f}, {0.68182f, 0.39365f}, {0.59091f, 0.23619f},
    {0.50000f, 0.23619f}, {0.40909f, 0.23619f}, {0.31818f, 0.23619f},
    {0.22727f, 0.23619f}, {0.13636f, 0.23619f}, {0.95455f, 0.23619f},
    {0.86364f, 0.23619f}, {0.77273f, 0.23619f}, {0.68182f, 0.23619f},
    {0.04545f, 0.23619f},
};

static const size_t skydome_index_size = sizeof(skydome_indices);
static const size_t skydome_geom_size =
    sizeof(skydome_positions) + sizeof(skydome_normals) + sizeof(skydome_uvs);
static const size_t skydome_index_count =
    sizeof(skydome_indices) / sizeof(uint16_t);
static const size_t skydome_vertex_count =
    sizeof(skydome_positions) / sizeof(float3);

cpumesh *create_skydome(allocator *a) {
  size_t size = sizeof(cpumesh) + skydome_geom_size + skydome_index_size;
  cpumesh *skydome = (cpumesh *)a->alloc(a->user_data, size);
  assert(skydome);

  size_t offset = sizeof(cpumesh);
  skydome->indices = (uint16_t *)((uint8_t *)skydome + offset);
  offset += skydome_index_size;
  skydome->vertices = ((uint8_t *)skydome) + offset;

  uint8_t *pos = ((uint8_t *)skydome) + offset;
  offset += sizeof(skydome_positions);
  uint8_t *norm = ((uint8_t *)skydome) + offset;
  offset += sizeof(skydome_normals);
  uint8_t *uv = ((uint8_t *)skydome) + offset;

  skydome->index_size = skydome_index_size;
  skydome->geom_size = skydome_geom_size;
  skydome->index_count = skydome_index_count;
  skydome->vertex_count = skydome_vertex_count;

  memcpy_s((void *)skydome->indices, skydome_index_size, skydome_indices,
           skydome_index_size);
  memcpy_s(pos, sizeof(skydome_positions), skydome_positions,
           sizeof(skydome_positions));
  memcpy_s(norm, sizeof(skydome_normals), skydome_normals,
           sizeof(skydome_normals));
  memcpy_s(uv, sizeof(skydome_uvs), skydome_uvs, sizeof(skydome_uvs));

  return skydome;
}