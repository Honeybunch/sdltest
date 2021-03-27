#include "simd.h"

#define _USE_MATH_DEFINES
#include <assert.h>
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

float magf3(float3 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

float magf4(float4 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]));
}

float magsqf3(float3 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
}

float magsqf4(float3 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]);
}

float3 normf3(float3 v) {
  float invSum = 1 / magf3(v);
  return (float3){v[0] * invSum, v[1] * invSum, v[2] * invSum};
}

void mf33_identity(float3x3 *m) {
  assert(m);
  *m = (float3x3){
      (float3){1, 0, 0},
      (float3){0, 1, 0},
      (float3){0, 0, 1},
  };
}

void mf34_identity(float3x4 *m) {
  assert(m);
  *m = (float3x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
  };
}

void mf44_identity(float4x4 *m) {
  assert(m);
  *m = (float4x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){0, 0, 0, 1},
  };
}

void mulf33(float3x3 *m, float3 v) {
  assert(m);
#pragma clang loop unroll_count(3)
  for (uint32_t i = 0; i < 3; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf34(float3x4 *m, float4 v) {
  assert(m);
#pragma clang loop unroll_count(4)
  for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf44(float4x4 *m, float4 v) {
  assert(m);
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
  assert(x);
  assert(y);
  assert(o);
#pragma clang loop unroll_count(3)
  for (uint32_t i = 0; i < 3; ++i) {
#pragma clang loop unroll_count(4)
    for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
#pragma clang loop unroll_count(3)
      for (uint32_t iii = 0; iii < 3; ++iii) {
        s += x->rows[i][iii] * y->rows[iii][ii];
      }
      o->rows[i][ii] = s;
    }
  }
}

void mulmf44(const float4x4 *x, const float4x4 *y, float4x4 *o) {
  assert(x);
  assert(y);
  assert(o);
#pragma clang loop unroll_count(4)
  for (uint32_t i = 0; i < 4; ++i) {
#pragma clang loop unroll_count(4)
    for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
#pragma clang loop unroll_count(4)
      for (uint32_t iii = 0; iii < 4; ++iii) {
        s += x->rows[i][iii] * y->rows[iii][ii];
      }
      o->rows[i][ii] = s;
    }
  }
}

void translate(transform *t, float3 p) {
  assert(t);
  t->position += p;
}
void scale(transform *t, float3 s) {
  assert(t);
  t->scale += s;
}
void rotate(transform *t, float3 r) {
  assert(t);
  t->rotation += r;
}

void transform_to_matrix(float3x4 *m, const transform *t) {
  assert(m);
  assert(t);
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

void look_at(float3x4 *m, float3 pos, float3 target, float3 up) {
  assert(m);

  float3 forward = normf3(pos - target);
  float3 right = crossf3(normf3(up), forward);
  up = crossf3(forward, right);

  *m = (float3x4){
      (float4){right[0], up[0], forward[0], pos[0]},
      (float4){right[1], up[1], forward[1], pos[1]},
      (float4){right[2], up[2], forward[2], pos[2]},
  };
}

void perspective(float3x4 *m, float near, float far, float fov) {
  assert(m);
  float scale = 1 / tanf(fov * 0.5f * M_PI / 180.0f);

  float m33 = -far / (far - near);
  float m43 = -far * near / (far - near);

  *m = (float3x4){
      (float4){scale, 0, 0, 0},
      (float4){0, scale, 0, 0},
      (float4){0, 0, m33, m43},
  };
}