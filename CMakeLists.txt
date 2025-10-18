cmake_minimum_required(VERSION 3.30)
project(SDL_shader)

option(SDL_SHADER_CLI "Build CLI tool" ON)
option(SDL_SHADER_LIBRARY "Build static library" ON)


if (NOT TARGET SDL3::SDL3)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL)
endif()

if (SDL_SHADER_CLI)
  set(SDLSHADERCROSS_STATIC ON CACHE BOOL "" FORCE)
  set(SDLSHADERCROSS_SHARED OFF CACHE BOOL "" FORCE)
  set(SDLSHADERCROSS_SPIRVCROSS_SHARED OFF CACHE BOOL "" FORCE)
  set(SDLSHADERCROSS_VENDORED ON CACHE BOOL "" FORCE)
  set(SDLSHADERCROSS_DXC ON CACHE BOOL "" FORCE)
  set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_shadercross)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/glslang)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/shaderc)
  add_executable(SDL_shader_cli
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vector.c
  )

  target_link_libraries(SDL_shader_cli PRIVATE SDL3::SDL3-static SDL3_shadercross-static shaderc)
  set_target_properties(SDL_shader_cli PROPERTIES OUTPUT_NAME "sdlshader")
endif()

if (SDL_SHADER_LIBRARY)
  add_library(SDL_shader STATIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SDL_shader.c
  )

  target_link_libraries(SDL_shader PRIVATE SDL3::SDL3)
  target_include_directories(SDL_shader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()
