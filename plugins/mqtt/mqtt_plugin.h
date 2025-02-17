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

#ifndef NEURON_MQTT_PLUGIN
#define NEURON_MQTT_PLUGIN

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>

#include <neuron.h>

typedef struct mqtt_routine mqtt_routine_t;

struct neu_plugin {
    neu_plugin_common_t common;
    bool                running;
    mqtt_routine_t *    routine;
    char *              config;
    neu_mem_cache_t *   cache;
    nng_mtx *           mutex;
};

#ifdef __cplusplus
}
#endif
#endif