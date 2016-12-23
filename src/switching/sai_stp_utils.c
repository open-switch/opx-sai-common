/************************************************************************
* LEGALESE:   "Copyright (c) 2015, Dell Inc. All rights reserved."
*
* This source code is confidential, proprietary, and contains trade
* secrets that are the sole property of Dell Inc.
* Copy and/or distribution of this source code or disassembly or reverse
* engineering of the resultant object code are strictly forbidden without
* the written consent of Dell Inc.
*
************************************************************************/
/**
* @file sai_stp_utils.c
*
* @brief This file contains memory alloc and free functions for SAI stp
*        data structures.
*
*************************************************************************/

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
