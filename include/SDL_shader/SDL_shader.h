#pragma once
#include <SDL3/SDL_gpu.h>

#ifdef __cplusplus
extern "C" {
#endif

SDL_GPUShader* SDL_SHADER_Load(SDL_GPUDevice *device, const char *file);
SDL_GPUShader* SDL_SHADER_Load_IO(SDL_GPUDevice* device, SDL_IOStream* src, bool closeio);

SDL_GPUComputePipeline* SDL_SHADER_LoadCompute(SDL_GPUDevice *device, const char *file);
SDL_GPUComputePipeline* SDL_SHADER_LoadCompute_IO(SDL_GPUDevice* device, SDL_IOStream* src, bool closeio);

#ifdef __cplusplus
}
#endif
