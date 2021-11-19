#include "scene.h"
#include "cpuresources.h"
#include "gpuresources.h"

#include <SDL2/SDL_assert.h>
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_rwops.h>

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
      (void)child_idx;

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

cgltf_result sdl_read_gltf(const struct cgltf_memory_options *memory_options,
                           const struct cgltf_file_options *file_options,
                           const char *path, cgltf_size *size, void **data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;
  cgltf_size file_size = SDL_RWsize(file);
  (void)path;

  void *mem = memory_options->alloc(memory_options->user_data, file_size);
  if (mem == NULL) {
    assert(0);
    return cgltf_result_out_of_memory;
  }

  if (SDL_RWread(file, mem, file_size, 1) == 0) {
    return cgltf_result_io_error;
  }

  *size = file_size;
  *data = mem;

  return cgltf_result_success;
}
void sdl_release_gltf(const struct cgltf_memory_options *memory_options,
                      const struct cgltf_file_options *file_options,
                      void *data) {
  SDL_RWops *file = (SDL_RWops *)file_options->user_data;

  memory_options->free(memory_options->user_data, data);

  if (SDL_RWclose(file) != 0) {
    assert(0);
  }
}

int32_t create_scene(DemoAllocContext alloc_ctx, scene *out_scene) {
  (*out_scene) = (scene){.alloc_ctx = alloc_ctx};
  return 0;
}

int32_t scene_append_gltf(scene *s, const char *filename) {
  const DemoAllocContext *alloc_ctx = &s->alloc_ctx;
  VkDevice device = alloc_ctx->device;
  allocator std_alloc = alloc_ctx->std_alloc;
  allocator tmp_alloc = alloc_ctx->tmp_alloc;
  VmaAllocator vma_alloc = alloc_ctx->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = alloc_ctx->vk_alloc;
  VmaPool up_pool = alloc_ctx->up_pool;
  VmaPool tex_pool = alloc_ctx->tex_pool;

  // Load a GLTF/GLB file off disk
  cgltf_data *data = NULL;
  {
    // We really only want to handle glbs; gltfs should be pre-packed
    SDL_RWops *gltf_file = SDL_RWFromFile(filename, "rb");

    if (gltf_file == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", SDL_GetError());
      assert(0);
      return -1;
    }

    cgltf_options options = {.type = cgltf_file_type_glb,
                             .memory =
                                 {
                                     .user_data = std_alloc.user_data,
                                     .alloc = std_alloc.alloc,
                                     .free = std_alloc.free,
                                 },
                             .file = {
                                 .read = sdl_read_gltf,
                                 .release = sdl_release_gltf,
                                 .user_data = gltf_file,
                             }};

    // Parse file loaded via SDL
    cgltf_result res = cgltf_parse_file(&options, filename, &data);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to parse gltf");
      SDL_TriggerBreakpoint();
      return -1;
    }

    res = cgltf_load_buffers(&options, data, filename);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", "Failed to load gltf buffers");
      SDL_TriggerBreakpoint();
      return -2;
    }

    // TODO: Only do this on non-final builds
    res = cgltf_validate(data);
    if (res != cgltf_result_success) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to load validate gltf");
      SDL_TriggerBreakpoint();
      return -3;
    }
  }

  // Collect some pre-append counts so that we can do math later
  uint32_t old_tex_count = s->texture_count;
  uint32_t old_mat_count = s->material_count;
  uint32_t old_mesh_count = s->mesh_count;
  uint32_t old_node_count = s->entity_count;

  // Append textures to scene
  {
    uint32_t new_tex_count = old_tex_count + (uint32_t)data->textures_count;

    s->textures =
        hb_realloc_nm_tp(std_alloc, s->textures, new_tex_count, gputexture);
    if (s->textures == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to allocate textures for scene");
      SDL_TriggerBreakpoint();
      return -4;
    }

    // TODO: Determine a good way to do texture de-duplication
    for (uint32_t i = old_tex_count; i < new_tex_count; ++i) {
      cgltf_texture *tex = &data->textures[i - old_tex_count];
      if (create_gputexture_cgltf(device, vma_alloc, vk_alloc, tex, data->bin,
                                  up_pool, tex_pool, &s->textures[i]) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to to create gputexture");
        SDL_TriggerBreakpoint();
        return -5;
      }
    }

    s->texture_count = new_tex_count;
  }

  // Append materials to scene
  {
    uint32_t new_material_count =
        old_mat_count + (uint32_t)data->textures_count;

    s->materials = hb_realloc_nm_tp(std_alloc, s->materials, new_material_count,
                                    gpumaterial);
    if (s->materials == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to allocate textures for scene");
      SDL_TriggerBreakpoint();
      return -4;
    }

    // TODO: Actually load materials

    // s->material_count = new_material_count;
  }

  // Append meshes to scene
  {
    uint32_t new_mesh_count = old_mesh_count + (uint32_t)data->meshes_count;

    s->meshes = hb_realloc_nm_tp(std_alloc, s->meshes, new_mesh_count, gpumesh);
    if (s->meshes == NULL) {
      SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                   "Failed to allocate meshes for scene");
      SDL_TriggerBreakpoint();
      return -4;
    }

    // TODO: Determine a good way to do texture de-duplication
    for (uint32_t i = old_mesh_count; i < new_mesh_count; ++i) {
      cgltf_mesh *mesh = &data->meshes[i - old_mesh_count];
      if (create_gpumesh_cgltf(vma_alloc, tmp_alloc, mesh, &s->meshes[i]) !=
          0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s",
                     "Failed to to create gpumesh");
        SDL_TriggerBreakpoint();
        return -5;
      }
    }

    s->mesh_count = new_mesh_count;
  }

  // Append nodes to scene
  {
    uint32_t new_node_count = old_node_count + (uint32_t)data->nodes_count;

    // Alloc entity component rows
    s->components =
        hb_realloc_nm_tp(std_alloc, s->components, new_node_count, uint64_t);
    s->static_mesh_indices = hb_realloc_nm_tp(std_alloc, s->static_mesh_indices,
                                              new_node_count, uint32_t);
    s->transforms_2 = hb_realloc_nm_tp(std_alloc, s->transforms_2,
                                       new_node_count, SceneTransform2);

    for (uint32_t i = old_node_count; i < new_node_count; ++i) {
      cgltf_node *node = &data->nodes[i - old_node_count];

      // For now, all nodes have transforms
      {
        // Assign a transform component
        s->components[i] |= COMPONENT_TYPE_TRANSFORM;

        {
          s->transforms_2[i].t.rotation[0] = node->rotation[0];
          s->transforms_2[i].t.rotation[1] = node->rotation[1];
          s->transforms_2[i].t.rotation[2] = node->rotation[2];
        }
        {
          s->transforms_2[i].t.scale[0] = node->scale[0];
          s->transforms_2[i].t.scale[1] = node->scale[1];
          s->transforms_2[i].t.scale[2] = node->scale[2];
        }
        {
          s->transforms_2[i].t.position[0] = node->translation[0];
          s->transforms_2[i].t.position[1] = node->translation[1];
          s->transforms_2[i].t.position[2] = node->translation[2];
        }

        // Appending a gltf to an existing scene means there should be no
        // references between scenes. We should be safe to a just create
        // children.
        s->transforms_2[i].child_count = node->children_count;

        if (node->children_count >= MAX_CHILD_COUNT) {
          SDL_LogError(
              SDL_LOG_CATEGORY_ERROR,
              "Node has number of children that exceeds max child count of: %d",
              MAX_CHILD_COUNT);
          SDL_TriggerBreakpoint();
          return -6;
        }

        // Go through all of the gltf node's children
        // We want to find the index of the child, relative to the gltf node
        // From there we can determine the index of the child relative to the
        // scene node
        for (uint32_t ii = 0; ii < node->children_count; ++ii) {
          for (uint32_t iii = 0; iii < data->nodes_count; ++iii) {
            if (&data->nodes[iii] == node->children[ii]) {
              s->transforms_2[i].children[ii] = old_node_count + iii;
              break;
            }
          }
        }
      }

      // Does this node have an associated mesh?
      if (node->mesh) {
        // Assign a static mesh component
        s->components[i] |= COMPONENT_TYPE_STATIC_MESH;

        // Find the index of the mesh that matches what this node wants
        for (uint32_t ii = 0; ii < data->meshes_count; ++ii) {
          if (node->mesh == &data->meshes[ii]) {
            s->static_mesh_indices[i] = old_mesh_count + ii;
            break;
          }
        }
      }

      // TODO: Lights, cameras, (action!)
    }

    s->entity_count = new_node_count;
  }

  cgltf_free(data);
  return 0;
}

