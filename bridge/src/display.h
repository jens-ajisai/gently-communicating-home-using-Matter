#pragma once


#ifdef __cplusplus
extern "C" {
#endif

int display_init(void);
void display_updateTranscription(const char* text);
void display_updateCompletions(const char* text);

void* display_malloc(size_t size);
void display_free(void* ptr);
void* display_realloc(void* ptr, size_t new_size);

#ifdef __cplusplus
}
#endif