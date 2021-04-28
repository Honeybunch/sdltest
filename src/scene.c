#include "scene.h"
#include "cpuresources.h"
#include "gpuresources.h"

#include <assert.h>
#include <stdlib.h>

#include <cgltf.h>

scene *alloc_scene(uint32_t max_entity_count, uint32_t max_mesh_count,
                   uint32_t max_texture_count) {

  size_t transform_size = sizeof(scene_transform) * max_entity_count;
  size_t static_mesh_size = sizeof(scene_static_mesh) * max_entity_count;
  size_t mesh_size = sizeof(gpumesh) * max_mesh_count;
  size_t texture_size = sizeof(gputexture) * max_texture_count;

  size_t scene_size_bytes = sizeof(scene) + transform_size + static_mesh_size +
                            mesh_size + texture_size;
  scene *s = calloc(1, scene_size_bytes);
  assert(s);

  s->max_entity_count = max_entity_count;
  s->max_mesh_count = max_mesh_count;
  s->max_texture_count = max_texture_count;

  size_t offset = sizeof(scene);
  s->transforms = (scene_transform *)((uint8_t *)s + offset);
  offset += transform_size;
  s->static_meshes = (scene_static_mesh *)((uint8_t *)s + offset);
  offset += static_mesh_size;
  s->meshes = (gpumesh *)((uint8_t *)s + offset);
  offset += mesh_size;
  s->textures = (gputexture *)((uint8_t *)s + offset);
  offset += texture_size;

  return s;
}

int32_t load_scene(VkDevice device, VmaAllocator alloc, const char *filename,
                   scene **out_scene) {
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
  uint32_t max_entity_count = data->scene->nodes_count;
  uint32_t max_mesh_count = data->meshes_count;
  uint32_t max_texture_count = data->images_count;
  scene *s = alloc_scene(max_entity_count, max_mesh_count, max_texture_count);

  // Parse scene
  for (uint32_t i = 0; i < max_mesh_count; ++i) {
    cgltf_mesh *mesh = &data->meshes[i];

    create_gpumesh_cgltf(device, alloc, mesh, &s->meshes[i]);
  }

  cgltf_free(data);

  *out_scene = s;
  return (int32_t)res;
}

void destroy_scene(VkDevice device, VmaAllocator alloc, scene *s) {
  for (uint32_t i = 0; i < s->mesh_count; i++) {
    destroy_gpumesh(device, alloc, &s->meshes[i]);
  }

  for (uint32_t i = 0; i < s->texture_count; i++) {
    destroy_texture(device, alloc, &s->textures[i]);
  }

  free(s);
}