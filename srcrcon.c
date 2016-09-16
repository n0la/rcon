#include "rcon.h"
#include "srcrcon.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

static void src_rcon_update_size(src_rcon_message_t *m);
static void src_rcon_random_id(src_rcon_message_t *m);

void src_rcon_free(src_rcon_message_t *msg)
{
    return_if_true(msg == NULL,);

    free(msg->body);
    free(msg);
}

void src_rcon_freev(src_rcon_message_t **m)
{
    src_rcon_message_t **i = NULL;

    return_if_true(m == NULL,);

    for (i = m; *i != NULL; i++) {
        src_rcon_free(*i);
    }

    free(m);
}

src_rcon_message_t *src_rcon_new(void)
{
    src_rcon_message_t *tmp = NULL;

    tmp = calloc(1, sizeof(src_rcon_message_t));
    if (tmp == NULL) {
        return NULL;
    }

    tmp->body = calloc(1, sizeof(uint8_t));
    if (tmp->body == NULL) {
        free(tmp);
        return NULL;
    }

    tmp->type = serverdata_command;
    tmp->null = '\0';
    src_rcon_random_id(tmp);
    src_rcon_update_size(tmp);

    return tmp;
}

static void src_rcon_update_size(src_rcon_message_t *m)
{
    return_if_true(m == NULL,);

    m->size = sizeof(m->id);
    m->size += sizeof(m->type);
    m->size += strlen((char const *)m->body) + 1;
    m->size += sizeof(m->null);
}

static void src_rcon_random_id(src_rcon_message_t *m)
{
#ifdef HAVE_ARC4RANDOM_UNIFORM
    m->id = (int32_t)arc4random_uniform(INT32_MAX-1);
#else
    m->id = rand() % (INT32_MAX - 1);
#endif
}

src_rcon_message_t *src_rcon_command(char const *cmd)
{
    src_rcon_message_t *msg = NULL;

    msg = src_rcon_new();
    if (msg == NULL) {
        return NULL;
    }

    msg->type = serverdata_command;
    msg->body = (uint8_t*)strdup(cmd);
    if (msg->body == NULL) {
        src_rcon_free(msg);
        return NULL;
    }

    src_rcon_update_size(msg);

    return msg;
}

int src_rcon_command_valid(src_rcon_message_t const *request,
                          src_rcon_message_t **reply)
{
    src_rcon_message_t *p = NULL;

    return_if_true(reply == NULL, -1);
    return_if_true(*reply == NULL, -1);

    p = *reply;

    if (p->type != serverdata_command) {
        return -1;
    }

    if (p->id != request->id) {
        return -1;
    }

    return 0;
}

src_rcon_message_t *src_rcon_auth(char const *password)
{
    src_rcon_message_t *msg = NULL;

    msg = src_rcon_new();
    if (msg == NULL) {
        return NULL;
    }

    msg->type = serverdata_auth;
    msg->body = (uint8_t*)strdup(password);
    if (msg->body == NULL) {
        src_rcon_free(msg);
        return NULL;
    }

    src_rcon_update_size(msg);

    return msg;
}

int src_rcon_auth_valid(src_rcon_message_t const *request,
                        src_rcon_message_t **reply)
{
    src_rcon_message_t **p = NULL;
    int count = 0;

    for (p = reply; *p != NULL && count < 2; p++, ++count) {
        if ((reply[count])->id != request->id) {
            return -1;
        }
        switch (count) {
        case 0:
        {
            if (reply[count]->type != serverdata_value) {
                return -1;
            }
        } break;

        case 1:
        {
            if (reply[count]->type != serverdata_auth_response) {
                return -1;
            }
        } break;

        }
    }

    if (count < 1) {
        return -1;
    }

    return 0;
}

int src_rcon_serialize(src_rcon_message_t const *m,
                       uint8_t **buf, size_t *sz)
{
    uint8_t *tmp = NULL;
    size_t size = 0;
    FILE *str = NULL;

    return_if_true(m == NULL, -1);
    return_if_true(buf == NULL, -1);
    return_if_true(sz == NULL, -1);

    str = open_memstream((char**)&tmp, &size);
    if (str == NULL) {
        return -2;
    }

    fwrite(&m->size, 1, sizeof(m->size), str);
    fwrite(&m->id, 1, sizeof(m->id), str);
    fwrite(&m->type, 1, sizeof(m->id), str);
    if (m->body != NULL) {
        fwrite(m->body, 1, strlen((char const *)m->body), str);
    }
    fwrite(&m->null, 1, sizeof(m->null), str);
    fwrite(&m->null, 1, sizeof(m->null), str);

    fclose(str);

    *buf = tmp;
    *sz = size;

    return 0;
}

int src_rcon_deserialize(src_rcon_message_t ***msg, size_t *off,
                         void const *buf, size_t sz)
{
    int count = 1;
    FILE *str = NULL;
    src_rcon_message_t **res = NULL;

    return_if_true(msg == NULL, -1);
    return_if_true(off == NULL, -1);
    return_if_true(buf == NULL, -1);
    return_if_true(sz == 0, -1);

    str = fmemopen((char*)buf, sz, "r");
    if (str == NULL) {
        return -2;
    }

    do {
        src_rcon_message_t *m = NULL;
        src_rcon_message_t **tmp = NULL;
        size_t bufsize = 0;

        m = src_rcon_new();

        if (fread(&m->size, 1, sizeof(m->size), str) < sizeof(m->size)) {
            src_rcon_free(m);
            break;
        }

        if (fread(&m->id, 1, sizeof(m->id), str) < sizeof(m->id)) {
            src_rcon_free(m);
            break;
        }

        if (fread(&m->type, 1, sizeof(m->type), str) < sizeof(m->type)) {
            src_rcon_free(m);
            break;
        }

        bufsize = m->size - sizeof(m->id) - sizeof(m->type) - sizeof(m->null);
        m->body = calloc(1, bufsize+1);
        if (m->body == NULL) {
            src_rcon_free(m);
            return -2;
        }

        if (fread(m->body, 1, bufsize, str) < bufsize) {
            src_rcon_free(m);
            break;
        }

        if (fread(&m->null, 1, sizeof(m->null), str) < sizeof(m->null)) {
            src_rcon_free(m);
            break;
        }

        ++count;

        tmp = realloc(res, count * sizeof(src_rcon_message_t*));
        if (tmp == NULL) {
            src_rcon_freev(res);
            return -2;
        }
        res = tmp;

        tmp[count-2] = m;
        tmp[count-1] = NULL;
    } while(true);

    *off = ftell(str);
    fclose(str);

    if (res != NULL) {
        *msg = res;
        return 0;
    }

    return -3;
}
