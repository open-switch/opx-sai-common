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
* @file sai_bridge.c
*
* @brief This file contains implementation of SAI Bridge APIs.
*************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "std_assert.h"
#include "saibridge.h"
#include "saitypes.h"
#include "saistatus.h"
#include "sai_lag_callback.h"
#include "sai_bridge_common.h"
#include "sai_bridge_npu_api.h"
#include "sai_bridge_api.h"
#include "sai_port_utils.h"
#include "sai_port_common.h"
#include "sai_lag_api.h"
#include "sai_oid_utils.h"
#include "sai_vlan_api.h"
#include "sai_l3_util.h"
#include "sai_common_infra.h"
#include "sai_switch_utils.h"
#include "sai_tunnel_util.h"
#include "sai_bridge_main.h"
#include "sai_gen_utils.h"

/* SAI default 1Q bridge */
static sai_object_id_t  sai_default_bridge_id = SAI_NULL_OBJECT_ID;

static sai_bridge_port_cb_t sai_bridge_port_cb[SAI_MODULE_MAX];

sai_status_t sai_bridge_port_event_cb_register(sai_module_t module_id,
                                               uint_t bridge_port_type_bmp,
                                               sai_bridge_port_event_cb_fn bridge_port_cb)
{
    if(module_id >= SAI_MODULE_MAX) {
        SAI_LAG_LOG_ERR("Error! Unknown module id %d",module_id);
        return SAI_STATUS_FAILURE;
    }
    sai_bridge_port_cb[module_id].bridge_port_type_bmp = bridge_port_type_bmp;
    sai_bridge_port_cb[module_id].bridge_port_event_cb = bridge_port_cb;
    return SAI_STATUS_SUCCESS;
}

static void sai_bridge_port_send_internal_notifs(sai_object_id_t bridge_port_id,
                                                 sai_bridge_port_type_t bridge_port_type,
                                                 const sai_bridge_port_notif_t *notif)
{
    sai_module_t mod_idx = 0;
    sai_status_t sai_rc = SAI_STATUS_FAILURE;

    if(notif == NULL) {
        SAI_BRIDGE_LOG_TRACE("Notif is NULL for bridge port 0x%"PRIx64" in send internal notif",
                             bridge_port_id);
        return;
    }
    for(mod_idx = 0; mod_idx < SAI_MODULE_MAX; mod_idx++) {
        if(sai_bridge_port_cb[mod_idx].bridge_port_event_cb != NULL) {
           if(SAI_IS_BRIDGE_PORT_TYPE_SUBSCRIBED(bridge_port_type,
                                                sai_bridge_port_cb[mod_idx].bridge_port_type_bmp)){
               sai_rc = sai_bridge_port_cb[mod_idx].bridge_port_event_cb(bridge_port_id, notif);
               if(sai_rc != SAI_STATUS_SUCCESS) {
                  SAI_BRIDGE_LOG_ERR("Error %d in invoking callback for module id %d"
                                     " bridge port 0x%"PRIx64" event %d", sai_rc, mod_idx,
                                     bridge_port_id, notif->event);
               }
           }
        }
    }
}

static inline sai_status_t sai_bridge_attr_valid_on_set (sai_bridge_attr_t attr_id)
{
    switch(attr_id) {
        case SAI_BRIDGE_ATTR_TYPE:
        case SAI_BRIDGE_ATTR_PORT_LIST:
            return SAI_STATUS_INVALID_ATTRIBUTE_0;
        case SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES:
        case SAI_BRIDGE_ATTR_LEARN_DISABLE:
            return SAI_STATUS_SUCCESS;
        default:
            return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    }
    return SAI_STATUS_FAILURE;
}

static inline bool sai_bridge_non_mandatory_attr(sai_bridge_attr_t attr_id)
{
    switch(attr_id) {
        case SAI_BRIDGE_ATTR_TYPE:
            return false;
        case SAI_BRIDGE_ATTR_PORT_LIST:
        case SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES:
        case SAI_BRIDGE_ATTR_LEARN_DISABLE:
            return true;
        default:
            return false;
    }
    return false;
}

static inline sai_status_t sai_bridge_port_attr_valid_on_set (sai_bridge_port_attr_t attr_id)
{
    switch(attr_id) {
        case SAI_BRIDGE_PORT_ATTR_TYPE:
        case SAI_BRIDGE_PORT_ATTR_PORT_ID:
        case SAI_BRIDGE_PORT_ATTR_VLAN_ID:
        case SAI_BRIDGE_PORT_ATTR_RIF_ID:
        case SAI_BRIDGE_PORT_ATTR_TUNNEL_ID:
        /* For now setting bridge id on set as false */
        case SAI_BRIDGE_PORT_ATTR_BRIDGE_ID:
            return SAI_STATUS_INVALID_ATTRIBUTE_0;
        case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
        case SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES:
        case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION:
        case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
        case SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING:
            return SAI_STATUS_SUCCESS;
        default:
            return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    }
    return SAI_STATUS_FAILURE;
}

static inline bool sai_bridge_port_non_mandatory_attr(sai_bridge_port_attr_t attr_id)
{
    switch(attr_id) {
        case SAI_BRIDGE_PORT_ATTR_TYPE:
        case SAI_BRIDGE_PORT_ATTR_PORT_ID:
        case SAI_BRIDGE_PORT_ATTR_VLAN_ID:
        case SAI_BRIDGE_PORT_ATTR_RIF_ID:
        case SAI_BRIDGE_PORT_ATTR_TUNNEL_ID:
        case SAI_BRIDGE_PORT_ATTR_BRIDGE_ID:
            return false;
        case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
        case SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES:
        case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION:
        case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
        case SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING:
            return true;
        default:
            return false;
    }
    return false;
}

static sai_status_t sai_bridge_validate_and_fill_mandatory_attributes (dn_sai_bridge_info_t
                                                                       *bridge_info,
                                                                       uint32_t attr_count,
                                                                       const sai_attribute_t
                                                                       *attr_list)
{
    uint_t attr_idx = 0;
    bool   type_present = false;

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    } else {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    if(bridge_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge info passed in fill mandatory attr");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id) {
            case SAI_BRIDGE_ATTR_TYPE:
                bridge_info->bridge_type = attr_list[attr_idx].value.s32;
                if((bridge_info->bridge_type != SAI_BRIDGE_TYPE_1Q) &&
                   (bridge_info->bridge_type != SAI_BRIDGE_TYPE_1D)) {
                    return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                if((bridge_info->bridge_type == SAI_BRIDGE_TYPE_1Q) &&
                   (sai_default_bridge_id != SAI_NULL_OBJECT_ID)) {
                    return SAI_STATUS_ITEM_ALREADY_EXISTS;
                }
                type_present = true;
                break;

            case SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES:
            case SAI_BRIDGE_ATTR_LEARN_DISABLE:
                break;
            case SAI_BRIDGE_ATTR_PORT_LIST:
                return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTRIBUTE_0, attr_idx);
        }
    }
    if(!type_present) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    return SAI_STATUS_SUCCESS;
}

