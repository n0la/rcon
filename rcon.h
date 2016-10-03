#ifndef RCON_H
#define RCON_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define return_if_true(a,v) do { if (a) return v; } while(0)

typedef enum {
    rcon_error_success = 0,
    rcon_error_moredata,
    rcon_error_auth,
    rcon_error_unspecified,
    rcon_error_protocol,
    rcon_error_internal,
    rcon_error_args,
    rcon_error_memory,
} rcon_error_t;

#endif
