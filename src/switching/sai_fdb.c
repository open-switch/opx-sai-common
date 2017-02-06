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
 * @file sai_fdb.c
 *
 * @brief This file contains implementation of SAI FDB APIs.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "saifdb.h"
#include "saivlan.h"
#include "saitypes.h"
#include "saistatus.h"
#include "sai_modules_init.h"
#include "sai_npu_fdb.h"
#include "sai_common_infra.h"
#include "sai_fdb_api.h"
#include "sai_fdb_common.h"
#include "sai_fdb_main.h"
#include "sai_vlan_api.h"
#include "std_mac_utils.h"
#include "sai_port_utils.h"
#include "sai_switch_utils.h"
#include "std_assert.h"
#include "sai_gen_utils.h"
#include "sai_lag_api.h"
#include "std_thread_tools.h"

static std_thread_create_param_t _thread;
static int sai_fdb_fd[SAI_FDB_MAX_FD];
static sai_fdb_event_notification_fn sai_l2_fdb_notification_fn = NULL;

static void * _sai_fdb_internal_notif(void * param) {
    int len = 0;
    while(1) {
        bool wake;
        len = read(sai_fdb_fd[SAI_FDB_READ_FD], &wake,sizeof(bool));
        STD_ASSERT (len!=0);
        if(wake) {
            sai_fdb_send_internal_notifications ();
        }
    }
    return NULL;
}


static void sai_fdb_wake_notification_thread(void)
{
    int rc = 0;
    bool wake = sai_fdb_is_notifications_pending();

    if ((wake) && (rc = write (sai_fdb_fd[SAI_FDB_WRITE_FD], &wake ,sizeof(bool)))!=sizeof(bool)) {
        SAI_FDB_LOG_ERR ("Writing to event queue failed");
    }
}

