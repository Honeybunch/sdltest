#include "scene.h"
#include "cpuresources.h"
#include "gpuresources.h"

#include <assert.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>

#include <cgltf.h>

typedef struct scene_alloc_info {
  uint32_t entity_count;
  uint32_t *child_counts;

  uint32_t mesh_count;
  uint32_t texture_count;
  uint32_t material_count;
} scene_alloc_info;

static scene *alloc_scene(const scene_alloc_info *info, allocator std_alloc) {
  uint32_t entity_count = info->entity_count;
  uint32_t *child_counts = info->child_counts;
  uint32_t mesh_count = info->mesh_count;
  uint32_t texture_count = info->texture_count;

  size_t children_size = 0;
  for (uint32_t i = 0; i < entity_count; ++i) {
    children_size += child_counts[i] * sizeof(uint32_t);
  }

  size_t transform_size = sizeof(scene_transform) * entity_count;
  size_t static_mesh_size = sizeof(scene_static_mesh) * entity_count;
  size_t mesh_size = sizeof(gpumesh) * mesh_count;
  size_t texture_size = sizeof(gputexture) * texture_count;
  size_t components_size = sizeof(uint64_t) * entity_count;

  size_t scene_size_bytes = sizeof(scene) + transform_size + static_mesh_size +
                            mesh_size + texture_size + components_size +
                            children_size;

  scene *s = hb_alloc(std_alloc, scene_size_bytes);
  assert(s);

  s->max_entity_count = entity_count;
  s->max_mesh_count = mesh_count;
  s->max_texture_count = texture_count;

  size_t offset = sizeof(scene);
  s->transforms = (scene_transform *)((uint8_t *)s + offset);
  offset += transform_size;
  assert(offset <= scene_size_bytes);

  s->static_meshes = (scene_static_mesh *)((uint8_t *)s + offset);
  offset += static_mesh_size;
  assert(offset <= scene_size_bytes);

  s->meshes = (gpumesh *)((uint8_t *)s + offset);
  offset += mesh_size;
  assert(offset <= scene_size_bytes);

  s->textures = (gputexture *)((uint8_t *)s + offset);
  offset += texture_size;
  assert(offset <= scene_size_bytes);

  s->components = (uint64_t *)((uint8_t *)s + offset);
  offset += components_size;
  assert(offset <= scene_size_bytes);

  for (uint32_t i = 0; i < entity_count; ++i) {
    scene_transform *transform = &s->transforms[i];
    uint32_t child_count = child_counts[i];
    if (child_count > 0) {
      transform->child_count = child_count;
      transform->children = (scene_transform **)((uint8_t *)s + offset);
      offset += child_count * sizeof(uint32_t);
      assert(offset <= scene_size_bytes);
    }
  }

  return s;
}

uint32_t parse_node(scene *s, cgltf_data *data, cgltf_node *node,
                    uint32_t *entity_id) {
  uint32_t idx = *entity_id;
  (*entity_id)++;

  uint64_t components = 0;
  {
    components |= COMPONENT_TYPE_TRANSFORM;
    scene_transform *transform = &s->transforms[idx];

    float *pos = node->translation;
    float *rot = node->rotation;
    float *scale = node->scale;
    transform->t.position = (float3){pos[0], pos[1], pos[2]};
    transform->t.rotation = (float3){rot[0], rot[1], rot[2]};
    transform->t.scale = (float3){scale[0], scale[1], scale[2]};

    assert(transform->child_count == node->children_count);
    for (uint32_t i = 0; i < node->children_count; ++i) {
      cgltf_node *child = node->children[i];
      uint32_t child_idx = parse_node(s, data, child, entity_id);

      // For some reason this causes a heap corruption
      // transform->children[i] = &s->transforms[child_idx];
    }
  }

  if (node->mesh != NULL) {
    components |= COMPONENT_TYPE_STATIC_MESH;
    scene_static_mesh *static_mesh = &s->static_meshes[idx];

    for (uint32_t i = 0; i < data->meshes_count; ++i) {
      if (node->mesh == &data->meshes[i]) {
        static_mesh->mesh = &s->meshes[i];
      }
    }
  }

  s->components[idx] = components;
  return idx;
}

