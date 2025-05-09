/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_input.h>
#include <fluent-bit/flb_input_plugin.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_error.h>
#include <fluent-bit/flb_str.h>
#include <fluent-bit/flb_pack.h>

#include <msgpack.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>

#include "in_isima_disk.h"

#define LINE_SIZE 512
#define BUF_SIZE  32

static char *shift_line(const char *line, char separator, int *idx,
                        char *buf, int buf_size)
{
    char pack_mode = FLB_FALSE;
    int  idx_buf = 0;

    while (1) {
        if (line[*idx] == '\0') {
            /* end of line */
            return NULL;
        }
        else if (line[*idx] != separator) {
            pack_mode = FLB_TRUE;
            buf[idx_buf] = line[*idx];
            idx_buf++;

            if (idx_buf >= buf_size) {
                buf[idx_buf-1] = '\0';
                return NULL;
            }
        }
        else if (pack_mode == FLB_TRUE) {
            buf[idx_buf] = '\0';
            return buf;
        }
        *idx += 1;
    }
}

static uint64_t parse_long(struct flb_input_instance *in, const char* buf) {
    uint64_t ret;

    ret = strtoull(buf, NULL, 10);
    if (ret == ULLONG_MAX) {
        flb_plg_error(in, "unable to parse long; error=%s, buffer=%s", strerror(errno), buf);
        return 0;
    }

    return ret;
}

static flb_sds_t get_mountpoint(struct flb_input_instance *in, const flb_sds_t device) {
    int ret;
    FILE *fp = NULL;
    flb_sds_t cmd = flb_sds_create_size(128);
    flb_sds_t mountpoint = flb_sds_create_size(128);
    flb_sds_snprintf(&cmd, flb_sds_alloc(cmd), "findmnt -S /dev/%s -f -o TARGET -n 2> /dev/null", device);
    fp = popen(cmd, "r");

    if (fp == NULL) {
        flb_plg_error(in, "command '%s' failed", cmd);
        goto error;
    }

    if (fgets(mountpoint, 128, fp) != NULL) {
        ret = strnlen(mountpoint, 128);
        if ((ret > 0) && (mountpoint[ret - 1] == '\n')) {
            mountpoint[ret - 1] = '\0'; /* chomp */
        }
    }

    ret = pclose(fp);

    if (ret == -1 || WIFSIGNALED(ret)) {
        flb_plg_error(in, "unable to get mountpoint for device: %s", device);
        goto error;
    } else if (WIFEXITED(ret)) {
        if (WEXITSTATUS(ret) == 0) {
            flb_sds_destroy(cmd);
            return mountpoint;
        } else {
            goto error;
        }
    }

error:
    flb_sds_destroy(cmd);
    flb_sds_destroy(mountpoint);
    return NULL;
}

static int update_disk_stats(struct flb_input_instance *in, struct flb_in_isima_disk_config *ctx)
{
    char line[LINE_SIZE] = {0};
    char buf[BUF_SIZE] = {0};
    char skip_line = FLB_FALSE;
    uint64_t temp_total = 0;
    FILE *fp  = NULL;
    int  i_line   = 0;
    int  i_entry = 0;
    int  i_field = 0;

    fp = fopen("/proc/diskstats", "r");
    if (fp == NULL) {
        flb_errno();
        return -1;
    }

    while (fgets(line, LINE_SIZE-1, fp) != NULL) {
        i_line = 0;
        i_field = 0;
        skip_line = FLB_FALSE;
        while (skip_line != FLB_TRUE &&
               shift_line(line, ' ', &i_line, buf, BUF_SIZE-1) != NULL) {
            i_field++;
            switch(i_field) {
            case 3: /* device name */
                if (ctx->dev_name != NULL && strstr(buf, ctx->dev_name) == NULL) {
                    skip_line = FLB_TRUE;
                    break;
                }
                if (ctx->device[i_entry] == NULL) {
                    ctx->device[i_entry] = flb_sds_create(buf);
                    if (ctx->device[i_entry] == NULL) {
                        flb_plg_error(in, "cannot create device string");
                    }
                }
                if (strcmp(ctx->device[i_entry], buf) != 0) {
                    flb_sds_destroy(ctx->device[i_entry]);
                    ctx->device[i_entry] = flb_sds_create(buf);
                    if (ctx->device[i_entry] == NULL) {
                        flb_plg_error(in, "cannot create device string");
                    }
                }
                break;
            case 4: /* reads completed */
                temp_total = parse_long(in, buf);
                ctx->prev_num_reads[i_entry] = ctx->num_reads[i_entry];
                ctx->num_reads[i_entry] = temp_total;
                break;
            case 6: /* sectors read */
                temp_total = parse_long(in, buf);
                temp_total *= 512;
                ctx->prev_read_size_total_bytes[i_entry] = ctx->read_size_total_bytes[i_entry];
                ctx->read_size_total_bytes[i_entry] = temp_total;
                break;
            case 7: /* time spent reading */
                temp_total = parse_long(in, buf);
                temp_total *= 1000;
                ctx->prev_read_latency_total_us[i_entry] = ctx->read_latency_total_us[i_entry];
                ctx->read_latency_total_us[i_entry] = temp_total;
                break;
            case 8: /* writes completed */
                temp_total = parse_long(in, buf);
                ctx->prev_num_writes[i_entry] = ctx->num_writes[i_entry];
                ctx->num_writes[i_entry] = temp_total;
                break;
            case 10: /* sectors written */
                temp_total = parse_long(in, buf);
                temp_total *= 512;
                ctx->prev_write_size_total_bytes[i_entry] = ctx->write_size_total_bytes[i_entry];
                ctx->write_size_total_bytes[i_entry] = temp_total;
                break;
            case 11: /* time spent writing */
                temp_total = parse_long(in, buf);
                temp_total *= 1000;
                ctx->prev_write_latency_total_us[i_entry] = ctx->write_latency_total_us[i_entry];
                ctx->write_latency_total_us[i_entry] = temp_total;
                break;
            case 12: /* I/Os currently in progress */
                temp_total = parse_long(in, buf);
                ctx->num_req_in_prog[i_entry] = temp_total;
                break;
            default:
                break;
            }
        }
        if (skip_line == FLB_FALSE) {
            i_entry++;
        }
    }
    ctx->active_devices = i_entry;

    fclose(fp);
    return 0;
}


