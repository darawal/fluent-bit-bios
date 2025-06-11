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

#ifndef FLB_IN_ISIMA_NETIF_H
#define FLB_IN_ISIMA_NETIF_H

#include <stdint.h>
#include <unistd.h>

#include <fluent-bit/flb_input.h>
#include <msgpack.h>

#define DEFAULT_INTERVAL_SEC  "30"
#define DEFAULT_INTERVAL_NSEC "0"

#define FLB_in_isima_netif_NAME "in_isima_netif"

#define STR_KEY_INTERFACE "interface"

struct entry_define
{
    char  *name;
    int   checked;
};

struct netif_entry {
    int   checked;

    char  *name;
    int   name_len;

    uint64_t prev;
    uint64_t now;
};

struct netif_desc {
    flb_sds_t interface;
    int interface_len;

    struct netif_entry *entry;
};

struct flb_in_isima_netif_config {
    int interval_sec;
    int interval_nsec;

    /* a field to indicate whether or not this is the first collect */
    int first_snapshot;
    int reinit; 

    struct netif_desc* if_desc;
    int if_desc_len;
    int entry_len;
    int map_num;

    struct flb_input_instance *ins;
};

#endif /* FLB_IN_ISIMA_NETIF_H */
