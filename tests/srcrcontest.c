#include <check.h>

#include <stdio.h>
#include <srcrcon.h>
#include <stdbool.h>

static void check_size(src_rcon_message_t const *m)
{
    size_t length = 0;

    /* Size field must be exactly this:
     */
    length = sizeof(m->id) +
        sizeof(m->type) +
        strlen((char const *)m->body) + 1 +
        sizeof(m->null);

    ck_assert_msg(length == m->size,
                  "srcrcon: size of package is not correct");
}

START_TEST(srcrcon_field_sizes)
{
    src_rcon_message_t m;

    /* Field sizes as defined by VALVE specification
     */
    ck_assert_msg(sizeof(m.size) == sizeof(uint32_t),
                  "srcrcon: size is not 4 bytes");
    ck_assert_msg(sizeof(m.id)   == sizeof(uint32_t),
                  "srcrcon: id is not 4 bytes");
    ck_assert_msg(sizeof(m.type) == sizeof(uint32_t),
                  "srcrcon: type is not 4 bytes");
    ck_assert_msg(sizeof(m.null) == sizeof(uint8_t),
                  "srcrcon: null is not 1 byte");
}
END_TEST

START_TEST(srcrcon_new_message)
{
    src_rcon_message_t *m = NULL;

    m = src_rcon_message_new();
    ck_assert_msg(m != NULL, "srcrcon: allocation failed");

    ck_assert_msg(m->null == '\0', "srcrcon: null is not \\0");

    check_size(m);

    src_rcon_message_free(m);
    m = NULL;
}
END_TEST

START_TEST(srcrcon_serialise_auth)
{
    src_rcon_t *rcon = NULL;
    src_rcon_message_t *auth = NULL;
    uint8_t *buf = NULL;
    size_t sz = 0;
    rcon_error_t err = 0;

    rcon = src_rcon_new();
    ck_assert_msg(rcon != NULL, "rcon: allocation failed");

    auth = src_rcon_auth(rcon, "test");
    ck_assert_msg(auth != NULL, "srcrcon: auth message allocation failed");

    err = src_rcon_serialize(rcon, auth, &buf, &sz, false);
    ck_assert_msg(err == rcon_error_success,
                  "srcrcon: auth message serializing failed");

    ck_assert_msg(auth->type == serverdata_auth,
                  "srcrcon: type is not serverdata_auth");

    check_size(auth);

    /* One message so the buffer must as big as packet + size field
     */
    ck_assert_msg(sz == (auth->size + sizeof(auth->size)),
                  "srcrcon: output size differs from packet size");

    /* Check contents of message
     */
    ck_assert_msg(((int32_t*)buf)[0] == auth->size,
                  "srcrcon: first int32_t field is not size");
    ck_assert_msg(((int32_t*)buf)[1] == auth->id,
                  "srcrcon: second int32_t field is not the id");
    ck_assert_msg(((int32_t*)buf)[2] == auth->type,
                  "srcrcon: third int32_t field is not the type");

    ck_assert_msg(strcmp((char const *)buf + 3 * sizeof(uint32_t), "test") == 0,
                  "srcrcon: no payload found");

    ck_assert_msg(buf[sz-1] == '\0', "srcrcon: last byte is not \\0");
}
END_TEST

START_TEST(srcrcon_serialise_command)
{
    src_rcon_t *rcon = NULL;
    src_rcon_message_t *cmd = NULL;
    uint8_t *buf = NULL;
    size_t sz = 0;
    rcon_error_t err = 0;
    size_t mk = 0;

    rcon = src_rcon_new();
    ck_assert_msg(rcon != NULL, "rcon: allocation failed");

    cmd = src_rcon_command(rcon, "asdf");
    ck_assert_msg(cmd != NULL, "srcrcon: command message allocation failed");

    err = src_rcon_serialize(rcon, cmd, &buf, &sz, false);
    ck_assert_msg(err == rcon_error_success,
                  "srcrcon: command message serializing failed");

    ck_assert_msg(cmd->type == serverdata_command,
                  "srcrcon: type is not serverdata_command");

    /* Check contents of message
     */
    ck_assert_msg(((int32_t*)buf)[0] == cmd->size,
                  "srcrcon: first int32_t field is not size");
    ck_assert_msg(((int32_t*)buf)[1] == cmd->id,
                  "srcrcon: second int32_t field is not the id");
    ck_assert_msg(((int32_t*)buf)[2] == cmd->type,
                  "srcrcon: third int32_t field is not the type");

    mk = 3 * sizeof(uint32_t);
    ck_assert_msg(strcmp((char const *)buf + mk, "asdf") == 0,
                  "srcrcon: no payload found");
    mk += strlen("asdf") + 1;

    ck_assert_msg(buf[mk] == '\0', "srcrcon: last byte is not \\0");
    mk += 1;

    ck_assert_msg(mk < sz, "srcrcon: command has no end marker");

    /* Move pointer forward
     */
    buf = buf + mk;

    /* Check for end marker
     */

    /* Check end marker. Size is always 10: (4 * 3 + 2) = 14 - sizeof(size)
     */
    ck_assert_msg(((int32_t*)buf)[0] == 10,
                  "srcrcon: endmarker's first int32_t field is not size");
    /* End marker ID must be the same as the command id
     */
    ck_assert_msg(((int32_t*)buf)[1] == cmd->id,
                  "srcrcon: endmarker's second int32_t field is not the id");
    ck_assert_msg(((int32_t*)buf)[2] == serverdata_value,
                  "srcrcon: endmarker's third int32_t field is not the type");

    mk = sizeof(uint32_t) * 3;
    ck_assert_msg(mk <= (sz-2), "srcrcon: invalid remaining length");

    ck_assert_msg(strcmp((char const *)buf+mk, "") == 0,
                  "srcrcon: endmarker's data is not empty");
    mk += 1;

    ck_assert_msg(buf[mk] == '\0',
                  "srcrcon: endmarker's end marker is not \\0");

    mk += 1;

    ck_assert_msg(mk == 14, "srcrcon: endmarker: did not reach end");
}
END_TEST

