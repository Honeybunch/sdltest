#pragma once

#include "simd.h"

#define MAX_SUBMATERIALS 8

typedef struct cputexture cputexture;

typedef struct materialoptions {
} materialoptions;

typedef struct submaterial {

} submaterial;

typedef struct material {
  uint32_t submaterial_count;
  submaterial submaterials[MAX_SUBMATERIALS];
} material;

typedef struct unlitmaterial {
  material mat;
} unlitmaterial;

typedef struct phongblinnmaterial {
  material mat;
} phongblinnmaterial;

typedef struct metalroughmaterial {
  float4 albedo;
  cputexture *albedo_map;

  cputexture *normal_map;

  float metallic;
  cputexture *metallic_map;

  float roughness;
  cputexture *roughness_map;

  material mat;
} metalroughmaterial;