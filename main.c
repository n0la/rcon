#include "rcon.h"
#include "config.h"
#include "srcrcon.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

static char *host = NULL;
static char *password = NULL;
static char *port = NULL;
static char *config = NULL;
static char *server = NULL;

static void cleanup(void)
{
    config_free();

    free(host);
    free(password);
    free(port);
    free(config);
    free(server);
}

static void usage(void)
{
    puts("");
    puts("Usage:");
    puts(" rcon [options] command");
    puts("");
    puts("Options:");
    puts(" -c, --config     Alternate configuration file");
    puts(" -h, --help       This bogus");
    puts(" -H, --host       Host name or IP");
    puts(" -P, --password   RCON Password");
    puts(" -p, --port       Port or service");
    puts(" -s, --server     Use this server from config file");
}

static int parse_args(int ac, char **av)
{
    static struct option opts[] = {
        { "config", required_argument, 0, 'c' },
        { "help", no_argument, 0, 'h' },
        { "host", required_argument, 0, 'H' },
        { "password", required_argument, 0, 'P' },
        { "port", required_argument, 0, 'p' },
        { "server", required_argument, 0, 's' },
        { NULL, 0, 0, 0 }
    };

    static char const *optstr = "c:Hh:P:p:s:";

    int c = 0;

    while ((c = getopt_long(ac, av, optstr, opts, NULL)) != -1) {
        switch (c)
        {
        case 'c': free(config); config = strdup(optarg); break;
        case 'H': free(host); host = strdup(optarg); break;
        case 'p': free(port); port = strdup(optarg); break;
        case 'P': free(password); password = strdup(optarg); break;
        case 's': free(server); server = strdup(optarg); break;
        case 'h': usage(); exit(0); break;
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

static int send_command(int sock, char const *cmd)
{
    src_rcon_message_t *command = NULL;
    src_rcon_message_t **commandanswers = NULL;
    src_rcon_message_t **p = NULL;
    int ec = -1;

    /* Send command
     */
    command = src_rcon_command(cmd);
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
        fprintf(stdout, "%s", (char const*)(*p)->body);
        fflush(stdout);
    }

    ec = 0;

cleanup:

    src_rcon_free(command);
    src_rcon_freev(commandanswers);

    return ec;
}

static int handle_arguments(int sock, int ac, char **av)
{
    char *c = NULL;
    size_t size = 0;
    FILE *cmd = NULL;
    int i = 0;

    cmd = open_memstream(&c, &size);
    if (cmd == NULL) {
        return -1;
    }

    for (i = 0; i < ac; i++) {
        if (i > 0) {
            fputc(' ', cmd);
        }
        fprintf(cmd, "%s", av[i]);
    }
    fclose(cmd);

    if (send_command(sock, c)) {
        free(c);
        return -1;
    }

    free(c);

    return 0;
}

static int handle_stdin(int sock)
{
    char *line = NULL;
    size_t sz = 0;
    int read = 0;
    int ec = 0;

    while ((read = getline(&line, &sz, stdin)) != -1) {
        char *cmd = line;

        /* Strip away \n
         */
        line[read-1] = '\0';

        while (*cmd != '\0' && isspace(*cmd)) {
            ++cmd;
        }

        /* Comment or empty line
         */
        if (cmd[0] == '\0' || cmd[0] == '#') {
            continue;
        }

        if (send_command(sock, cmd)) {
            ec = -1;
            break;
        }
    }

    free(line);

    return ec;
}

int do_config(void)
{
    if (server == NULL) {
        return 0;
    }

    if (config == NULL) {
        char const *home = getenv("HOME");
        size_t sz = 0;

        if (home == NULL) {
            fprintf(stderr, "Neither config file nor $HOME is set\n");
            return 4;
        }

        sz = strlen(home) + 10;
        config = calloc(1, sz);
        if (config == NULL) {
            return 4;
        }

        g_strlcpy(config, getenv("HOME"), sz);
        g_strlcat(config, "/.rconrc", sz);
    }

    if (config_load(config)) {
        return 2;
    }

    free(host);
    free(port);
    free(password);

    if (config_host_data(server, &host, &port, &password)) {
        fprintf(stderr, "Server %s not found in configuration\n", server);
        return 2;
    }

    return 0;
}

int main(int ac, char **av)
{
    struct addrinfo *info = NULL, *ai = NULL;
    src_rcon_message_t *auth = NULL;
    src_rcon_message_t **authanswers = NULL;
    int sock = 0;
    int ret = 0;
    int ec = 3;

    atexit(cleanup);

    parse_args(ac, av);
    if (do_config()) {
        return 2;
    }
    /* Now parse arguments *again*. This allows for overrides on the command
     * line.
     */
    optind = 1;
    parse_args(ac, av);


    ac -= optind;
    av += optind;

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

    if (ac > 0) {
        if (handle_arguments(sock, ac, av)) {
            goto cleanup;
        }
    } else {
        if (handle_stdin(sock)) {
            goto cleanup;
        }
    }


    ec = 0;

cleanup:

    src_rcon_free(auth);
    src_rcon_freev(authanswers);

    if (sock > -1) {
        close(sock);
    }

    if (info) {
        freeaddrinfo(info);
    }

    return ec;
}