static bool  sai_bridge_port_is_valid_port(sai_object_id_t port_id)
{
    if(sai_is_obj_id_port(port_id)) {
        if(sai_is_port_valid(port_id)) {
            return true;
        }
    } else if (sai_is_obj_id_lag(port_id)) {
        if(sai_is_lag_created(port_id)) {
            return true;
        }
    }
    return false;
}

static sai_status_t sai_bridge_port_validate_and_fill_attachment(dn_sai_bridge_port_info_t *bridge_port_info,
                                                                 sai_object_id_t            port_id,
                                                                 uint16_t                   vlan_id,
                                                                 sai_object_id_t            rif_id,
                                                                 sai_object_id_t            tunnel_id)
{
    sai_object_id_t tmp_bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_status_t    sai_rc = SAI_STATUS_FAILURE;

    if(bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge port info passed in fill attachment");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    switch (bridge_port_info->bridge_port_type) {
        case SAI_BRIDGE_PORT_TYPE_PORT:
            if(port_id == SAI_NULL_OBJECT_ID) {
                return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
            }

            if(sai_is_obj_id_port(port_id)) {
               sai_rc = sai_port_def_bridge_port_get(port_id, &tmp_bridge_port_id);
            } else if(sai_is_obj_id_lag(port_id)) {
               sai_rc = sai_lag_def_bridge_port_get(port_id, &tmp_bridge_port_id);
            } else {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            if(sai_rc != SAI_STATUS_SUCCESS) {
                return sai_rc;
            }
            if(tmp_bridge_port_id != SAI_NULL_OBJECT_ID) {
                return SAI_STATUS_ITEM_ALREADY_EXISTS;
            }
            bridge_port_info->attachment.port.port_id = port_id;
            break;

        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            if((port_id == SAI_NULL_OBJECT_ID) || (vlan_id == VLAN_UNDEF)) {
                return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
            }
            if(sai_bridge_is_bridge_sub_port_duplicate(port_id, vlan_id)) {
                SAI_BRIDGE_LOG_ERR("Already sub port exists with port 0x%"PRIx64" vlan %d",
                                   port_id, vlan_id);
                return SAI_STATUS_ITEM_ALREADY_EXISTS;
            }
            bridge_port_info->attachment.sub_port.port_id = port_id;
            bridge_port_info->attachment.sub_port.vlan_id = vlan_id;
            break;

        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
            if(rif_id == SAI_NULL_OBJECT_ID) {
                return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
            }
            bridge_port_info->attachment.router_port.rif_id = rif_id;
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            if(tunnel_id == SAI_NULL_OBJECT_ID) {
                return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
            }
            if(sai_bridge_is_bridge_connected_to_tunnel(bridge_port_info->bridge_id,
                                                        tunnel_id)) {
                SAI_BRIDGE_LOG_ERR("Tunnel port already exists for tunnel 0x%"PRIx64
                                   " and Bridge 0x%"PRIx64"",tunnel_id,
                                   bridge_port_info->bridge_id);

                return SAI_STATUS_ITEM_ALREADY_EXISTS;
            }
            bridge_port_info->attachment.tunnel_port.tunnel_oid = tunnel_id;
            break;

        default:
            return SAI_STATUS_INVALID_ATTR_VALUE_0;
    }
    return SAI_STATUS_SUCCESS;
}

static bool sai_is_bridge_and_port_type_match(dn_sai_bridge_port_info_t *bridge_port_info)
{
    dn_sai_bridge_info_t *bridge_info = NULL;
    sai_status_t          sai_rc = SAI_STATUS_SUCCESS;

    if(bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("bridge port info is null in bridge and port type match check");
        return false;
    }

    sai_rc = sai_bridge_cache_read(bridge_port_info->bridge_id, &bridge_info);

    if((sai_rc != SAI_STATUS_SUCCESS) || (bridge_info == NULL)) {
        SAI_BRIDGE_LOG_ERR("Error %d in getting bridge info for bridge 0x%"PRIx64"",
                           sai_rc, bridge_port_info->bridge_id);
        return false;
    }

    if(bridge_info->bridge_type == SAI_BRIDGE_TYPE_1Q) {
        if((bridge_port_info->bridge_port_type == SAI_BRIDGE_PORT_TYPE_PORT) ||
           (bridge_port_info->bridge_port_type == SAI_BRIDGE_PORT_TYPE_1Q_ROUTER)) {
            return true;
        }
    } else if (bridge_info->bridge_type == SAI_BRIDGE_TYPE_1D) {
        if((bridge_port_info->bridge_port_type == SAI_BRIDGE_PORT_TYPE_SUB_PORT) ||
           (bridge_port_info->bridge_port_type == SAI_BRIDGE_PORT_TYPE_TUNNEL) ||
           (bridge_port_info->bridge_port_type == SAI_BRIDGE_PORT_TYPE_1D_ROUTER)) {
            return true;
        }
    }
    return false;
}

static sai_status_t sai_bridge_port_validate_and_fill_attributes (dn_sai_bridge_port_info_t *bridge_port_info,
                                                                  uint32_t attr_count,
                                                                  const sai_attribute_t *attr_list)
{
    uint_t          attr_idx = 0;
    uint_t          type_idx = 0;
    bool            type_present = false;
    bool            bridge_id_present = false;
    sai_object_id_t sai_port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t rif_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t tunnel_id = SAI_NULL_OBJECT_ID;
    uint16_t        vlan_id = VLAN_UNDEF;
    sai_status_t    sai_rc = SAI_STATUS_FAILURE;

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    } else {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    if(bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge port info passed in fill attributes");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id) {
            case SAI_BRIDGE_PORT_ATTR_TYPE:
                bridge_port_info->bridge_port_type = attr_list[attr_idx].value.s32;
                type_present = true;
                type_idx = attr_idx;
                break;

            case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE:
                bridge_port_info->fdb_learn_mode = attr_list[attr_idx].value.s32;
                break;

            case SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES:
                bridge_port_info->max_learned_address = attr_list[attr_idx].value.u32;
                break;

            case SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION:
                bridge_port_info->learn_limit_violation_action = attr_list[attr_idx].value.s32;
                break;

            case SAI_BRIDGE_PORT_ATTR_ADMIN_STATE:
                bridge_port_info->admin_state = attr_list[attr_idx].value.booldata;
                break;

            case SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING:
                bridge_port_info->ingress_filtering = attr_list[attr_idx].value.booldata;
                break;

            case SAI_BRIDGE_PORT_ATTR_PORT_ID:
                sai_port_id = attr_list[attr_idx].value.oid;
                if(!sai_bridge_port_is_valid_port(sai_port_id)) {
                    return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                break;

            case SAI_BRIDGE_PORT_ATTR_VLAN_ID:
                vlan_id = attr_list[attr_idx].value.u16;
                if(!sai_is_valid_vlan_id(vlan_id)) {
                    return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                break;

            case SAI_BRIDGE_PORT_ATTR_RIF_ID:
                rif_id = attr_list[attr_idx].value.oid;
                if(!sai_fib_is_rif_created(rif_id)) {
                    return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                break;

            case SAI_BRIDGE_PORT_ATTR_TUNNEL_ID:
                tunnel_id = attr_list[attr_idx].value.oid;
                if(dn_sai_tunnel_obj_get(tunnel_id) == NULL) {
                    return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                break;

            case SAI_BRIDGE_PORT_ATTR_BRIDGE_ID:
                if(!sai_is_bridge_created(attr_list[attr_idx].value.oid)) {
                   return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, attr_idx);
                }
                bridge_port_info->bridge_id = attr_list[attr_idx].value.oid;
                bridge_id_present = true;
                break;

            default:
                return sai_get_indexed_ret_val(SAI_STATUS_UNKNOWN_ATTRIBUTE_0, attr_idx);

        }
    }
    if((!type_present) || (!bridge_id_present)) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    if(!sai_is_bridge_and_port_type_match(bridge_port_info)) {
        return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, type_idx);
    }
    sai_rc = sai_bridge_port_validate_and_fill_attachment(bridge_port_info, sai_port_id, vlan_id,
                                                          rif_id, tunnel_id);
    if(sai_rc == SAI_STATUS_INVALID_ATTR_VALUE_0) {
        sai_rc = sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTR_VALUE_0, type_idx);
    }

    return sai_rc;
}

static sai_status_t sai_bridge_port_incr_attachment_ref_counter (dn_sai_bridge_port_info_t
                                                                 *bridge_port_info)
{
    sai_object_id_t attach_id = SAI_NULL_OBJECT_ID;
    sai_status_t    sai_rc = SAI_STATUS_SUCCESS;

    if(bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge port info passed in incr ref count");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    sai_rc = sai_bridge_increment_ref_count(bridge_port_info->bridge_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in incrementing bridge reference count", sai_rc);
        return sai_rc;
    }
    switch(bridge_port_info->bridge_port_type) {
        case SAI_BRIDGE_PORT_TYPE_PORT:
            attach_id = sai_bridge_port_info_get_port_id(bridge_port_info);
            if(sai_is_obj_id_port(attach_id)) {
               sai_rc = sai_port_def_bridge_port_set(attach_id,
                                                     bridge_port_info->bridge_port_id);
            } else if(sai_is_obj_id_lag(attach_id)) {
               sai_rc = sai_lag_def_bridge_port_set(attach_id,
                                                    bridge_port_info->bridge_port_id);
            } else {
                return SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            if(sai_rc != SAI_STATUS_SUCCESS) {
                return sai_rc;
            }
        /* Break is not needed as this portion is common to both port and subport */
        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            attach_id = sai_bridge_port_info_get_port_id(bridge_port_info);
            if(sai_is_obj_id_port(attach_id)) {

               sai_rc = sai_port_increment_ref_count(attach_id);

            } else if(sai_is_obj_id_lag(attach_id)) {

                sai_rc = sai_lag_to_bridge_port_map_insert(attach_id,
                                                           bridge_port_info->bridge_port_id);
                if(sai_rc  != SAI_STATUS_SUCCESS) {
                    return sai_rc;
                }

                sai_rc = sai_lag_increment_ref_count(attach_id);

                if(sai_rc != SAI_STATUS_SUCCESS) {
                    sai_lag_to_bridge_port_map_remove(attach_id,
                                                      bridge_port_info->bridge_port_id);
                }
            } else {

                sai_rc = SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            break;

        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
            attach_id = sai_bridge_port_info_get_rif_id(bridge_port_info);
            sai_rc = sai_rif_increment_ref_count(attach_id);
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            attach_id = sai_bridge_port_info_get_tunnel_id(bridge_port_info);
            sai_rc = sai_tunnel_increment_ref_count(attach_id);
            break;

        default:
            sai_rc = SAI_STATUS_SUCCESS;
            break;
    }
    if(sai_rc != SAI_STATUS_SUCCESS) {
        sai_bridge_decrement_ref_count(bridge_port_info->bridge_id);
    }

    return sai_rc;
}

static sai_status_t sai_bridge_port_decr_attachment_ref_counter (dn_sai_bridge_port_info_t
                                                                  *bridge_port_info)
{
    sai_object_id_t attach_id = SAI_NULL_OBJECT_ID;
    sai_status_t    sai_rc = SAI_STATUS_SUCCESS;

    if(bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge port info passed in decr ref count");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    sai_rc = sai_bridge_decrement_ref_count(bridge_port_info->bridge_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in decrementing bridge reference count", sai_rc);
        return sai_rc;
    }

    switch(bridge_port_info->bridge_port_type) {
        case SAI_BRIDGE_PORT_TYPE_PORT:
            attach_id = sai_bridge_port_info_get_port_id(bridge_port_info);
            if(sai_is_obj_id_port(attach_id)) {
               sai_rc = sai_port_def_bridge_port_set(attach_id, SAI_NULL_OBJECT_ID);
               if(sai_rc != SAI_STATUS_SUCCESS) {
                   return sai_rc;
               }
            } else if(sai_is_obj_id_lag(attach_id)) {
               sai_rc = sai_lag_def_bridge_port_set(attach_id, SAI_NULL_OBJECT_ID);
               if(sai_rc != SAI_STATUS_SUCCESS) {
                   return sai_rc;
               }
            }
        /* Break is not needed as this portion is common to both port and subport */
        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            attach_id = sai_bridge_port_info_get_port_id(bridge_port_info);
            if(sai_is_obj_id_port(attach_id)) {

                sai_rc = sai_port_decrement_ref_count(attach_id);

            } else if(sai_is_obj_id_lag(attach_id)) {

                sai_rc = sai_lag_to_bridge_port_map_remove(attach_id,
                                                           bridge_port_info->bridge_port_id);
                if(sai_rc  != SAI_STATUS_SUCCESS) {
                    return sai_rc;
                }

                sai_rc = sai_lag_decrement_ref_count(attach_id);

                if(sai_rc != SAI_STATUS_SUCCESS) {
                    sai_lag_to_bridge_port_map_insert(attach_id,
                                                      bridge_port_info->bridge_port_id);
                }

            } else {

                sai_rc = SAI_STATUS_INVALID_OBJECT_TYPE;
            }
            break;
        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
            attach_id = sai_bridge_port_info_get_rif_id(bridge_port_info);
            sai_rc = sai_rif_decrement_ref_count(attach_id);
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            attach_id = sai_bridge_port_info_get_tunnel_id(bridge_port_info);
            sai_rc = sai_tunnel_decrement_ref_count(attach_id);
            break;

        default:
            sai_rc = SAI_STATUS_SUCCESS;
            break;
    }
    if(sai_rc != SAI_STATUS_SUCCESS) {
        sai_bridge_increment_ref_count(bridge_port_info->bridge_id);
    }
    return sai_rc;
}

static sai_status_t sai_bridge_set_attribute (sai_object_id_t bridge_id,
                                              const sai_attribute_t *attr)
{
    sai_status_t         sai_rc = SAI_STATUS_FAILURE;
    dn_sai_bridge_info_t *p_bridge_info = NULL;

    STD_ASSERT(attr != NULL);

    sai_rc = sai_bridge_attr_valid_on_set(attr->id);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d on validity of attribute %d in bridge attr set",
                           sai_rc, attr->id);
        return sai_rc;
    }

    do {
        sai_rc = sai_bridge_cache_read (bridge_id, &p_bridge_info);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Invalid bridge object id 0x%"PRIx64" in set attribute", bridge_id);
            break;
        }
        if(p_bridge_info == NULL) {
            SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge info for bridge 0x%"PRIx64"",
                               bridge_id);
            sai_rc = SAI_STATUS_INVALID_PARAMETER;
            break;
        }
        sai_rc = sai_bridge_npu_api_get()->bridge_set_attribute(p_bridge_info, attr);

        if(sai_rc != SAI_STATUS_SUCCESS) {

            SAI_BRIDGE_LOG_ERR("Bridge object id 0x%"PRIx64" set attribute id %d returns error %d",
                    bridge_id, attr->id, sai_rc);
            break;
        }
        sai_bridge_update_attr_value_in_cache (p_bridge_info, attr);
    } while (0);

    return sai_rc;
}

