#pragma once

#include <stdint.h>

#include "allocator.h"

typedef struct cputexture cputexture;

void alloc_pattern(allocator alloc, uint32_t width, uint32_t height,
                   cputexture **out);
void create_pattern(uint32_t width, uint32_t height, cputexture *out);
