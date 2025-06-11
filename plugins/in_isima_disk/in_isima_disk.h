/*
 * Copyright (C) 2025 Isima, Inc.
 *
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

#ifndef FLB_IN_ISIMA_DISK_H
#define FLB_IN_ISIMA_DISK_H

#include <stdint.h>
#include <fluent-bit/flb_config.h>
#include <fluent-bit/flb_input.h>

#define DEFAULT_INTERVAL_SEC  "30"
#define DEFAULT_INTERVAL_NSEC "0"

#define STR_KEY_DEVICE "device"
#define STR_KEY_MOUNTPOINT "mountpoint"
#define STR_KEY_NUM_READS  "numReads"
#define STR_KEY_NUM_WRITES "numWrites"
#define STR_KEY_READ_SIZE "bytesRead"
#define STR_KEY_WRITE_SIZE "bytesWritten"
#define STR_KEY_READ_LATENCY "readLatencySum"
#define STR_KEY_WRITE_LATENCY "writeLatencySum"
#define STR_KEY_NUM_IN_PROG "numReqInProgress"

struct flb_in_isima_disk_config {
    flb_sds_t dev_name;
    flb_sds_t *device;
    uint64_t  *num_reads;
    uint64_t  *num_writes;
    uint64_t  *read_size_total_bytes;
    uint64_t  *write_size_total_bytes;
    uint64_t  *read_latency_total_us;
    uint64_t  *write_latency_total_us;
    uint64_t  *num_req_in_prog;
    uint64_t  *prev_num_reads;
    uint64_t  *prev_num_writes;
    uint64_t  *prev_read_size_total_bytes;
    uint64_t  *prev_write_size_total_bytes;
    uint64_t  *prev_read_latency_total_us;
    uint64_t  *prev_write_latency_total_us;
    int       active_devices;
    int       num_devices;
    int       interval_sec;
    int       interval_nsec;
    int       mounted_devices_only;
    /* field to indicate whethor or not this is the first collect */
    int       first_snapshot;
};

extern struct flb_input_plugin in_isima_disk_plugin;

#endif /* FLB_IN_ISIMA_DISK_H */