static sai_status_t sai_api_bridge_set_attribute (sai_object_id_t bridge_id,
                                                  const sai_attribute_t *attr)
{
    sai_status_t         sai_rc = SAI_STATUS_FAILURE;
    STD_ASSERT(attr != NULL);

    sai_bridge_lock();
    sai_rc = sai_bridge_set_attribute(bridge_id, attr);
    sai_bridge_unlock();

    return sai_rc;
}
static sai_status_t sai_bridge_create (sai_object_id_t *bridge_id, sai_object_id_t switch_id,
                                       uint32_t attr_count, const sai_attribute_t *attr_list)
{
    sai_status_t         sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_bridge_info_t bridge_info;
    bool                 npu_update = false;
    bool                 cache_update = false;
    uint_t               attr_idx = 0;

    STD_ASSERT(bridge_id != NULL);
    *bridge_id = SAI_NULL_OBJECT_ID;
    memset(&bridge_info,0,sizeof(bridge_info));
    sai_bridge_init_default_bridge_info(&bridge_info);

    do {
        sai_rc = sai_bridge_validate_and_fill_mandatory_attributes(&bridge_info, attr_count, attr_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }

        bridge_info.switch_obj_id = switch_id;
        sai_rc = sai_bridge_npu_api_get()->bridge_create(bridge_id, &bridge_info);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
        npu_update = true;

        bridge_info.bridge_id = *bridge_id;

        sai_rc = sai_bridge_cache_write (*bridge_id, &bridge_info);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
        cache_update = true;

        for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            if(sai_bridge_non_mandatory_attr(attr_list[attr_idx].id)) {
                sai_rc = sai_bridge_set_attribute(*bridge_id, &attr_list[attr_idx]);
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    break;
                }
            }
        }
    } while(0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        if(npu_update) {
            sai_bridge_npu_api_get()->bridge_remove(&bridge_info);
        }
        if(cache_update) {
            sai_bridge_cache_delete(*bridge_id);
        }
    }

    return sai_rc;
}

