#pragma once

#include "simd.h"

typedef struct cpumesh cpumesh;
typedef struct allocator allocator;

cpumesh *create_skydome(allocator *a);