sai_status_t sai_fdb_init(void)
{
    sai_status_t ret_val;

    ret_val = sai_fdb_npu_api_get()->fdb_init();
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_FDB_LOG_ERR("SAI NPU FDB init failled");
        return ret_val;
    }
    ret_val = sai_init_fdb_tree();
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_FDB_LOG_ERR("SAI FDB Cache init failled");
        return ret_val;
    }

    if (pipe(sai_fdb_fd) != 0) {
        SAI_FDB_LOG_ERR("Pipe initilization failed");
        return SAI_STATUS_FAILURE;
    }

    std_thread_init_struct(&_thread);
    _thread.name = "sai_fdb_internal_notif";
    _thread.thread_function = _sai_fdb_internal_notif;

    if (std_thread_create(&_thread)!=STD_ERR_OK) {
        return SAI_STATUS_FAILURE;
    }

    return SAI_STATUS_SUCCESS;
}
static sai_status_t sai_l2_flush_fdb_entry (unsigned int attr_count,
                                            const sai_attribute_t *attr_list)
{
    sai_object_id_t port_id = 0;
    sai_vlan_id_t vlan_id = VLAN_UNDEF;
    unsigned int attr_idx = 0;
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    bool delete_all = true;
    sai_fdb_flush_entry_type_t flush_type = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;

    if((attr_count > 0) && (attr_list == NULL)) {
        SAI_FDB_LOG_ERR("Null pointer is passed as attribute list");
        STD_ASSERT(0);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        if(attr_list[attr_idx].id == SAI_FDB_FLUSH_ATTR_PORT_ID) {
            port_id = attr_list[attr_idx].value.oid;
        } else if(attr_list[attr_idx].id == SAI_FDB_FLUSH_ATTR_VLAN_ID) {
            vlan_id = attr_list[attr_idx].value.u16;
        } else if(attr_list[attr_idx].id == SAI_FDB_FLUSH_ATTR_ENTRY_TYPE) {
            flush_type = attr_list[attr_idx].value.s32;
            delete_all = false;
        } else {
            return (SAI_STATUS_UNKNOWN_ATTRIBUTE_0 + attr_idx);
        }
    }

    if(port_id != 0) {
        if(sai_is_obj_id_lag(port_id) &&
           !sai_is_lag_created(port_id)) {
            SAI_FDB_LOG_ERR("Invalid LAG id 0x%"PRIx64"", port_id);
            return SAI_STATUS_INVALID_OBJECT_ID;
        } else if(sai_is_obj_id_port(port_id) && !sai_is_port_valid(port_id)) {
            SAI_FDB_LOG_ERR("Invalid port id 0x%"PRIx64"", port_id);
            return SAI_STATUS_INVALID_OBJECT_ID;
        } else if(!sai_is_obj_id_lag(port_id) && !sai_is_obj_id_port(port_id)){
            SAI_FDB_LOG_ERR("Invalid object type for object id 0x%"PRIx64"", port_id);
            return SAI_STATUS_INVALID_OBJECT_TYPE;
        }
    }
    if(vlan_id != VLAN_UNDEF) {
        if(!sai_is_valid_vlan_id(vlan_id)) {
            SAI_FDB_LOG_ERR("Invalid vlan id %d", vlan_id);
            return SAI_STATUS_INVALID_VLAN_ID;
        }
    }
    switch(flush_type) {
        case SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC:
        case SAI_FDB_FLUSH_ENTRY_TYPE_STATIC:
            break;
        default:
            SAI_FDB_LOG_ERR("Invalid flush type:%d",flush_type);
            return SAI_STATUS_INVALID_PARAMETER;
    }
    sai_fdb_lock();
    ret_val = sai_fdb_npu_api_get()->flush_all_fdb_entries (port_id, vlan_id,
                                                            delete_all, flush_type);
    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_fdb_unlock();
        return ret_val;
    }
    if((port_id == 0) && (vlan_id == VLAN_UNDEF)) {
        SAI_FDB_LOG_TRACE("Flushing all FDB entries");
        sai_delete_all_fdb_entry_nodes (delete_all, flush_type);
    } else if((port_id != 0) && (vlan_id == VLAN_UNDEF)) {
        SAI_FDB_LOG_TRACE("Flushing all FDB entries per port:0x%"PRIx64"",
                           port_id);
        sai_delete_fdb_entry_nodes_per_port (port_id, delete_all, flush_type);
    } else if((port_id == 0) && (vlan_id != VLAN_UNDEF)) {
        SAI_FDB_LOG_TRACE("Flushing all FDB entries per vlan:%d",
                          vlan_id);
        sai_delete_fdb_entry_nodes_per_vlan (vlan_id, delete_all, flush_type);
    } else {
        SAI_FDB_LOG_TRACE("Flushing all FDB entries per port:0x%"PRIx64" per vlan:%d",
                          port_id, vlan_id);
        sai_delete_fdb_entry_nodes_per_port_vlan (port_id, vlan_id, delete_all, flush_type);
    }
    sai_fdb_unlock();
    sai_fdb_wake_notification_thread ();
    return ret_val;
}

