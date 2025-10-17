#include "common.h"
#include "vector.h"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3/SDL_endian.h>
#include <shaderc/shaderc.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t SDL_SHADER_Lang;
enum {
  SDL_SHADER_LANG_UNKNOWN,
  SDL_SHADER_LANG_GLSL,
  SDL_SHADER_LANG_SPIRV,
  SDL_SHADER_LANG_HLSL
};

struct SDL_SHADER_Input 
{
  char *path;
  char *base;
  SDL_SHADER_Type type;
  SDL_SHADER_Lang lang;
  SDL_Time last_modified;
};

struct SDL_SHADER_Output 
{
  char *path;
  bool folder;
};

struct SDL_SHADER_State
{
  struct Vector *inputs;
  struct Vector *outputs;
  
  SDL_SHADER_Type shader_type;
  SDL_GPUShaderFormat shader_formats;
  
  char* extension;
  char* entry;
  
  bool recompile;
  bool sync;
  bool silent;
  bool is_output;
  bool is_extension;
  bool is_entry;
};

void print_help()
{
  printf("%s", "sdlshader"); 
  printf("%s", "\n\tUSAGE:");
  printf("%s", "\n\t\tsdlshader -[vertex/fragment/compute] <input> -o <output> [options]\n\n");
  printf("%s", "\t\t<input>:  \tone or multiple GLSL, SPIRV, OR HLSL shader files or folders.\n");
  printf("%s", "\t\t<output>: \trespective output files or folder.\n");
  printf("%s", "\t\tFolders are marked with a \"/\", \"\\\", or \".\". For example \"test/\" is a folder." );

  printf("%s", "\n\tTYPE:\n"); 
  printf("%s", "\t\t-v, --vert/vertex:   a vertex shader\n"); 
  printf("%s", "\t\t-f, --frag/fragment: a fragment shader\n"); 
  printf("%s", "\t\t-c, --comp/compute:  a computer shader\n");
  printf("%s", "\n\t\tYou can chain up multiple types in a single command like this:\n");
  printf("%s", "\t\t\tsdlshader -f fragment.spv -v vertex.spv -o shaders/\n\n");
  printf("%s", "\t\tyou can also append [.vert/.frag/.comp] before the file extension like .vert.glsl to specify the type.\n");
    
  printf("%s", "\n\tBUILT FORMATS:\n"); 
  printf("%s", "\t\t--spv:   includes .spv shaders to the output.\n"); 
  printf("%s", "\t\t--msl:   includes .msl shaders to the output.\n"); 
  printf("%s", "\t\t--dxil:  includes .dxil shaders to the output.\n"); 
  printf("%s", "\t\t--dxbc:  includes .dxbc shaders to the output.\n"); 
  printf("%s", "\n\t\tIf none is specified, it will default to all of them.\n");

  printf("%s", "\n\tOTHERS:\n");
  printf("%s", "\t\t-h, --help: shows this message\n");
  printf("%s", "\t\t-o, --out/output: where the output is going.\n");
  printf("%s", "\t\t-e, --entry: the entry point of the shader code, defaults to \"main\".\n");
  printf("%s", "\t\t--extension: the output extension when using folders, defaults to \".bin\".\n");
  printf("%s", "\t\t--silent: disables all outputs, except errors.\n");
  printf("%s", "\t\t--recompile: wipe and recompile cached shaders.\n");
 //printf("%s", "\t\t--sync-folders [DANGEROUS!]: delete any content of the target folder that doesn't match any corresponding input.\n\n");
}

Uint8 *write_le32(Uint8 *dst, Uint32 value)
{
  Uint32 tmp = SDL_Swap32LE(value);
  SDL_memcpy(dst, &tmp, sizeof(Uint32));
  return dst + sizeof(Uint32);
}

Uint8 *write_le64(Uint8 *dst, Uint64 value)
{
  Uint64 tmp = SDL_Swap64LE(value);
  SDL_memcpy(dst, &tmp, sizeof(Uint64));
  return dst + sizeof(Uint64);
}