void destroy_scene2(scene *s) {
  const DemoAllocContext *alloc_ctx = &s->alloc_ctx;
  VkDevice device = alloc_ctx->device;
  allocator std_alloc = alloc_ctx->std_alloc;
  VmaAllocator vma_alloc = alloc_ctx->vma_alloc;
  const VkAllocationCallbacks *vk_alloc = alloc_ctx->vk_alloc;

  // Clean up GPU memory
  for (uint32_t i = 0; i < s->mesh_count; i++) {
    destroy_gpumesh(vma_alloc, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; i++) {
    destroy_texture(device, vma_alloc, vk_alloc, &s->textures[i]);
  }

  // Clean up CPU-side arrays
  hb_free(std_alloc, s->materials);
  hb_free(std_alloc, s->meshes);
  hb_free(std_alloc, s->textures);
  hb_free(std_alloc, s->components);
  hb_free(std_alloc, s->static_mesh_indices);
  hb_free(std_alloc, s->transforms_2);
}

int32_t load_scene(VkDevice device, allocator tmp_alloc, allocator std_alloc,
                   const VkAllocationCallbacks *vk_alloc,
                   VmaAllocator vma_alloc, VmaPool up_pool, VmaPool tex_pool,
                   const char *filename, scene **out_scene) {
  // We really only want to handle glbs; gltfs should be pre-packed
  SDL_RWops *gltf_file = SDL_RWFromFile(filename, "rb");

  if (gltf_file == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s", SDL_GetError());
    assert(0);
    return -1;
  }

  cgltf_options options = {.type = cgltf_file_type_glb,
                           .memory =
                               {
                                   .user_data = std_alloc.user_data,
                                   .alloc = std_alloc.alloc,
                                   .free = std_alloc.free,
                               },
                           .file = {
                               .read = sdl_read_gltf,
                               .release = sdl_release_gltf,
                               .user_data = gltf_file,
                           }};
  cgltf_data *data = NULL;
  // Parse file loaded via SDL
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
    create_gpumesh_cgltf(vma_alloc, tmp_alloc, mesh, &s->meshes[i]);
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
    destroy_gpumesh(vma_alloc, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; i++) {
    destroy_texture(device, vma_alloc, vk_alloc, &s->textures[i]);
  }

  hb_free(std_alloc, s);
}
