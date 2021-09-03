/**
 * NEURON IIoT System for Industry 4.0
 * Copyright (C) 2020-2021 EMQ Technologies Co., Ltd All rights reserved.
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
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>

#include "connection/neu_tcp.h"
#include "neu_datatag_table.h"
#include "neu_tag_group_config.h"
#include "neu_vector.h"
#include "neuron.h"

#include "modbus.h"
#include "modbus_point.h"

const neu_plugin_module_t neu_plugin_module;

enum process_status {
    STOP    = 0,
    RUNNING = 1,
    WAIT    = 2,
};

enum device_connect_status {
    DISCONNECTED = 0,
    CONNECTED    = 1,
};

struct neu_plugin {
    neu_plugin_common_t        common;
    pthread_mutex_t            mtx;
    pthread_t                  loop;
    neu_tcp_client_t *         client;
    uint32_t                   interval;
    enum process_status        status;
    enum device_connect_status connect_status;
    neu_datatag_table_t *      tag_table;
    modbus_point_context_t *   point_ctx;
};

static void *  loop(void *arg);
static ssize_t send_recv_callback(void *arg, char *send_buf, ssize_t send_len,
                                  char *recv_buf, ssize_t recv_len);

static ssize_t send_recv_callback(void *arg, char *send_buf, ssize_t send_len,
                                  char *recv_buf, ssize_t recv_len)
{
    neu_plugin_t *plugin = (neu_plugin_t *) arg;
    return neu_tcp_client_send_recv(plugin->client, send_buf, send_len,
                                    recv_buf, recv_len);
}

static void *loop(void *arg)
{
    neu_plugin_t *plugin = (neu_plugin_t *) arg;

    while (1) {
        uint32_t interval = 0;
        bool     wait     = false;

        pthread_mutex_lock(&plugin->mtx);
        if (plugin->status == STOP) {
            pthread_mutex_unlock(&plugin->mtx);
            break;
        }
        interval = plugin->interval;
        pthread_mutex_unlock(&plugin->mtx);

        pthread_mutex_lock(&plugin->mtx);
        wait = plugin->status == WAIT;
        pthread_mutex_unlock(&plugin->mtx);

        if (!wait) {
            modbus_point_all_read(plugin->point_ctx, true, send_recv_callback);
            //     modbus_data_t data = { .type = MODBUS_B16, .val.val_u16 =
            //     0x5678 }; modbus_point_write(plugin->point_ctx, "1!400008",
            //     &data,
            //                        send_recv_callback);
            //     modbus_data_t data1 = { .type = MODBUS_B16, .val.val_u16 =
            //     0x1234 }; modbus_point_write(plugin->point_ctx, "1!400007",
            //     &data1,
            //                        send_recv_callback);
        }

        usleep(interval * 1000);
    }

    return NULL;
}

static neu_plugin_t *modbus_tcp_open(neu_adapter_t *            adapter,
                                     const adapter_callbacks_t *callbacks)
{
    neu_plugin_t *plugin;

    if (adapter == NULL || callbacks == NULL) {
        log_error("Open plugin with NULL adapter or callbacks");
        return NULL;
    }

    plugin = (neu_plugin_t *) calloc(1, sizeof(neu_plugin_t));
    if (plugin == NULL) {
        log_error("Failed to allocate plugin %s",
                  neu_plugin_module.module_name);
        return NULL;
    }

    neu_plugin_common_init(&plugin->common);

    plugin->common.adapter           = adapter;
    plugin->common.adapter_callbacks = callbacks;

    return plugin;
}

static int modbus_tcp_close(neu_plugin_t *plugin)
{
    free(plugin);
    return 0;
}

static int modbus_tcp_init(neu_plugin_t *plugin)
{

    pthread_mutex_init(&plugin->mtx, NULL);
    // plugin->status    = WAIT;
    plugin->status    = RUNNING;
    plugin->point_ctx = modbus_point_init(plugin);

    modbus_point_add(plugin->point_ctx, "1!400001", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!400003", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!400005", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!400007", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!400009", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!400011", MODBUS_B16);
    modbus_point_add(plugin->point_ctx, "1!00001", MODBUS_B8);
    modbus_point_add(plugin->point_ctx, "1!00002", MODBUS_B8);
    modbus_point_add(plugin->point_ctx, "1!00003", MODBUS_B8);

    modbus_point_new_cmd(plugin->point_ctx);
    // plugin->client   = neu_tcp_client_create("192.168.50.17", 502);
    plugin->client   = neu_tcp_client_create("192.168.8.30", 502);
    plugin->interval = 10000;
    pthread_create(&plugin->loop, NULL, loop, plugin);

    log_info("modbus tcp init.....");
    return 0;
}

static int modbus_tcp_uninit(neu_plugin_t *plugin)
{

    pthread_mutex_lock(&plugin->mtx);
    plugin->status = STOP;
    pthread_mutex_unlock(&plugin->mtx);

    modbus_point_clean(plugin->point_ctx);

    neu_tcp_client_close(plugin->client);

    pthread_mutex_destroy(&plugin->mtx);

    return 0;
}

static int get_datatags_table(neu_plugin_t *plugin)
{
    int                     ret = 0;
    neu_request_t           cmd;
    neu_cmd_get_datatags_t  get_datatags_cmd;
    neu_response_t *        datatags_result = NULL;
    neu_reqresp_datatags_t *resp_datatags;

    cmd.req_type = NEU_REQRESP_GET_DATATAGS;
    cmd.req_id   = 2;
    cmd.buf      = (void *) &get_datatags_cmd;
    cmd.buf_len  = sizeof(neu_cmd_get_datatags_t);

    ret = plugin->common.adapter_callbacks->command(plugin->common.adapter,
                                                    &cmd, &datatags_result);

    if (ret < 0) {
        return -1;
    }
    resp_datatags     = (neu_reqresp_datatags_t *) datatags_result->buf;
    plugin->tag_table = resp_datatags->datatag_tbl;

    free(resp_datatags);
    free(datatags_result);
    return 0;
}

static int modbus_tcp_config(neu_plugin_t *plugin, neu_config_t *configs)
{
    (void) configs;
    int ret = 0;

    // plugin->client = neu_tcp_client_create("192.168.50.17", 502);
    log_info("modbus config.............");

    ret = get_datatags_table(plugin);

    return ret;
}

static void read_data_process(neu_plugin_t *plugin, neu_request_t *req)
{
    neu_reqresp_read_t *data = (neu_reqresp_read_t *) req->buf;
    vector_t *          tagv = neu_taggrp_cfg_get_datatag_ids(data->grp_config);

    neu_response_t     resp = { 0 };
    neu_reqresp_data_t data_resp;
    neu_variable_t *   head = neu_variable_create();

    VECTOR_FOR_EACH(tagv, iter)
    {
        datatag_id_t * id    = (datatag_id_t *) iterator_get(&iter);
        neu_datatag_t *tag   = neu_datatag_tbl_get(plugin->tag_table, *id);
        int            ret   = 0;
        modbus_data_t  mdata = { 0 };

        ret = modbus_point_find(plugin->point_ctx, tag->addr_str, &mdata);
        if (ret != 0) {
            neu_variable_t *err = neu_variable_create();
            neu_variable_set_error(err, 1);
            neu_variable_add_item(head, err);
        } else {
            neu_variable_t *v = neu_variable_create();
            switch (tag->dataType) {
            case NEU_DATATYPE_BOOLEAN:
                neu_variable_set_byte(v, mdata.val.val_u8);
                break;
            case NEU_DATATYPE_WORD:
            case NEU_DATATYPE_UWORD:
                neu_variable_set_word(v, mdata.val.val_u16);
                break;
            case NEU_DATATYPE_DWORD:
            case NEU_DATATYPE_UDWORD:
                neu_variable_set_qword(v, mdata.val.val_u32);
                break;
            case NEU_DATATYPE_FLOAT:
                neu_variable_set_double(v, mdata.val.val_f32);
                break;
            default: {
                ret = -1;
                break;
            }
            }

            neu_variable_add_item(head, v);
        }
    }

    data_resp.grp_config = data->grp_config;
    data_resp.data_var   = head;
    resp.req_id          = req->req_id;
    resp.resp_type       = NEU_REQRESP_TRANS_DATA;
    resp.buf_len         = sizeof(neu_reqresp_data_t);
    resp.buf             = &data_resp;
    plugin->common.adapter_callbacks->response(plugin->common.adapter, &resp);

    neu_variable_destroy(head);
};

static void read_data_process_demo(neu_plugin_t *plugin, neu_request_t *req)
{
    neu_reqresp_read_t *data = (neu_reqresp_read_t *) req->buf;
    neu_response_t      resp = { 0 };
    neu_reqresp_data_t  data_resp;
    neu_variable_t *    head  = neu_variable_create();
    int                 ret   = 0;
    modbus_data_t       mdata = { 0 };

    mdata.type = MODBUS_B16;
    ret        = modbus_point_find(plugin->point_ctx, "1!400001", &mdata);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 1);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *v = neu_variable_create();
        neu_variable_set_word(v, mdata.val.val_u16);
        neu_variable_add_item(head, v);
    }

    mdata.type = MODBUS_B16;
    ret        = modbus_point_find(plugin->point_ctx, "1!400003", &mdata);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 1);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *v = neu_variable_create();
        neu_variable_set_word(v, mdata.val.val_u16);
        neu_variable_add_item(head, v);
    }

    mdata.type = MODBUS_B8;
    ret        = modbus_point_find(plugin->point_ctx, "1!00001", &mdata);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 1);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *v = neu_variable_create();
        neu_variable_set_byte(v, mdata.val.val_u8);
        neu_variable_add_item(head, v);
    }

    mdata.type = MODBUS_B8;
    ret        = modbus_point_find(plugin->point_ctx, "1!00002", &mdata);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 1);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *v = neu_variable_create();
        neu_variable_set_byte(v, mdata.val.val_u8);
        neu_variable_add_item(head, v);
    }

    mdata.type = MODBUS_B8;
    ret        = modbus_point_find(plugin->point_ctx, "1!00003", &mdata);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 1);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *v = neu_variable_create();
        neu_variable_set_byte(v, mdata.val.val_u8);
        neu_variable_add_item(head, v);
    }

    data_resp.grp_config = data->grp_config;
    data_resp.data_var   = head;
    resp.req_id          = req->req_id;
    resp.resp_type       = NEU_REQRESP_TRANS_DATA;
    resp.buf_len         = sizeof(neu_reqresp_data_t);
    resp.buf             = &data_resp;

    plugin->common.adapter_callbacks->response(plugin->common.adapter, &resp);
    neu_variable_destroy(head);
}

static void write_data_process(neu_plugin_t *plugin, neu_request_t *req)
{
    neu_reqresp_write_t *data = (neu_reqresp_write_t *) req->buf;
    vector_t *tagv = neu_taggrp_cfg_get_datatag_ids(data->grp_config);

    neu_response_t     resp = { 0 };
    neu_reqresp_data_t data_resp;
    neu_variable_t *   head = neu_variable_create();
    neu_variable_t *   var  = data->data_var;

    VECTOR_FOR_EACH(tagv, iter)
    {
        datatag_id_t * id  = (datatag_id_t *) iterator_get(&iter);
        neu_datatag_t *tag = neu_datatag_tbl_get(plugin->tag_table, *id);
        modbus_data_t  mdata;
        int            ret = 0;

        switch (tag->dataType) {
        case NEU_DATATYPE_BOOLEAN:
            mdata.type = MODBUS_B8;
            neu_variable_get_byte(var, (int8_t *) &mdata.val.val_u8);
            break;
        case NEU_DATATYPE_WORD:
        case NEU_DATATYPE_UWORD:
            mdata.type = MODBUS_B16;
            neu_variable_get_uword(var, &mdata.val.val_u16);
            break;
        case NEU_DATATYPE_DWORD:
        case NEU_DATATYPE_UDWORD:
        case NEU_DATATYPE_FLOAT:
            mdata.type = MODBUS_B32;
            neu_variable_get_udword(var, &mdata.val.val_u32);
            break;
        default: {
            neu_variable_t *err = neu_variable_create();
            neu_variable_set_error(err, 1);
            neu_variable_add_item(head, err);
            ret = -1;
            break;
        }
        }
        ret = modbus_point_write(plugin->point_ctx, tag->addr_str, &mdata,
                                 send_recv_callback);
        if (ret != 0) {
            neu_variable_t *err = neu_variable_create();
            neu_variable_set_error(err, 2);
            neu_variable_add_item(head, err);
        } else {
            neu_variable_t *ok = neu_variable_create();
            neu_variable_set_error(ok, 0);
            neu_variable_add_item(head, ok);
        }

        var = neu_variable_next(var);
    }

    data_resp.grp_config = data->grp_config;
    data_resp.data_var   = head;
    resp.req_id          = req->req_id;
    resp.resp_type       = NEU_REQRESP_TRANS_DATA;
    resp.buf_len         = sizeof(neu_reqresp_data_t);
    resp.buf             = &data_resp;
    plugin->common.adapter_callbacks->response(plugin->common.adapter, &resp);

    neu_variable_destroy(head);
}

static void write_data_process_demo(neu_plugin_t *plugin, neu_request_t *req)
{
    neu_reqresp_write_t *data = (neu_reqresp_write_t *) req->buf;
    neu_response_t       resp = { 0 };
    neu_reqresp_data_t   data_resp;
    neu_variable_t *     head  = neu_variable_create();
    modbus_data_t        mdata = { 0 };

    mdata.type = MODBUS_B16;
    neu_variable_get_uword(data->data_var, &mdata.val.val_u16);
    int ret = modbus_point_write(plugin->point_ctx, "1!400001", &mdata,
                                 send_recv_callback);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 2);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 0);
        neu_variable_add_item(head, err);
    }

    neu_variable_t *var = neu_variable_next(data->data_var);
    mdata.type          = MODBUS_B16;
    neu_variable_get_uword(var, &mdata.val.val_u16);
    ret = modbus_point_write(plugin->point_ctx, "1!400003", &mdata,
                             send_recv_callback);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 2);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 0);
        neu_variable_add_item(head, err);
    }

    var        = neu_variable_next(var);
    mdata.type = MODBUS_B8;
    neu_variable_get_byte(var, (int8_t *) &mdata.val.val_u8);
    ret = modbus_point_write(plugin->point_ctx, "1!00001", &mdata,
                             send_recv_callback);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 2);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 0);
        neu_variable_add_item(head, err);
    }

    var        = neu_variable_next(var);
    mdata.type = MODBUS_B8;
    neu_variable_get_byte(var, (int8_t *) &mdata.val.val_u8);
    ret = modbus_point_write(plugin->point_ctx, "1!00002", &mdata,
                             send_recv_callback);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 2);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 0);
        neu_variable_add_item(head, err);
    }

    var        = neu_variable_next(var);
    mdata.type = MODBUS_B8;
    neu_variable_get_byte(var, (int8_t *) &mdata.val.val_u8);
    ret = modbus_point_write(plugin->point_ctx, "1!00003", &mdata,
                             send_recv_callback);
    if (ret != 0) {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 2);
        neu_variable_add_item(head, err);
    } else {
        neu_variable_t *err = neu_variable_create();
        neu_variable_set_error(err, 0);
        neu_variable_add_item(head, err);
    }

    data_resp.grp_config = data->grp_config;
    data_resp.data_var   = head;
    resp.req_id          = req->req_id;
    resp.resp_type       = NEU_REQRESP_TRANS_DATA;
    resp.buf_len         = sizeof(neu_reqresp_data_t);
    resp.buf             = &data_resp;
    plugin->common.adapter_callbacks->response(plugin->common.adapter, &resp);
    neu_variable_destroy(head);
}

static int modbus_tcp_request(neu_plugin_t *plugin, neu_request_t *req)
{
    switch (req->req_type) {
    case NEU_REQRESP_READ_DATA:
#ifdef DEMO
        read_data_process_demo(plugin, req);
#else
        read_data_process(plugin, req);
#endif
        break;
    case NEU_REQRESP_WRITE_DATA:
#ifdef DEMO
        write_data_process_demo(plugin, req);
#else
        write_data_process(plugin, req);
#endif
        break;
    default:
        break;
    }

    return 0;
}

static int modbus_tcp_event_reply(neu_plugin_t *     plugin,
                                  neu_event_reply_t *reqply)
{
    (void) plugin;
    (void) reqply;
    return 0;
}

static const neu_plugin_intf_funs_t plugin_intf_funs = {
    .open        = modbus_tcp_open,
    .close       = modbus_tcp_close,
    .init        = modbus_tcp_init,
    .uninit      = modbus_tcp_uninit,
    .config      = modbus_tcp_config,
    .request     = modbus_tcp_request,
    .event_reply = modbus_tcp_event_reply,
};

const neu_plugin_module_t neu_plugin_module = {
    .version      = NEURON_PLUGIN_VER_1_0,
    .module_name  = "neuron-modbus-tcp-plugin",
    .module_descr = "modbus tcp driver plugin",
    .intf_funs    = &plugin_intf_funs,
};