#ifndef RCON_MEMSTREAM_H
#define RCON_MEMSTREAM_H

#include "sysconfig.h"

#ifndef HAVE_OPEN_MEMSTREAM
#include <stdio.h>

extern FILE *open_memstream(char **ptr, size_t *sizeloc);
#endif

#endif
