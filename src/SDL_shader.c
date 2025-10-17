#include "common.h"

#include <SDL_shader/SDL_shader.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_gpu.h>

#include <stdio.h>
#include <string.h>

static Uint8 *read_le32(Uint8 *src, Uint32 *value)
{
    Uint32 temp;
    SDL_memcpy(&temp, src, sizeof(Uint32));
    *value = SDL_Swap32LE(temp);
    return src + sizeof(Uint32);
}

static Uint8 *read_le64(Uint8 *src, Uint64 *value)
{
    Uint64 temp;
    SDL_memcpy(&temp, src, sizeof(Uint64));
    *value = SDL_Swap64LE(temp);
    return src + sizeof(Uint64);
}

SDL_GPUShader* SDL_SHADER_Load(SDL_GPUDevice *device, const char *file)
{
  if (device == NULL)
  {
    return NULL;
  }

  size_t size = 0;
  void* data = SDL_LoadFile(file, &size);

  SDL_GPUShader* shader = SDL_SHADER_Load_IO(device, SDL_IOFromConstMem(data, size), true);

  SDL_free(data);

  return shader;
}


SDL_GPUShader* SDL_SHADER_Load_IO(SDL_GPUDevice* device, SDL_IOStream* src, bool closeio)
{
  SDL_GPUShader *gpuShader = NULL;
  size_t size = SDL_GetIOSize(src);
  void* data = SDL_malloc(size);

  // try to load data
  if (!SDL_ReadIO(src, data, size))
  {
    if (closeio)
    {
      SDL_CloseIO(src);
    }
    return NULL;
  }

  // start reading data
  struct SDL_SHADER_Blob blob = {0};
  Uint8 *p = data;
  p = read_le32(p, &blob.formats);
  p = read_le32(p, &blob.type);
  blob.num_shaders = 0;

  // skip compute shaders
  if (blob.type == SDL_SHADER_TYPE_COMPUTE)
  {
    if (closeio)
    {
      SDL_CloseIO(src);
    }
    return NULL;
  }

  p = read_le32(p, &blob.num_samplers);
  p = read_le32(p, &blob.num_uniform_buffers);
  p = read_le32(p, &blob.num_storage_buffers);
  p = read_le32(p, &blob.num_storage_textures);
  p = read_le32(p, &blob.num_shaders);
  p = read_le32(p, &blob.entry_size);
  
  blob.entry = (char*)p;
  p += blob.entry_size * sizeof(char);


  blob.shaders = SDL_malloc(blob.num_shaders * sizeof(struct SDL_SHADER_Code*));

  for (int i = 0; i < blob.num_shaders; i++)
  { 
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct SDL_SHADER_Code));
    p = read_le32(p, &shader->format);
    p = read_le64(p, &shader->code_size);
    shader->code = p;
    p += shader->code_size;
    blob.shaders[i] = shader;
  }

  SDL_GPUShaderCreateInfo info = {0};
  info.entrypoint = blob.entry;
  info.num_samplers = blob.num_samplers;
  info.num_uniform_buffers = blob.num_uniform_buffers;
  info.num_storage_buffers = blob.num_storage_buffers;
  info.num_storage_textures = blob.num_storage_textures;
  
  if (blob.type == SDL_SHADER_TYPE_VERTEX)
  {
    info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
  }
  else if (blob.type == SDL_SHADER_TYPE_FRAGMENT)
  {
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  }

  info.code = NULL;

  for (int i = 0; i < blob.num_shaders; i++)
  {
    struct SDL_SHADER_Code *shader = blob.shaders[i];
    if (SDL_GetGPUShaderFormats(device) & shader->format)
    {
      info.code = shader->code;
      info.code_size = shader->code_size;
      info.format = shader->format;
      break;
    }
  }

  if (info.code != NULL)
  {
    // replace main with main0 on MSL
    if (info.format == SDL_GPU_SHADERFORMAT_MSL && SDL_strcmp(blob.entry, "main") == 0)
    {
      info.entrypoint = "main0";
    }
    
    gpuShader = SDL_CreateGPUShader(device, &info);
  }

  // free the memory
  for (int i = 0; i < blob.num_shaders; i++) 
  {
    SDL_free(blob.shaders[i]);
  }
  SDL_free(blob.shaders);

  // close the IOStream
  if (closeio)
  {
    SDL_CloseIO(src);
  }

  return gpuShader;
}

