/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_pack.h>
#include <msgpack.h>

#include <stdio.h>
#include "in_isima_netif.h"

#define LINE_LEN 256

static struct entry_define entry_name_linux[] = {
    {"rx.bytes",       FLB_TRUE},
    {"rx.packets",     FLB_TRUE},
    {"rx.errors",      FLB_TRUE},
    {"rx.drop",        FLB_FALSE},
    {"rx.fifo",        FLB_FALSE},
    {"rx.frame",       FLB_FALSE},
    {"rx.compressed",  FLB_FALSE},
    {"rx.multicast",   FLB_FALSE},
    {"tx.bytes",       FLB_TRUE},
    {"tx.packets",     FLB_TRUE},
    {"tx.errors",      FLB_TRUE},
    {"tx.drop",        FLB_FALSE},
    {"tx.fifo",        FLB_FALSE},
    {"tx.collisions",  FLB_FALSE},
    {"tx.carrier",     FLB_FALSE},
    {"tx.compressepd", FLB_FALSE}
};

static void destroy_interface_desc(struct flb_in_isima_netif_config *ctx) {
    int i;
    for (i = 0; i < ctx->if_desc_len; i++) {
        flb_sds_destroy(ctx->if_desc[i].interface);
        flb_free(ctx->if_desc[i].entry);
    }
    flb_free(ctx->if_desc);
}

static int config_destroy(struct flb_in_isima_netif_config *ctx)
{
    destroy_interface_desc(ctx);
    flb_free(ctx);
    return 0;
}


static int in_isima_netif_exit(void *data, struct flb_config *config)
{
    (void) *config;
    struct flb_in_isima_netif_config *ctx = data;

    /* Destroy context */
    config_destroy(ctx);

    return 0;
}

static int init_interface_desc(struct flb_in_isima_netif_config *ctx)
{
    FILE *fp = NULL;
    char line[LINE_LEN] = {0};
    int num_ifaces = -2;

    fp = fopen("/proc/net/dev", "r");
    if (fp == NULL) {
        flb_errno();
        flb_plg_error(ctx->ins, "cannot open /proc/net/dev");
        return -1;
    }
    while (fgets(line, LINE_LEN-1, fp) != NULL) {
        num_ifaces++;
    }
    fclose(fp);
    if (num_ifaces <= 0) {
        return -1;
    }

    ctx->if_desc = flb_calloc(1, sizeof(struct netif_desc) * num_ifaces);
    if (!ctx->if_desc) {
        flb_errno();
        return -1;
    }
    ctx->if_desc_len = num_ifaces;

    return 0;
}

static int init_entry_linux(struct flb_in_isima_netif_config *ctx)
{
    int i, j;

    ctx->entry_len = sizeof(entry_name_linux) / sizeof(struct entry_define);
    for (i = 0; i < ctx->entry_len; i++) {
        if (entry_name_linux[i].checked) {
            ctx->map_num++;
        }
    }

    if (init_interface_desc(ctx)) {
        return -1;
    }

    for (i = 0; i < ctx->if_desc_len; i++) {
        struct netif_desc* desc = &ctx->if_desc[i];
        desc->entry = flb_malloc(sizeof(struct netif_entry) * ctx->entry_len);
        if (!desc->entry) {
            flb_errno();
            return -1;
        }
        for (j = 0; j < ctx->entry_len; j++) {
            struct netif_entry* entry = &desc->entry[j];
            entry->name = entry_name_linux[j].name;
            entry->name_len = strlen(entry_name_linux[j].name);
            entry->prev = 0;
            entry->now = 0;
            entry->checked = entry_name_linux[j].checked;
        }
    }

    return 0;
}

static int configure(struct flb_in_isima_netif_config *ctx,
                     struct flb_input_instance *in)
{
    int ret;
    ctx->map_num = 0;

    /* Load the config map */
    ret = flb_input_config_map_set(in, (void *)ctx);
    if (ret == -1) {
        flb_plg_error(in, "unable to load configuration");
        return -1;
    }

