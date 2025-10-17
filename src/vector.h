#pragma once
#include <SDL3/SDL_stdinc.h>

struct Vector
{
  size_t capacity;
  size_t size;
  void** data;
};

struct Vector *vector_create(size_t capacity);
void vector_push(struct Vector *vector, void* data);
void vector_delete(struct Vector *vector);
void* vector_get(struct Vector *vector, size_t index);
