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

static sai_status_t sai_l2_delete_vlan(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    ret_val = sai_stp_vlan_destroy (vlan_id);
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


static sai_status_t sai_l2_create_vlan(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    SAI_VLAN_LOG_TRACE("Creating VLAN %d ", vlan_id);
    sai_vlan_lock();
    ret_val = sai_is_vlan_id_available(vlan_id);
    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_vlan_unlock();
        return ret_val;
    }
    ret_val = sai_insert_vlan_in_list(vlan_id);
    if(ret_val != SAI_STATUS_SUCCESS){
        SAI_VLAN_LOG_ERR("Not creating vlan id %d", vlan_id);
        sai_vlan_unlock();
        return ret_val;
    }
    ret_val = sai_vlan_npu_api_get()->vlan_create(vlan_id);
    if((ret_val != SAI_STATUS_SUCCESS) &&
       (ret_val != SAI_STATUS_ITEM_ALREADY_EXISTS)) {
        sai_remove_vlan_from_list(vlan_id);
    } else {
        if(vlan_id != SAI_INIT_DEFAULT_VLAN) {
            ret_val = sai_stp_default_stp_vlan_update(vlan_id);
            if(ret_val != SAI_STATUS_SUCCESS) {
                SAI_VLAN_LOG_ERR("Association of vlan %d to default STP instance failed", vlan_id);
            }
        }
    }

    sai_vlan_unlock();
    return ret_val;
}
static sai_status_t sai_l2_remove_vlan(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    SAI_VLAN_LOG_TRACE("Deleting VLAN %d ", vlan_id);
    sai_vlan_lock();
    ret_val = sai_is_vlan_configurable(vlan_id);
    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_vlan_unlock();
        return ret_val;
    }
    ret_val = sai_l2_delete_vlan(vlan_id);
    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_validate_vlan_port(const sai_vlan_port_t *vlan_port)
{
    if(!sai_is_obj_id_port(vlan_port->port_id)){
        SAI_VLAN_LOG_ERR("Invalid port obj id 0x%"PRIx64"", vlan_port->port_id);
         return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    if(!sai_is_port_valid(vlan_port->port_id)){
       SAI_VLAN_LOG_ERR("Invalid port 0x%"PRIx64"", vlan_port->port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    if(!sai_is_valid_vlan_tagging_mode(vlan_port->tagging_mode)) {
        SAI_VLAN_LOG_ERR("Invalid tagging mode %d for port 0x%"PRIx64"",
                         vlan_port->tagging_mode, vlan_port->port_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2_add_ports_to_vlan(sai_vlan_id_t vlan_id, uint32_t port_count,
                                             const sai_vlan_port_t *port_list)
{
    unsigned int num_port = 0;
    unsigned int tmp_port = 0;
    unsigned int num_nodes = 0;
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_port_fwd_mode_t fwd_mode = SAI_PORT_FWD_MODE_UNKNOWN;
    sai_vlan_port_t non_dup_list[port_count];

    STD_ASSERT(port_list != NULL);
    do {
        sai_vlan_lock();
        if(port_count == 0) {
            SAI_VLAN_LOG_ERR("Invalid port count");
            ret_val = SAI_STATUS_INVALID_PARAMETER;
            break;
        }
        ret_val = sai_is_vlan_configurable(vlan_id);
        if(ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
        memset(non_dup_list, 0, sizeof(non_dup_list));
        for(num_port = 0; num_port < port_count; num_port++) {
            if((ret_val = sai_validate_vlan_port(&port_list[num_port])) != SAI_STATUS_SUCCESS) {
                break;
            }
            sai_port_forward_mode_info(port_list[num_port].port_id,
                                       &fwd_mode, false);
            if(fwd_mode == SAI_PORT_FWD_MODE_ROUTING) {
                SAI_VLAN_LOG_WARN("Port 0x%"PRIx64" is in routing mode.",
                                  port_list[num_port].port_id);
            }
            if(sai_is_port_in_different_tagging_mode(vlan_id,
                                          &port_list[num_port])) {
                SAI_VLAN_LOG_ERR("port 0x%"PRIx64" already tagged as %d on vlan:%d",
                                 port_list[num_port].port_id,
                                 port_list[num_port].tagging_mode, vlan_id);
                ret_val = SAI_STATUS_INVALID_PORT_MEMBER;
                break;
            }
            if(!sai_is_valid_vlan_port_member(vlan_id, &port_list[num_port])) {
                SAI_VLAN_LOG_TRACE("Adding non duplicate port 0x%"PRIx64" on vlan:%d",
                                 port_list[num_port].port_id, vlan_id);
                non_dup_list[num_nodes] = port_list[num_port];
                num_nodes++;
                ret_val = sai_add_vlan_port_node (vlan_id, &port_list[num_port]);
                if(ret_val != SAI_STATUS_SUCCESS) {
                    break;
                }
            }
        }
    } while(0);
    if(ret_val == SAI_STATUS_SUCCESS) {
        ret_val = sai_vlan_npu_api_get()->add_ports_to_vlan(vlan_id, num_nodes,
                                                            non_dup_list);
    }
    if(ret_val != SAI_STATUS_SUCCESS) {
        for(tmp_port = 0; tmp_port < num_nodes; tmp_port++) {
           sai_remove_vlan_port_node(vlan_id, &non_dup_list[tmp_port]);
        }
    }
    sai_vlan_unlock();
    return ret_val;
}


static sai_status_t sai_l2_remove_ports_from_vlan(sai_vlan_id_t vlan_id,
                                           uint32_t port_count,
                                           const sai_vlan_port_t *port_list)
{
    unsigned int num_port = 0;
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    STD_ASSERT(port_list != NULL);
    if(port_count == 0) {
        SAI_VLAN_LOG_ERR("Invalid port count");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    sai_vlan_lock();
    do {
        ret_val = sai_is_vlan_configurable(vlan_id);
        if(ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
        for(num_port = 0; num_port < port_count; num_port++) {
            ret_val = sai_validate_vlan_port(&port_list[num_port]);
            if(ret_val != SAI_STATUS_SUCCESS) {
                break;
            }
            if(!sai_is_valid_vlan_port_member(vlan_id, &port_list[num_port])) {
                /*Return even if single member is invalid.
                  This API should be atomic*/
                SAI_VLAN_LOG_ERR("Unable to find port 0x%"PRIx64" on vlan:%d",
                        port_list[num_port].port_id, vlan_id);
                ret_val = SAI_STATUS_INVALID_PORT_MEMBER;
                break;
            }
        }
        if(ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
        ret_val = sai_vlan_npu_api_get()->remove_ports_from_vlan(vlan_id, port_count,
                                                                 port_list);
        if(ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
        for(num_port = 0; num_port < port_count; num_port++) {
            sai_remove_vlan_port_node (vlan_id, &port_list[num_port]);
        }
    } while(0);
    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_set_vlan_attribute(sai_vlan_id_t vlan_id,
                                           const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

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
            ret_val = sai_stp_vlan_handle  (vlan_id, attr->value.oid);
            break;

        case SAI_VLAN_ATTR_PORT_LIST:
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

        case SAI_VLAN_ATTR_META_DATA:
            SAI_VLAN_LOG_TRACE("Setting Vlan Meta Data for vlan:%d value=%d",
                    vlan_id, attr->value.u32);
            ret_val = sai_vlan_npu_api_get()->set_vlan_meta_data(vlan_id,
                      attr->value.u32);
            if(ret_val == SAI_STATUS_SUCCESS) {
                sai_vlan_meta_data_cache_write(vlan_id, attr->value.u32);
            }
            break;

        default:
            SAI_VLAN_LOG_ERR("Invalid attr :%d for vlan id %d",
                             attr->id, vlan_id);
            ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
            break;

    }
    sai_vlan_unlock();
    return ret_val;
}

static sai_status_t sai_l2_get_vlan_attribute(sai_vlan_id_t vlan_id, uint32_t attr_count,
                                           sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    unsigned int attr_idx = 0;

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
            case SAI_VLAN_ATTR_MAX_LEARNED_ADDRESSES:
                SAI_VLAN_LOG_TRACE("Getting mac learn limit for vlan:%d", vlan_id);
                ret_val = sai_vlan_npu_api_get()->get_vlan_max_learned_address(vlan_id,
                        &attr_list[attr_idx].value.u32);
                break;

            case SAI_VLAN_ATTR_STP_INSTANCE:
                SAI_VLAN_LOG_TRACE("Getting STP instance id for vlan:%d", vlan_id);
                ret_val = sai_stp_vlan_stp_get (vlan_id, &attr_list[attr_idx].value.oid);
                break;

            case SAI_VLAN_ATTR_PORT_LIST:
                SAI_VLAN_LOG_TRACE("Getting list of ports for VLAN:%d", vlan_id);
                ret_val = sai_vlan_port_list_get(vlan_id, &attr_list[attr_idx].value.vlanportlist);
                break;

            case SAI_VLAN_ATTR_LEARN_DISABLE:
                SAI_VLAN_LOG_TRACE("Getting learn disable for vlan:%d",
                        vlan_id);
                ret_val = sai_vlan_npu_api_get()->disable_vlan_learn_get(vlan_id,
                                                                         &attr_list[attr_idx].value.booldata);
                break;

            case SAI_VLAN_ATTR_META_DATA:
                SAI_VLAN_LOG_TRACE("Getting Vlan Meta Data for vlan:%d", vlan_id);
                ret_val = sai_vlan_npu_api_get()->get_vlan_meta_data(vlan_id,
                      &attr_list[attr_idx].value.u32);
                break;

            default:
                SAI_VLAN_LOG_ERR("Invalid attr :%d for vlan id %d",
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

static sai_status_t sai_l2_get_vlan_stats(sai_vlan_id_t vlan_id,
                                   const sai_vlan_stat_t *counter_ids,
                                   uint32_t number_of_counters,
                                   uint64_t *counters)
{
    STD_ASSERT(counter_ids != NULL);
    STD_ASSERT(counters != NULL);
    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_VLAN_LOG_TRACE("Get stats collection for vlan id %d",
                       vlan_id);

    return sai_vlan_npu_api_get()->get_vlan_stats(vlan_id, counter_ids,
                                     number_of_counters, counters);
}

static sai_status_t sai_l2_clear_vlan_stats (sai_vlan_id_t vlan_id,
                                             const sai_vlan_stat_t *counter_ids,
                                             uint32_t number_of_counters)
{
    STD_ASSERT(counter_ids != NULL);

    if(!sai_is_valid_vlan_id(vlan_id)) {
        SAI_VLAN_LOG_ERR("Invalid vlan id %d",vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_VLAN_LOG_TRACE("Clear stats for vlan id %d",
                       vlan_id);

    return sai_vlan_npu_api_get()->clear_vlan_stats (vlan_id, counter_ids,
                                                     number_of_counters);
}

static sai_status_t sai_vlan_port_default_init(sai_vlan_id_t vlan_id, sai_object_id_t port_id)
{
    sai_vlan_port_t vlan_port;
    sai_status_t ret_val;
    sai_attribute_t attr;

    memset(&vlan_port, 0, sizeof(vlan_port));
    /*Add newly created port to default VLAN ID*/
    vlan_port.port_id = port_id;
    vlan_port.tagging_mode = SAI_DEF_VLAN_TAGGING_MODE;
    ret_val = sai_l2_add_ports_to_vlan(vlan_id, 1, &vlan_port);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Unable to add port 0x%"PRIx64" to def vlan:%d",
                port_id, vlan_id);
        return ret_val;
    }
    memset(&attr, 0, sizeof(attr));
    attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;
    attr.value.u16 = vlan_id;
    ret_val = sai_port_api_query()->set_port_attribute(port_id,
                                                      (const sai_attribute_t*)&attr);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_VLAN_LOG_ERR("Unable to set port 0x%"PRIx64" pvid to def vlan:%d",
                port_id, vlan_id);
        sai_l2_remove_ports_from_vlan(vlan_id, 1, &vlan_port);
        return ret_val;
    }

    memset(&attr, 0, sizeof(attr));
    attr.id = SAI_PORT_ATTR_DEFAULT_VLAN_PRIORITY;
    attr.value.u8 = SAI_DFLT_VLAN_PRIORITY;
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
    sai_vlan_port_t vlan_port;
    sai_status_t ret_val = SAI_STATUS_SUCCESS;

    for(port_idx = 0; port_idx < count; port_idx++) {
        port_id = data[port_idx].port_id;
        port_event = data[port_idx].port_event;
        /*Add newly created port to default VLAN ID*/
        if(port_event == SAI_PORT_EVENT_ADD) {
            ret_val =  sai_vlan_port_default_init(SAI_INIT_DEFAULT_VLAN, port_id);
            if(ret_val != SAI_STATUS_SUCCESS) {
                SAI_VLAN_LOG_ERR ("Unable to add port 0x%"PRIx64" to default VLAN",
                                   port_id);
            }
        } else if(port_event == SAI_PORT_EVENT_DELETE) {
            memset(&vlan_port, 0, sizeof(vlan_port));
            vlan_port.port_id = port_id;
            vlan_port.tagging_mode = SAI_DEF_VLAN_TAGGING_MODE;
            if(sai_is_valid_vlan_port_member(SAI_INIT_DEFAULT_VLAN,
                                            (const sai_vlan_port_t*)&vlan_port)) {
                ret_val = sai_l2_remove_ports_from_vlan(SAI_INIT_DEFAULT_VLAN,
                                                        1, &vlan_port);
                if(ret_val != SAI_STATUS_SUCCESS) {
                    SAI_VLAN_LOG_ERR ("Unable to remove port 0x%"PRIx64" from default VLAN",
                                       port_id);
                }
            }
        }
    }
    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_default_vlan_init_clean_up(sai_vlan_id_t vlan_id)
{
    sai_status_t  ret_val;
    unsigned int num_ports = sai_switch_get_max_lport();
    sai_vlan_port_list_t vlan_port_list;
    sai_vlan_port_t vlan_port[num_ports];

    memset(&vlan_port_list, 0, sizeof(vlan_port_list));
    memset(vlan_port, 0, sizeof(vlan_port));
    vlan_port_list.count = num_ports;
    vlan_port_list.list = vlan_port;

    sai_vlan_port_list_get(vlan_id, &vlan_port_list);
    ret_val = sai_l2_remove_ports_from_vlan(vlan_id, vlan_port_list.count,
                                            vlan_port_list.list);

    return ret_val;
}
static sai_status_t sai_l2_default_vlan_init(sai_vlan_id_t vlan_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_port_info_t *port_info = NULL;

    ret_val = sai_l2_create_vlan(vlan_id);
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

static sai_vlan_api_t sai_vlan_method_table =
{
    sai_l2_create_vlan,
    sai_l2_remove_vlan,
    sai_l2_set_vlan_attribute,
    sai_l2_get_vlan_attribute,
    sai_l2_add_ports_to_vlan,
    sai_l2_remove_ports_from_vlan,
    sai_l2_get_vlan_stats,
    sai_l2_clear_vlan_stats,
};

sai_vlan_api_t  *sai_vlan_api_query (void)
{
    return (&sai_vlan_method_table);
}
