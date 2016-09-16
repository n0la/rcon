#include "rcon.h"
#include "srcrcon.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

static char *host = NULL;
static char *password = NULL;
static char *port = NULL;

static void usage(void)
{
    puts("rcon [options] command");
    puts("options:");
    puts("  -H --help       This bougs");
    puts("  -h --host       Host name or IP");
    puts("  -P --password   RCON Password");
    puts("  -p --port       Port or service");
}

static int parse_args(int ac, char **av)
{
    static struct option opts[] = {
        { "help", no_argument, 0, 'H' },
        { "host", required_argument, 0, 'h' },
        { "password", required_argument, 0, 'P' },
        { "port", required_argument, 0, 'p' },
        { NULL, 0, 0, 0 }
    };

    static char const *optstr = "Hh:P:p:";

    int c = 0;

    while ((c = getopt_long(ac, av, optstr, opts, NULL)) != -1) {
        switch (c)
        {
        case 'h': free(host); host = strdup(optarg); break;
        case 'p': free(port); port = strdup(optarg); break;
        case 'P': free(password); password = strdup(optarg); break;
        case 'H': usage(); exit(0); break;
        default: /* intentional */
        case '?': usage(); exit(1); break;
        }
    }

    return 0;
}

static int send_message(int sock, src_rcon_message_t *msg)
{
    uint8_t *data = NULL;
    uint8_t *p = NULL;
    size_t size = 0;
    int ret = 0;

    if (src_rcon_serialize(msg, &data, &size)) {
        return -1;
    }

    p = data;
    do {
        ret = write(sock, p, size);
        if (ret == 0 || ret < 0) {
            free(data);
            fprintf(stderr, "Failed to communicate: %s\n", strerror(errno));
            return -2;
        }

        p += ret;
        size -= ret;
    } while (size > 0);

    free(data);

    return 0;
}

static int recv_messages(int sock, src_rcon_message_t ***msgs)
{
    char *buf = NULL;
    size_t sz = 0;
    FILE *mem = NULL;
    char tmp[512];
    int ret = 0;
    size_t off = 0;

    mem = open_memstream(&buf, &sz);
    if (mem == NULL) {
        return -1;
    }

    do {
        ret = read(sock, tmp, sizeof(tmp));
        if (ret < 0) {
            fclose(mem);
            free(buf);
            fprintf(stderr, "Failed to receive data: %s\n", strerror(errno));
            return -1;
        }

        fwrite(tmp, 1, ret, mem);
        if (ret < sizeof(tmp)) {
            break;
        }
    } while (true);

    fclose(mem);
    mem = NULL;

    if (src_rcon_deserialize(msgs, &off, buf, sz)) {
        free(buf);
        return -1;
    }

    free(buf);

    return 0;

}

int main(int ac, char **av)
{
    struct addrinfo *info = NULL, *ai = NULL;
    char *c = NULL;
    size_t size = 0;
    FILE *cmd = NULL;
    src_rcon_message_t *auth = NULL;
    src_rcon_message_t **authanswers = NULL;
    src_rcon_message_t *command = NULL;
    src_rcon_message_t **commandanswers = NULL;
    src_rcon_message_t **p = NULL;
    int sock = 0;
    int ret = 0, i = 0;
    int ec = 3;

    parse_args(ac, av);

    ac -= optind;
    av += optind;

    if (ac < 1) {
        fprintf(stderr, "No command given\n");
        return 1;
    }

    if (host == NULL || port == NULL) {
        fprintf(stderr, "No host and/or port specified\n");
        return 1;
    }

    if ((ret = getaddrinfo(host, port, NULL, &info))) {
        fprintf(stderr, "Failed to resolve host: %s: %s\n",
                host, gai_strerror(ret)
            );
        goto cleanup;
    }

    for (ai = info; ai != NULL; ai = ai->ai_next ) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) {
            continue;
        }

        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }

        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        fprintf(stderr, "Failed to connect to the given host/service\n");
        goto cleanup;
    }

    /* Do we have a password?
     */
    if (password != NULL && strlen(password) > 0) {
        /* Send auth request first
         */
        auth = src_rcon_auth(password);

        if (send_message(sock, auth)) {
            goto cleanup;
        }

        if (recv_messages(sock, &authanswers)) {
            goto cleanup;
        }

        if (src_rcon_auth_valid(auth, authanswers)) {
            fprintf(stderr, "Invalid auth reply, valid password?\n");
            goto cleanup;
        }
    }

    cmd = open_memstream(&c, &size);
    if (cmd == NULL) {
        goto cleanup;
    }

    for (i = 0; i < ac; i++) {
        if (i > 0) {
            fputc(' ', cmd);
        }
        fprintf(cmd, "%s", av[i]);
    }

    fclose(cmd);

    /* Send command
     */
    command = src_rcon_command(c);
    if (command == NULL) {
        goto cleanup;
    }

    if (send_message(sock, command)) {
        goto cleanup;
    }

    if (recv_messages(sock, &commandanswers)) {
        goto cleanup;
    }

    if (!src_rcon_command_valid(command, commandanswers)) {
        goto cleanup;
    }

    if (commandanswers[1] == NULL) {

        src_rcon_freev(commandanswers);
        commandanswers = NULL;

        if (recv_messages(sock, &commandanswers)) {
            goto cleanup;
        }

        p = commandanswers;
    } else {
        p = &commandanswers[1];
    }

    for (; *p != NULL; p++) {
        fprintf(stdout, "%s\n", (char const*)(*p)->body);
    }

    ec = 0;

cleanup:

    free(c);

    src_rcon_free(auth);
    src_rcon_freev(authanswers);

    src_rcon_free(command);
    src_rcon_freev(commandanswers);

    if (sock > -1) {
        close(sock);
    }

    if (info) {
        freeaddrinfo(info);
    }

    return ec;
}