static sai_status_t sai_api_bridge_create (sai_object_id_t *bridge_id, sai_object_id_t switch_id,
                                           uint32_t attr_count, const sai_attribute_t *attr_list)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    STD_ASSERT(bridge_id != NULL);

    sai_bridge_lock();
    sai_rc = sai_bridge_create(bridge_id, switch_id, attr_count, attr_list);
    sai_bridge_unlock();

    return sai_rc;
}

static sai_status_t sai_bridge_remove (sai_object_id_t bridge_id)
{
    sai_status_t          sai_rc = SAI_STATUS_FAILURE;
    dn_sai_bridge_info_t *p_bridge_info = NULL;
    sai_object_id_t       tmp_bridge_id = bridge_id;

    sai_rc = sai_bridge_cache_read (bridge_id, &p_bridge_info);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Invalid bridge object id 0x%"PRIx64" in set attribute", bridge_id);
        return sai_rc;
    }

    if(p_bridge_info == NULL) {
        SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge info for bridge 0x%"PRIx64"",
                           bridge_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    if(p_bridge_info->ref_count > 0) {
        return SAI_STATUS_OBJECT_IN_USE;
    }
    sai_rc = sai_bridge_npu_api_get()->bridge_remove(p_bridge_info);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in removing bridge 0x%"PRIx64" at npu", sai_rc, bridge_id);
        return sai_rc;
    }

    sai_rc = sai_bridge_cache_delete(bridge_id);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in removing bridge id 0x%"PRIx64" cache",sai_rc, bridge_id);

        sai_bridge_npu_api_get()->bridge_create(&tmp_bridge_id, p_bridge_info);
        if(tmp_bridge_id != bridge_id) {
            SAI_BRIDGE_LOG_ERR("Error unable to create bridge with same id on "
                    "rollback for bridge 0x%"PRIx64"", bridge_id);
        }
    }

    return sai_rc;
}

static sai_status_t sai_api_bridge_remove (sai_object_id_t bridge_id)
{
    sai_status_t          sai_rc = SAI_STATUS_FAILURE;

    sai_bridge_lock();
    do {
        if(bridge_id == sai_default_bridge_id) {
            SAI_BRIDGE_LOG_ERR("Error - cannot remove default bridge 0x%"PRIx64"", bridge_id);
            sai_rc = SAI_STATUS_OBJECT_IN_USE;
            break;
        }
        sai_rc = sai_bridge_remove(bridge_id);
    } while (0);

    sai_bridge_unlock();
    return sai_rc;
}
static sai_status_t sai_bridge_get_attribute (sai_object_id_t bridge_id, uint32_t attr_count,
                                              sai_attribute_t *attr_list)
{
    sai_status_t          sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_bridge_info_t *p_bridge_info = NULL;

    STD_ASSERT(attr_list != NULL);


    sai_bridge_lock();
    do {
        sai_rc = sai_bridge_cache_read (bridge_id, &p_bridge_info);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Invalid bridge object id 0x%"PRIx64" in set attribute", bridge_id);
            break;
        }

        if(p_bridge_info == NULL) {
            SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge info for bridge 0x%"PRIx64"",
                               bridge_id);
            sai_rc = SAI_STATUS_INVALID_PARAMETER;
            break;
        }

        sai_rc = sai_bridge_get_attr_value_from_bridge_info (p_bridge_info, attr_count, attr_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Bridge object id 0x%"PRIx64" get attribute returns error %d",
                               bridge_id, sai_rc);
            break;
        }

    } while(0);
    sai_bridge_unlock();

    return sai_rc;
}