static sai_status_t sai_l2_remove_fdb_entry(const sai_fdb_entry_t *fdb_entry)
{
    char mac_str[SAI_MAC_STR_LEN] = {0};
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    STD_ASSERT(fdb_entry != NULL);
    if(!sai_is_valid_vlan_id(fdb_entry->vlan_id)) {
        SAI_FDB_LOG_ERR("Invalid vlan id %d",
                        fdb_entry->vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_FDB_LOG_TRACE("Remove FDB Node MAC:%s vlan:%d",
                        std_mac_to_string(&(fdb_entry->mac_address), mac_str,
                                    sizeof(mac_str)), fdb_entry->vlan_id);
    sai_fdb_lock();
    ret_val = sai_fdb_npu_api_get()->flush_fdb_entry(fdb_entry);
    if(ret_val == SAI_STATUS_SUCCESS) {
        sai_delete_fdb_entry_node(fdb_entry);
    }
    sai_fdb_unlock();
    sai_fdb_wake_notification_thread ();
    return ret_val;
}
static sai_status_t sai_l2_create_fdb_entry(const sai_fdb_entry_t *fdb_entry,
                uint32_t attr_count,
                const sai_attribute_t *attr_list)
{
    sai_object_id_t port_id = 0;
    sai_fdb_entry_type_t entry_type = SAI_FDB_ENTRY_TYPE_DYNAMIC;
    sai_packet_action_t action;
    uint32_t metadata = 0;
    unsigned int attr_index = 0;
    bool port_attr_init = false;
    bool type_attr_init = false;
    bool action_attr_init = false;
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    char mac_str[SAI_MAC_STR_LEN] = {0};

    STD_ASSERT(fdb_entry != NULL);
    STD_ASSERT(attr_list != NULL);
    if(attr_count == 0) {
        SAI_FDB_LOG_ERR("Invalid attribute count");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if(!sai_is_valid_vlan_id(fdb_entry->vlan_id)) {
        SAI_FDB_LOG_ERR("Invalid vlan id %d",
                        fdb_entry->vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_FDB_LOG_TRACE("Create FDB entry for MAC:%s vlan:%d",
                       std_mac_to_string(&(fdb_entry->mac_address),
                           mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
    for(attr_index = 0; attr_index < attr_count; attr_index++) {
        ret_val = sai_is_valid_fdb_attribute_val(&attr_list[attr_index]);
        if(ret_val != SAI_STATUS_SUCCESS) {
            ret_val = sai_get_indexed_ret_val(ret_val, attr_index);
            SAI_FDB_LOG_ERR("Invalid attribute for MAC:%s vlan:%d",
                           std_mac_to_string(&(fdb_entry->mac_address),
                                mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
            return ret_val;
        }
        if(attr_list[attr_index].id == SAI_FDB_ENTRY_ATTR_PORT_ID) {
            port_id = attr_list[attr_index].value.oid;
            port_attr_init = true;
        } else if(attr_list[attr_index].id == SAI_FDB_ENTRY_ATTR_TYPE) {
            entry_type = (sai_fdb_entry_type_t)attr_list[attr_index].value.s32;
            type_attr_init = true;
        } else if(attr_list[attr_index].id ==SAI_FDB_ENTRY_ATTR_PACKET_ACTION) {
            action = (sai_packet_action_t)attr_list[attr_index].value.s32;
            action_attr_init = true;
        } else if(attr_list[attr_index].id == SAI_FDB_ENTRY_ATTR_META_DATA) {
            metadata = attr_list[attr_index].value.u32;
        }
    }
    if(!(port_attr_init && type_attr_init && action_attr_init)) {
        SAI_FDB_LOG_ERR("Mandatory attr missing for MAC:%s vlan:%d",
                        std_mac_to_string(&(fdb_entry->mac_address),
                           mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }
    sai_fdb_lock();
    ret_val = sai_fdb_npu_api_get()->create_fdb_entry(fdb_entry, port_id,
                                                      entry_type, action, metadata);
    if(ret_val == SAI_STATUS_SUCCESS) {
        sai_insert_fdb_entry_node(fdb_entry, port_id, entry_type, action, metadata);
    }
    sai_fdb_unlock();
    sai_fdb_wake_notification_thread ();
    return ret_val;
}

static sai_fdb_entry_node_t *sai_fdb_populate_node_from_hardware(
                                     const sai_fdb_entry_t *fdb_entry)
{
    sai_fdb_entry_node_t *fdb_entry_node = NULL;
    sai_fdb_entry_node_t *tmp_fdb_entry_node = NULL;
    char                  mac_str[SAI_MAC_STR_LEN] = {0};

    fdb_entry_node = (sai_fdb_entry_node_t *)
                            calloc(1, sizeof(sai_fdb_entry_node_t));
    if(fdb_entry_node == NULL) {
        SAI_FDB_LOG_CRIT("No memory for %d",sizeof(sai_fdb_entry_node_t));
        return NULL;
    }
    if(sai_fdb_npu_api_get()->get_fdb_entry_from_hardware(fdb_entry,
               fdb_entry_node)!= SAI_STATUS_SUCCESS) {
        free(fdb_entry_node);
        SAI_FDB_LOG_WARN("FDB Entry not found for MAC:%s vlan:%d",
                        std_mac_to_string(&(fdb_entry->mac_address),
                            mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
        return NULL;
    }

    tmp_fdb_entry_node = sai_add_fdb_entry_node_in_global_tree (fdb_entry_node);

    if (tmp_fdb_entry_node != fdb_entry_node) {
        free (fdb_entry_node);
        fdb_entry_node = tmp_fdb_entry_node;
    }

    return fdb_entry_node;
}

static sai_status_t sai_l2_set_fdb_entry_attribute(const sai_fdb_entry_t *fdb_entry,
                     const sai_attribute_t *attr)
{
    sai_fdb_entry_node_t *fdb_entry_node = NULL;
    sai_fdb_entry_node_t temp_node;
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    char mac_str[SAI_MAC_STR_LEN] = {0};

    STD_ASSERT(fdb_entry != NULL);
    STD_ASSERT(attr != NULL);
    memset(&temp_node, 0, sizeof(temp_node));
    if(!sai_is_valid_vlan_id(fdb_entry->vlan_id)) {
        SAI_FDB_LOG_ERR("Invalid vlan id %d",
                        fdb_entry->vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    SAI_FDB_LOG_TRACE("Set FDB attribute:%d MAC:%s vlan:%d",
                      attr->id,std_mac_to_string(&(fdb_entry->mac_address),
                                 mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
    ret_val = sai_is_valid_fdb_attribute_val(attr);
    if(ret_val != SAI_STATUS_SUCCESS) {
        memset(mac_str, 0, sizeof(mac_str));
        SAI_FDB_LOG_ERR("Invalid attribute for MAC:%s vlan:%d",
                        std_mac_to_string(&(fdb_entry->mac_address),
                            mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
        return ret_val;
    }
    sai_fdb_lock();
    fdb_entry_node = sai_get_fdb_entry_node(fdb_entry);
    if(fdb_entry_node == NULL) {
        fdb_entry_node = sai_fdb_populate_node_from_hardware(fdb_entry);
        if(fdb_entry_node == NULL) {
            sai_fdb_unlock();
            return SAI_STATUS_ADDR_NOT_FOUND;
        }
    }
    memcpy(&temp_node, fdb_entry_node, sizeof(temp_node));
    sai_update_fdb_entry_node(fdb_entry_node, attr);
    ret_val = sai_fdb_npu_api_get()->write_fdb_entry_to_hardware(fdb_entry_node);
    if(ret_val != SAI_STATUS_SUCCESS) {
        memcpy(fdb_entry_node, &temp_node, sizeof(temp_node));
    }
    sai_fdb_unlock();
    sai_fdb_wake_notification_thread ();

    return ret_val;
}

static sai_status_t sai_l2_get_fdb_entry_attribute(const sai_fdb_entry_t *fdb_entry,
                     uint32_t attr_count,
                     sai_attribute_t *attr_list)
{
    unsigned int attr_index = 0;
    sai_fdb_entry_node_t *fdb_entry_node = NULL;
    char mac_str[SAI_MAC_STR_LEN];

    memset(mac_str, 0, sizeof(mac_str));

    STD_ASSERT(fdb_entry != NULL);
    STD_ASSERT(attr_list != NULL);
    if(attr_count == 0) {
        SAI_FDB_LOG_ERR("Invalid attribute count");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    if(!sai_is_valid_vlan_id(fdb_entry->vlan_id)) {
        SAI_FDB_LOG_ERR("Invalid vlan id %d",
                        fdb_entry->vlan_id);
        return SAI_STATUS_INVALID_VLAN_ID;
    }
    for(attr_index = 0; attr_index < attr_count; attr_index++) {
        SAI_FDB_LOG_TRACE("Get FDB attribute:%d MAC:%s vlan:%d",
                           attr_list[attr_index].id,
                           std_mac_to_string(&(fdb_entry->mac_address),
                           mac_str, sizeof(mac_str)), fdb_entry->vlan_id);
        switch(attr_list[attr_index].id) {
            case SAI_FDB_ENTRY_ATTR_PORT_ID:
            case SAI_FDB_ENTRY_ATTR_TYPE:
            case SAI_FDB_ENTRY_ATTR_PACKET_ACTION:
            case SAI_FDB_ENTRY_ATTR_META_DATA:
                /*Correct values*/
                break;
            default:
                SAI_FDB_LOG_ERR("Invalid attribute for MAC:%s vlan:%d",
                                std_mac_to_string(&(fdb_entry->mac_address),
                                                  mac_str, sizeof(mac_str)),
                                fdb_entry->vlan_id);
                return sai_get_indexed_ret_val (SAI_STATUS_UNKNOWN_ATTRIBUTE_0,
                                                attr_index);
        }
    }
    sai_fdb_lock();
    fdb_entry_node = sai_get_fdb_entry_node(fdb_entry) ;

    if(fdb_entry_node == NULL) {
        fdb_entry_node = sai_fdb_populate_node_from_hardware(fdb_entry);
        if(fdb_entry_node == NULL) {
            sai_fdb_unlock();
            return SAI_STATUS_ADDR_NOT_FOUND;
        }
    }

    for(attr_index = 0; attr_index < attr_count; attr_index++) {
        if(attr_list[attr_index].id == SAI_FDB_ENTRY_ATTR_PORT_ID) {
            attr_list[attr_index].value.oid = fdb_entry_node->port_id;
        } else if(attr_list[attr_index].id == SAI_FDB_ENTRY_ATTR_TYPE) {
            attr_list[attr_index].value.s32 = fdb_entry_node->entry_type;
        } else if(attr_list[attr_index].id ==SAI_FDB_ENTRY_ATTR_PACKET_ACTION) {
            attr_list[attr_index].value.s32 = fdb_entry_node->action;
        } else if(attr_list[attr_index].id ==SAI_FDB_ENTRY_ATTR_META_DATA) {
            attr_list[attr_index].value.u32 = fdb_entry_node->metadata;
        }
    }
    sai_fdb_unlock();
    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_l2_fdb_set_aging_time(uint32_t value)
{
    return sai_fdb_npu_api_get()->set_aging_time(value);
}

sai_status_t sai_l2_fdb_get_aging_time(uint32_t *value)
{
    STD_ASSERT(value != NULL);
    return sai_fdb_npu_api_get()->get_aging_time(value);
}

static void sai_common_fdb_event_notification (uint32_t count,
                                               sai_fdb_event_notification_data_t *data)
{
    sai_object_id_t port_id = 0;
    sai_fdb_entry_type_t entry_type = 0;
    sai_fdb_entry_node_t *fdb_entry_node = NULL;
    sai_packet_action_t action = 0;
    unsigned int attr_idx = 0;
    unsigned int entry_idx = 0;
    unsigned int metadata = 0;

    STD_ASSERT(data != NULL);
    if(count == 0) {
        SAI_FDB_LOG_ERR("Invalid attribute count");
        return;
    }
    sai_fdb_lock();
    for(entry_idx = 0; entry_idx < count; entry_idx++) {
        STD_ASSERT(data[entry_idx].attr != NULL);
        for(attr_idx = 0; attr_idx < data[entry_idx].attr_count; attr_idx++) {
            if(data[entry_idx].attr[attr_idx].id == SAI_FDB_ENTRY_ATTR_PORT_ID) {
                port_id = data[entry_idx].attr[attr_idx].value.oid;
            } else if(data[entry_idx].attr[attr_idx].id == SAI_FDB_ENTRY_ATTR_TYPE) {
                entry_type = (sai_fdb_entry_type_t)data[entry_idx].attr[attr_idx].value.s32;
            } else if(data[entry_idx].attr[attr_idx].id == SAI_FDB_ENTRY_ATTR_PACKET_ACTION) {
                action = (sai_packet_action_t)data[entry_idx].attr[attr_idx].value.s32;
            } else if (data[entry_idx].attr[attr_idx].id == SAI_FDB_ENTRY_ATTR_META_DATA) {
                metadata = data[entry_idx].attr[attr_idx].value.u32;
            }
        }

        if(data[entry_idx].event_type == SAI_FDB_EVENT_LEARNED) {
            sai_insert_fdb_entry_node((const sai_fdb_entry_t*)&data[entry_idx].fdb_entry, port_id,
                    entry_type, action, metadata);
        }else if((data[entry_idx].event_type == SAI_FDB_EVENT_AGED) ||
                (data[entry_idx].event_type == SAI_FDB_EVENT_FLUSHED)) {
            fdb_entry_node = sai_get_fdb_entry_node(&data[entry_idx].fdb_entry);
            if(fdb_entry_node != NULL) {
                sai_delete_fdb_entry_node ((const sai_fdb_entry_t*)&data[entry_idx].fdb_entry);
            }
        }
    }
    sai_fdb_unlock();
    sai_fdb_wake_notification_thread ();
    if(sai_l2_fdb_notification_fn != NULL) {
        sai_l2_fdb_notification_fn(count,data);
    }
}
sai_status_t sai_l2_fdb_register_callback(sai_fdb_event_notification_fn
                                                         fdb_notification_fn)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    sai_fdb_lock();
    ret_val = sai_fdb_npu_api_get()->register_callback((sai_fdb_event_notification_fn)
                                        sai_common_fdb_event_notification);
    if(ret_val == SAI_STATUS_SUCCESS) {
        sai_l2_fdb_notification_fn = fdb_notification_fn;
    }
    sai_fdb_unlock();
    return ret_val;
}

sai_status_t sai_l2_register_fdb_entry (const sai_fdb_entry_t *fdb_entry)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    sai_fdb_lock();
    ret_val =  sai_fdb_write_registered_entry_into_cache (fdb_entry);
    sai_fdb_unlock();
    return ret_val;
}

sai_status_t sai_l2_deregister_fdb_entry (const sai_fdb_entry_t *fdb_entry)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    sai_fdb_lock();
    ret_val = sai_fdb_remove_registered_entry_from_cache (fdb_entry);
    sai_fdb_unlock();
    return ret_val;
}

void sai_l2_fdb_register_internal_callback (sai_fdb_internal_callback_fn
                                                       fdb_internal_callback)
{
    sai_fdb_lock();
    sai_fdb_internal_callback_cache_update(fdb_internal_callback);
    sai_fdb_unlock();
}

sai_status_t sai_l2_fdb_set_switch_max_learned_address(uint32_t value)
{
    return sai_fdb_npu_api_get()->set_switch_max_learned_address(value);
}

sai_status_t sai_l2_fdb_get_switch_max_learned_address(uint32_t *value)
{
    STD_ASSERT(value != NULL);
    return sai_fdb_npu_api_get()->get_switch_max_learned_address(value);
}

sai_status_t sai_l2_fdb_ucast_miss_action_set(const sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_ucast_miss_action_set(attr);
}

sai_status_t sai_l2_fdb_ucast_miss_action_get(sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_ucast_miss_action_get(attr);
}

sai_status_t sai_l2_fdb_mcast_miss_action_set(const sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_mcast_miss_action_set(attr);
}

sai_status_t sai_l2_fdb_mcast_miss_action_get(sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_mcast_miss_action_get(attr);
}

sai_status_t sai_l2_fdb_bcast_miss_action_set(const sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_bcast_miss_action_set(attr);
}

sai_status_t sai_l2_fdb_bcast_miss_action_get(sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->fdb_bcast_miss_action_get(attr);
}

sai_status_t sai_l2_mcast_cpu_flood_enable_set(bool enable)
{
     return sai_fdb_npu_api_get()->mcast_cpu_flood_enable_set(enable);
}

sai_status_t sai_l2_mcast_cpu_flood_enable_get(bool *enable)
{
    STD_ASSERT(enable != NULL);
    return sai_fdb_npu_api_get()->mcast_cpu_flood_enable_get(enable);
}

sai_status_t sai_l2_bcast_cpu_flood_enable_set(bool enable)
{
     return sai_fdb_npu_api_get()->bcast_cpu_flood_enable_set(enable);
}

sai_status_t sai_l2_bcast_cpu_flood_enable_get(bool *enable)
{
    STD_ASSERT(enable != NULL);
    return sai_fdb_npu_api_get()->bcast_cpu_flood_enable_get(enable);
}
sai_status_t sai_l2_get_fdb_table_size(sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);
    return sai_fdb_npu_api_get()->get_fdb_table_size(attr);
}

static sai_fdb_api_t sai_fdb_method_table =
{
    sai_l2_create_fdb_entry,
    sai_l2_remove_fdb_entry,
    sai_l2_set_fdb_entry_attribute,
    sai_l2_get_fdb_entry_attribute,
    sai_l2_flush_fdb_entry,
};

sai_fdb_api_t  *sai_fdb_api_query (void)
{
    return (&sai_fdb_method_table);
}
