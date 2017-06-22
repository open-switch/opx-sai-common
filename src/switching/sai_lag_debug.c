/************************************************************************
* * LEGALESE:   "Copyright (c) 2017, Dell Inc. All rights reserved."
* *
* * This source code is confidential, proprietary, and contains trade
* * secrets that are the sole property of Dell Inc.
* * Copy and/or distribution of this source code or disassembly or reverse
* * engineering of the resultant object code are strictly forbidden without
* * the written consent of Dell Inc.
* *
************************************************************************/
/**
* @file sai_lag_debug.c
*
* @brief This file contains debug APIs for SAI LAG module
*************************************************************************/

#include "saistatus.h"
#include "saitypes.h"
#include "std_llist.h"
#include "sai_lag_common.h"
#include "sai_lag_api.h"
#include "sai_debug_utils.h"
#include "sai_oid_utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

sai_status_t sai_dump_lag_port_list(sai_object_id_t lag_id)
{
    sai_lag_node_t *lag_node = sai_lag_node_get(lag_id);
    sai_lag_port_node_t *lag_port_node = NULL;
    std_dll *node = NULL;

    if(lag_node == NULL) {
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    SAI_DEBUG("Lag ID:0x%"PRIx64" Trunk ID:%d  port count:%d",lag_node->sai_lag_id,
            (int)sai_uoid_npu_obj_id_get(lag_node->sai_lag_id),
            lag_node->port_count);
    SAI_DEBUG("Port List:");
    for(node = std_dll_getfirst(&(lag_node->port_list));
        node != NULL;
        node = std_dll_getnext(&(lag_node->port_list),node)) {
        lag_port_node = (sai_lag_port_node_t *)node;
        SAI_DEBUG("\t0x%"PRIx64"(%d) ",lag_port_node->port_id,
              (int)sai_uoid_npu_obj_id_get(lag_port_node->port_id));
    }
    SAI_DEBUG("\r\n");

    return SAI_STATUS_SUCCESS;
}

void sai_dump_lag_info(sai_object_id_t lag_id, bool all)
{
    sai_lag_node_t *lag_node = NULL;
    std_dll *node = NULL;
    std_dll_head *lag_list = sai_lag_list_get();
    sai_object_id_t sai_lag_id = SAI_NULL_OBJECT_ID;

    if(all) {
        for(node = std_dll_getfirst(lag_list);
                node != NULL;
                node = std_dll_getnext(lag_list,node)) {
            lag_node = (sai_lag_node_t *)node;
            sai_dump_lag_port_list(lag_node->sai_lag_id);
        }
    } else {
        if(sai_is_obj_id_stp_instance(lag_id)) {
            sai_lag_id = lag_id;
        } else {
            sai_lag_id = sai_uoid_create(SAI_OBJECT_TYPE_LAG,
                    (sai_npu_object_id_t)lag_id);
        }

        if(sai_dump_lag_port_list(sai_lag_id) != SAI_STATUS_SUCCESS) {
            SAI_DEBUG("Lag/Trunk ID:0x%"PRIx64" not found or invalid\r\n",lag_id);
        }
    }

    return;
}
