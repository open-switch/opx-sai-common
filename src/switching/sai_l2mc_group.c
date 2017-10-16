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
* @file sai_l2mc.c
*
* @brief This file contains implementation of SAI L2MC APIs.
*************************************************************************/

#include <stdlib.h>
#include <inttypes.h>
#include "std_assert.h"
#include "sail2mc.h"
#include "sail2mcgroup.h"
#include "saitypes.h"
#include "saistatus.h"
#include "saiport.h"
#include "sai_modules_init.h"
#include "sai_l2mc_common.h"
#include "sai_l2mc_api.h"
#include "sai_mcast_api.h"
#include "sai_npu_port.h"
#include "sai_npu_l2mc.h"
#include "sai_switch_utils.h"
#include "sai_gen_utils.h"
#include "sai_oid_utils.h"
#include "sai_stp_api.h"
#include "sai_common_infra.h"
#include "sai_lag_api.h"
#include "sai_map_utl.h"
#include "sai_bridge_api.h"
#include "sai_bridge_main.h"

sai_status_t sai_l2mc_handle_lag_change_notification(sai_object_id_t bridge_port_id,
        const sai_bridge_port_notif_t *data)
{
    uint_t                    l2mc_member_cnt = 0;
    uint_t                     member_idx = 0;
    sai_object_id_t           *l2mc_member_list = NULL;
    sai_status_t              sai_rc = SAI_STATUS_FAILURE;
    dn_sai_l2mc_member_node_t *l2mc_member_node = NULL;


    sai_l2mc_lock();
    sai_lag_lock();
    do {
        sai_rc = sai_bridge_port_to_l2mc_member_count_get(bridge_port_id, &l2mc_member_cnt);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_L2MC_LOG_ERR("Error %d in obtaining the l2mc member count for bridge port 0x%"PRIx64"",
                             sai_rc, bridge_port_id);
            break;
        }

        if(l2mc_member_cnt == 0) {
            SAI_L2MC_LOG_TRACE("No L2MC members associated to bridge port 0x%"PRIx64"",
                               bridge_port_id);
            sai_rc =  SAI_STATUS_SUCCESS;
            break;
        }

        l2mc_member_list = calloc(0, sizeof(sai_object_id_t)*l2mc_member_cnt);

        if(l2mc_member_list == NULL) {
            SAI_L2MC_LOG_ERR("Unable to allocate memory %d in l2mc lag handler",
                             sizeof(sai_object_id_t)*l2mc_member_cnt);
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }

        sai_rc = sai_bridge_port_to_l2mc_member_list_get(bridge_port_id, &l2mc_member_cnt, l2mc_member_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_L2MC_LOG_ERR("Error %d in obtaining the l2mc member list for bridge port "
                             "0x%"PRIx64"", sai_rc, bridge_port_id);
            break;
        }

        for(member_idx = 0; member_idx < l2mc_member_cnt; member_idx++) {
            l2mc_member_node = sai_find_l2mc_member_node(l2mc_member_list[member_idx]);
            if(l2mc_member_node == NULL) {
                SAI_L2MC_LOG_ERR("Error - Unable to find info for l2mc member 0x%"PRIx64"",
                                 l2mc_member_list[member_idx]);
                sai_rc = SAI_STATUS_INVALID_OBJECT_ID;
                break;
            }
            sai_rc = sai_l2mc_npu_api_get()->
                l2mc_member_lag_notif_handler(l2mc_member_node, data->lag_id,
                                              data->lag_port_mod_list->count,
                                              data->lag_port_mod_list->list,
                                              data->lag_add_port);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_L2MC_LOG_ERR("Error %d in npu update of lag handler for l2mc member "
                                 "0x%"PRIx64"",l2mc_member_list[member_idx]);
                break;
            }
        }

    } while(0);

    if(l2mc_member_list != NULL) {
        free(l2mc_member_list);
    }
    sai_lag_unlock();
    sai_l2mc_unlock();
    return sai_rc;
}