static void
srcrcon_check_short(void const *data, size_t size)
{
    src_rcon_t *r = NULL;
    src_rcon_message_t **msgs = NULL;
    size_t off = 0, sz = 0;
    rcon_error_t e;

    r = src_rcon_new();
    ck_assert_msg(r != NULL, "rcon: allocation error");

    e = src_rcon_deserialize(r, &msgs, &off, &sz, data, size);
    ck_assert_msg(e == rcon_error_moredata,
                  "srcrcon: deserialize: didn't report more data");

    ck_assert_msg(msgs == NULL,
                  "srcrcon: deserialize: returned data");

    ck_assert_msg(off == 0,
                  "srcrcon: deserialize; reported offset");

    ck_assert_msg(sz == 0,
                  "srcrcon: deserialize: reported returned data");
}

START_TEST(srcrcon_deserialise_short)
{
    static char const *short1 = "\x00\x0A\0x00\x02";
    static char const *short2 =
        "\xE0\x00\x00\x00"
        "\x11\x11\x11\x11"
        "\x02\x00\x00\x00"
        "\x00";

    srcrcon_check_short(short1, 4);
    srcrcon_check_short(short2, 13);
}
END_TEST

START_TEST(srcrcon_deserialise_leftover)
{
    static char const *leftover =
        "\x0A\x00\x00\x00"
        "\x11\x11\x11\x11"
        "\x02\x00\x00\x00"
        "\x00\x00" /* complete message */
        "\x00\x00" /* new message */
        ;
    static const size_t size = 16;

    src_rcon_t *r = NULL;
    src_rcon_message_t **msgs = NULL;
    size_t off = 0, sz = 0;
    rcon_error_t e;

    r = src_rcon_new();
    ck_assert_msg(r != NULL, "rcon: allocation error");

    e = src_rcon_deserialize(r, &msgs, &off, &sz, leftover, size);

    /* As it can read one message it must return error_success
     */
    ck_assert_msg(e == rcon_error_success,
                  "srcrcon: deserialize: didn't report success");

    ck_assert_msg(msgs != NULL,
                  "srcrcon: deserialize: didn't return data");

    ck_assert_msg(off == 14,
                  "srcrcon: deserialize; reported wrong offset");

    ck_assert_msg(sz == 1,
                  "srcrcon: deserialize: reported returned data");
}
END_TEST

START_TEST(srcrcon_deserialise_leftover2)
{
    static char const *leftover =
        "\x0A\x00\x00\x00"
        "\x11\x11\x11\x11"
        "\x02\x00\x00\x00"
        "\x00\x00" /* complete message */
        "\x0A\x00\x00\x00"
        "\x11\x11\x11\x11"
        "\x02\x00\x00\x00"
        "\x00\x00" /* complete message */
        "\x00\x00\x00" /* new message */
        ;
    static const size_t size = 31;

    src_rcon_t *r = NULL;
    src_rcon_message_t **msgs = NULL;
    size_t off = 0, sz = 0;
    rcon_error_t e;

    r = src_rcon_new();
    ck_assert_msg(r != NULL, "rcon: allocation error");

    e = src_rcon_deserialize(r, &msgs, &off, &sz, leftover, size);

    /* As it can read one message it must return error_success
     */
    ck_assert_msg(e == rcon_error_success,
                  "srcrcon: deserialize: didn't report success");

    ck_assert_msg(msgs != NULL,
                  "srcrcon: deserialize: didn't return data");

    ck_assert_msg(off == 28,
                  "srcrcon: deserialize; reported wrong offset");

    ck_assert_msg(sz == 2,
                  "srcrcon: deserialize: reported returned data");
}
END_TEST


