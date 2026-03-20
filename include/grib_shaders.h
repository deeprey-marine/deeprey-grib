/***************************************************************************
 *   Copyright (C) 2024 by Deeprey Research Ltd                           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 **************************************************************************/

#ifndef __GRIB_SHADERS_H__
#define __GRIB_SHADERS_H__

#include "pi_gl.h"

struct DpGribGLCapabilities {
  bool hasGL33;        // GL 3.3+ for particle shaders
  bool hasGL43;        // GL 4.3+ for compute shader particles (future)
  int maxTextureSize;  // GL_MAX_TEXTURE_SIZE
};

// Shader program handles (0 = not initialized)
extern GLuint grib_particle_update_program;
extern GLuint grib_trail_decay_program;
extern GLuint grib_particle_draw_program;
extern GLuint grib_ribbon_draw_program;
extern GLuint grib_trail_composite_program;

// Detect runtime GL capabilities
DpGribGLCapabilities grib_DetectCapabilities();

// Initialize GPU shaders. Returns true if GL 3.3+ shaders compiled OK.
bool grib_InitGPUShaders();

// Cleanup GPU shaders
void grib_CleanupGPUShaders();

#endif  // __GRIB_SHADERS_H__
