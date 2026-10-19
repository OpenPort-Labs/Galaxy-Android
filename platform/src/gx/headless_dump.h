#pragma once
#include <stddef.h>
#include "gx_record.h"

bool headless_write_png(const char* path, const unsigned char* rgba, int w, int h);
void headless_dump_frame(const gpu::Frame& f, const char* dir);