START_TEST(srcrcon_deserialise_correct)
{
    static char const *leftover =
        "\x0A\x00\x00\x00"
        "\x11\x00\x00\x00"
        "\x02\x00\x00\x00"
        "\x00\x00" /* complete message */
        ;
    static const size_t size = 14;

    src_rcon_t *r = NULL;
    src_rcon_message_t **msgs = NULL;
    size_t off = 0, sz = 0;
    rcon_error_t e;

    r = src_rcon_new();
    ck_assert_msg(r != NULL, "rcon: allocation error");

    e = src_rcon_deserialize(r, &msgs, &off, &sz, leftover, size);

    /* As it can read one message it must return error_success
     */
    ck_assert_msg(e == rcon_error_success,
                  "srcrcon: deserialize: didn't report success");

    ck_assert_msg(msgs != NULL,
                  "srcrcon: deserialize: didn't return data");

    ck_assert_msg(off == 14,
                  "srcrcon: deserialize; reported wrong offset");

    ck_assert_msg(sz == 1,
                  "srcrcon: deserialize: reported returned data");

    ck_assert_msg(msgs[0]->size == 10,
                  "srcrcon: deserialize: size is not correct");
    ck_assert_msg(msgs[0]->id == 17,
                  "srcrcon: deserialize: id is not correct");
    ck_assert_msg(msgs[0]->type == 2,
                  "srcrcon: deserialize: type is not correct");
    ck_assert_msg(strcmp((char const *)msgs[0]->body, "") == 0,
                  "srcrcon: deserialize: body is not correct");
    ck_assert_msg(msgs[0]->null == '\0',
                  "srcrcon: deserialize: end marker is not correct");
}
END_TEST

START_TEST(srcrcon_deserialise_body)
{
    static char const *leftover =
        "\x15\x00\x00\x00"
        "\x11\x00\x00\x00"
        "\x00\x00\x00\x00"
        "hello world\x00\x00" /* complete message */
        ;
    static const size_t size = 25;

    src_rcon_t *r = NULL;
    src_rcon_message_t **msgs = NULL;
    size_t off = 0, sz = 0;
    rcon_error_t e;

    r = src_rcon_new();
    ck_assert_msg(r != NULL, "rcon: allocation error");

    e = src_rcon_deserialize(r, &msgs, &off, &sz, leftover, size);

    /* As it can read one message it must return error_success
     */
    ck_assert_msg(e == rcon_error_success,
                  "srcrcon: deserialize: didn't report success");

    ck_assert_msg(msgs != NULL,
                  "srcrcon: deserialize: didn't return data");

    ck_assert_msg(off == 25,
                  "srcrcon: deserialize; reported wrong offset");

    ck_assert_msg(sz == 1,
                  "srcrcon: deserialize: reported returned data");

    ck_assert_msg(msgs[0]->size == 0x15,
                  "srcrcon: deserialize: size is not correct");
    ck_assert_msg(msgs[0]->id == 17,
                  "srcrcon: deserialize: id is not correct");
    ck_assert_msg(msgs[0]->type == 0,
                  "srcrcon: deserialize: type is not correct");
    ck_assert_msg(strcmp((char const *)msgs[0]->body, "hello world") == 0,
                  "srcrcon: deserialize: body is not correct");
    ck_assert_msg(msgs[0]->null == '\0',
                  "srcrcon: deserialize: end marker is not correct");
}
END_TEST


int main(int ac, char **av)
{
    Suite *s = NULL;
    SRunner *r = NULL;
    TCase *c = NULL;
    int failed = 0;

    /* Don't fork
     */
    putenv("CK_FORK=no");

    s = suite_create("rcon");

    c = tcase_create("srcrcon");

    tcase_add_test(c, srcrcon_new_message);
    tcase_add_test(c, srcrcon_field_sizes);

    tcase_add_test(c, srcrcon_serialise_auth);
    tcase_add_test(c, srcrcon_serialise_command);

    tcase_add_test(c, srcrcon_deserialise_short);
    tcase_add_test(c, srcrcon_deserialise_leftover);
    tcase_add_test(c, srcrcon_deserialise_leftover2);
    tcase_add_test(c, srcrcon_deserialise_correct);
    tcase_add_test(c, srcrcon_deserialise_body);

    suite_add_tcase(s, c);

    r = srunner_create(s);
    srunner_run_all(r, CK_NORMAL);
    failed = srunner_ntests_failed(r);

    srunner_free(r);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
