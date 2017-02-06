/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/**
 * @file sai_stp_utils.c
 *
 * @brief This file contains memory alloc and free functions for SAI stp
 *        data structures.
 */

#include <stdlib.h>
#include "std_mutex_lock.h"

#include "sai_stp_defs.h"
#include "sai_stp_api.h"
#include "saistp.h"

static std_mutex_type_t stp_lock;

dn_sai_stp_info_t *sai_stp_info_node_alloc (void)
{
    return ((dn_sai_stp_info_t *)calloc (1,
                                        sizeof (dn_sai_stp_info_t)));
}

void sai_stp_info_node_free (dn_sai_stp_info_t *p_stp_info)
{
    free ((void *) p_stp_info);
}

sai_vlan_id_t *sai_stp_vlan_node_alloc (void)
{
    return ((sai_vlan_id_t *)calloc (1,
                                        sizeof (sai_vlan_id_t)));
}

void sai_stp_vlan_node_free (sai_vlan_id_t *p_vlan_node)
{
    free ((void *) p_vlan_node);
}

void sai_stp_mutex_lock_init(void)
{
    std_mutex_lock_create_static_init_fast (fast_lock);

    stp_lock = fast_lock;
}

void sai_stp_lock(void)
{
    std_mutex_lock (&stp_lock);
}

void sai_stp_unlock(void)
{
    std_mutex_unlock (&stp_lock);
}
