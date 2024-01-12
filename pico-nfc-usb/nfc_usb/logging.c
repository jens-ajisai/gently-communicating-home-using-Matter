#include "logging.h"

#include <stdarg.h>
#include <stdio.h>

int logging(const char* fmt, ...) {
  va_list ap;
  int res = 0;

  if (ENABLE_LOGGING) {
    va_start(ap, fmt);
    res = vprintf(fmt, ap);
    va_end(ap);
  }
  return res;
}