static sai_status_t sai_bridge_port_set_attribute (sai_object_id_t bridge_port_id,
                                                  const sai_attribute_t *attr)
{
    sai_status_t               sai_rc = SAI_STATUS_FAILURE;
    dn_sai_bridge_port_info_t *p_bridge_port_info = NULL;

    STD_ASSERT(attr != NULL);

    sai_rc = sai_bridge_port_attr_valid_on_set(attr->id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d on validity of attribute %d in bridge port set attr",
                           sai_rc, attr->id);
        return sai_rc;
    }

    do {
        sai_rc = sai_bridge_port_cache_read (bridge_port_id, &p_bridge_port_info);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Invalid bridge_port object id 0x%"PRIx64" in set attribute",
                               bridge_port_id);
            break;
        }

        if(p_bridge_port_info == NULL) {
            SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge port info "
                               "for bridge port 0x%"PRIx64"", bridge_port_id);
            sai_rc = SAI_STATUS_INVALID_PARAMETER;
            break;
        }

        sai_rc = sai_bridge_npu_api_get()->bridge_port_set_attribute(p_bridge_port_info, attr);

        if(sai_rc != SAI_STATUS_SUCCESS) {

            SAI_BRIDGE_LOG_ERR("Bridge port object id 0x%"PRIx64" set attribute id %d returns error %d",
                    bridge_port_id, attr->id, sai_rc);
            break;
        }
        sai_bridge_port_update_attr_value_in_cache (p_bridge_port_info, attr);
    } while(0);

    return sai_rc;
}

static sai_status_t sai_bridge_port_get_type_from_attr(uint32_t attr_count,
                                                       const sai_attribute_t *attr_list,
                                                       sai_bridge_port_type_t *bridge_port_type,
                                                       bool *is_lag)
{
    uint_t       attr_idx = 0;
    sai_status_t sai_rc = SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;

    if((attr_list == NULL) || (bridge_port_type == NULL) ||(is_lag == NULL)) {
        SAI_BRIDGE_LOG_TRACE("Attrlist is %p bridge port type is %p is_lag is %p in "
                             "get type from attr", attr_list, bridge_port_type, is_lag);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        if(attr_list[attr_idx].id == SAI_BRIDGE_PORT_ATTR_TYPE) {
            *bridge_port_type = attr_list[attr_idx].value.s32;
            sai_rc = SAI_STATUS_SUCCESS;
        }
        if(attr_list[attr_idx].id == SAI_BRIDGE_PORT_TYPE_PORT) {
            *is_lag = sai_is_obj_id_lag(attr_list[attr_idx].value.oid);
        }
    }

    return sai_rc;
}

static void sai_bridge_take_module_lock(sai_bridge_port_type_t bridge_port_type, bool is_lag)
{
    switch(bridge_port_type) {
        case SAI_BRIDGE_PORT_TYPE_PORT:
        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            if(is_lag) {
                sai_lag_lock();
            } else {
                sai_port_lock();
            }
            break;

        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
            sai_fib_lock();
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            dn_sai_tunnel_lock();
            break;
    }
}

static void sai_bridge_give_module_lock(sai_bridge_port_type_t bridge_port_type, bool is_lag)
{
    switch(bridge_port_type) {
        case SAI_BRIDGE_PORT_TYPE_PORT:
        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            if(is_lag) {
                sai_lag_unlock();
            } else {
                sai_port_unlock();
            }
            break;

        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
            sai_fib_unlock();
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            dn_sai_tunnel_unlock();
            break;
    }
}

static sai_status_t sai_bridge_port_create(sai_object_id_t *bridge_port_id,
                                           sai_object_id_t switch_id,
                                           uint32_t attr_count, const sai_attribute_t *attr_list)
{
    sai_status_t              sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_bridge_port_info_t bridge_port_info;
    bool                      npu_update = false;
    bool                      cache_update = false;
    bool                      ref_count_update = false;
    uint_t                    attr_idx = 0;
    sai_bridge_port_type_t    bridge_port_type = SAI_BRIDGE_PORT_TYPE_PORT;
    bool                      is_lag = false;
    sai_object_id_t           port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t           tunnel_id = SAI_NULL_OBJECT_ID;
    sai_vlan_id_t             vlan_id = VLAN_UNDEF;

    STD_ASSERT(bridge_port_id != NULL);
    STD_ASSERT(attr_list != NULL);

    sai_rc = sai_bridge_port_get_type_from_attr(attr_count, attr_list, &bridge_port_type, &is_lag);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in getting bridge port type", sai_rc);
        return sai_rc;
    }

    memset(&bridge_port_info,0,sizeof(bridge_port_info));
    sai_bridge_init_default_bridge_port_info(&bridge_port_info);
    *bridge_port_id = SAI_NULL_OBJECT_ID;

    sai_bridge_take_module_lock(bridge_port_type, is_lag);
    do {
        sai_rc = sai_bridge_port_validate_and_fill_attributes (&bridge_port_info, attr_count,
                                                               attr_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in validating bridge port attributes", sai_rc);
            break;
        }

        sai_rc = sai_bridge_npu_api_get()->bridge_port_create(bridge_port_id, &bridge_port_info);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in creating bridge port", sai_rc);
            break;
        }
        npu_update = true;

        bridge_port_info.bridge_port_id = *bridge_port_id;
        sai_rc = sai_bridge_port_cache_write (*bridge_port_id, &bridge_port_info);

        if( sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in write cache for bridge port id 0x%"PRIx64"",
                               sai_rc, *bridge_port_id);
            break;
        }

        cache_update = true;

        for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            if(sai_bridge_port_non_mandatory_attr(attr_list[attr_idx].id)) {
                sai_rc = sai_bridge_port_set_attribute(*bridge_port_id, &attr_list[attr_idx]);
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    break;
                }
            }
        }

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
        sai_rc = sai_bridge_port_incr_attachment_ref_counter (&bridge_port_info);
        if( sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d incrementing attachment ref counter for bridge port id "
                               "0x%"PRIx64"", sai_rc, *bridge_port_id);
            break;
        }
        ref_count_update = true;
        if(sai_bridge_port_info_is_type_sub_port(&bridge_port_info)) {
            port_id = sai_bridge_port_info_get_port_id(&bridge_port_info);
            vlan_id = sai_bridge_port_info_get_vlan_id(&bridge_port_info);
            sai_rc  = sai_bridge_port_vlan_to_bridge_port_map_insert(port_id, vlan_id,
                                                                     *bridge_port_id);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in creating port 0x%"PRIx64" vlan %d to bridge port "
                                   "0x%"PRIx64" map", port_id, vlan_id, *bridge_port_id);
                break;
            }

        } else if(sai_bridge_port_info_is_type_tunnel(&bridge_port_info)){
            tunnel_id = sai_bridge_port_info_get_tunnel_id(&bridge_port_info);
            sai_rc = sai_tunnel_to_bridge_port_map_insert(tunnel_id, *bridge_port_id);

            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in creating tunnel 0x%"PRIx64" to bridge port "
                                   "0x%"PRIx64" map", sai_rc, tunnel_id, *bridge_port_id);
                break;
            }
        }

    } while (0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        if(npu_update) {
            sai_bridge_npu_api_get()->bridge_port_remove(&bridge_port_info);
        }
        if(ref_count_update) {
            sai_bridge_port_decr_attachment_ref_counter(&bridge_port_info);
        }
        if(cache_update) {
            sai_bridge_port_cache_delete(*bridge_port_id);
        }
    }
    sai_bridge_give_module_lock(bridge_port_type, is_lag);

    return sai_rc;
}