void* encode(struct SDL_SHADER_Blob *blob, size_t *size)
{
  // calculate file size
  size_t bin_size = 0;
  bin_size += 8 * sizeof(Uint32);
  bin_size += sizeof(char) * blob->entry_size + 1;
  bin_size += blob->num_shaders * (sizeof(size_t) + sizeof(SDL_GPUShaderFormat));

  if (blob->type == SDL_SHADER_TYPE_COMPUTE)
  {
    bin_size += 5 * sizeof(Uint32);
  }

  for (int i = 0; i < blob->num_shaders; i++) 
  {
    bin_size += blob->shaders[i]->code_size;
  }

  // prepare the binary blob
  Uint8* bin = SDL_malloc(bin_size);
  size_t pos = 0;

  Uint8* p = bin;
  p = write_le32(p, blob->formats);
  p = write_le32(p, blob->type);
  p = write_le32(p, blob->num_samplers);
  p = write_le32(p, blob->num_uniform_buffers);
  p = write_le32(p, blob->num_storage_buffers);
  p = write_le32(p, blob->num_storage_textures);

  if (blob->type == SDL_SHADER_TYPE_COMPUTE)
  {
    p = write_le32(p, blob->num_storage_buffers_readonly);
    p = write_le32(p, blob->num_storage_textures_readonly);
    p = write_le32(p, blob->thread_x);
    p = write_le32(p, blob->thread_y);
    p = write_le32(p, blob->thread_z);
  }

  p = write_le32(p, blob->num_shaders);
  p = write_le32(p, blob->entry_size);

  SDL_memcpy(p, blob->entry, blob->entry_size * sizeof(char));
  p += blob->entry_size * sizeof(char);

  for (int i = 0; i < blob->num_shaders; i++)
  {
    p = write_le32(p, blob->shaders[i]->format);
    p = write_le64(p, blob->shaders[i]->code_size);

    SDL_memcpy(p, blob->shaders[i]->code, blob->shaders[i]->code_size);
    p += blob->shaders[i]->code_size;
  }

  *size = bin_size;
  return bin;
}

struct SDL_SHADER_Blob* compile(void* code, size_t code_size, SDL_SHADER_Type type, SDL_SHADER_Lang lang, SDL_GPUShaderFormat formats, char *entry, char* filename, size_t *size)
{
  struct SDL_SHADER_Blob blob = {0};
  blob.type = type;
  blob.entry_size = SDL_strlen(entry) + 1; // the 1 is for \0
  blob.entry = entry;
  blob.num_shaders = 0;
  blob.shaders = SDL_malloc(4 * sizeof(void*));

  // convert the shader type to the stage used SDL_Shadercross
  SDL_ShaderCross_ShaderStage stage;
  if (type == SDL_SHADER_TYPE_VERTEX)
  {
    stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
  }
  else if (type == SDL_SHADER_TYPE_FRAGMENT)
  {
    stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
  }
  else if (type == SDL_SHADER_TYPE_COMPUTE)
  {
    stage = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
  }

  size_t spirv_size;
  void* spirv;

