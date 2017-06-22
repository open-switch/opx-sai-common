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
#include "saitypes.h"
#include "saistatus.h"
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

dn_sai_stp_port_info_t *sai_stp_port_node_alloc (void)
{
    return ((dn_sai_stp_port_info_t *)calloc (1,
                sizeof (dn_sai_stp_port_info_t)));
}

void sai_stp_port_node_free (dn_sai_stp_port_info_t *p_stp_port_node)
{
    free ((void *) p_stp_port_node);
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

bool sai_stp_can_port_learn_mac (sai_vlan_id_t vlan_id, sai_object_id_t port_id)
{
    sai_status_t sai_rc;
    sai_object_id_t stp_inst_id;
    sai_stp_port_state_t port_stp_state;
    bool mac_learn_allowed = false;

    sai_stp_lock ();
    stp_inst_id =  sai_stp_get_instance_from_vlan_map(vlan_id);

    sai_rc = sai_npu_stp_port_state_get (stp_inst_id,port_id, &port_stp_state);

    if(sai_rc == SAI_STATUS_SUCCESS) {
        if(port_stp_state !=  SAI_STP_PORT_STATE_BLOCKING) {
            mac_learn_allowed = true;
        }
    }

    sai_stp_unlock ();
    return mac_learn_allowed;
}

bool sai_stp_port_state_valid(sai_stp_port_state_t port_state)
{
    switch (port_state) {
        case SAI_STP_PORT_STATE_LEARNING:
        case SAI_STP_PORT_STATE_FORWARDING:
        case SAI_STP_PORT_STATE_BLOCKING:
            return true;
        default:
            return false;
    }
}
