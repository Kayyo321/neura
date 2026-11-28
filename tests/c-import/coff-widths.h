#ifndef NEURA_TEST_COFF_WIDTHS_H
#define NEURA_TEST_COFF_WIDTHS_H

#include <stddef.h>

long coff_signed_long(long value);
unsigned long coff_unsigned_long(unsigned long value);
wchar_t coff_wide_char(wchar_t value);
size_t coff_size(size_t value);

#endif