  if (lang == SDL_SHADER_LANG_GLSL)
  {
    // compile GLSL to SPIRV
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compile_options_t options = shaderc_compile_options_initialize();

    shaderc_shader_kind kind;
    if (type == SDL_SHADER_TYPE_VERTEX)
    {
      kind = shaderc_glsl_vertex_shader;
    }
    else if (type == SDL_SHADER_TYPE_FRAGMENT)
    {
      kind = shaderc_glsl_fragment_shader;
    }
    else if (type == SDL_SHADER_TYPE_COMPUTE)
    {
      kind = shaderc_glsl_compute_shader;
    }

    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, code, code_size, kind, filename, entry, options);
    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success) 
    {
      printf("ERROR: GLSL: %s\n", shaderc_result_get_error_message(result));
      spirv = NULL;
      spirv_size = 0;
    }
    else
    {
      spirv_size = shaderc_result_get_length(result);
      spirv = SDL_malloc(spirv_size);
      SDL_memcpy(spirv, (uint8_t*)shaderc_result_get_bytes(result), spirv_size);
    }

    shaderc_result_release(result);
    shaderc_compile_options_release(options);
    shaderc_compiler_release(compiler);
  }

  else if (lang == SDL_SHADER_LANG_HLSL)
  {
    // compile HLSL to SPIRV
    SDL_ShaderCross_HLSL_Info hlsl_info = {0};
    hlsl_info.source = code;
    hlsl_info.entrypoint = entry;
    hlsl_info.shader_stage = stage;
    hlsl_info.defines = NULL;
    hlsl_info.include_dir = NULL;
    hlsl_info.props = 0;
   
    spirv = SDL_ShaderCross_CompileSPIRVFromHLSL(&hlsl_info, &spirv_size);
  
    if (spirv == NULL)
    {
      printf("ERROR: HLSL: %s\n",  SDL_GetError());
    }
  }
  else 
  {
    spirv = SDL_malloc(code_size);
    SDL_memcpy(spirv, code, code_size);
    spirv_size = code_size;
  }

  // failed to compile spirv
  if (spirv == NULL)
  {
    SDL_free(blob.shaders);
    *size = 0;
    return NULL; 
  }

  // shader info
  SDL_ShaderCross_SPIRV_Info spirv_info = {0};
  spirv_info.shader_stage = stage;
  spirv_info.bytecode = spirv;
  spirv_info.bytecode_size = spirv_size;
  spirv_info.entrypoint = entry;

  // reflection
  if (type == SDL_SHADER_TYPE_COMPUTE)
  {
    SDL_ShaderCross_ComputePipelineMetadata* metadata = SDL_ShaderCross_ReflectComputeSPIRV(spirv, spirv_size, 0);
    
    blob.num_samplers = metadata->num_samplers;
    blob.num_uniform_buffers = metadata->num_uniform_buffers;
    blob.num_storage_buffers = metadata->num_readwrite_storage_buffers;
    blob.num_storage_textures = metadata->num_readwrite_storage_textures;
    blob.num_storage_buffers_readonly = metadata->num_readonly_storage_buffers;
    blob.num_storage_textures_readonly = metadata->num_readonly_storage_textures;
    blob.thread_x = metadata->threadcount_x;
    blob.thread_y = metadata->threadcount_y;
    blob.thread_z = metadata->threadcount_z;

    SDL_free(metadata);
  }
  else
  {
    SDL_ShaderCross_GraphicsShaderMetadata* metadata = SDL_ShaderCross_ReflectGraphicsSPIRV(spirv, spirv_size, 0);

    blob.num_samplers = metadata->resource_info.num_samplers;
    blob.num_uniform_buffers = metadata->resource_info.num_uniform_buffers;
    blob.num_storage_buffers = metadata->resource_info.num_storage_buffers;
    blob.num_storage_textures = metadata->resource_info.num_storage_textures;
   
    SDL_free(metadata);
  }

  // DXIL
  if (formats & SDL_GPU_SHADERFORMAT_DXIL)
  {
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct  SDL_SHADER_Code));
    shader->code = SDL_ShaderCross_CompileDXILFromSPIRV(&spirv_info, &shader->code_size);
    shader->format = SDL_GPU_SHADERFORMAT_DXIL;

    if (shader->code == NULL)
    {
      printf("ERROR: DXIL: %s\n", SDL_GetError());
      formats &= ~SDL_GPU_SHADERFORMAT_DXIL;
      SDL_free(shader);
    }
    else 
    {
      blob.shaders[blob.num_shaders] = shader;
      blob.num_shaders += 1;
    }
  }

  // DXBC
  if (formats & SDL_GPU_SHADERFORMAT_DXBC)
  {
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct  SDL_SHADER_Code));
    shader->code = SDL_ShaderCross_CompileDXBCFromSPIRV(&spirv_info, &shader->code_size);
    shader->format = SDL_GPU_SHADERFORMAT_DXBC;
 
    if (shader->code == NULL)
    {
      printf("ERROR: DXBC: %s\n", SDL_GetError());
      formats &= ~SDL_GPU_SHADERFORMAT_DXBC;
      SDL_free(shader);
    }
    else 
    {
      blob.shaders[blob.num_shaders] = shader;
      blob.num_shaders += 1;
    }
  }

  // MSL
  if (formats & SDL_GPU_SHADERFORMAT_MSL)
  {
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct  SDL_SHADER_Code));
    shader->code = SDL_ShaderCross_TranspileMSLFromSPIRV(&spirv_info);
    shader->code_size = SDL_strlen(shader->code) + 1;
    shader->format = SDL_GPU_SHADERFORMAT_MSL;

    if (shader->code == NULL)
    {
      printf("ERROR: MSL: %s\n", SDL_GetError());
      formats &= ~SDL_GPU_SHADERFORMAT_MSL;
      SDL_free(shader);
    }
    else 
    {
      blob.shaders[blob.num_shaders] = shader;
      blob.num_shaders += 1;
    }
  }


  // SPIRV
  if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
  {
    struct SDL_SHADER_Code *shader = SDL_malloc(sizeof(struct  SDL_SHADER_Code));
    shader->code = spirv;
    shader->code_size = spirv_size;
    shader->format = SDL_GPU_SHADERFORMAT_SPIRV;
  
    blob.shaders[blob.num_shaders] = shader;
    blob.num_shaders += 1;
  }


  // save compiled formats
  blob.formats = formats;

  // convert to a binary blob
  size_t bin_size;
  void* bin = encode(&blob, &bin_size);

  // free the blob
  for (int i = 0; i < blob.num_shaders; i++)
  {
    SDL_free(blob.shaders[i]->code);
  }

  SDL_free(blob.shaders);
  
  // return
  *size = bin_size;
  return bin;
}