static sai_status_t sai_api_bridge_port_create(sai_object_id_t *bridge_port_id,
                                               sai_object_id_t switch_id, uint32_t attr_count,
                                               const sai_attribute_t *attr_list)
{
    sai_status_t               sai_rc;
    sai_bridge_port_notif_t    notif_data;
    sai_bridge_port_type_t     bridge_port_type;

    STD_ASSERT(attr_list != NULL);
    STD_ASSERT(bridge_port_id != NULL);

    sai_bridge_lock();
    do {
        sai_rc = sai_bridge_port_create(bridge_port_id, switch_id, attr_count, attr_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }

        sai_rc = sai_bridge_port_get_type (*bridge_port_id, &bridge_port_type);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Invalid bridge_port object id 0x%"PRIx64" in bridge port create",
                               *bridge_port_id);
        }
    } while (0);
    sai_bridge_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        memset(&notif_data, 0, sizeof(notif_data));
        notif_data.event = SAI_BRIDGE_PORT_EVENT_CREATE;
        sai_bridge_port_send_internal_notifs(*bridge_port_id, bridge_port_type, &notif_data);
    }

    return sai_rc;
}

static sai_status_t sai_bridge_port_remove (sai_object_id_t bridge_port_id)
{
    sai_status_t               sai_rc = SAI_STATUS_FAILURE;
    bool                       npu_update = false;
    bool                       ref_count_update = false;
    bool                       map_update = false;
    sai_object_id_t            tmp_bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t            attach_port_id = SAI_NULL_OBJECT_ID;
    bool                       is_lag = false;
    sai_bridge_port_type_t     bridge_port_type = SAI_BRIDGE_PORT_TYPE_PORT;
    sai_object_id_t            port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t            tunnel_id = SAI_NULL_OBJECT_ID;
    sai_vlan_id_t              vlan_id = VLAN_UNDEF;
    dn_sai_bridge_port_info_t *p_bridge_port_info = NULL;

    sai_rc = sai_bridge_port_cache_read (bridge_port_id, &p_bridge_port_info);

    if(p_bridge_port_info == NULL) {
        SAI_BRIDGE_LOG_TRACE("Invalid bridge port info %p",p_bridge_port_info);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if(p_bridge_port_info->ref_count > 0) {
        SAI_BRIDGE_LOG_ERR("Error - bridge port 0x%"PRIx64" in use", bridge_port_id);
        return SAI_STATUS_OBJECT_IN_USE;
    }
    attach_port_id = sai_bridge_port_info_get_port_id(p_bridge_port_info);
    is_lag = sai_is_obj_id_lag(attach_port_id);
    bridge_port_type = p_bridge_port_info->bridge_port_type;

    sai_bridge_take_module_lock(bridge_port_type, is_lag);
    do {
        sai_rc = sai_bridge_npu_api_get()->bridge_port_remove(p_bridge_port_info);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d unable to remove bridge port 0x%"PRIx64"",
                               sai_rc, bridge_port_id);
            break;
        }

        npu_update = true;

        sai_rc = sai_bridge_port_decr_attachment_ref_counter (p_bridge_port_info);
        if( sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d decrementing attachment ref counter for bridge port id"
                               "0x%"PRIx64"", sai_rc, bridge_port_id);
            break;
        }
        ref_count_update = true;

        if(sai_bridge_port_info_is_type_sub_port(p_bridge_port_info)) {
            port_id = sai_bridge_port_info_get_port_id(p_bridge_port_info);
            vlan_id = sai_bridge_port_info_get_vlan_id(p_bridge_port_info);
            sai_rc  = sai_bridge_port_vlan_to_bridge_port_map_remove(port_id, vlan_id);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in removing port 0x%"PRIx64" vlan %d to bridge port "
                                   "0x%"PRIx64" map", sai_rc, port_id, vlan_id, bridge_port_id);
                break;
            }
            map_update = true;

        } else if(sai_bridge_port_info_is_type_tunnel(p_bridge_port_info)) {
            tunnel_id = sai_bridge_port_info_get_tunnel_id(p_bridge_port_info);
            sai_rc  = sai_tunnel_to_bridge_port_map_remove(tunnel_id, bridge_port_id);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in removing tunnel 0x%"PRIx64" to bridge port "
                                   "0x%"PRIx64" map", sai_rc, tunnel_id, bridge_port_id);
                break;
            }
            map_update = true;
        }

        sai_rc = sai_bridge_port_cache_delete(bridge_port_id);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in deleting bridge port cache 0x%"PRIx64"",
                               sai_rc, bridge_port_id);
            break;
        }

    } while(0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        if(npu_update) {
            tmp_bridge_port_id = bridge_port_id;
            sai_bridge_npu_api_get()->bridge_port_create(&tmp_bridge_port_id, p_bridge_port_info);
            if(tmp_bridge_port_id != bridge_port_id) {
                SAI_BRIDGE_LOG_ERR("Error - Same Bridge port ID 0x%"PRIx64""
                                   " cannot be allocated in rollback", bridge_port_id);
            } else {
                if(ref_count_update) {
                    sai_bridge_port_incr_attachment_ref_counter (p_bridge_port_info);
                }
                if(map_update) {
                    if(sai_bridge_port_info_is_type_sub_port(p_bridge_port_info)) {
                        sai_bridge_port_vlan_to_bridge_port_map_insert(port_id, vlan_id, bridge_port_id);
                    } else if(sai_bridge_port_info_is_type_tunnel(p_bridge_port_info)) {
                        sai_tunnel_to_bridge_port_map_insert(tunnel_id, bridge_port_id);
                    }
                }
            }
        }
    }
    sai_bridge_give_module_lock(bridge_port_type, is_lag);
    return sai_rc;
}

