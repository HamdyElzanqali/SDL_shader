#include "vector.h"

#include <SDL3/SDL_stdinc.h>

struct Vector *vector_create(size_t capacity)
{
  struct Vector *vector = SDL_malloc(sizeof(struct Vector));
  vector->capacity = capacity;
  vector->size = 0;
  vector->data = SDL_malloc(capacity * sizeof(void*));
  return vector;
}

void vector_push(struct Vector *vector, void* data)
{
  // resize vector
  if (vector->size == vector->capacity)
  {
    vector->capacity *= 2;
    void* new_data = SDL_malloc(vector->capacity * sizeof(void*));
    SDL_memcpy(new_data, vector->data, vector->size * sizeof(void*));
    SDL_free(vector->data);
    vector->data = new_data;
  }

  vector->data[vector->size] = data;
  vector->size += 1;
}

void vector_delete(struct Vector *vector)
{
  SDL_free(vector->data);
  SDL_free(vector);
}

void* vector_get(struct Vector *vector, size_t index)
{
  if (index >= vector->size)
  {
    return NULL;
  }

  return vector->data[index];
}
