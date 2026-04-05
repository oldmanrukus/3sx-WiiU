/**
 * @file wiiu_shaders.h
 * @brief GX2 shader management for the Wii U renderer
 */
#ifndef WIIU_SHADERS_H
#define WIIU_SHADERS_H

#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize shaders. Tries to load from sd:/wiiu/apps/3sx/shader.gsh
 * first, falls back to embedded microcode.
 * @return true if shaders are ready to use
 */
bool WiiUShaders_Init(void);
void WiiUShaders_Shutdown(void);

/** Bind the vertex + pixel + fetch shaders for drawing */
void WiiUShaders_Bind(void);

/** Bind a GX2 texture to sampler slot 0, or white texture if NULL */
void WiiUShaders_BindTexture(GX2Texture* tex);

/** Set the vertex attribute buffer (position + texcoord + color) */
void WiiUShaders_SetAttribBuffer(const void* buffer, uint32_t size);

/** Check if shaders loaded successfully */
bool WiiUShaders_IsReady(void);

GX2Sampler* WiiUShaders_GetNearestSampler(void);
GX2Sampler* WiiUShaders_GetLinearSampler(void);

#endif
