#include "camera.h"

void camera_view_projection(const camera *c, float4x4 *vp) {
  float4x4 model_matrix = {0};
  transform_to_matrix(&model_matrix, &c->transform);

  float4 row3 = model_matrix.rows[2];
  float3 forward = {row3[0], row3[1], row3[2]};

  float4x4 view = {0};
  look_forward(&view, c->transform.position, forward, (float3){0, 1, 0});

  float4x4 proj = {0};
  perspective(&proj, c->fov, c->aspect, c->near, c->far);

  mulmf44(&proj, &view, vp);
}