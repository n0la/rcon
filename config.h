#ifndef RCON_CONFIG_H
#define RCON_CONFIG_H

#include <stdbool.h>

void config_free(void);
int config_load(char const *file);

int config_host_data(char const *name, char **hostname,
                     char **port, char **passwd);

#endif