/* cb_collect callback */
static int in_isima_disk_collect(struct flb_input_instance *i_ins,
                           struct flb_config *config, void *in_context)
{
    struct flb_in_isima_disk_config *ctx = in_context;
    flb_sds_t mountpoint;
    (void) *i_ins;
    (void) *config;
    msgpack_packer mp_pck;
    msgpack_sbuffer mp_sbuf;

    int i;

    update_disk_stats(i_ins, ctx);

    if (ctx->first_snapshot == FLB_TRUE) {
        ctx->first_snapshot = FLB_FALSE;    /* assign first_snapshot with FLB_FALSE */
    }
    else {
        for (i = 0; i < ctx->active_devices; i++) {
            mountpoint = get_mountpoint(i_ins, ctx->device[i]);
            if (ctx->mounted_devices_only && (mountpoint == NULL)) {
                continue;
            }
            /* Initialize local msgpack buffer */
            msgpack_sbuffer_init(&mp_sbuf);
            msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

            /* Pack data */
            msgpack_pack_array(&mp_pck, 2);
            flb_pack_time_now(&mp_pck);
            if (mountpoint != NULL) {
                msgpack_pack_map(&mp_pck, 9);
            } else {
                msgpack_pack_map(&mp_pck, 8);
            }

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_DEVICE));
            msgpack_pack_str_body(&mp_pck, STR_KEY_DEVICE, strlen(STR_KEY_DEVICE));
            msgpack_pack_str(&mp_pck, strlen(ctx->device[i]));
            msgpack_pack_str_body(&mp_pck, ctx->device[i], strlen(ctx->device[i]));

            if (mountpoint != NULL) {
                msgpack_pack_str(&mp_pck, strlen(STR_KEY_MOUNTPOINT));
                msgpack_pack_str_body(&mp_pck, STR_KEY_MOUNTPOINT, strlen(STR_KEY_MOUNTPOINT));
                msgpack_pack_str(&mp_pck, strlen(mountpoint));
                msgpack_pack_str_body(&mp_pck, mountpoint, strlen(mountpoint));
                flb_sds_destroy(mountpoint);
            }

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_NUM_READS));
            msgpack_pack_str_body(&mp_pck, STR_KEY_NUM_READS, strlen(STR_KEY_NUM_READS));
            msgpack_pack_int64(&mp_pck, ctx->num_reads[i] - ctx->prev_num_reads[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_NUM_WRITES));
            msgpack_pack_str_body(&mp_pck, STR_KEY_NUM_WRITES, strlen(STR_KEY_NUM_WRITES));
            msgpack_pack_int64(&mp_pck, ctx->num_writes[i] - ctx->prev_num_writes[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_READ_SIZE));
            msgpack_pack_str_body(&mp_pck, STR_KEY_READ_SIZE, strlen(STR_KEY_READ_SIZE));
            msgpack_pack_int64(&mp_pck, ctx->read_size_total_bytes[i] - ctx->prev_read_size_total_bytes[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_WRITE_SIZE));
            msgpack_pack_str_body(&mp_pck, STR_KEY_WRITE_SIZE, strlen(STR_KEY_WRITE_SIZE));
            msgpack_pack_int64(&mp_pck, ctx->write_size_total_bytes[i] - ctx->prev_write_size_total_bytes[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_READ_LATENCY));
            msgpack_pack_str_body(&mp_pck, STR_KEY_READ_LATENCY, strlen(STR_KEY_READ_LATENCY));
            msgpack_pack_int64(&mp_pck, ctx->read_latency_total_us[i] - ctx->prev_read_latency_total_us[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_WRITE_LATENCY));
            msgpack_pack_str_body(&mp_pck, STR_KEY_WRITE_LATENCY, strlen(STR_KEY_WRITE_LATENCY));
            msgpack_pack_int64(&mp_pck, ctx->write_latency_total_us[i] - ctx->prev_write_latency_total_us[i]);

            msgpack_pack_str(&mp_pck, strlen(STR_KEY_NUM_IN_PROG));
            msgpack_pack_str_body(&mp_pck, STR_KEY_NUM_IN_PROG, strlen(STR_KEY_NUM_IN_PROG));
            msgpack_pack_int64(&mp_pck, ctx->num_req_in_prog[i]);

            /* Send to output */
            if (mp_sbuf.size > 0) {
                flb_input_chunk_append_raw(i_ins, NULL, 0, mp_sbuf.data, mp_sbuf.size);
            }
            msgpack_sbuffer_destroy(&mp_sbuf);
        }
    }

    return 0;
}

