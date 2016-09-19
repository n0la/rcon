#ifndef SOURCE_RCON_H
#define SOURCE_RCON_H

#include <stdint.h>
#include <stdlib.h>

typedef enum {
    serverdata_auth = 3,
    serverdata_auth_response = 2,
    serverdata_command = 2,
    serverdata_value = 0,
} src_rcon_type_t;

typedef struct {
    int32_t size;
    int32_t id;
    int32_t type;
    uint8_t *body;
    uint8_t null;
} src_rcon_message_t;

typedef enum {
    src_rcon_success = 0,
    src_rcon_moredata = 1,
    src_rcon_authinvalid = 2,
    src_rcon_unspecified = 3,
    src_rcon_protocolerror = 4,
} src_rcon_error_t;

extern src_rcon_message_t *src_rcon_new(void);
extern void src_rcon_free(src_rcon_message_t *msg);
extern void src_rcon_freev(src_rcon_message_t **msg);

extern src_rcon_message_t *src_rcon_command(char const *cmd);
extern src_rcon_message_t *src_rcon_end_marker(src_rcon_message_t const *cmd);
extern src_rcon_error_t src_rcon_command_wait(src_rcon_message_t const *cmd,
                                              src_rcon_message_t ***replies,
                                              size_t *off, void const *buf,
                                              size_t size);

extern src_rcon_message_t *src_rcon_auth(char const *password);
extern src_rcon_error_t src_rcon_auth_wait(src_rcon_message_t const *auth,
                                           size_t *off,
                                           void const *buf, size_t sz);

extern int src_rcon_serialize(src_rcon_message_t const *m,
                              uint8_t **buf, size_t *sz);

extern int src_rcon_deserialize(src_rcon_message_t ***msg, size_t *off,
                                size_t *count, void const *buf, size_t sz);

#endif
