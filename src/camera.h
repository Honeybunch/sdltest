#pragma once

#include <simd.h>

typedef struct camera {
  transform transform;
  float aspect;
  float fov;
  float near;
  float far;
} camera;

void camera_view_projection(const camera *c, float4x4 *vp);