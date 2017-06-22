/************************************************************************
* LEGALESE:   "Copyright (c) 2017, Dell Inc. All rights reserved."
*
* This source code is confidential, proprietary, and contains trade
* secrets that are the sole property of Dell Inc.
* Copy and/or distribution of this source code or disassembly or reverse
* engineering of the resultant object code are strictly forbidden without
* the written consent of Dell Inc.
*
************************************************************************/
/**
* @file  sai_qos_wred_debugs.c
*
* @brief This file contains function definitions for debug routines to
*        dump the data structure information for QoS Wred objects.
*
*************************************************************************/
#include "saitypes.h"
#include "sai_qos_util.h"
#include "sai_qos_common.h"
#include "sai_debug_utils.h"
#include "std_type_defs.h"
#include <inttypes.h>

void sai_qos_wred_dump_help (void)
{
    SAI_DEBUG ("******************** Dump routines ***********************");

    SAI_DEBUG ("  void sai_qos_wred_dump (sai_object_id_t wred_id)");
    SAI_DEBUG ("  void sai_qos_wred_dump_all (void)");
}

void sai_qos_wred_dump_node(dn_sai_qos_wred_t  *p_wred_node)
{
    size_t color = 0;
    dn_sai_qos_queue_t *p_queue_node = NULL;
    if(p_wred_node == NULL){
        SAI_DEBUG("Wred node is NULL");
        return;
    }

    SAI_DEBUG("Wredid : 0x%"PRIx64" weight: %d ecn_mark_enable: %d",
              p_wred_node->key.wred_id, p_wred_node->weight, p_wred_node->ecn_mark_enable);

    SAI_DEBUG("Wred Thresholds:");
    for(color = 0; color < SAI_QOS_MAX_PACKET_COLORS; color ++){
        SAI_DEBUG("Color %ld", color);
        SAI_DEBUG("Enable: %d Minlimit: %d Maxlimit: %d Dropprobability: %d",
                  p_wred_node->threshold[color].enable, p_wred_node->threshold[color].min_limit,
                  p_wred_node->threshold[color].max_limit,p_wred_node->threshold[color].drop_probability);
    }

    p_queue_node = sai_qos_queue_node_from_wred_get(p_wred_node);

    while(p_queue_node != NULL){
        SAI_DEBUG("Wrednode applied on queue id 0x%"PRIx64"", p_queue_node->key.queue_id);
        p_queue_node = sai_qos_next_queue_node_from_wred_get(p_wred_node, p_queue_node);
    }

}

void sai_qos_wred_dump(sai_object_id_t wred_id)
{
    dn_sai_qos_wred_t *p_wred_node = NULL;

    SAI_DEBUG("Dumping the wred node contents: ");
    p_wred_node = sai_qos_wred_node_get(wred_id);

    if(p_wred_node != NULL){
        sai_qos_wred_dump_node(p_wred_node);
    }
    else{
        SAI_DEBUG("Policer id not in tree");
    }
}

void sai_qos_wred_dump_all(void)
{
    rbtree_handle  wred_tree;
    dn_sai_qos_wred_t *p_wred_node = NULL;

    wred_tree = sai_qos_access_global_config()->wred_tree;

    p_wred_node = std_rbtree_getfirst (wred_tree);
    while(p_wred_node != NULL){
        sai_qos_wred_dump_node (p_wred_node);

        p_wred_node = std_rbtree_getnext (wred_tree, p_wred_node);
    }
}
