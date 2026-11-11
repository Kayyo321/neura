/* Nullability qualifiers (Apple's headers put them on pointers), a
   function-pointer parameter, and a declaration that comes from an included
   header. Clang accepts the qualifiers on every platform. The functions are
   the C library's own, so the import needs no library of its own. */
#include <stddef.h>
#include "nullability-inner.h"

size_t strlen(const char *_Nonnull text);
void *_Nullable memchr(const void *_Nonnull base, int value, size_t count);
void qsort(void *_Nonnull base, size_t count, size_t width,
           int (*_Nonnull compare)(const void *_Nullable, const void *_Nullable));