void parse_arg(struct SDL_SHADER_State *state, char* arg)
{
  // main
  if (SDL_strcmp(arg, "-h") == 0 || SDL_strcmp(arg, "--help") == 0) {
    print_help();
    return;
  }
  else if (SDL_strcmp(arg, "-o") == 0 || SDL_strcmp(arg, "--out") == 0 || SDL_strcmp(arg, "--output") == 0)
  {
    state->is_output = true;
    return;
  }
  if (SDL_strcmp(arg, "-e") == 0 || SDL_strcmp(arg, "--entry") == 0) {
    state->is_entry = true;
    return;
  }
  else if (SDL_strcmp(arg, "--extension") == 0)
  {
    state->is_extension = true;
    return;
  }
  else if (SDL_strcmp(arg, "--silent") == 0)
  {
    state->silent = true;
    return;
  }
  else if (SDL_strcmp(arg, "--recompile") == 0)
  {
    state->recompile = true;
    return;
  }
  else if (SDL_strcmp(arg, "--sync-folders") == 0)
  {
    state->sync = true;
    return;
  }

  // formats
  else if (SDL_strcmp(arg, "--spv") == 0)
  {
    state->shader_formats |= SDL_GPU_SHADERFORMAT_SPIRV;
    return;
  }
  else if (SDL_strcmp(arg, "--msl") == 0)
  {
    state->shader_formats |= SDL_GPU_SHADERFORMAT_MSL;
    return;
  }
  else if (SDL_strcmp(arg, "--dxil") == 0)
  {
    state->shader_formats |= SDL_GPU_SHADERFORMAT_DXIL;
    return;
  }
  else if (SDL_strcmp(arg, "--dxbc") == 0)
  {
    state->shader_formats |= SDL_GPU_SHADERFORMAT_DXBC;
    return;
  }

  // type
  else if (SDL_strcmp(arg, "-v") == 0 || SDL_strcmp(arg, "--vert") == 0 || SDL_strcmp(arg, "--vertex") == 0)
  {
    state->shader_type = SDL_SHADER_TYPE_VERTEX;
    return;
  }
  else if (SDL_strcmp(arg, "-f") == 0 || SDL_strcmp(arg, "--frag") == 0 || SDL_strcmp(arg, "--fragment") == 0)
  {
    state->shader_type = SDL_SHADER_TYPE_FRAGMENT;
    return;
  }
  else if (SDL_strcmp(arg, "-c") == 0 || SDL_strcmp(arg, "--comp") == 0 || SDL_strcmp(arg, "--compute") == 0)
  {
    state->shader_type = SDL_SHADER_TYPE_COMPUTE;
    return;
  }
  else if (*arg == '-')
  {
    printf("ERROR: unknown argument \"%s\".\n", arg);
    return;
  }

  // change the entry point
  if (state->is_entry)
  {
    state->is_entry = false;
    state->entry = arg;
    return;
  }

  // change the default file extension
  if (state->is_extension) 
  {
    state->is_extension = false;
    state->extension = arg;
    return;
  }

  // check folders
  char last = arg[SDL_strlen(arg) - 1];
  bool is_folder = last == '/' || last == '\\' || last == '.';

  // outputs
  if (state->is_output)
  {
    struct SDL_SHADER_Output *output = SDL_malloc(sizeof(struct SDL_SHADER_Output));
    output->path = SDL_strdup(arg);
    output->folder = is_folder;
    vector_push(state->outputs, output);
    return;
  }

  int count = 1;
  char** files = NULL;
  int arg_len = SDL_strlen(arg);
  bool globbed = false;

  // find all files in a directory
  if (is_folder)
  {
    files = SDL_GlobDirectory(arg, NULL, 0, &count);
    globbed = true;
    
    if (files == NULL)
    {
      count = 0;
    }
  }

  // inputs
  for (int i = 0; i < count; i++) {
    char* path;
    char* base;

    if (globbed)
    {
      int path_size = arg_len + SDL_strlen(files[i]) + 1;
      path = SDL_calloc(path_size, sizeof(char));
      SDL_strlcat(path, arg, path_size);
      SDL_strlcat(path, files[i], path_size);
    }
    else 
    {
      path = SDL_strdup(arg);
    }
   
    // skip missing files
    SDL_PathInfo info = {0};
    if (!SDL_GetPathInfo(path, &info))
    {
      printf("ERROR: \"%s\" does not exist.\n", path);
      SDL_free(path);
      return;
    }

    // skip directories in a glob
    if (globbed && info.type == SDL_PATHTYPE_DIRECTORY)
    {
      SDL_free(path);
      return;
    }

    struct SDL_SHADER_Input *input = SDL_malloc(sizeof(struct SDL_SHADER_Input));
    input->path = path;
    input->type = state->shader_type;
    input->last_modified = info.modify_time;

    // set the shader langauge based on file extension
    size_t len = SDL_strlen(path);
    if (len > 6 && SDL_strcmp(&path[len - 5], ".glsl") == 0)
    {
      input->lang = SDL_SHADER_LANG_GLSL;
    }
    else if (len > 6 && SDL_strcmp(&path[len - 5], ".hlsl") == 0)
    {
      input->lang = SDL_SHADER_LANG_HLSL;
    }
    else if (len > 5 && SDL_strcmp(&path[len - 4], ".spv") == 0)
    {
      input->lang = SDL_SHADER_LANG_SPIRV;
    }
    else 
    {
      input->lang = SDL_SHADER_LANG_UNKNOWN;
    }

    // the size of the extension string
    int extension_offset = 0;
    if (input->lang == SDL_SHADER_LANG_GLSL || input->lang == SDL_SHADER_LANG_HLSL)
    {
      extension_offset = 5;
    }
    else if (input->lang == SDL_SHADER_LANG_SPIRV)
    {
      extension_offset = 4;
    }

    int base_size = 0;
    if (globbed)
    {
      // glob path without the extension
      base_size = SDL_strlen(files[i]) - extension_offset + 1;
      input->base = SDL_calloc(base_size, sizeof(char));
      SDL_strlcat(input->base, files[i], base_size);
    }
    else 
    {
      // skip to the last \ or / 
      int filename_start = 0;
      for (int i = 0; i < len; i++) 
      {
        if (path[i] == '/' || path[i] == '\\')
        {
          filename_start = i;
        }
      }

      // filename without the extension
      base_size = len - filename_start -  extension_offset;
      input->base = SDL_calloc(base_size, sizeof(char));
      SDL_strlcat(input->base, &path[filename_start + 1], base_size);
    }

    // figure out the shader type based on file name
    if (SDL_strcmp(&input->base[base_size - 6], ".vert") == 0)
    {
      input->type = SDL_SHADER_TYPE_VERTEX;
    }
    else if (SDL_strcmp(&input->base[base_size - 6], ".frag") == 0)
    {
      input->type = SDL_SHADER_TYPE_FRAGMENT;
    }
    else if (SDL_strcmp(&input->base[base_size - 6], ".comp") == 0)
    {
      input->type = SDL_SHADER_TYPE_COMPUTE;
    }

    vector_push(state->inputs, input);
  }

    // free the list of files
  SDL_free(files);
}