static sai_status_t sai_l2mc_bridge_port_notif_handler(sai_object_id_t bridge_port_id,
                                                       const sai_bridge_port_notif_t *data)
{
    if(data == NULL) {
        SAI_L2MC_LOG_ERR("NULL data in bridge port 0x%"PRIx64" callback",bridge_port_id);
    }

    if(data->event == SAI_BRIDGE_PORT_EVENT_LAG_MODIFY) {
        return sai_l2mc_handle_lag_change_notification(bridge_port_id, data);
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_l2mc_init(void)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    uint_t type_bmp = SAI_BRIDGE_PORT_TYPE_TO_BMP(SAI_BRIDGE_PORT_TYPE_PORT);

    sai_rc = sai_l2mc_tree_init();
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_L2MC_LOG_ERR("L2mc Tree init failed");
    }

    sai_rc = sai_l2mc_npu_api_get()->l2mc_init();
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_L2MC_LOG_ERR("L2mc NPU init failed");
    }

    sai_rc = sai_mcast_init();
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_L2MC_LOG_ERR("MCAST NPU init failed");
    }
    sai_rc = sai_bridge_port_event_cb_register(SAI_MODULE_L2MC, type_bmp,
                                          sai_l2mc_bridge_port_notif_handler);
    return sai_rc;
}

static sai_status_t sai_l2mc_create_group(
        sai_object_id_t *l2mc_obj_id,
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_l2mc_group_node_t l2mc_group_node;

    if (attr_count > 0) {
        return SAI_STATUS_INVALID_PARAMETER;
    }

    SAI_L2MC_LOG_TRACE("Creating L2MC Group ");
    sai_l2mc_lock();
    do {

        sai_rc = sai_l2mc_npu_api_get()->l2mc_group_create(&l2mc_group_node);
        if((sai_rc != SAI_STATUS_SUCCESS)) {
            break;
        }

        *l2mc_obj_id = l2mc_group_node.l2mc_group_id;
        if((sai_rc = sai_add_l2mc_group_node(&l2mc_group_node))
                != SAI_STATUS_SUCCESS) {
            SAI_L2MC_LOG_ERR("Unable to add L2mc Group id 0x%"PRIx64"",
                    sai_obj_id_to_l2mc_grp_id(l2mc_group_node.l2mc_group_id));
            sai_l2mc_npu_api_get()->l2mc_group_delete(&l2mc_group_node);
        }
        SAI_L2MC_LOG_TRACE("Created L2MC Group id 0x%"PRIx64"",
                sai_obj_id_to_l2mc_grp_id(l2mc_group_node.l2mc_group_id));


    }while(0);

    sai_l2mc_unlock();

    return sai_rc;
}

static sai_status_t sai_l2mc_set_group_attribute(
        sai_object_id_t l2mc_group_id,
        const sai_attribute_t *attr)
{
    /* No set attribute present for l2mc group */
    return SAI_STATUS_INVALID_ATTRIBUTE_0;
}

