WORK IN PROGRESS

README WILL BE UPDATED LATER

## CLI

#### Example usage:
```bash
./sdlshader --fragment myshader.glsl -o myshader.bin
```

use `-h` for all available options.

## LIBRARY
for integrating with cmake in existing projects you can simply do the following:

```cmake
set(SDL_SHADER_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/SDL_shader)
target_link_libraries(app PRIVATE SDL_shader)
```

#### Example usage:
```c
SDL_GPUShader *shader = SDL_SHADER_Load(device, "shader.bin"); //that's all you need!
```