    if (ctx->interval_sec <= 0 && ctx->interval_nsec <= 0) {
        /* Illegal settings. Override them. */
        ctx->interval_sec = atoi(DEFAULT_INTERVAL_SEC);
        ctx->interval_nsec = atoi(DEFAULT_INTERVAL_NSEC);
    }

    ctx->first_snapshot = FLB_TRUE;    /* assign first_snapshot with FLB_TRUE */
    ctx->reinit = FLB_FALSE;

    return init_entry_linux(ctx);
}

static int parse_proc_line(char *line, int line_no,
                           struct flb_in_isima_netif_config *ctx)
{
    struct mk_list *head = NULL;
    struct mk_list *split = NULL;
    struct flb_split_entry *sentry = NULL;

    int i = 0;
    int entry_num;

    split = flb_utils_split(line, ' ', 256);
    entry_num = mk_list_size(split);
    if (entry_num != ctx->entry_len + 1) {
        flb_utils_split_free(split);
        return -1;
    }

    mk_list_foreach(head, split) {
        sentry = mk_list_entry(head, struct flb_split_entry ,_head);
        if (i == 0) {
            if (ctx->if_desc[line_no].interface == NULL) {
                ctx->if_desc[line_no].interface = flb_sds_create(sentry->value);
                ctx->if_desc[line_no].interface_len = strlen(ctx->if_desc[line_no].interface);
            } else if (strcmp(ctx->if_desc[line_no].interface, sentry->value) != 0) {
                ctx->reinit = FLB_TRUE;
                flb_plg_error(ctx->ins, "interface mismatch when parsing /proc/net/dev", ctx->if_desc[line_no].interface, sentry->value);
                flb_utils_split_free(split);
                return -1;
            }
        } else {
            ctx->if_desc[line_no].entry[i-1].prev = ctx->if_desc[line_no].entry[i-1].now;
            ctx->if_desc[line_no].entry[i-1].now = strtoul(sentry->value, NULL, 10);
        }
        i++;
    }

    flb_utils_split_free(split);

    return 0;
}

static inline uint64_t calc_diff(struct netif_entry *entry)
{
    if (entry->prev <= entry->now) {
        return entry->now - entry->prev;
    }
    else {
        return entry->now + (UINT64_MAX - entry->prev);
    }
}

static int read_proc_file_linux(struct flb_in_isima_netif_config *ctx)
{
    int line_no = -2;
    FILE *fp = NULL;
    char line[LINE_LEN] = {0};
    int error = FLB_FALSE;

    fp = fopen("/proc/net/dev", "r");
    if (fp == NULL) {
        flb_errno();
        flb_plg_error(ctx->ins, "cannot open /proc/net/dev");
        return -1;
    }
    while (fgets(line, LINE_LEN-1, fp) != NULL) {
        if (line_no >= 0) {
            if (parse_proc_line(line, line_no, ctx)) {
                error = FLB_TRUE;
            }
        }
        line_no++;
    }
    fclose(fp);
    if (error == FLB_TRUE) {
        return -1;
    }
    return 0;
}

static int in_isima_netif_collect_linux(struct flb_input_instance *i_ins,
                           struct flb_config *config, void *in_context)
{
    struct flb_in_isima_netif_config *ctx = in_context;
    int interface_key_len;
    int i, j;
    msgpack_packer mp_pck;
    msgpack_sbuffer mp_sbuf;
    int num_entries = 0;

    interface_key_len = strlen(STR_KEY_INTERFACE);

    if (read_proc_file_linux(ctx)) {
        if (ctx->reinit == FLB_TRUE) {
            destroy_interface_desc(ctx);
            init_entry_linux(ctx);
            ctx->reinit = FLB_FALSE;
            ctx->first_snapshot = FLB_TRUE;
        }
        return -1;
    }