static sai_status_t sai_l2mc_get_group_attribute(
        sai_object_id_t l2mc_group_id,
        uint32_t attr_count,
        sai_attribute_t *attr_list)
{
    dn_sai_l2mc_group_node_t *l2mc_group_node = NULL;
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    unsigned int attr_idx = 0;
    unsigned int l2mc_id;

    if(!sai_is_obj_id_l2mc_group(l2mc_group_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    STD_ASSERT(attr_list != NULL);
    l2mc_id = sai_obj_id_to_l2mc_grp_id(l2mc_group_id);
    if(attr_count == 0) {
        SAI_L2MC_LOG_ERR("Invalid attribute count 0 for L2MC id:0x%"PRIx64"",l2mc_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_l2mc_lock();
    if((l2mc_group_node = sai_find_l2mc_group_node(l2mc_group_id)) == NULL) {
        sai_l2mc_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id)
        {
            case SAI_L2MC_GROUP_ATTR_L2MC_OUTPUT_COUNT:
                SAI_L2MC_LOG_TRACE("Getting member count for L2MC id 0x%"PRIx64"", l2mc_id);
                attr_list[attr_idx].value.u32 = l2mc_group_node->port_count;
                break;
            case SAI_L2MC_GROUP_ATTR_L2MC_MEMBER_LIST:
                SAI_L2MC_LOG_TRACE("Getting member list for L2MC id 0x%"PRIx64"", l2mc_id);
                sai_rc = sai_l2mc_port_list_get(l2mc_group_node, &attr_list[attr_idx].value.objlist);
                break;
            default:
                SAI_L2MC_LOG_TRACE("Invalid attr : %d for L2mc Group id 0x%"PRIx64"",
                        attr_list[attr_idx], l2mc_id);
                break;
        }
        if(sai_rc != SAI_STATUS_SUCCESS){
            sai_rc = sai_get_indexed_ret_val(sai_rc, attr_idx);
            break;
        }
    }
    sai_l2mc_unlock();
    return sai_rc;
}

static sai_status_t sai_l2mc_remove_group(sai_object_id_t l2mc_group_id)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    dn_sai_l2mc_group_node_t *l2mc_group_node = NULL;

    SAI_L2MC_LOG_TRACE("Removing L2MC Group 0x%"PRIx64"",
            sai_obj_id_to_l2mc_grp_id(l2mc_group_id));
    sai_l2mc_lock();
    do {
        if((l2mc_group_node = sai_find_l2mc_group_node(l2mc_group_id)) == NULL) {
            sai_rc = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if(l2mc_group_node->port_count > 0) {
            sai_rc = SAI_STATUS_OBJECT_IN_USE;
            break;
        }

        if((sai_rc = sai_l2mc_npu_api_get()->l2mc_group_delete(l2mc_group_node))
                != SAI_STATUS_SUCCESS) {
            break;
        }
        sai_rc = sai_remove_l2mc_group_node(l2mc_group_node);
        SAI_L2MC_LOG_TRACE("Removed L2MC Group ");
    } while(0);

    sai_l2mc_unlock();
    return sai_rc;
}

static sai_status_t sai_l2mc_check_if_attr_bridge_port_is_lag(uint32_t attr_count,
                                                              const sai_attribute_t *attr_list,
                                                              bool *is_lag)
{
    uint_t attr_idx;

    if((attr_list == NULL) || (is_lag == NULL)) {
        SAI_L2MC_LOG_TRACE("Error attr_list is %p and is_lag is %p in check if bridge port is lag",
                           attr_list, is_lag);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    *is_lag = false;
    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        if(attr_list[attr_idx].id == SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_OUTPUT_ID) {
            if(sai_is_bridge_port_obj_lag(attr_list[attr_idx].value.oid)) {
                *is_lag = true;
                break;
            }
        }
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2mc_create_group_member(sai_object_id_t *l2mc_member_id,
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    bool l2mc_grp_attr_present = false;
    bool port_id_attr_present = false;
    dn_sai_l2mc_member_node_t l2mc_member_node;
    dn_sai_l2mc_group_node_t *l2mc_group_node;
    unsigned int l2mc_id = 0;
    uint32_t attr_idx = 0;
    bool is_lag = false;

    STD_ASSERT (l2mc_member_id != NULL);
    STD_ASSERT (attr_list != NULL);

    *l2mc_member_id = SAI_INVALID_L2MC_MEMBER_ID;

    if (attr_count == 0) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    } else {
        STD_ASSERT ((attr_list != NULL));
    }

    sai_rc = sai_l2mc_check_if_attr_bridge_port_is_lag(attr_count, attr_list, &is_lag);

    l2mc_member_node.switch_id = switch_id;

    for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch (attr_list [attr_idx].id) {
            case SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_GROUP_ID:
                if(!sai_is_obj_id_l2mc_group(attr_list[attr_idx].value.oid)) {
                    return SAI_STATUS_INVALID_OBJECT_ID;
                }
                l2mc_member_node.l2mc_group_id = attr_list[attr_idx].value.oid;
                l2mc_id = sai_obj_id_to_l2mc_grp_id(
                        attr_list[attr_idx].value.oid);
                l2mc_grp_attr_present = true;
                break;
            case SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_OUTPUT_ID:
                l2mc_member_node.bridge_port_id = attr_list[attr_idx].value.oid;
                port_id_attr_present = true;
                break;
            default:
                return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
        }
    }

    if(!(l2mc_grp_attr_present) || !(port_id_attr_present)) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    sai_l2mc_lock();
    if(is_lag) {
        sai_lag_lock();
    }
    do {
        if((l2mc_group_node = sai_find_l2mc_group_node(l2mc_member_node.l2mc_group_id)) == NULL) {
            sai_rc = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if(sai_find_l2mc_member_node_from_port(l2mc_group_node, l2mc_member_node.bridge_port_id)) {
            sai_rc = SAI_STATUS_ITEM_ALREADY_EXISTS;
            break;
        }

        if((sai_rc = sai_l2mc_npu_api_get()->l2mc_member_create(&l2mc_member_node))
                != SAI_STATUS_SUCCESS) {
            break;
        }

        *l2mc_member_id = l2mc_member_node.l2mc_member_id;
        if((sai_rc = sai_add_l2mc_member_node(l2mc_member_node))
                != SAI_STATUS_SUCCESS) {
            SAI_L2MC_LOG_ERR("Unable to add L2MC member 0x%"PRIx64" \
                    to Group:0x%"PRIx64" cache",
                    l2mc_member_node.l2mc_member_id,l2mc_id);
            sai_l2mc_npu_api_get()->l2mc_member_remove(&l2mc_member_node);
            break;
        }
        if(is_lag) {
            sai_bridge_port_to_l2mc_member_map_insert(l2mc_member_node.bridge_port_id, *l2mc_member_id);
        }

        SAI_L2MC_LOG_TRACE("Added port 0x%"PRIx64" on L2mc Group:0x%"PRIx64"",
                l2mc_member_node.bridge_port_id, l2mc_id);
    } while(0);

    if(is_lag) {
        sai_lag_unlock();
    }
    sai_l2mc_unlock();
    return sai_rc;
}

static sai_status_t sai_l2mc_set_group_member_attribute(sai_object_id_t l2mc_member_id,
        const sai_attribute_t *attr)
{
    /* There is no set attribute for the group member */
    return SAI_STATUS_INVALID_ATTRIBUTE_0;
}

static sai_status_t sai_l2mc_get_group_member_attribute(sai_object_id_t l2mc_member_id,
        const uint32_t attr_count, sai_attribute_t *attr_list)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_l2mc_member_node_t *l2mc_member_node = NULL;
    uint32_t attr_idx = 0;

    STD_ASSERT(attr_list != NULL);
    if (attr_count == 0) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    } else if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    }

    if(!sai_is_obj_id_l2mc_member(l2mc_member_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_l2mc_lock();
    if((l2mc_member_node = sai_find_l2mc_member_node(l2mc_member_id))
            == NULL) {
        sai_rc = SAI_STATUS_ITEM_NOT_FOUND;
    } else {
        for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            switch (attr_list [attr_idx].id) {
                case SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_GROUP_ID:
                    attr_list[attr_idx].value.oid = l2mc_member_node->l2mc_group_id;
                    break;
                case SAI_L2MC_GROUP_MEMBER_ATTR_L2MC_OUTPUT_ID:
                    attr_list[attr_idx].value.oid = l2mc_member_node->bridge_port_id;
                    break;
                default:
                    sai_rc = (SAI_STATUS_UNKNOWN_ATTRIBUTE_0+attr_idx);
                    break;
            }
            if(SAI_STATUS_SUCCESS != sai_rc) {
                break;
            }
        }
    }

    sai_l2mc_unlock();
    return sai_rc;
}

static sai_status_t sai_l2mc_remove_group_member(sai_object_id_t l2mc_member_id)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    dn_sai_l2mc_member_node_t *l2mc_member_node = NULL;
    bool is_lag = false;

    if(!sai_is_obj_id_l2mc_member(l2mc_member_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_l2mc_lock();
    if((l2mc_member_node = sai_find_l2mc_member_node(l2mc_member_id)) == NULL) {
        sai_l2mc_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    is_lag = sai_is_bridge_port_obj_lag(l2mc_member_node->bridge_port_id);
    if(is_lag) {
        sai_lag_lock();
    }

    do {
        if((sai_rc = sai_l2mc_npu_api_get()->l2mc_member_remove(l2mc_member_node))
                != SAI_STATUS_SUCCESS) {
            break;
        }
        if(is_lag) {
            sai_bridge_port_to_l2mc_member_map_remove(l2mc_member_node->bridge_port_id, l2mc_member_id);
        }

        sai_rc = sai_remove_l2mc_member_node(l2mc_member_node);
    } while(0);

    if(is_lag) {
        sai_lag_unlock();
    }
    sai_l2mc_unlock();
    return sai_rc;
}

static sai_l2mc_group_api_t sai_l2mc_group_method_table =
{
    sai_l2mc_create_group,
    sai_l2mc_remove_group,
    sai_l2mc_set_group_attribute,
    sai_l2mc_get_group_attribute,
    sai_l2mc_create_group_member,
    sai_l2mc_remove_group_member,
    sai_l2mc_set_group_member_attribute,
    sai_l2mc_get_group_member_attribute,
};

sai_l2mc_group_api_t  *sai_l2mc_group_api_query (void)
{
    return (&sai_l2mc_group_method_table);
}
