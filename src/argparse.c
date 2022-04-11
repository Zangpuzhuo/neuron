/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2022 EMQ Technologies Co., Ltd All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"

#define OPTIONAL_ARGUMENT_IS_PRESENT                             \
    ((optarg == NULL && optind < argc && argv[optind][0] != '-') \
         ? (bool) (optarg = argv[optind++])                      \
         : (optarg != NULL))

#define STRDUP(var, str)                                                       \
    do {                                                                       \
        (var) = strdup(str);                                                   \
        if (NULL == (var)) {                                                   \
            fprintf(                                                           \
                stderr,                                                        \
                "Neuron argument parser fail strdup string: %s, reason: %s\n", \
                (str), strerror(errno));                                       \
            goto quit;                                                         \
        }                                                                      \
    } while (0)

// clang-format off
const char *usage_text =
"USAGE:\n"
"    neuron [OPTIONS]\n\n"
"OPTIONS:\n"
"    -c, --config <FILE>  neuron configuration file\n"
"                         without this option, default from ./config/neuron.yaml\n"
"    -d, --daemon         run as daemon process\n"
"    -h, --help           show this help message\n"
"    --log [FILE]         log to the given file, or stdout if no FILE argument\n"
"                         without this option, default log to ./logs/neuron.log\n"
"\n";
// clang-format on

static inline void usage()
{
    fprintf(stderr, "%s", usage_text);
}

void neu_cli_args_init(neu_cli_args_t *args, int argc, char *argv[])
{
    char *        opts           = "c:dh";
    struct option long_options[] = {
        { "help", no_argument, NULL, 1 },
        { "config", required_argument, NULL, 'c' },
        { "daemon", no_argument, NULL, 'd' },
        { "log", optional_argument, NULL, 'l' },
        { NULL, 0, NULL, 0 },
    };

    memset(args, 0, sizeof(*args));

    int c = 0;
    while ((c = getopt_long(argc, argv, opts, long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            usage();
            exit(0);
        case 'd':
            args->daemonized = true;
            break;
        case ':':
            fprintf(stderr, "%s: option '-%c' requires an argument\n", argv[0],
                    optopt);
            usage();
            goto quit;
        case 'c':
            STRDUP(args->conf_file, optarg);
            break;
        case 'l':
            if (OPTIONAL_ARGUMENT_IS_PRESENT) {
                // log to file
                STRDUP(args->log_file, optarg);
            } else {
                // log to stdout
                STRDUP(args->log_file, NEU_LOG_STDOUT_FNAME);
            }
            break;
        case '?':
        default:
            fprintf(stderr, "%s: option '-%c' is invalid: ignored\n", argv[0],
                    optopt);
            usage();
            goto quit;
        }
    }

    if (NULL == args->conf_file) {
        STRDUP(args->conf_file, "./config/neuron.yaml");
    }

    if (NULL == args->log_file) {
        STRDUP(args->log_file, "./logs/neuron.log");
    }

    return;

quit:
    neu_cli_args_fini(args);
    exit(1);
}

void neu_cli_args_fini(neu_cli_args_t *args)
{
    free(args->conf_file);
    args->conf_file = NULL;
    free(args->log_file);
    args->log_file = NULL;
}