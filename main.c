#include "rcon.h"
#include "config.h"
#include "srcrcon.h"
#include "sysconfig.h"
#include "memstream.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <err.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

static char *host = NULL;
static char *password = NULL;
static char *port = NULL;
static char *config = NULL;
static char *server = NULL;
static bool debug = false;
static bool nowait = false;
static bool minecraft = false;

static GByteArray *response = NULL;
static src_rcon_t *r = NULL;

static void cleanup(void)
{
    config_free();

    src_rcon_free(r);

    free(host);
    free(password);
    free(port);
    free(config);
    free(server);

    if (response) {
        g_byte_array_free(response, TRUE);
    }
}

static void usage(void)
{
    puts("");
    puts("Usage:");
    puts(" rcon [options] command");
    puts("");
    puts("Options:");
    puts(" -c, --config     Alternate configuration file");
    puts(" -d, --debug      Debug output");
    puts(" -h, --help       This bogus");
    puts(" -H, --host       Host name or IP");
    puts(" -m, --minecraft  Minecraft mode");
    puts(" -n, --nowait     Don't wait for reply from server for commands.");
    puts(" -P, --password   RCON Password");
    puts(" -p, --port       Port or service");
    puts(" -s, --server     Use this server from config file");
    puts(" -1, --1packet    Unused, backward compability");
}

static int parse_args(int ac, char **av)
{
    static struct option opts[] = {
        { "config", required_argument, 0, 'c' },
        { "debug", no_argument, 0, 'd' },
        { "help", no_argument, 0, 'h' },
        { "host", required_argument, 0, 'H' },
        { "minecraft", no_argument, 0, 'm' },
        { "nowait", no_argument, 0, 'n' },
        { "password", required_argument, 0, 'P' },
        { "port", required_argument, 0, 'p' },
        { "server", required_argument, 0, 's' },
        { "1packet", no_argument, 0, '1' },
        { NULL, 0, 0, 0 }
    };

    static char const *optstr = "c:dH:hmnP:p:s:1";

    int c = 0;

    while ((c = getopt_long(ac, av, optstr, opts, NULL)) != -1) {
        switch (c)
        {
        case 'c': free(config); config = strdup(optarg); break;
        case 'd': debug = true; break;
        case 'H': free(host); host = strdup(optarg); break;
        case 'm': minecraft = true; break;
        case 'p': free(port); port = strdup(optarg); break;
        case 'P': free(password); password = strdup(optarg); break;
        case 's': free(server); server = strdup(optarg); break;
        case 'n': nowait = true; break;
        case '1': /* backward compability */ break;
        case 'h': usage(); exit(0); break;
        default: /* intentional */
        case '?': usage(); exit(1); break;
        }
    }

    return 0;
}

static void debug_dump(bool in, uint8_t const *data, size_t sz)
{
    size_t i = 0;

    if (!debug) {
        return;
    }

    printf("%s ", (in ? ">>" : "<<"));

    for (i = 0; i < sz; i++) {
        if (isprint((int)data[i])) {
            fputc((int)data[i], stdout);
        } else {
            printf("0x%.2X", (int)data[i]);
        }
        if (i < sz-1) {
            printf(",");
        }
    }

    printf("\n");
}

