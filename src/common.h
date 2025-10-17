#pragma once
#include <SDL3/SDL_gpu.h>

typedef uint32_t SDL_SHADER_Type;
enum 
{ 
  SDL_SHADER_TYPE_VERTEX,
  SDL_SHADER_TYPE_FRAGMENT,
  SDL_SHADER_TYPE_COMPUTE
};

struct SDL_SHADER_Code
{
  size_t code_size;
  SDL_GPUShaderFormat format;
  void* code;
};

struct SDL_SHADER_Blob
{
  SDL_GPUShaderFormat formats;
  SDL_SHADER_Type type;

  Uint32 num_samplers;
  Uint32 num_uniform_buffers;
  Uint32 num_storage_buffers;
  Uint32 num_storage_textures;
 
  Uint32 num_storage_buffers_readonly;
  Uint32 num_storage_textures_readonly;
  Uint32 thread_x;
  Uint32 thread_y;
  Uint32 thread_z;

  Uint32 entry_size;
  char* entry;
  
  Uint32 num_shaders;
  struct SDL_SHADER_Code** shaders;
};