static int get_diskstats_entries(void)
{
    char line[LINE_SIZE] = {0};
    int   ret = 0;
    FILE *fp = NULL;

    fp = fopen("/proc/diskstats", "r");
    if (fp == NULL) {
        perror("fopen");
        return 0;
    }
    while (fgets(line, LINE_SIZE-1, fp) != NULL) {
        ret++;
    }

    fclose(fp);
    return ret;
}

static int configure(struct flb_in_isima_disk_config *disk_config,
                     struct flb_input_instance *in)
{
    (void) *in;
    int entry = 0;
    int i;
    int ret;

    /* Load the config map */
    ret = flb_input_config_map_set(in, (void *)disk_config);
    if (ret == -1) {
        flb_plg_error(in, "unable to load configuration.");
        return -1;
    }

    /* interval settings */
    if (disk_config->interval_sec <= 0 && disk_config->interval_nsec <= 0) {
        /* Illegal settings. Override them. */
        disk_config->interval_sec = atoi(DEFAULT_INTERVAL_SEC);
        disk_config->interval_nsec = atoi(DEFAULT_INTERVAL_NSEC);
    }

    entry = get_diskstats_entries();
    if (entry == 0) {
        /* no entry to count */
        return -1;
    }

    disk_config->device = (flb_sds_t*)flb_malloc(sizeof(flb_sds_t)*entry);
    disk_config->num_reads = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->num_writes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->read_size_total_bytes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->write_size_total_bytes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->read_latency_total_us = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->write_latency_total_us = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->num_req_in_prog = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_num_reads = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_num_writes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_read_size_total_bytes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_write_size_total_bytes = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_read_latency_total_us = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->prev_write_latency_total_us = (uint64_t*)flb_malloc(sizeof(uint64_t)*entry);
    disk_config->num_devices = entry;

    if (disk_config->device == NULL ||
        disk_config->num_reads == NULL ||
        disk_config->num_writes == NULL ||
        disk_config->read_size_total_bytes == NULL ||
        disk_config->write_size_total_bytes == NULL ||
        disk_config->read_latency_total_us == NULL ||
        disk_config->write_latency_total_us == NULL ||
        disk_config->num_req_in_prog == NULL ||
        disk_config->prev_num_reads == NULL ||
        disk_config->prev_num_writes == NULL ||
        disk_config->prev_read_size_total_bytes == NULL ||
        disk_config->prev_write_size_total_bytes == NULL ||
        disk_config->prev_read_latency_total_us == NULL ||
        disk_config->prev_write_latency_total_us == NULL) {
        flb_plg_error(in, "could not allocate memory");
        return -1;
    }

    /* initialize */
    for (i = 0; i < entry; i++) {
        disk_config->device[i] = NULL;
        disk_config->num_reads[i] = 0;
        disk_config->num_writes[i] = 0;
        disk_config->read_size_total_bytes[i] = 0;
        disk_config->write_size_total_bytes[i] = 0;
        disk_config->read_latency_total_us[i] = 0;
        disk_config->write_latency_total_us[i] = 0;
        disk_config->num_req_in_prog[i] = 0;
        disk_config->prev_num_reads[i] = 0;
        disk_config->prev_num_writes[i] = 0;
        disk_config->prev_read_size_total_bytes[i] = 0;
        disk_config->prev_write_size_total_bytes[i] = 0;
        disk_config->prev_read_latency_total_us[i] = 0;
        disk_config->prev_write_latency_total_us[i] = 0;
    }