void parse_child_counts(cgltf_node *node, uint32_t *child_counts,
                        uint32_t *index) {
  uint32_t idx = *index;
  (*index)++;

  child_counts[idx] = node->children_count;
  for (uint32_t i = 0; i < node->children_count; ++i) {
    parse_child_counts(node->children[i], child_counts, index);
  }
}

int32_t load_scene(VkDevice device, allocator tmp_alloc, allocator std_alloc,
                   const VkAllocationCallbacks *vk_alloc,
                   VmaAllocator vma_alloc, VmaPool up_pool, VmaPool tex_pool,
                   const char *filename, scene **out_scene) {
  // We really only want to handle glbs; gltfs should be pre-packed
  cgltf_options options = {.type = cgltf_file_type_glb};
  cgltf_data *data = NULL;
  cgltf_result res = cgltf_parse_file(&options, filename, &data);
  assert(res == cgltf_result_success);

  res = cgltf_load_buffers(&options, data, filename);
  assert(res == cgltf_result_success);

  // Won't bother to validate in a release build
  assert(cgltf_validate(data) == cgltf_result_success);

  // Allocate scene
  assert(data->scenes_count == 1);

  uint32_t entity_count = data->nodes_count;
  uint32_t mesh_count = data->meshes_count;
  uint32_t texture_count = data->images_count;
  uint32_t material_count = data->materials_count;

  scene *s = NULL;
  {
    scene_alloc_info alloc_info = {0};
    alloc_info.entity_count = entity_count;
    alloc_info.mesh_count = mesh_count;
    alloc_info.texture_count = texture_count;
    alloc_info.material_count = material_count;
    alloc_info.child_counts =
        hb_alloc_nm_tp(tmp_alloc, alloc_info.entity_count, uint32_t);

    // The order of the child counts has to reflect the heirarchy of the nodes
    {
      uint32_t node_idx = 0;
      for (uint32_t i = 0; i < alloc_info.entity_count; ++i) {
        if (data->nodes[i].parent == NULL) {
          parse_child_counts(&data->nodes[i], alloc_info.child_counts,
                             &node_idx);
        }
      }
    }

    s = alloc_scene(&alloc_info, std_alloc);
    hb_free(tmp_alloc, alloc_info.child_counts);
  }
  assert(s);

  // Parse meshes
  s->mesh_count = mesh_count;
  for (uint32_t i = 0; i < mesh_count; ++i) {
    cgltf_mesh *mesh = &data->meshes[i];
    create_gpumesh_cgltf(device, vma_alloc, tmp_alloc, mesh, &s->meshes[i]);
  }

  // Parse Textures
  s->texture_count = texture_count;
  for (uint32_t i = 0; i < texture_count; ++i) {
    cgltf_texture *tex = &data->textures[i];
    create_gputexture_cgltf(device, vma_alloc, vk_alloc, tex, data->bin,
                            up_pool, tex_pool, &s->textures[i]);
  }

  // Parse Materials
  s->material_count = material_count;

  // Parse scene
  s->entity_count = entity_count;
  uint32_t entity_tracker = 0;

  cgltf_scene *gltf_scene = data->scene;
  for (uint32_t i = 0; i < gltf_scene->nodes_count; ++i) {
    cgltf_node *node = gltf_scene->nodes[i];

    if (node->parent == NULL) {
      parse_node(s, data, node, &entity_tracker);
    }
  }

  cgltf_free(data);

  *out_scene = s;
  return (int32_t)res;
}

void destroy_scene(VkDevice device, allocator std_alloc, VmaAllocator vma_alloc,
                   const VkAllocationCallbacks *vk_alloc, scene *s) {
  for (uint32_t i = 0; i < s->mesh_count; i++) {
    destroy_gpumesh(device, vma_alloc, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; i++) {
    destroy_texture(device, vma_alloc, vk_alloc, &s->textures[i]);
  }

  hb_free(std_alloc, s);
}