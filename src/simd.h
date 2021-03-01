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
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
  };

  float4 rows[4];
} float4x4;

typedef struct float3x4 {
  union {
    float4 row0;
    float4 row1;
    float4 row2;
  };

  float4 rows[3];
} float3x4;

typedef struct float3x3 {
  union {
    float3 row0;
    float3 row1;
    float3 row2;
  };

  float3 rows[3];
} float3x3;

float dotf3(float3 x, float3 y);
float dotf4(float4 x, float4 y);
float3 crossf3(float3 x, float3 y);

float3x3 mulf33(float3x3 x, float3x3 y);
float4x4 mulf44(float4x4 x, float4x4 y);