static int send_message(int sock, src_rcon_message_t *msg)
{
    uint8_t *data = NULL;
    uint8_t *p = NULL;
    size_t size = 0;
    int ret = 0;

    if (src_rcon_serialize(r, msg, &data, &size)) {
        return -1;
    }

    debug_dump(false, data, size);

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

static int wait_auth(int sock, src_rcon_message_t *auth)
{
    uint8_t tmp[512];
    int ret = 0;
    rcon_error_t status;
    size_t off = 0;

    do {
        ret = read(sock, tmp, sizeof(tmp));
        if (ret < 0) {
            fprintf(stderr, "Failed to receive data: %s\n", strerror(errno));
            return -1;
        }

        debug_dump(true, tmp, ret);

        g_byte_array_append(response, tmp, ret);

        status = src_rcon_auth_wait(r, auth, &off,
                                    response->data, response->len
            );
        if (status != rcon_error_moredata) {
            g_byte_array_remove_range(response, 0, off);
            return (int)status;
        }
    } while (true);

    return 1;
}

static int send_command(int sock, char const *cmd)
{
    src_rcon_message_t *command = NULL, *end = NULL;
    src_rcon_message_t **commandanswers = NULL;
    src_rcon_message_t **p = NULL;
    uint8_t tmp[512];
    int ret = 0;
    rcon_error_t status;
    size_t off = 0;
    int ec = -1;
    bool done = false;

    /* Send command
     */
    command = src_rcon_command(r, cmd);
    if (command == NULL) {
        goto cleanup;
    }

    if (send_message(sock, command)) {
        goto cleanup;
    }

    if (nowait == true) {
        goto cleanup;
    }

    if (!minecraft) {
        /* minecraft does not like the empty command at the end.
         * it will abort the connection if it finds an empty command
         * and we get no answer back.
         */
        end = src_rcon_command(r, "");
        if (end == NULL) {
            goto cleanup;
        }
        if (send_message(sock, end)) {
            goto cleanup;
        }
    }

    do {
        ret = read(sock, tmp, sizeof(tmp));
        if (ret < 0) {
            fprintf(stderr, "Failed to receive data: %s\n", strerror(errno));
            return -1;
        }

        if (ret == 0) {
            fprintf(stderr, "Peer: connection closed\n");
            done = true;
        }

        g_byte_array_append(response, tmp, ret);
        status = src_rcon_command_wait(r, command, &commandanswers, &off,
                                       response->data, response->len
            );
        if (status != rcon_error_moredata) {
            g_byte_array_remove_range(response, 0, off);
        }

        if (status == rcon_error_success) {
            if (commandanswers != NULL) {
                for (p = commandanswers; *p != NULL; p++) {
                    if (!minecraft && (*p)->id == end->id) {
                        done = true;
                    } else {
                        size_t bodylen = strlen((char const*)(*p)->body);

                        fprintf(stdout, "%s", (char const*)(*p)->body);

                        if (bodylen > 0 && (*p)->body[bodylen-1] != '\n') {
                            fprintf(stdout, "\n");
                        }

                        /* in minecraft mode we are done after the first message
                         */
                        if (minecraft) {
                            done = true;
                        }
                    }
                }
            }
        }

        src_rcon_message_freev(commandanswers);
        commandanswers = NULL;
    } while (!done);

    ec = 0;

cleanup:

    src_rcon_message_free(command);
    src_rcon_message_free(end);
    src_rcon_message_freev(commandanswers);

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

    if (config_host_data(server, &host, &port, &password, &minecraft)) {
        fprintf(stderr, "Server %s not found in configuration\n", server);
        return 2;
    }

    return 0;
}

int main(int ac, char **av)
{
    struct addrinfo *info = NULL, *ai = NULL, hint = {0};
    struct sockaddr_in *addr;
    src_rcon_message_t *auth = NULL;
    src_rcon_message_t **authanswers = NULL;
    int sock = 0;
    int ret = 0;
    int ec = 3;

#ifdef HAVE_PLEDGE
    /* stdio = standard IO and send/recv
     * rpath = config file
     * inet = dns = :-)
     */
    if (pledge("stdio rpath inet dns", NULL) == -1) {
        err(1, "pledge");
    }
#endif

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

    if (host == NULL) {
        fprintf(stderr, "No host specified\n");
        return 1;
    }

    if (port == NULL) {
        fprintf(stderr, "No port specified\n");
        return 1;
    }

    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_INET;
    hint.ai_flags = AI_PASSIVE;

    if ((ret = getaddrinfo(host, port, &hint, &info))) {
        fprintf(stderr, "Failed to resolve host: %s: %s\n",
                host, gai_strerror(ret)
            );
        goto cleanup;
    }

    for (ai = info; ai != NULL; ai = ai->ai_next ) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) {
	    printf ("Error opening socket: %s\n",strerror(errno));
            continue;
        }

	addr = (struct sockaddr_in *)ai->ai_addr; 
	printf("Connecting to: %s\n",inet_ntoa((struct in_addr)addr->sin_addr));

        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        } else {
	    printf ("Error connecting to socket: %s\n",strerror(errno));
	}

        close(sock);
        sock = -1;
    }

    if (sock < 0) {
        fprintf(stderr, "Failed to connect to the given host/port\n");
        goto cleanup;
    }

#ifdef HAVE_PLEDGE
    /* Drop privileges further, since we are done socket()ing.
     */
    if (pledge("stdio", NULL) == -1) {
        err(1, "pledge");
    }
#endif

    response = g_byte_array_new();
    r = src_rcon_new();

    /* Do we have a password?
     */
    if (password != NULL && strlen(password) > 0) {
        /* Send auth request first
         */
        auth = src_rcon_auth(r, password);

        if (send_message(sock, auth)) {
            goto cleanup;
        }

        if (wait_auth(sock, auth)) {
            fprintf(stderr, "Invalid auth reply, valid password?\n");
            goto cleanup;
        }
    }

    printf ("Authorization successful...\n");

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

    src_rcon_message_free(auth);
    src_rcon_message_freev(authanswers);

    if (sock > -1) {
        close(sock);
    }

    if (info) {
        freeaddrinfo(info);
    }

    return ec;
}
