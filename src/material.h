#pragma once

#include "simd.h"

typedef struct gputexture gputexture;
typedef struct gpupipeline gpupipeline;
typedef struct gpupass gpupass;
typedef struct gpuconstbuffer gpuconstbuffer;

typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef enum materialoptionflags {
  MATOPT_None = 0x00000000,
  MATOPT_Alpha = 0x00000001,
  MATOPT_CastShadows = 0x00000002,
  MATOPT_Count = 3,
} materialoptionflags;

#define MAX_SUBMATERIALS (MATOPT_Count * MATOPT_Count)
#define MAX_PASS_PIPELINES 8

typedef void (*updatedescriptor_fn)(VkDescriptorSet, void *);

typedef struct submaterial {
  uint32_t pass_count;
  gpupass *passes[MAX_PASS_PIPELINES];
  gpuconstbuffer *material_data[MAX_PASS_PIPELINES];
  gpupipeline *pipelines[MAX_PASS_PIPELINES];
  updatedescriptor_fn update_descriptor_fns[MAX_PASS_PIPELINES];
} submaterial;

typedef struct submaterialselection {
  int32_t submaterial_idx;
  uint32_t pipeline_perm_flags;
} submaterialselection;

typedef submaterialselection (*submaterialselect_fn)(materialoptionflags,
                                                     const void *);

typedef struct material {
  uint32_t submaterial_count;
  submaterial submaterials[MAX_SUBMATERIALS];

  materialoptionflags options;
  submaterialselect_fn submaterial_select;
} material;

typedef struct unlitmaterial {
  float4 albedo;
  gputexture *albedo_map;

  gputexture *normal_map;
  material mat;
} unlitmaterial;

typedef struct phongblinnmaterial {
  float4 albedo;
  gputexture *albedo_map;

  gputexture *normal_map;

  material mat;
} phongblinnmaterial;

static const uint32_t phong_blinn_submaterial_count = 3;
typedef struct phongblinnmaterialdesc {
  gpupass *shadowcast;
  gpupass *zprepassalpha;
  gpupass *zprepassopaque;
  gpupass *coloralpha;
  gpupass *coloropaque;
} phongblinnmaterialdesc;

phongblinnmaterial
phong_blinn_material_init(const phongblinnmaterialdesc *desc);

typedef struct metalroughmaterial {
  float4 albedo;
  gputexture *albedo_map;

  gputexture *normal_map;

  float metallic;
  gputexture *metallic_map;

  float roughness;
  gputexture *roughness_map;

  material mat;
} metalroughmaterial;