    update_disk_stats(in, disk_config);

    /* assign first_snapshot with FLB_TRUE */
    disk_config->first_snapshot = FLB_TRUE;

    return 0;
}

/* Initialize plugin */
static int in_isima_disk_init(struct flb_input_instance *in,
                              struct flb_config *config, void *data)
{
    struct flb_in_isima_disk_config *disk_config = NULL;
    int ret = -1;
    int i;

    /* Allocate space for the configuration */
    disk_config = flb_calloc(1, sizeof(struct flb_in_isima_disk_config));
    if (disk_config == NULL) {
        return -1;
    }
    memset(disk_config, 0, sizeof(struct flb_in_isima_disk_config));

    /* Initialize head config */
    ret = configure(disk_config, in);
    if (ret < 0) {
        goto init_error;
    }

    flb_input_set_context(in, disk_config);

    ret = flb_input_set_collector_time(in,
                                       in_isima_disk_collect,
                                       disk_config->interval_sec,
                                       disk_config->interval_nsec, config);
    if (ret < 0) {
        flb_plg_error(in, "could not set collector for disk input plugin");
        goto init_error;
    }

    return 0;

  init_error:
    for (i = 0; i < disk_config->num_devices; i++) {
        flb_sds_destroy(disk_config->device[i]);
    }
    flb_free(disk_config->device);
    flb_free(disk_config->num_reads);
    flb_free(disk_config->num_writes);
    flb_free(disk_config->read_size_total_bytes);
    flb_free(disk_config->write_size_total_bytes);
    flb_free(disk_config->read_latency_total_us);
    flb_free(disk_config->write_latency_total_us);
    flb_free(disk_config->num_req_in_prog);
    flb_free(disk_config->prev_num_reads);
    flb_free(disk_config->prev_num_writes);
    flb_free(disk_config->prev_read_size_total_bytes);
    flb_free(disk_config->prev_write_size_total_bytes);
    flb_free(disk_config->prev_read_latency_total_us);
    flb_free(disk_config->prev_write_latency_total_us);
    flb_free(disk_config);
    return -1;
}

static int in_isima_disk_exit(void *data, struct flb_config *config)
{
    (void) *config;
    struct flb_in_isima_disk_config *disk_config = data;
    int i;

    for (i = 0; i < disk_config->num_devices; i++) {
        flb_sds_destroy(disk_config->device[i]);
    }
    flb_free(disk_config->device);
    flb_free(disk_config->num_reads);
    flb_free(disk_config->num_writes);
    flb_free(disk_config->read_size_total_bytes);
    flb_free(disk_config->write_size_total_bytes);
    flb_free(disk_config->read_latency_total_us);
    flb_free(disk_config->write_latency_total_us);
    flb_free(disk_config->num_req_in_prog);
    flb_free(disk_config->prev_num_reads);
    flb_free(disk_config->prev_num_writes);
    flb_free(disk_config->prev_read_size_total_bytes);
    flb_free(disk_config->prev_write_size_total_bytes);
    flb_free(disk_config->prev_read_latency_total_us);
    flb_free(disk_config->prev_write_latency_total_us);
    flb_free(disk_config);
    return 0;
}

/* Configuration properties map */
static struct flb_config_map config_map[] = {
    {
      FLB_CONFIG_MAP_INT, "interval_sec", DEFAULT_INTERVAL_SEC,
      0, FLB_TRUE, offsetof(struct flb_in_isima_disk_config, interval_sec),
      "Set the collector interval"
    },
    {
      FLB_CONFIG_MAP_INT, "interval_nsec", DEFAULT_INTERVAL_NSEC,
      0, FLB_TRUE, offsetof(struct flb_in_isima_disk_config, interval_nsec),
      "Set the collector interval (nanoseconds)"
    },
    {
      FLB_CONFIG_MAP_STR, "dev_name", (char *)NULL,
      0, FLB_TRUE, offsetof(struct flb_in_isima_disk_config, dev_name),
      "Set the device name"
    },
    {
      FLB_CONFIG_MAP_BOOL, "mounted_devices_only", "true",
      0, FLB_TRUE, offsetof(struct flb_in_isima_disk_config, mounted_devices_only),
      "Show only the devices that are mounted"
    },
    /* EOF */
    {0}
};

struct flb_input_plugin in_isima_disk_plugin = {
    .name         = "isima_disk",
    .description  = "Isima Disk Stats",
    .cb_init      = in_isima_disk_init,
    .cb_pre_run   = NULL,
    .cb_collect   = in_isima_disk_collect,
    .cb_flush_buf = NULL,
    .cb_exit      = in_isima_disk_exit,
    .config_map   = config_map
};