SDL_GPUComputePipeline* SDL_SHADER_LoadCompute(SDL_GPUDevice *device, const char *file)
{
  if (device == NULL)
  {
    return NULL;
  }

  size_t size = 0;
  void* data = SDL_LoadFile(file, &size);

  SDL_GPUComputePipeline* pipeline = SDL_SHADER_LoadCompute_IO(device, SDL_IOFromConstMem(data, size), true);

  SDL_free(data);

  return pipeline;
}


SDL_GPUComputePipeline* SDL_SHADER_LoadCompute_IO(SDL_GPUDevice* device, SDL_IOStream* src, bool closeio)
{
  SDL_GPUComputePipeline *pipeline = NULL;
  size_t size = SDL_GetIOSize(src);
  void* data = SDL_malloc(size);

  // try to load data
  if (!SDL_ReadIO(src, data, size))
  {
    SDL_free(data);
    if (closeio)
    {
      SDL_CloseIO(src);
    }
    return NULL;
  }

  // start reading data
  struct SDL_SHADER_Blob blob = {0};
  Uint8 *p = data;
  p = read_le32(p, &blob.formats);
  p = read_le32(p, &blob.type);
  blob.num_shaders = 0;

  // skip graphics shaders
  if (blob.type != SDL_SHADER_TYPE_COMPUTE)
  {
    if (closeio)
    {
      SDL_CloseIO(src);
    }
    return NULL;
  }

  p = read_le32(p, &blob.num_samplers);
  p = read_le32(p, &blob.num_uniform_buffers);
  p = read_le32(p, &blob.num_storage_buffers);
  p = read_le32(p, &blob.num_storage_textures);
  p = read_le32(p, &blob.num_storage_buffers_readonly);
  p = read_le32(p, &blob.num_storage_textures_readonly);
  p = read_le32(p, &blob.thread_x);
  p = read_le32(p, &blob.thread_y);
  p = read_le32(p, &blob.thread_z);
  p = read_le32(p, &blob.num_shaders);
  p = read_le32(p, &blob.entry_size);
  
  blob.entry = (char*)p;
  p += blob.entry_size * sizeof(char);

  blob.shaders = SDL_malloc(blob.num_shaders * sizeof(struct SDL_SHADER_Code*));

  for (int i = 0; i < blob.num_shaders; i++)
  { 
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct SDL_SHADER_Code));
    p = read_le32(p, &shader->format);
    p = read_le64(p, &shader->code_size);
    shader->code = p;
    p += shader->code_size;
    blob.shaders[i] = shader;
    
    char* c = (char*)shader->code;
  }

  SDL_GPUComputePipelineCreateInfo info = {0};
  info.entrypoint = blob.entry;
  info.num_samplers = blob.num_samplers;
  info.num_uniform_buffers = blob.num_uniform_buffers;
  info.num_readwrite_storage_buffers = blob.num_storage_buffers;
  info.num_readwrite_storage_textures = blob.num_storage_textures;
  info.num_readonly_storage_buffers = blob.num_storage_buffers_readonly;
  info.num_readonly_storage_textures = blob.num_storage_textures_readonly;
  info.threadcount_x = blob.thread_x;
  info.threadcount_y = blob.thread_y;
  info.threadcount_z = blob.thread_z;
  info.props = 0;

  info.code = NULL;

  for (int i = 0; i < blob.num_shaders; i++)
  {
    struct SDL_SHADER_Code* shader = blob.shaders[i];
    if (SDL_GetGPUShaderFormats(device) & shader->format)
    {
      info.code = shader->code;
      info.code_size = shader->code_size;
      info.format = shader->format;
      break;
    }
  }

  if (info.code != NULL)
  {
    // replace main with main0 on MSL
    if (info.format == SDL_GPU_SHADERFORMAT_MSL && SDL_strcmp(blob.entry, "main") == 0)
    {
      info.entrypoint = "main0";
    }

    pipeline = SDL_CreateGPUComputePipeline(device, &info);
  }

  // free the memory
  for (int i = 0; i < blob.num_shaders; i++) 
  {
    SDL_free(blob.shaders[i]);
  }
  SDL_free(blob.shaders);

  // close the IOStream
  if (closeio)
  {
    SDL_CloseIO(src);
  }

  return pipeline;
}

