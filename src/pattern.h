#pragma once

#include <stdint.h>

typedef struct cputexture cputexture;

void alloc_pattern(uint32_t width, uint32_t height, cputexture **out);
void create_pattern(uint32_t width, uint32_t height, cputexture *out);