static sai_status_t sai_api_bridge_port_remove (sai_object_id_t bridge_port_id)
{
    sai_status_t               sai_rc = SAI_STATUS_FAILURE;
    sai_bridge_port_notif_t    notif_data;
    sai_bridge_port_type_t     bridge_port_type;

    sai_bridge_lock();

    do {
        sai_rc = sai_bridge_port_get_type (bridge_port_id, &bridge_port_type);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in getting type for bridge_port object id 0x%"PRIx64"",
                               bridge_port_id);
            break;
        }

        sai_rc = sai_bridge_port_remove(bridge_port_id);
    } while (0);

    sai_bridge_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        memset(&notif_data, 0, sizeof(notif_data));
        notif_data.event = SAI_BRIDGE_PORT_EVENT_REMOVE;
        sai_bridge_port_send_internal_notifs(bridge_port_id, bridge_port_type, &notif_data);
    }
    return sai_rc;
}

static sai_status_t sai_api_bridge_port_set_attribute (sai_object_id_t bridge_port_id,
                                                       const sai_attribute_t *attr)
{
    sai_status_t  sai_rc = SAI_STATUS_FAILURE;

    STD_ASSERT(attr != NULL);

    sai_bridge_lock();
    sai_rc = sai_bridge_port_set_attribute(bridge_port_id, attr);
    sai_bridge_unlock();

    return sai_rc;
}

static sai_status_t sai_bridge_port_get_attribute (sai_object_id_t bridge_port_id, uint32_t attr_count,
                                                   sai_attribute_t *attr_list)
{
    sai_status_t               sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_bridge_port_info_t *p_bridge_port_info = NULL;

    STD_ASSERT(attr_list != NULL);

    sai_bridge_lock();
    do {
        sai_rc = sai_bridge_port_cache_read (bridge_port_id, &p_bridge_port_info);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Invalid bridge_port object id 0x%"PRIx64" in set attribute",
                               bridge_port_id);
            break;
        }

        if(p_bridge_port_info == NULL) {
            SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge port info "
                               "for bridge port 0x%"PRIx64"", bridge_port_id);
            sai_rc = SAI_STATUS_INVALID_PARAMETER;
            break;
        }

        sai_rc = sai_bridge_port_get_attr_value_from_bridge_port_info (p_bridge_port_info,
                                                                       attr_count, attr_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Bridge port object id 0x%"PRIx64" get attribute returns error %d",
                               bridge_port_id, sai_rc);
            break;
        }

    } while(0);
    sai_bridge_unlock();

    return sai_rc;

}

static sai_status_t sai_bridge_add_default_bridge_port(sai_object_id_t sai_port_id)
{
    sai_attribute_t            bridge_port_attr[SAI_BRIDGE_DEF_BRIDGE_PORT_ATTR_COUNT];
    uint32_t                   attr_count = 0;
    sai_object_id_t            tmp_bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_status_t               sai_rc = SAI_STATUS_FAILURE;
    sai_bridge_port_notif_t    notif_data;

    bridge_port_attr[attr_count].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    bridge_port_attr[attr_count].value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;
    attr_count++;
    bridge_port_attr[attr_count].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    bridge_port_attr[attr_count].value.oid = sai_port_id;
    attr_count++;
    bridge_port_attr[attr_count].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    bridge_port_attr[attr_count].value.oid = sai_default_bridge_id;
    attr_count++;
    sai_bridge_lock();

    sai_rc = sai_bridge_port_create(&tmp_bridge_port_id, SAI_DEFAULT_SWITCH_ID,
                                    attr_count, (const sai_attribute_t *)bridge_port_attr);
    sai_bridge_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        memset(&notif_data, 0, sizeof(notif_data));
        notif_data.event = SAI_BRIDGE_PORT_EVENT_INIT_CREATE;
        sai_bridge_port_send_internal_notifs(tmp_bridge_port_id, SAI_BRIDGE_PORT_TYPE_PORT, &notif_data);
    }
    return sai_rc;
}

static sai_status_t sai_bridge_port_handle_lag_notification (uint_t bridge_port_count,
                                                             sai_object_id_t* bridge_port_list,
                                                             sai_bridge_port_type_t*
                                                             bridge_port_type_list,
                                                             sai_object_id_t lag_id, bool add_ports,
                                                             const sai_object_list_t *port_list)
{
    uint_t port_idx = 0;
    const sai_bridge_port_notif_t internal_notif = {SAI_BRIDGE_PORT_EVENT_LAG_MODIFY,
                                                    lag_id,add_ports,port_list};

    if((bridge_port_list == NULL) || (bridge_port_type_list == NULL) || (port_list == NULL)) {
        SAI_BRIDGE_LOG_TRACE("Error bridge port list is %p bridge port type list is %p "
                             "port list is %p",bridge_port_list, bridge_port_type_list, port_list);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(port_idx = 0; port_idx < bridge_port_count; port_idx++) {
        sai_bridge_port_send_internal_notifs(bridge_port_list[port_idx],
                                             bridge_port_type_list[port_idx], &internal_notif);
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_bridge_lag_notification_handler (sai_object_id_t lag_id,
                                                         sai_lag_operation_t lag_opcode,
                                                         const sai_object_list_t *port_list)
{
    uint_t                     bridge_port_cnt = 0;
    sai_status_t               sai_rc = SAI_STATUS_FAILURE;
    uint_t                     idx = 0;
    dn_sai_bridge_port_info_t *p_bridge_port_info = NULL;
    sai_object_id_t           *bridge_port_list = NULL;
    sai_bridge_port_type_t    *bridge_port_type_list = NULL;
    bool                       add_ports = (lag_opcode == SAI_LAG_OPER_ADD_PORTS)?true:false;


    /* Validate lag_opcode */
    if ((lag_opcode != SAI_LAG_OPER_ADD_PORTS) &&
        (lag_opcode != SAI_LAG_OPER_DEL_PORTS)) {

        SAI_BRIDGE_LOG_TRACE ("LAG Id: 0x%"PRIx64" op_code: %d is not handled.",
                              lag_id, lag_opcode);

        return SAI_STATUS_SUCCESS;
    }

    sai_bridge_lock();
    do {
        sai_rc = sai_lag_map_get_bridge_port_count (lag_id, &bridge_port_cnt);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in getting bridge port count for lag 0x%"PRIx64"",
                    sai_rc, lag_id);
            break;
        }
        if(bridge_port_cnt == 0) {
            SAI_BRIDGE_LOG_TRACE("LAG 0x%"PRIx64" has no bridge ports", lag_id);
            sai_rc = SAI_STATUS_SUCCESS;
            break;
        }

        bridge_port_list = (sai_object_id_t *)calloc(bridge_port_cnt, sizeof(sai_object_id_t));
        if(bridge_port_list == NULL) {
            SAI_BRIDGE_LOG_ERR("Error unable to allocate memory");
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }
        bridge_port_type_list = (sai_bridge_port_type_t *)calloc(bridge_port_cnt,
                                                                 sizeof(sai_bridge_port_type_t));

        if(bridge_port_type_list == NULL) {
            SAI_BRIDGE_LOG_ERR("Error unable to allocate memory");
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }

        sai_rc = sai_lag_bridge_map_port_list_get(lag_id, &bridge_port_cnt, bridge_port_list);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in getting bridge port list for lag 0x%"PRIx64"",
                               sai_rc, lag_id);
            break;
        }
        for(idx = 0; idx < bridge_port_cnt; idx++) {
            sai_rc = sai_bridge_port_cache_read (bridge_port_list[idx], &p_bridge_port_info);
            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Invalid bridge_port object id 0x%"PRIx64" in lag notification",
                        bridge_port_list[idx]);
                break;
            }

            if(p_bridge_port_info == NULL) {
                SAI_BRIDGE_LOG_ERR("Unable to retrieve valid bridge port info "
                        "for bridge port 0x%"PRIx64"", bridge_port_list[idx]);
                sai_rc = SAI_STATUS_INVALID_PARAMETER;
                break;
            }
            bridge_port_type_list[idx] = p_bridge_port_info->bridge_port_type;

            sai_rc = sai_bridge_npu_api_get()->bridge_port_lag_handler(p_bridge_port_info, lag_id,
                                                                       add_ports, port_list);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in handling lag notification for lag id 0x%"PRIx64""
                                   "by bridge port 0x%"PRIx64"", lag_id, bridge_port_list[idx]);
                break;
            }
        }
    } while (0);
    sai_bridge_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        if(bridge_port_cnt > 0) {
            sai_rc = sai_bridge_port_handle_lag_notification(bridge_port_cnt, bridge_port_list,
                                                             bridge_port_type_list, lag_id,
                                                             add_ports, port_list);
        }
    }

    if(bridge_port_list != NULL) {
        free(bridge_port_list);
    }
    if(bridge_port_type_list != NULL) {
        free(bridge_port_type_list);
    }

    return sai_rc;
}