void run(struct SDL_SHADER_State *state)
{
  // skip when no inputs are available
  if (state->inputs->size == 0)
  {
    printf("%s", "ERROR: no input files.\n");
    return;
  }

  size_t extension_size = SDL_strlen(state->extension);
  size_t output_index = 0;

  for (int i = 0; i < state->inputs->size; i++) 
  {
    struct SDL_SHADER_Input *input = vector_get(state->inputs, i);
    struct SDL_SHADER_Output *output = vector_get(state->outputs, output_index);
  
    if (output == NULL)
    {
      printf("ERROR: no output for \"%s\"\n", input->path);
      continue;
    }

    if (input->lang == SDL_SHADER_LANG_UNKNOWN)
    {
      printf("ERROR: \"%s\" has unknown file extension. \n\tSupported extensions: \".glsl\", \".hlsl\", or \".spv\".\n", input->path);
      continue;
    }

    SDL_PathInfo code_info = {0};
    if (!SDL_GetPathInfo(input->path, &code_info))
    {
      continue;
    }

    char* target;
    if (output->folder)
    {
      uint32_t target_size = SDL_strlen(output->path) + SDL_strlen(input->base) + extension_size + 1;
      target = SDL_calloc(target_size, sizeof(char));
      SDL_strlcat(target, output->path, target_size);
      SDL_strlcat(target, input->base, target_size);
      SDL_strlcat(target, state->extension, target_size);
    }
    else 
    {
      target = output->path;
      output_index++;
    }

    // skip modified file
    if (!state->recompile)
    {
      SDL_PathInfo target_info = {0};
      
      // if another newer compiled blob exists
      if (SDL_GetPathInfo(target, &target_info) && target_info.modify_time > input->last_modified)
      {
        if (output->folder)
        {
          SDL_free(target);
        }

        continue;
      }
    }

    // print progress
    if (!state->silent)
    {
      printf("COMPILING: \"%s\" -> \"%s\".\n", input->path, target);
    }

    size_t code_size = 0;
    void* code = SDL_LoadFile(input->path, &code_size);

    if (code == NULL)
    {
      printf("ERROR: could not open file \"%s\".\n", input->path);
    }
    else 
    {
      size_t bin_size;
      void* bin = compile(code, code_size, input->type, input->lang, state->shader_formats, state->entry, input->path, &bin_size);

      if (bin != NULL)
      {
        // create a directory and try again if failed
        if(!SDL_SaveFile(target, bin, bin_size))
        {
          char* dir = SDL_strdup(target);
          uint32_t target_size = SDL_strlen(target);
          uint32_t pos = 0;

          // find the last slash
          for (uint32_t i = 0; i < target_size; i++) 
          {
            if (target[i] == '/' || target[i] == '\'')
            {
              pos = i;
            }
          }

          // terminate the strig early
          dir[pos] = '\0';
          SDL_CreateDirectory(dir);

          if (!SDL_SaveFile(target, bin, bin_size))
          {
            printf("ERROR: could not write \"%s\"", target);
          }

          SDL_free(dir);
        }

        SDL_free(bin);
      }
    }

    SDL_free(code);

    if (output->folder)
    {
      SDL_free(target);
    }
  }
}