    if (ctx->first_snapshot == FLB_TRUE) {
        /* assign first_snapshot with FLB_FALSE */
        ctx->first_snapshot = FLB_FALSE;
    }
    else {
        for (i = 0; i < ctx->if_desc_len; i++) {
            struct netif_desc* desc = &ctx->if_desc[i];
            /* Initialize local msgpack buffer */
            msgpack_sbuffer_init(&mp_sbuf);
            msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

            /* Pack data */
            msgpack_pack_array(&mp_pck, 2);
            flb_pack_time_now(&mp_pck);
            msgpack_pack_map(&mp_pck, ctx->map_num + 1);

            msgpack_pack_str(&mp_pck, interface_key_len);
            msgpack_pack_str_body(&mp_pck, STR_KEY_INTERFACE, interface_key_len);

            msgpack_pack_str(&mp_pck, desc->interface_len - 1);
            msgpack_pack_str_body(&mp_pck, desc->interface, desc->interface_len - 1);

            num_entries = 0;

            for (j = 0; j < ctx->entry_len; j++) {
                if (desc->entry[j].checked) {
                    msgpack_pack_str(&mp_pck, desc->entry[j].name_len);
                    msgpack_pack_str_body(&mp_pck, desc->entry[j].name, desc->entry[j].name_len);
                    msgpack_pack_uint64(&mp_pck, calc_diff(&desc->entry[j]));
                    num_entries++;
                }
            }

            if (ctx->map_num != num_entries) {
                flb_plg_error(i_ins, "num entries mismatch, map_num=%d, num_entries=%d", ctx->map_num, num_entries);
                msgpack_sbuffer_destroy(&mp_sbuf);
                continue;
            }

            flb_input_chunk_append_raw(i_ins, NULL, 0, mp_sbuf.data, mp_sbuf.size);
            msgpack_sbuffer_destroy(&mp_sbuf);
        }
    }

    return 0;
}

static int in_isima_netif_collect(struct flb_input_instance *i_ins,
                            struct flb_config *config, void *in_context)
{
    return in_isima_netif_collect_linux(i_ins, config, in_context);
}

static int in_isima_netif_init(struct flb_input_instance *in,
                         struct flb_config *config, void *data)
{
    int ret;

    struct flb_in_isima_netif_config *ctx = NULL;
    (void) data;

    /* Allocate space for the configuration */
    ctx = flb_calloc(1, sizeof(struct flb_in_isima_netif_config));
    if (!ctx) {
        flb_errno();
        return -1;
    }
    ctx->ins = in;

    if (configure(ctx, in) < 0) {
        config_destroy(ctx);
        return -1;
    }

    /* Set the context */
    flb_input_set_context(in, ctx);

    /* Set our collector based on time */
    ret = flb_input_set_collector_time(in,
                                       in_isima_netif_collect,
                                       ctx->interval_sec,
                                       ctx->interval_nsec,
                                       config);
    if (ret == -1) {
        flb_plg_error(ctx->ins, "Could not set collector for Proc input plugin");
        config_destroy(ctx);
        return -1;
    }

    return 0;
}

static struct flb_config_map config_map[] = {
    {
      FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
      0, FLB_TRUE, offsetof(struct flb_in_isima_netif_config, interval_sec),
      "Set the collector interval"
    },
    {
      FLB_CONFIG_MAP_INT, "interval_nsec", DEFAULT_INTERVAL_NSEC,
      0, FLB_TRUE, offsetof(struct flb_in_isima_netif_config, interval_nsec),
      "Set the collector interval (nanoseconds)"
    },
    /* EOF */
    {0}
};

/* Plugin reference */
struct flb_input_plugin in_isima_netif_plugin = {
    .name         = "isima_netif",
    .description  = "Isima Network Stats",
    .cb_init      = in_isima_netif_init,
    .cb_pre_run   = NULL,
    .cb_collect   = in_isima_netif_collect,
    .cb_flush_buf = NULL,
    .cb_exit      = in_isima_netif_exit,
    .config_map   = config_map,
    .flags        = 0,
};
