#include "simd.h"

#include <math.h>

float dotf3(float3 x, float3 y) {
  return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]);
}

float dotf4(float4 x, float4 y) {
  return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]) + (x[3] * y[3]);
}

float3 crossf3(float3 x, float3 y) {
  return (float3){
      (x[1] * y[2]) - (x[2] * y[1]),
      (x[2] * y[0]) - (x[0] * y[2]),
      (x[0] * y[1]) - (x[1] * y[0]),
  };
}

void mulf33(float3x3 *m, float3 v) {
#pragma clang loop unroll_count(3)
  for (uint32_t i = 0; i < 3; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf34(float3x4 *m, float4 v) {
#pragma clang loop unroll_count(4)
  for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf44(float4x4 *m, float4 v) {
#pragma clang loop unroll_count(4)
  for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
    m->row3[i] *= s;
  }
}

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o) {
#pragma clang loop unroll_count(3)
  for (uint32_t i = 0; i < 3; ++i) {
#pragma clang loop unroll_count(4)
    for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
#pragma clang loop unroll_count(3)
      for (uint32_t iii = 0; iii < 3; ++iii) {
        s += x->rows[i][iii] * x->rows[iii][ii];
      }
      o->rows[i][ii] = s;
    }
  }
}

void translate(transform *t, float3 p) { t->position += p; }
void scale(transform *t, float3 s) { t->scale += s; }
void rotate(transform *t, float3 r) { t->rotation += r; }

void transform_to_matrix(float3x4 *m, const transform *t) {
  // Position matrix
  float3x4 p = {
      (float4){1, 0, 0, t->position[0]},
      (float4){0, 1, 0, t->position[1]},
      (float4){0, 0, 1, t->position[2]},
  };

  // Rotation matrix from euler angles
  float3x4 r = {0};
  {
    float x_angle = t->rotation[0];
    float y_angle = t->rotation[1];
    float z_angle = t->rotation[2];

    float3x4 rx = {
        (float4){1, 0, 0, 0},
        (float4){0, cosf(x_angle), -sinf(x_angle), 0},
        (float4){0, sinf(x_angle), cosf(x_angle), 0},
    };
    float3x4 ry = {
        (float4){cosf(y_angle), 0, sinf(y_angle), 0},
        (float4){0, 1, 0, 0},
        (float4){-sinf(y_angle), 0, cosf(y_angle), 0},

    };
    float3x4 rz = {
        (float4){cosf(z_angle), -sinf(z_angle), 0, 0},
        (float4){sinf(z_angle), cosf(z_angle), 0, 0},
        (float4){0, 0, 1, 0},
    };
    float3x4 temp = {0};
    mulmf34(&rx, &ry, &temp);
    mulmf34(&temp, &rz, &r);
  }

  // Scale matrix
  float3x4 s = {
      (float4){t->scale[0], 0, 0, 0},
      (float4){0, t->scale[1], 0, 0},
      (float4){0, 0, t->scale[2], 0},
  };

  // Transformation matrix = r * p * s;
  float3x4 temp = {0};
  mulmf34(&r, &p, &temp);
  mulmf34(&temp, &s, m);
}