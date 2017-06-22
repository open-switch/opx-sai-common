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
 * @file sai_vlan.c
 *
 * @brief This file contains implementation of SAI VLAN APIs.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "std_assert.h"
#include "saivlan.h"
#include "saitypes.h"
#include "saistatus.h"
#include "saiport.h"
#include "sai_modules_init.h"
#include "sai_vlan_common.h"
#include "sai_npu_port.h"
#include "sai_npu_vlan.h"
#include "sai_vlan_api.h"
#include "sai_port_utils.h"
#include "sai_port_common.h"
#include "sai_port_main.h"
#include "sai_switch_utils.h"
#include "sai_gen_utils.h"
#include "sai_oid_utils.h"
#include "sai_stp_api.h"
#include "sai_common_infra.h"

#define SAI_L2_DEFAULT_VLAN_MAX_ATTR_COUNT 1

static sai_status_t sai_is_vlan_id_available(sai_vlan_id_t vlan_id)
{
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    if(sai_is_internal_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_WARN("Not modifying internal vlan id %d", vlan_id);
        return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_is_vlan_configurable(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val;
    ret_val = sai_is_vlan_id_available(vlan_id);

    if(ret_val != SAI_STATUS_SUCCESS) {
        return ret_val;
    }
    if(!sai_is_vlan_created(vlan_id)) {
        SAI_VLAN_LOG_ERR("vlan id not found %d",vlan_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2_set_vlan_attribute(sai_object_id_t vlan_obj_id,
                                           const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;

    if(!sai_is_obj_id_vlan(vlan_obj_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    vlan_id = sai_vlan_obj_id_to_vlan_id(vlan_obj_id);

    STD_ASSERT(attr != NULL);
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d", vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    sai_vlan_lock();
    if(!sai_is_vlan_created(vlan_id)) {
        SAI_VLAN_LOG_ERR("vlan id not found %d",vlan_id);
        sai_vlan_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    switch(attr->id)
    {
        case SAI_VLAN_ATTR_VLAN_ID:
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
            break;

        case SAI_VLAN_ATTR_MAX_LEARNED_ADDRESSES:
            SAI_VLAN_LOG_TRACE("Setting mac learn limit for vlan:%d value=%d",
                    vlan_id,(int)attr->value.u32);
            ret_val = sai_vlan_npu_api_get()->set_vlan_max_learned_address(vlan_id,
                    attr->value.u32);
            if(ret_val == SAI_STATUS_SUCCESS) {
                sai_vlan_max_learn_adddress_cache_write(vlan_id, attr->value.u32);
            }
            break;

        case SAI_VLAN_ATTR_STP_INSTANCE:
            SAI_VLAN_LOG_TRACE("Setting stp instance for vlan:%d value=0x%"PRIx64"",
                                 vlan_id, attr->value.oid);
            ret_val = sai_stp_vlan_handle(vlan_id, attr->value.oid);
            break;

        case SAI_VLAN_ATTR_MEMBER_LIST:
            SAI_VLAN_LOG_ERR("Invalid attr :%d for vlan id %d",
                             attr->id, vlan_id);
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
            break;

        case SAI_VLAN_ATTR_LEARN_DISABLE:
            SAI_VLAN_LOG_TRACE("Setting learn disable for vlan:%d value=%d",
                    vlan_id, attr->value.booldata);
            ret_val = sai_vlan_npu_api_get()->disable_vlan_learn_set(vlan_id,
                    attr->value.booldata);
            if(ret_val == SAI_STATUS_SUCCESS) {
                sai_vlan_learn_disable_cache_write(vlan_id, attr->value.booldata);
            }
            break;

        case SAI_VLAN_ATTR_IPV4_MCAST_LOOKUP_KEY_TYPE:
        case SAI_VLAN_ATTR_IPV6_MCAST_LOOKUP_KEY_TYPE:
        case SAI_VLAN_ATTR_UNKNOWN_NON_IP_MCAST_OUTPUT_GROUP_ID:
        case SAI_VLAN_ATTR_UNKNOWN_IPV4_MCAST_OUTPUT_GROUP_ID:
        case SAI_VLAN_ATTR_UNKNOWN_IPV6_MCAST_OUTPUT_GROUP_ID:
        case SAI_VLAN_ATTR_UNKNOWN_LINKLOCAL_MCAST_OUTPUT_GROUP_ID:
            ret_val = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
            break;

        case SAI_VLAN_ATTR_META_DATA:
            SAI_VLAN_LOG_TRACE("Setting Vlan Meta Data for vlan:%d value=%d",
                    vlan_id, attr->value.u32);
            ret_val = sai_vlan_npu_api_get()->set_vlan_meta_data(vlan_id,
                      attr->value.u32);
            if(ret_val == SAI_STATUS_SUCCESS) {
                sai_vlan_meta_data_cache_write(vlan_id, attr->value.u32);
            }
            break;

        case SAI_VLAN_ATTR_INGRESS_ACL:
        case SAI_VLAN_ATTR_EGRESS_ACL:
            ret_val = SAI_STATUS_SUCCESS;
            break;

        default:
            SAI_VLAN_LOG_TRACE("Invalid attr :%d for vlan id %d",
                               attr->id, vlan_id);
            ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
            break;

    }
    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_get_vlan_attribute(sai_object_id_t vlan_obj_id,
        uint32_t attr_count, sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    unsigned int attr_idx = 0;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;

    if(!sai_is_obj_id_vlan(vlan_obj_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    vlan_id = sai_vlan_obj_id_to_vlan_id(vlan_obj_id);

    STD_ASSERT(attr_list != NULL);
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    if(attr_count == 0) {
        SAI_VLAN_LOG_ERR("Invalid attribute count 0 for vlan:%d",vlan_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    sai_vlan_lock();
    if(!sai_is_vlan_created(vlan_id)) {
        SAI_VLAN_LOG_ERR("vlan id not found %d",vlan_id);
        sai_vlan_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id)
        {
            case SAI_VLAN_ATTR_VLAN_ID:
                SAI_VLAN_LOG_TRACE("Getting VLAN ID:%d", vlan_id);
                attr_list[attr_idx].value.u16 = vlan_id;
                ret_val = SAI_STATUS_SUCCESS;
                break;

            case SAI_VLAN_ATTR_MAX_LEARNED_ADDRESSES:
                SAI_VLAN_LOG_TRACE("Getting mac learn limit for vlan:%d", vlan_id);
                ret_val = sai_vlan_npu_api_get()->get_vlan_max_learned_address(vlan_id,
                        &attr_list[attr_idx].value.u32);
                break;

            case SAI_VLAN_ATTR_STP_INSTANCE:
                SAI_VLAN_LOG_TRACE("Getting STP instance id for vlan:%d", vlan_id);
                ret_val = sai_stp_vlan_stp_get (vlan_id, &attr_list[attr_idx].value.oid);
                break;

            case SAI_VLAN_ATTR_MEMBER_LIST:
                SAI_VLAN_LOG_TRACE("Getting list of ports for VLAN:%d", vlan_id);
                ret_val = sai_vlan_port_list_get(vlan_id, &attr_list[attr_idx].value.objlist);
                break;

            case SAI_VLAN_ATTR_LEARN_DISABLE:
                SAI_VLAN_LOG_TRACE("Getting learn disable for vlan:%d",
                        vlan_id);
                ret_val = sai_vlan_npu_api_get()->disable_vlan_learn_get(vlan_id,
                                                                         &attr_list[attr_idx].value.booldata);
                break;

            case SAI_VLAN_ATTR_IPV4_MCAST_LOOKUP_KEY_TYPE:
            case SAI_VLAN_ATTR_IPV6_MCAST_LOOKUP_KEY_TYPE:
            case SAI_VLAN_ATTR_UNKNOWN_NON_IP_MCAST_OUTPUT_GROUP_ID:
            case SAI_VLAN_ATTR_UNKNOWN_IPV4_MCAST_OUTPUT_GROUP_ID:
            case SAI_VLAN_ATTR_UNKNOWN_IPV6_MCAST_OUTPUT_GROUP_ID:
            case SAI_VLAN_ATTR_UNKNOWN_LINKLOCAL_MCAST_OUTPUT_GROUP_ID:
                ret_val = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + attr_idx;
                break;

            case SAI_VLAN_ATTR_INGRESS_ACL:
            case SAI_VLAN_ATTR_EGRESS_ACL:
                ret_val = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0 + attr_idx;
                break;

            case SAI_VLAN_ATTR_META_DATA:
                SAI_VLAN_LOG_TRACE("Getting Vlan Meta Data for vlan:%d", vlan_id);
                ret_val = sai_vlan_npu_api_get()->get_vlan_meta_data(vlan_id,
                      &attr_list[attr_idx].value.u32);
                break;

            default:
                SAI_VLAN_LOG_TRACE("Invalid attr :%d for vlan id %d",
                        attr_list[attr_idx].id, vlan_id);
                ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;

        }
        if(ret_val != SAI_STATUS_SUCCESS){
            ret_val = sai_get_indexed_ret_val(ret_val, attr_idx);
            break;
        }
    }
    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_delete_vlan(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    ret_val = sai_stp_vlan_destroy(vlan_id);
    if(ret_val != SAI_STATUS_SUCCESS){
        SAI_VLAN_LOG_ERR("vlan removal from stp instance failed for vlan %d",vlan_id);
        return ret_val;
    }

    ret_val = sai_vlan_npu_api_get()->vlan_delete(vlan_id);
    if(ret_val != SAI_STATUS_SUCCESS){
        SAI_VLAN_LOG_ERR("Not removing vlan id %d",vlan_id);
        return ret_val;
    }
    sai_remove_vlan_from_list(vlan_id);
    return ret_val;
}

static sai_status_t sai_l2_remove_vlan(sai_object_id_t vlan_obj_id)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;

    if(!sai_is_obj_id_vlan(vlan_obj_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    SAI_VLAN_LOG_TRACE("Deleting VLAN %d ", vlan_id);
    vlan_id = sai_vlan_obj_id_to_vlan_id(vlan_obj_id);

    sai_vlan_lock();

    if((ret_val = sai_is_vlan_configurable(vlan_id))
            == SAI_STATUS_SUCCESS) {
        if(sai_is_vlan_obj_in_use(vlan_id)) {
            return SAI_STATUS_OBJECT_IN_USE;
        }
        ret_val = sai_l2_delete_vlan(vlan_id);
    }

    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_create_vlan(
        sai_object_id_t *vlan_obj_id,
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    uint32_t attr_idx = 0;
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;
    bool vlan_id_attr_present = false;

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    } else {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    for (attr_idx = 0;
            (attr_idx < attr_count) && !(vlan_id_attr_present);
            attr_idx++) {
        switch (attr_list [attr_idx].id) {
            case SAI_VLAN_ATTR_VLAN_ID:
                vlan_id_attr_present = true;
                vlan_id = attr_list[attr_idx].value.u16;
                break;
        }
    }

    if(!vlan_id_attr_present) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    SAI_VLAN_LOG_TRACE("Creating VLAN %d ", vlan_id);
    sai_vlan_lock();
    do {
        if((ret_val = sai_is_vlan_id_available(vlan_id))
                != SAI_STATUS_SUCCESS) {
            break;
        }

        if((ret_val = sai_insert_vlan_in_list(vlan_id))
                != SAI_STATUS_SUCCESS) {
            SAI_VLAN_LOG_ERR("Not creating vlan id %d", vlan_id);
            break;
        }

        ret_val = sai_vlan_npu_api_get()->vlan_create(vlan_id);
        if((ret_val != SAI_STATUS_SUCCESS) &&
            (ret_val != SAI_STATUS_ITEM_ALREADY_EXISTS)) {
            sai_remove_vlan_from_list(vlan_id);
            break;
        }

        if(vlan_id != SAI_INIT_DEFAULT_VLAN) {
            if((ret_val = sai_stp_default_stp_vlan_update(vlan_id))
                    != SAI_STATUS_SUCCESS) {
                SAI_VLAN_LOG_ERR(
                        "Association of vlan %d to default STP instance failed",
                        vlan_id);
            }
        }
    }while(0);

    sai_vlan_unlock();

    if (attr_count > 1) {
        for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            if((ret_val = sai_l2_set_vlan_attribute(
                            sai_vlan_id_to_vlan_obj_id(vlan_id),
                            &attr_list[attr_idx])) != SAI_STATUS_SUCCESS) {
                sai_l2_remove_vlan(sai_vlan_id_to_vlan_obj_id(vlan_id));
            }
        }
    }

    if(ret_val == SAI_STATUS_SUCCESS) {
        *vlan_obj_id = sai_vlan_id_to_vlan_obj_id(vlan_id);
    } else {
        *vlan_obj_id = SAI_NULL_OBJECT_ID;
    }

    return ret_val;
}

static sai_status_t sai_validate_vlan_port(sai_object_id_t port_id,
        sai_vlan_tagging_mode_t tagging_mode)
{
    if(!sai_is_obj_id_port(port_id)){
        SAI_VLAN_LOG_ERR("Invalid port obj id 0x%"PRIx64"", port_id);
         return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    if(!sai_is_port_valid(port_id)){
       SAI_VLAN_LOG_ERR("Invalid port 0x%"PRIx64"", port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    if(!sai_is_valid_vlan_tagging_mode(tagging_mode)) {
        SAI_VLAN_LOG_ERR("Invalid tagging mode %d for port 0x%"PRIx64"",
                         tagging_mode, port_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2_create_vlan_member(sai_object_id_t *vlan_member_id,
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_port_fwd_mode_t fwd_mode = SAI_PORT_FWD_MODE_UNKNOWN;
    bool vlan_id_attr_present = false;
    bool port_id_attr_present = false;
    sai_vlan_member_node_t vlan_node;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;
    uint32_t attr_idx = 0;

    STD_ASSERT (vlan_member_id != NULL);
    STD_ASSERT (attr_list != NULL);

    *vlan_member_id = SAI_INVALID_VLAN_MEMBER_ID;

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    } else {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    vlan_node.switch_id = switch_id;
    vlan_node.tagging_mode = SAI_VLAN_TAGGING_MODE_UNTAGGED;

    for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch (attr_list [attr_idx].id) {
            case SAI_VLAN_MEMBER_ATTR_VLAN_ID:
                if(!sai_is_obj_id_vlan(attr_list[attr_idx].value.oid)) {
                    return SAI_STATUS_INVALID_OBJECT_ID;
                }
                vlan_node.vlan_id = attr_list[attr_idx].value.oid;
                vlan_id = sai_vlan_obj_id_to_vlan_id(
                        attr_list[attr_idx].value.oid);
                vlan_id_attr_present = true;
                break;
            case SAI_VLAN_MEMBER_ATTR_PORT_ID:
                vlan_node.port_id = attr_list[attr_idx].value.oid;
                port_id_attr_present = true;
                break;
            case SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE:
                vlan_node.tagging_mode = attr_list[attr_idx].value.u32;
                break;
            default:
                return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
        }
    }

    if(!(vlan_id_attr_present) || !(port_id_attr_present)) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    sai_vlan_lock();
    do {
        if((ret_val = sai_is_vlan_configurable(vlan_id))
                != SAI_STATUS_SUCCESS) {
            break;
        }

        if((ret_val = sai_validate_vlan_port(vlan_node.port_id,
                        vlan_node.tagging_mode)) != SAI_STATUS_SUCCESS) {
            break;
        }

        sai_port_forward_mode_info(vlan_node.port_id, &fwd_mode, false);
        if(fwd_mode == SAI_PORT_FWD_MODE_ROUTING) {
            SAI_VLAN_LOG_WARN("port 0x%"PRIx64" is in routing mode.",
                    vlan_node.port_id);
        }

        if(sai_is_port_vlan_member(vlan_id, vlan_node.port_id)) {
            ret_val = SAI_STATUS_ITEM_ALREADY_EXISTS;
            break;
        }

        if((ret_val = sai_vlan_npu_api_get()->vlan_member_create(&vlan_node))
                != SAI_STATUS_SUCCESS) {
            break;
        }

        *vlan_member_id = vlan_node.vlan_member_id;
        if((ret_val = sai_add_vlan_member_node(vlan_node))
                != SAI_STATUS_SUCCESS) {
            SAI_VLAN_LOG_ERR("Unable to add VLAN member 0x%"PRIx64" \
                    to vlan:%d cache",
                    vlan_node.vlan_member_id,vlan_id);
            sai_vlan_npu_api_get()->vlan_member_remove(vlan_node);
        }

        SAI_VLAN_LOG_TRACE("Added port 0x%"PRIx64" on vlan:%d",
                vlan_node.port_id, vlan_id);
    } while(0);

    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_set_vlan_member_attribute(sai_object_id_t vlan_member_id,
        const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    sai_vlan_member_node_t *vlan_member_node = NULL;
    sai_vlan_tagging_mode_t old_tagging_mode =
        SAI_VLAN_TAGGING_MODE_UNTAGGED;

    STD_ASSERT(attr != NULL);

    if(!sai_is_obj_id_vlan_member(vlan_member_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_vlan_lock();

    if((vlan_member_node = sai_find_vlan_member_node(vlan_member_id))
            == NULL) {
        ret_val = SAI_STATUS_ITEM_NOT_FOUND;
    } else {
        switch(attr->id)
        {
            case SAI_VLAN_MEMBER_ATTR_VLAN_ID:
                ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;
            case SAI_VLAN_MEMBER_ATTR_PORT_ID:
                ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;
            case SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE:
                if(vlan_member_node->tagging_mode != attr->value.u32) {
                    old_tagging_mode = vlan_member_node->tagging_mode;
                    vlan_member_node->tagging_mode = attr->value.u32;
                    ret_val = sai_vlan_npu_api_get()->set_vlan_member_tagging_mode(
                            *vlan_member_node);
                    if(ret_val != SAI_STATUS_SUCCESS) {
                        vlan_member_node->tagging_mode = old_tagging_mode;
                    }
                }
                break;
            default:
                ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
                break;
        }
    }

    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_get_vlan_member_attribute(sai_object_id_t vlan_member_id,
        const uint32_t attr_count, sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    sai_vlan_member_node_t *vlan_member_node = NULL;
    uint32_t attr_idx = 0;

    STD_ASSERT(attr_list != NULL);
    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    }

    if(!sai_is_obj_id_vlan_member(vlan_member_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_vlan_lock();
    if((vlan_member_node = sai_find_vlan_member_node(vlan_member_id))
            == NULL) {
        ret_val = SAI_STATUS_ITEM_NOT_FOUND;
    } else {
        for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            switch (attr_list [attr_idx].id) {
                case SAI_VLAN_MEMBER_ATTR_VLAN_ID:
                    attr_list[attr_idx].value.oid = vlan_member_node->vlan_id;
                    break;
                case SAI_VLAN_MEMBER_ATTR_PORT_ID:
                    attr_list[attr_idx].value.oid = vlan_member_node->port_id;
                    break;
                case SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE:
                    attr_list[attr_idx].value.u32 = vlan_member_node->tagging_mode;
                    break;
                default:
                    ret_val = (SAI_STATUS_UNKNOWN_ATTRIBUTE_0+attr_idx);
                    break;
            }
            if(SAI_STATUS_SUCCESS != ret_val) {
                break;
            }
        }
    }

    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_remove_vlan_member(sai_object_id_t vlan_member_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_vlan_member_node_t *vlan_node = NULL;

    if(!sai_is_obj_id_vlan_member(vlan_member_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_vlan_lock();
    do {
        if((vlan_node = sai_find_vlan_member_node(vlan_member_id)) == NULL) {
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if((ret_val = sai_vlan_npu_api_get()->vlan_member_remove(*vlan_node))
                != SAI_STATUS_SUCCESS) {
            break;
        }

        ret_val = sai_remove_vlan_member_node(*vlan_node);
    } while(0);

    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_get_vlan_stats(sai_object_id_t vlan_obj_id,
        const sai_vlan_stat_t *counter_ids,
        uint32_t number_of_counters,
        uint64_t *counters)
{
    sai_vlan_id_t vlan_id = VLAN_UNDEF;

    STD_ASSERT(counter_ids != NULL);
    STD_ASSERT(counters != NULL);

    if(!sai_is_obj_id_vlan(vlan_obj_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    vlan_id = sai_vlan_obj_id_to_vlan_id(vlan_obj_id);
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_VLAN_LOG_TRACE("Get stats collection for vlan id %d",
            vlan_id);

    return sai_vlan_npu_api_get()->get_vlan_stats(vlan_id, counter_ids,
            number_of_counters, counters);
}

static sai_status_t sai_l2_clear_vlan_stats (sai_object_id_t vlan_obj_id,
        const sai_vlan_stat_t *counter_ids,
        uint32_t number_of_counters)
{
    sai_vlan_id_t vlan_id = VLAN_UNDEF;

    STD_ASSERT(counter_ids != NULL);
    if(!sai_is_obj_id_vlan(vlan_obj_id)) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    vlan_id = sai_vlan_obj_id_to_vlan_id(vlan_obj_id);
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_VLAN_LOG_TRACE("Clear stats for vlan id %d",
            vlan_id);

    return sai_vlan_npu_api_get()->clear_vlan_stats (vlan_id, counter_ids,
            number_of_counters);
}

static sai_status_t sai_vlan_port_default_init(sai_vlan_id_t vlan_id,
        sai_object_id_t port_id)
{
    sai_status_t ret_val;
    sai_attribute_t attr[SAI_VLAN_MEMBER_ATTR_END];
    uint32_t attr_count = 0;
    sai_object_id_t vlan_member_id = SAI_NULL_OBJECT_ID;

    /*Add newly created port to default VLAN ID*/

    attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
    attr[attr_count].value.oid = sai_vlan_id_to_vlan_obj_id(vlan_id);
    attr_count++;

    attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_PORT_ID;
    attr[attr_count].value.oid = port_id;
    attr_count++;

    attr[attr_count].id = SAI_VLAN_MEMBER_ATTR_VLAN_TAGGING_MODE;
    attr[attr_count].value.u32 = SAI_DEF_VLAN_TAGGING_MODE;
    attr_count++;

    ret_val = sai_l2_create_vlan_member(&vlan_member_id, 0, attr_count, attr);
    if((ret_val != SAI_STATUS_SUCCESS) &&
            (ret_val != SAI_STATUS_ITEM_ALREADY_EXISTS)) {
        SAI_VLAN_LOG_ERR("Unable to add port 0x%"PRIx64" to def vlan:%d rc:%d",
                port_id, vlan_id,ret_val);
        return ret_val;
    }
    memset(&attr, 0, sizeof(attr));
    attr->id = SAI_PORT_ATTR_PORT_VLAN_ID;
    attr->value.u16 = vlan_id;
    ret_val = sai_port_api_query()->set_port_attribute(port_id,
                                                      (const sai_attribute_t*)&attr);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Unable to set port 0x%"PRIx64" pvid to def vlan:%d",
                port_id, vlan_id);
        sai_l2_remove_vlan_member(vlan_member_id);
        return ret_val;
    }

    memset(&attr, 0, sizeof(attr));
    attr->id = SAI_PORT_ATTR_DEFAULT_VLAN_PRIORITY;
    attr->value.u8 = SAI_DFLT_VLAN_PRIORITY;
    ret_val = sai_port_api_query()->set_port_attribute(port_id,
                                                      (const sai_attribute_t*)&attr);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Unable to set port 0x%"PRIx64" def vlan priority:%d",
                port_id, SAI_DFLT_VLAN_PRIORITY);
        return ret_val;
    }

    return SAI_STATUS_SUCCESS;
}
static sai_status_t sai_vlan_port_notification_handler(uint32_t count,
                                                       sai_port_event_notification_t *data)
{
    uint32_t port_idx = 0;
    sai_object_id_t port_id = 0;
    sai_port_event_t port_event = 0;
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    sai_vlan_member_dll_node_t *vlan_member_node = NULL;

    for(port_idx = 0; port_idx < count; port_idx++) {
        port_id = data[port_idx].port_id;
        port_event = data[port_idx].port_event;
        /*Add newly created port to default VLAN ID*/
        if(port_event == SAI_PORT_EVENT_ADD) {
            ret_val =  sai_vlan_port_default_init(SAI_INIT_DEFAULT_VLAN, port_id);
            if((ret_val != SAI_STATUS_SUCCESS) &&
                    (ret_val != SAI_STATUS_ITEM_ALREADY_EXISTS)) {
                SAI_VLAN_LOG_ERR ("Unable to add port 0x%"PRIx64" to default"
                        " VLAN, rc:%d",
                        port_id,ret_val);
            }
        } else if(port_event == SAI_PORT_EVENT_DELETE) {
            if((vlan_member_node =
                        sai_find_vlan_member_node_from_port(
                            SAI_INIT_DEFAULT_VLAN, port_id)) != NULL) {
                ret_val = sai_l2_remove_vlan_member(
                        vlan_member_node->vlan_member_info->vlan_member_id);
                if(ret_val != SAI_STATUS_SUCCESS) {
                    SAI_VLAN_LOG_ERR ("Unable to remove port 0x%"PRIx64" from"
                            " default VLAN, rc:%d",
                            port_id,ret_val);
                }
            } else {
                SAI_VLAN_LOG_ERR ("Unable to find port 0x%"PRIx64" in default VLAN member list",
                        port_id);
            }
        }
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_default_vlan_init_clean_up(sai_vlan_id_t vlan_id)
{
    sai_status_t  ret_val = SAI_STATUS_SUCCESS;
    unsigned int num_ports = sai_switch_get_max_lport();
    sai_object_list_t vlan_port_list;
    uint32_t port_idx = 0;

    memset(&vlan_port_list, 0, sizeof(vlan_port_list));

    vlan_port_list.list = (sai_object_id_t *)calloc(1,
            (num_ports * sizeof(sai_object_id_t)));
    if(NULL == vlan_port_list.list) {
        return SAI_STATUS_NO_MEMORY;
    }

    vlan_port_list.count = num_ports;

    sai_vlan_port_list_get(vlan_id, &vlan_port_list);

    for(port_idx = 0; port_idx < vlan_port_list.count; port_idx++) {
        ret_val |= sai_l2_remove_vlan_member(vlan_port_list.list[port_idx]);
    }

    free(vlan_port_list.list);

    return ret_val;
}

static sai_status_t sai_l2_default_vlan_init(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_port_info_t *port_info = NULL;
    sai_attribute_t attr[SAI_L2_DEFAULT_VLAN_MAX_ATTR_COUNT];
    uint32_t attr_count = 0;
    sai_object_id_t vlan_obj_id = SAI_NULL_OBJECT_ID;

    attr[attr_count].id = SAI_VLAN_ATTR_VLAN_ID;
    attr[attr_count].value.u16 = vlan_id;
    attr_count++;

    ret_val = sai_l2_create_vlan(&vlan_obj_id, 0, attr_count, attr);
    if((ret_val != SAI_STATUS_SUCCESS) &&
            (ret_val != SAI_STATUS_ITEM_ALREADY_EXISTS)){
        SAI_VLAN_LOG_ERR("Not creating vlan id %d", vlan_id);
        return ret_val;
    }

    for (port_info = sai_port_info_getfirst(); (port_info != NULL);
            port_info = sai_port_info_getnext(port_info)) {
        if(!sai_is_port_valid(port_info->sai_port_id)) {
            continue;
        }
        ret_val = sai_vlan_port_default_init(vlan_id, port_info->sai_port_id);
        if(ret_val != SAI_STATUS_SUCCESS) {
            sai_default_vlan_init_clean_up(vlan_id);
            SAI_VLAN_LOG_ERR("Unable to add port 0x%"PRIx64"to def vlan %d",
                    port_info->sai_port_id, vlan_id);
            return ret_val;
        }

    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_vlan_init(void)
{
    sai_status_t ret_val;
    ret_val = sai_vlan_cache_init();
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Vlan cache init failed");
        return ret_val;
    }
    ret_val = sai_vlan_npu_api_get()->vlan_init();
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Vlan NPU init failed");
        return ret_val;
    }
    ret_val = sai_l2_default_vlan_init(SAI_INIT_DEFAULT_VLAN);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Default Vlan init failed");
        return ret_val;
    }
    ret_val = sai_port_event_internal_notif_register(SAI_MODULE_VLAN,(sai_port_event_notification_fn)
                                                     sai_vlan_port_notification_handler);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_WARN("Port notification reg failed");
        return ret_val;
    }
    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_l2_bulk_create_vlan_member(
        sai_object_id_t switch_id,
        uint32_t object_count,
        uint32_t *attr_count,
        sai_attribute_t **attrs,
        sai_bulk_op_type_t type,
        sai_object_id_t *object_id,
        sai_status_t *object_statuses)
{
    return SAI_STATUS_NOT_SUPPORTED;
}

sai_status_t sai_l2_bulk_remove_vlan_member(
        uint32_t object_count,
        sai_object_id_t *object_id,
        sai_bulk_op_type_t type,
        sai_status_t *object_statuses)
{
    return SAI_STATUS_NOT_SUPPORTED;
}

static sai_vlan_api_t sai_vlan_method_table =
{
    sai_l2_create_vlan,
    sai_l2_remove_vlan,
    sai_l2_set_vlan_attribute,
    sai_l2_get_vlan_attribute,
    sai_l2_create_vlan_member,
    sai_l2_remove_vlan_member,
    sai_l2_set_vlan_member_attribute,
    sai_l2_get_vlan_member_attribute,
    sai_l2_bulk_create_vlan_member,
    sai_l2_bulk_remove_vlan_member,
    sai_l2_get_vlan_stats,
    sai_l2_clear_vlan_stats,
};

sai_vlan_api_t  *sai_vlan_api_query (void)
{
    return (&sai_vlan_method_table);
}
