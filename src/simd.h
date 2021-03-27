#pragma once

#include <stdint.h>

typedef float __attribute__((vector_size(16))) float4;
typedef float __attribute__((vector_size(12))) float3;
typedef float __attribute__((vector_size(8))) float2;

typedef double __attribute__((vector_size(32))) double4;
typedef double __attribute__((vector_size(24))) double3;
typedef double __attribute__((vector_size(16))) double2;

typedef int32_t __attribute__((vector_size(16))) int4;
typedef int32_t __attribute__((vector_size(12))) int3;
typedef int32_t __attribute__((vector_size(8))) int2;

typedef uint32_t __attribute__((vector_size(16))) uint4;
typedef uint32_t __attribute__((vector_size(12))) uint3;
typedef uint32_t __attribute__((vector_size(8))) uint2;

typedef struct float4x4 {
  union {
    struct {
      float4 row0;
      float4 row1;
      float4 row2;
      float4 row3;
    };
    float4 rows[4];
  };
} float4x4;

typedef struct float3x4 {
  union {
    struct {
      float4 row0;
      float4 row1;
      float4 row2;
    };
    float4 rows[3];
  };
} float3x4;

typedef struct float3x3 {
  union {
    struct {
      float3 row0;
      float3 row1;
      float3 row2;
    };
    float3 rows[3];
  };
} float3x3;

typedef struct transform {
  float3 position;
  float3 scale;
  float3 rotation;
} transform;

float dotf3(float3 x, float3 y);
float dotf4(float4 x, float4 y);
float3 crossf3(float3 x, float3 y);

float magf3(float3 v);
float magsqf3(float3 v);
float3 normf3(float3 v);

void mf33_identity(float3x3 *m);
void mf34_identity(float3x4 *m);
void mf44_identity(float4x4 *m);

void mulf33(float3x3 *m, float3 v);
void mulf34(float3x4 *m, float4 v);
void mulf44(float4x4 *m, float4 v);

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o);

void translate(transform *t, float3 p);
void scale(transform *t, float3 s);
void rotate(transform *t, float3 r);

void transform_to_matrix(float3x4 *m, const transform *t);

void look_at(float4x4 *m, float3 pos, float3 target, float3 up);
void perspective(float4x4 *m, float near, float far, float fov);