static sai_status_t sai_bridge_deinit()
{
    uint_t           bridge_port_count = 0;
    uint_t           port_idx = 0;
    sai_status_t     sai_rc = SAI_STATUS_FAILURE;
    sai_object_id_t *bridge_port_list = NULL;

    sai_rc = sai_bridge_npu_api_get()->bridge_init(false);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in bridge de init", sai_rc);
        return sai_rc;
    }

    sai_rc = sai_bridge_map_get_port_count(sai_default_bridge_id, &bridge_port_count);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in getting bridge port count for bridge id 0x%"PRIx64"",
                           sai_rc, sai_default_bridge_id);
        return sai_rc;
    }

    bridge_port_list = (sai_object_id_t *)calloc(bridge_port_count, sizeof(sai_object_id_t));
    if(bridge_port_list == NULL) {
        SAI_BRIDGE_LOG_ERR("No memory to allocate in bridge deinit");
        return SAI_STATUS_NO_MEMORY;
    }
    do {
        sai_rc = sai_bridge_map_port_list_get(sai_default_bridge_id, &bridge_port_count,
                                              bridge_port_list);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in getting bridge port list for bridge id 0x%"PRIx64"",
                               sai_rc, sai_default_bridge_id);
            break;
        }
        for(port_idx = 0; port_idx < bridge_port_count; port_idx++) {
            sai_rc = sai_bridge_port_remove(bridge_port_list[port_idx]);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in removing bridge port 0x%"PRIx64" in deinit",
                                   sai_rc, bridge_port_list[port_idx]);
                break;
            }
        }
        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
        sai_rc = sai_bridge_remove(sai_default_bridge_id);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in removing bridge 0x%"PRIx64"",
                               sai_rc, sai_default_bridge_id);
        }
    } while (0);

    free(bridge_port_list);
    return sai_rc;
}

sai_status_t sai_bridge_init (void)
{
    sai_attribute_t  bridge_attr;
    sai_status_t     sai_rc = SAI_STATUS_FAILURE;
    sai_port_info_t *sai_port_info = NULL;

    sai_rc = sai_bridge_npu_api_get()->bridge_init(true);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_BRIDGE_LOG_ERR("Error %d in bridge init", sai_rc);
        return sai_rc;
    }

    /* Create a default 1Q Bridge */
    memset(&bridge_attr, 0, sizeof(bridge_attr));
    bridge_attr.id = SAI_BRIDGE_ATTR_TYPE;
    bridge_attr.value.s32 = SAI_BRIDGE_TYPE_1Q;

    sai_rc = sai_bridge_create (&sai_default_bridge_id, SAI_DEFAULT_SWITCH_ID,
                                1, &bridge_attr);

    do {
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in creating default 1Q bridge", sai_rc);
            break;
        }

        /* Add all valid ports to 1Q Bridge */
        for (sai_port_info = sai_port_info_getfirst(); (sai_port_info != NULL);
                sai_port_info = sai_port_info_getnext(sai_port_info)) {
            if(!sai_is_port_valid(sai_port_info->sai_port_id)) {
                continue;
            }

            sai_rc  = sai_bridge_add_default_bridge_port(sai_port_info->sai_port_id);

            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_BRIDGE_LOG_ERR("Error %d in adding port 0x%"PRIx64" to default 1Q bridge",
                        sai_rc, sai_port_info->sai_port_id);
                break;
            }
        }
        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
        sai_rc = sai_lag_event_callback_register (SAI_MODULE_BRIDGE,
                                                  sai_bridge_lag_notification_handler);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_BRIDGE_LOG_ERR("Error %d in registering bridge to lag module", sai_rc);
        }
    } while(0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        sai_bridge_deinit();
    }

    return sai_rc;
}

sai_status_t sai_bridge_default_id_get (sai_object_id_t *default_bridge_id)
{
    if(sai_default_bridge_id == SAI_NULL_OBJECT_ID) {
        return SAI_STATUS_UNINITIALIZED;
    }
    if(default_bridge_id == NULL) {
        SAI_BRIDGE_LOG_TRACE("NULL bridge port id passed in default bridge id get");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    *default_bridge_id = sai_default_bridge_id;
    return SAI_STATUS_SUCCESS;
}

static sai_bridge_api_t sai_bridge_method_table =
{
    sai_api_bridge_create,
    sai_api_bridge_remove,
    sai_api_bridge_set_attribute,
    sai_bridge_get_attribute,
    sai_api_bridge_port_create,
    sai_api_bridge_port_remove,
    sai_api_bridge_port_set_attribute,
    sai_bridge_port_get_attribute,
};

sai_bridge_api_t  *sai_bridge_api_query (void)
{
    return (&sai_bridge_method_table);
}