int main(int argc, char** argv)
{
  // state
  struct SDL_SHADER_State state = {0};
  state.inputs = vector_create(256);
  state.outputs = vector_create(256);
  
  state.shader_type = SDL_SHADER_TYPE_VERTEX;
  state.shader_formats = 0;
  
  state.extension = ".bin";
  state.entry = "main";
  
  state.recompile = false;
  state.sync = false;
  state.silent = false;
  state.is_output = false;
  state.is_extension = false;
  state.is_entry = false;
  
  // parse args
  for (int i = 1; i < argc; i++)
  {
    parse_arg(&state, argv[i]);
  }

  // deafult to all formats if none is forced
  if (state.shader_formats == 0)
  {
    state.shader_formats |= SDL_GPU_SHADERFORMAT_SPIRV;
    state.shader_formats |= SDL_GPU_SHADERFORMAT_MSL;
    state.shader_formats |= SDL_GPU_SHADERFORMAT_DXIL;
    state.shader_formats |= SDL_GPU_SHADERFORMAT_DXBC;
  }

  // executate the command
  run(&state);

  // free inputs
  for (int i = 0; i < state.inputs->size; i++) 
  {
    struct SDL_SHADER_Input *input = vector_get(state.inputs, i);
    SDL_free(input->path);
    SDL_free(input->base);
    SDL_free(input);
  }

  // free outputs
  for (int i = 0; i < state.outputs->size; i++) 
  {
    struct SDL_SHADER_Output *output = vector_get(state.outputs, i);
    SDL_free(output->path);
    SDL_free(output);
  }

  // delete vectors
  vector_delete(state.inputs);
  vector_delete(state.outputs);
}
