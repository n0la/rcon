#ifndef SOURCE_RCON_H
#define SOURCE_RCON_H

#include <stdint.h>
#include <stdlib.h>
#include "rcon.h"

typedef enum {
    serverdata_auth = 3,
    serverdata_auth_response = 2,
    serverdata_command = 2,
    serverdata_value = 0,
} src_rcon_type_t;

typedef struct _src_rcon src_rcon_t;

typedef struct {
    int32_t size;
    int32_t id;
    int32_t type;
    uint8_t *body;
    uint8_t null;
} src_rcon_message_t;

extern src_rcon_t *src_rcon_new(void);
extern void src_rcon_free(src_rcon_t *msg);

extern src_rcon_message_t *src_rcon_message_new(void);
extern void src_rcon_message_free(src_rcon_message_t *m);
extern void src_rcon_message_freev(src_rcon_message_t **msg);

extern src_rcon_message_t *src_rcon_command(src_rcon_t *r, char const *cmd);
extern rcon_error_t src_rcon_command_wait(src_rcon_t *r,
                                          src_rcon_message_t const *cmd,
                                          src_rcon_message_t ***replies,
                                          size_t *off, void const *buf,
                                          size_t size);

extern src_rcon_message_t *src_rcon_auth(src_rcon_t *r, char const *password);
extern rcon_error_t src_rcon_auth_wait(src_rcon_t *r,
                                       src_rcon_message_t const *auth,
                                       size_t *off,
                                       void const *buf, size_t sz);

extern rcon_error_t src_rcon_serialize(src_rcon_t *r,
                                       src_rcon_message_t const *m,
                                       uint8_t **buf, size_t *sz);

extern rcon_error_t src_rcon_deserialize(src_rcon_t *r,
                                         src_rcon_message_t ***msg, size_t *off,
                                         size_t *count, void const *buf,
                                         size_t sz);

#endif
