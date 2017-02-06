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
 * @file sai_lag.c
 *
 * @brief This file contains implementation of SAI LAG APIs.
 */

#include "saitypes.h"
#include "sailag.h"
#include "saistatus.h"
#include "sai_modules_init.h"
#include "sai_lag_common.h"
#include "sai_npu_lag.h"
#include "sai_lag_api.h"
#include "sai_lag_callback.h"
#include "sai_port_utils.h"
#include "sai_gen_utils.h"
#include "sai_common_infra.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "std_assert.h"

static sai_lag_callback_t lag_callback;

sai_status_t sai_lag_init(void)
{
    sai_lag_cache_init();
    sai_lag_npu_api_get()->lag_init();
    return SAI_STATUS_SUCCESS;
}

void sai_lag_rif_callback_register(sai_lag_l3_rif_callback rif_callback)
{
    sai_lag_lock();
    lag_callback.rif_callback = rif_callback;
    sai_lag_unlock();
}

static sai_status_t sai_lag_rif_notify(sai_object_id_t lag_id,
                                       sai_lag_operation_t lag_operation,
                                       const sai_object_list_t *lag_port_list)
{
    sai_object_id_t rif_id;

    rif_id = sai_lag_get_rif_id(lag_id);
    if(sai_is_obj_id_rif(rif_id) &&
       (lag_callback.rif_callback !=NULL)) {
        return lag_callback.rif_callback(lag_id, rif_id, lag_port_list, lag_operation);
    }
    return SAI_STATUS_SUCCESS;
}
static sai_status_t sai_l2_remove_all_lag_ports(sai_object_id_t lag_id)
{
    sai_object_list_t lag_port_list;
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    ret_val = sai_lag_port_count_get(lag_id, &(lag_port_list.count));

    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_LAG_LOG_WARN("Unable to obtain port count for lag :0x%"PRIx64"",lag_id);
        return ret_val;
    }
    if(lag_port_list.count == 0 ) {
        return SAI_STATUS_SUCCESS;
    }
    sai_object_id_t port_list[lag_port_list.count];

    memset(port_list, 0, sizeof(port_list));
    lag_port_list.list = port_list;

    ret_val = sai_lag_port_list_get(lag_id, &lag_port_list);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_LAG_LOG_ERR("Unable to obtain port list for lag :0x%"PRIx64"",lag_id);
        return ret_val;
    }

    ret_val = sai_lag_npu_api_get()->remove_ports_from_lag(lag_id,(const sai_object_list_t *)
                                            &lag_port_list);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_LAG_LOG_ERR("Unable to remove port list for lag :0x%"PRIx64"",lag_id);
        return ret_val;
    }
    sai_remove_all_lag_port_nodes(lag_id);

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2_add_lag_port (sai_object_id_t  lag_id,
                                         sai_object_id_t  port_id,
                                         sai_object_id_t *member_id)
{
    sai_object_list_t port_list;
    sai_object_list_t member_id_list;
    sai_status_t      ret_val;

    SAI_LAG_LOG_TRACE ("Adding port 0x%"PRIx64" on lag:0x%"PRIx64"",
                       port_id, lag_id);

    memset (&port_list, 0, sizeof (port_list));
    memset (&member_id_list, 0, sizeof (member_id_list));

    port_list.count      = 1;
    port_list.list       = &port_id;
    member_id_list.count = 1;
    member_id_list.list  = member_id;

    ret_val = sai_lag_npu_api_get()->add_ports_to_lag (lag_id,
                                                       &port_list,
                                                       &member_id_list);
    if (ret_val != SAI_STATUS_SUCCESS) {
        return ret_val;
    }

    ret_val = sai_lag_port_node_add (lag_id, port_id, *member_id);

    if (ret_val != SAI_STATUS_SUCCESS) {
        sai_lag_npu_api_get()->remove_ports_from_lag (lag_id, &port_list);
    }

    return ret_val;
}

static sai_status_t sai_l2_remove_lag_port (sai_object_id_t lag_id,
                                            sai_object_id_t port_id)
{
    sai_object_list_t port_list;
    sai_status_t      ret_val = SAI_STATUS_SUCCESS;

    SAI_LAG_LOG_TRACE ("Removing port 0x%"PRIx64" from lag:0x%"PRIx64"",
                       port_id, lag_id);

    port_list.count = 1;
    port_list.list  = &port_id;

    ret_val = sai_lag_npu_api_get()->remove_ports_from_lag (lag_id, &port_list);

    sai_lag_port_node_remove (lag_id, port_id);

    return ret_val;
}

static sai_status_t sai_l2_lag_port_list_set(sai_object_id_t lag_id,
                                      const sai_object_list_t *lag_port_list)
{
    unsigned int num_port = 0;
    unsigned int tmp_port = 0;
    unsigned int num_nodes = 0;
    unsigned int invalid_idx = 0;
    sai_object_id_t member_id;
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_object_list_t non_dup_port_list;

    STD_ASSERT(lag_port_list != NULL);
    if(!sai_is_port_list_valid(lag_port_list, &invalid_idx)) {
        SAI_LAG_LOG_ERR("Invalid port list");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_object_id_t non_dup_list[lag_port_list->count];
    memset(non_dup_list, 0, sizeof(non_dup_list));

    do {
        if(!sai_is_lag_created(lag_id)) {
            SAI_LAG_LOG_WARN("lag id not found 0x%"PRIx64"",lag_id);
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }
        ret_val = sai_l2_remove_all_lag_ports(lag_id);
        if(ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
        for(num_port = 0; num_port < lag_port_list->count; num_port++) {
            if(sai_is_port_part_of_different_lag(lag_id, lag_port_list->list[num_port])) {
                SAI_LAG_LOG_WARN("Invalid port 0x%"PRIx64" to lag 0x%"PRIx64"",
                                lag_id, lag_port_list->list[num_port]);
                ret_val = SAI_STATUS_INVALID_PORT_MEMBER;
                break;
            }
            if(!sai_is_port_lag_member(lag_id, lag_port_list->list[num_port])) {
                SAI_LAG_LOG_TRACE("Adding non duplicate port 0x%"PRIx64" on lag:0x%"PRIx64"",
                                  lag_port_list->list[num_port], lag_id);
                non_dup_list[num_nodes] = lag_port_list->list[num_port];
                num_nodes++;

                /*
                 * sai_l2_lag_port_list_set() will be obsoleted. The member
                 * addition API is changed, such that they can be added
                 * one by one. Each member port will be assigned an
                 * object id (member_id) by the NPU layer. However the old
                 * implementation (port list addition) does not require
                 * the member_id. Hence setting it to 0.
                 */
                member_id = 0;
                ret_val = sai_lag_port_node_add (lag_id,
                                                 lag_port_list->list[num_port],
                                                 member_id);
                if(ret_val != SAI_STATUS_SUCCESS) {
                    break;
                }
            }
        }
    } while(0);
    if(ret_val == SAI_STATUS_SUCCESS) {
        sai_object_list_t member_id_list;
        sai_object_id_t   member_ids [num_nodes];
        sai_object_id_t   port_list[num_nodes];

        memset(&non_dup_port_list, 0, sizeof(sai_object_list_t));
        memset(port_list, 0, sizeof(port_list));
        memset (&member_id_list, 0, sizeof (member_id_list));
        memset (member_ids, 0, sizeof (member_ids));

        non_dup_port_list.count = num_nodes;
        non_dup_port_list.list  = port_list;
        member_id_list.list     = member_ids;
        member_id_list.count    = num_nodes;

        ret_val = sai_lag_port_list_get(lag_id, &non_dup_port_list);
        if(ret_val == SAI_STATUS_SUCCESS) {
            ret_val = sai_lag_npu_api_get()->add_ports_to_lag(lag_id,
                                          &non_dup_port_list,
                                          &member_id_list);
        }
    }
    if(ret_val != SAI_STATUS_SUCCESS) {
        for(tmp_port = 0; tmp_port < num_nodes; tmp_port++) {
           sai_lag_port_node_remove(lag_id, non_dup_list[tmp_port]);
        }
    }

    return ret_val;
}

static sai_status_t sai_l2_create_lag(sai_object_id_t *lag_id,uint32_t attr_count,
                                      const sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    unsigned int attr_idx = 0;

    if(attr_count > 0) {
        STD_ASSERT((attr_list != NULL));
    }
    sai_lag_lock();
    ret_val = sai_lag_npu_api_get()->lag_create(lag_id);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_LAG_LOG_ERR("Unable to create new LAG");
        sai_lag_unlock();
        return ret_val;
    }
    ret_val = sai_lag_node_add(*lag_id);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_LAG_LOG_WARN("Unable to add LAG id:0x%"PRIx64" in cache", *lag_id);
        sai_lag_npu_api_get()->lag_remove(*lag_id);
    }
    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id) {
            case SAI_LAG_ATTR_PORT_LIST:
                SAI_LAG_LOG_TRACE("Setting list of ports for lag:0x%"PRIx64"", *lag_id);
                ret_val = sai_l2_lag_port_list_set(*lag_id,&(attr_list[attr_idx].value.objlist));
                break;
            default:
                SAI_LAG_LOG_ERR("Unknown attr :%d for lag id 0x%"PRIx64"",
                                 attr_list[attr_idx].id, *lag_id);
                ret_val = sai_get_indexed_ret_val(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,attr_idx);
                break;
        }
        if(ret_val != SAI_STATUS_SUCCESS){
            break;
        }
    }
    SAI_LAG_LOG_TRACE("Return value is %d",ret_val);
    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_lag_npu_api_get()->lag_remove(*lag_id);
        sai_remove_all_lag_port_nodes(*lag_id);
        sai_lag_node_remove(*lag_id);
    }
    sai_lag_unlock();
    return ret_val;
}

static sai_status_t sai_l2_remove_lag(sai_object_id_t lag_id)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    sai_object_id_t rif_id = 0;

    sai_lag_lock();

    if(!sai_is_lag_created(lag_id)) {
        SAI_LAG_LOG_WARN("LAG id 0x%"PRIx64" not found",lag_id);
        sai_lag_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    rif_id = sai_lag_get_rif_id(lag_id);
    if(sai_is_obj_id_rif(rif_id)) {
        SAI_LAG_LOG_ERR("LAG id 0x%"PRIx64" is associated to rif 0x%"PRIx64"",
                        lag_id, rif_id);
        sai_lag_unlock();
        return SAI_STATUS_OBJECT_IN_USE;
    }

    sai_remove_all_lag_port_nodes(lag_id);
    ret_val = sai_lag_node_remove(lag_id);
    if(ret_val == SAI_STATUS_SUCCESS) {
        ret_val = sai_lag_npu_api_get()->lag_remove(lag_id);
        if(ret_val != SAI_STATUS_SUCCESS) {
            SAI_LAG_LOG_ERR("Unable to remove LAG id:0x%"PRIx64"", lag_id);
            sai_lag_node_add(lag_id);
        }
    }
    sai_lag_unlock();
    return ret_val;
}

static sai_status_t sai_l2_set_lag_attribute(sai_object_id_t lag_id,
                                             const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    unsigned int port_count = 0;
    sai_object_list_t old_lag_port_list;

    STD_ASSERT(attr != NULL);
    sai_lag_lock();
    if(!sai_is_lag_created(lag_id)) {
        SAI_LAG_LOG_WARN("lag id not found 0x%"PRIx64"",lag_id);
        sai_lag_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }
    switch(attr->id)
    {
        case SAI_LAG_ATTR_PORT_LIST:
            SAI_LAG_LOG_TRACE("Setting list of ports for lag:0x%"PRIx64"", lag_id);
            sai_lag_port_count_get(lag_id, &port_count);
            if(port_count != 0 ) {
                old_lag_port_list.count = port_count;
                sai_object_id_t old_port_list[port_count];

                memset(old_port_list, 0, sizeof(old_port_list));
                old_lag_port_list.list = old_port_list;
                ret_val = sai_lag_port_list_get(lag_id, &old_lag_port_list);
                if(ret_val != SAI_STATUS_SUCCESS) {
                    SAI_LAG_LOG_ERR("Unable to obtain port list for lag :0x%"PRIx64"",lag_id);
                    break;
                }
            }
            ret_val = sai_l2_lag_port_list_set(lag_id,&(attr->value.objlist));
            if(ret_val == SAI_STATUS_SUCCESS) {
                ret_val = sai_lag_rif_notify (lag_id, SAI_LAG_OPER_DEL_PORTS,
                                              (const sai_object_list_t *)&old_lag_port_list);
                if(ret_val == SAI_STATUS_SUCCESS) {
                    ret_val = sai_lag_rif_notify (lag_id, SAI_LAG_OPER_ADD_PORTS,
                                                  &(attr->value.objlist));
                }
                if(ret_val != SAI_STATUS_SUCCESS) {
                    sai_lag_rif_notify (lag_id, SAI_LAG_OPER_ADD_PORTS,
                                                  (const sai_object_list_t *)&old_lag_port_list);
                    sai_l2_lag_port_list_set(lag_id,(const sai_object_list_t *)&old_lag_port_list);
                }
            }
            ret_val = sai_l2_lag_port_list_set(lag_id,&(attr->value.objlist));
            break;
        default:
            SAI_LAG_LOG_ERR("Unknown attr :%d for lag id 0x%"PRIx64"",
                             attr->id, lag_id);
            ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
            break;

    }
    sai_lag_unlock();

    return ret_val;
}

static sai_status_t sai_l2_get_lag_attribute(sai_object_id_t lag_id, uint32_t attr_count,
                                             sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;
    unsigned int attr_idx = 0;

    STD_ASSERT(attr_list != NULL);
    if(!sai_is_lag_created(lag_id)) {
        SAI_LAG_LOG_WARN("lag id not found 0x%"PRIx64"",lag_id);
        sai_lag_unlock();
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id)
        {
            case SAI_LAG_ATTR_PORT_LIST:
                SAI_LAG_LOG_TRACE("Getting list of ports for lag:0x%"PRIx64"", lag_id);
                ret_val = sai_lag_port_list_get(lag_id,&(attr_list[attr_idx].value.objlist));
                break;
            default:
                SAI_LAG_LOG_ERR("Unknown attr :%d for lag id 0x%"PRIx64"",
                                 attr_list[attr_idx].id, lag_id);
                ret_val = sai_get_indexed_ret_val(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,attr_idx);
                break;
        }
        if(ret_val != SAI_STATUS_SUCCESS){
            SAI_LAG_LOG_ERR("Error in setting the lag attributes RC = %d",ret_val);
            break;
        }
    }
    return ret_val;
}

static sai_status_t sai_l2_lag_port_flag_set (sai_object_id_t lag,
                                              sai_object_id_t port_id,
                                              bool            is_ingress,
                                              bool            value)
{
    sai_status_t rc;
    bool         old_value;

    rc = sai_lag_member_get_disable_status (lag, port_id,
                                            is_ingress, &old_value);

    if (rc != SAI_STATUS_SUCCESS) {
        return rc;
    }

    if (old_value == value) {
        return SAI_STATUS_SUCCESS;
    }

    rc = sai_lag_npu_api_get()->lag_port_flag_set (lag, port_id,
                                                   is_ingress, value);

    if (rc == SAI_STATUS_SUCCESS) {
        sai_lag_member_set_disable_status (lag, port_id, is_ingress, value);
    }
    else {
        sai_lag_npu_api_get()->lag_port_flag_set(lag, port_id,
                                                 is_ingress, old_value);
    }

    return rc;
}

static sai_status_t sai_l2_validate_lag_port_add (sai_object_id_t lag_id,
                                                  sai_object_id_t port_id)
{
    sai_port_fwd_mode_t fwd_mode;

    if (!sai_is_port_valid (port_id)) {
        SAI_LAG_LOG_ERR("Invalid port");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if (!sai_is_lag_created (lag_id)) {
        SAI_LAG_LOG_WARN ("lag id not found 0x%"PRIx64"", lag_id);
        return SAI_STATUS_ITEM_NOT_FOUND;
    }

    if (sai_is_port_lag_member (lag_id, port_id)) {
        SAI_LAG_LOG_WARN ("Member Port 0x%"PRIx64" already a member "
                          "of lag 0x%"PRIx64"", port_id, lag_id);
        return SAI_STATUS_ITEM_ALREADY_EXISTS;
    }

    if (sai_is_port_part_of_different_lag (lag_id, port_id)) {
        SAI_LAG_LOG_WARN ("Invalid port 0x%"PRIx64" to lag 0x%"PRIx64"",
                          port_id, lag_id);
        return SAI_STATUS_INVALID_PORT_MEMBER;
    }

    if (sai_port_forward_mode_info (port_id, &fwd_mode, false)
        != SAI_STATUS_SUCCESS) {
        return SAI_STATUS_OBJECT_IN_USE;
    }

    if (fwd_mode == SAI_PORT_FWD_MODE_ROUTING) {
        SAI_LAG_LOG_ERR ("port 0x%"PRIx64" in routing mode. "
                         "Cannot add to lag:0x%"PRIx64"", port_id, lag_id);
        return SAI_STATUS_OBJECT_IN_USE;
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_l2_create_lag_member (sai_object_id_t *out_member_id,
                                              uint32_t         attr_count,
                                              const sai_attribute_t *attr_list)
{
    sai_object_id_t   lag = SAI_NULL_OBJECT_ID;
    sai_object_id_t   port_id = SAI_NULL_OBJECT_ID;
    sai_object_list_t port_list;
    bool              lag_attr_present = false;
    bool              port_attr_present = false;
    bool              ing_disable_attr_present = false;
    bool              egr_disable_attr_present = false;
    bool              ing_disable = false;
    bool              egr_disable = false;
    bool              lag_port_added = false;
    sai_status_t      ret_val = SAI_STATUS_SUCCESS;
    uint_t            attr_idx;

    STD_ASSERT (out_member_id != NULL);

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    }

    for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {

        switch (attr_list [attr_idx].id) {

            case SAI_LAG_MEMBER_ATTR_LAG_ID:
                if (lag_attr_present) {

                    if (lag != attr_list [attr_idx].value.oid) {

                        ret_val = SAI_STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                else {
                    lag = attr_list [attr_idx].value.oid;
                    lag_attr_present = true;
                }
                break;

            case SAI_LAG_MEMBER_ATTR_PORT_ID:
                if (port_attr_present) {

                    if (port_id != attr_list [attr_idx].value.oid) {

                        ret_val = SAI_STATUS_INVALID_PARAMETER;
                        break;
                    }
                }
                else {
                    port_id = attr_list [attr_idx].value.oid;
                    port_attr_present = true;
                }
                break;

            case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
                egr_disable_attr_present = true;
                egr_disable = attr_list [attr_idx].value.booldata;
                break;

            case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
                ing_disable_attr_present = true;
                ing_disable = attr_list [attr_idx].value.booldata;
                break;

            default:
                return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
        }
    }

    if ((!lag_attr_present) || (!port_attr_present)) {
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    sai_lag_lock();

    do {
        ret_val = sai_l2_validate_lag_port_add (lag, port_id);

        if (ret_val == SAI_STATUS_SUCCESS) {

            ret_val = sai_l2_add_lag_port (lag, port_id, out_member_id);

            if (ret_val != SAI_STATUS_SUCCESS) {
                break;
            }
        }
        else {
            break;
        }

        lag_port_added = true;

        if (ing_disable_attr_present) {
            ret_val
                = sai_l2_lag_port_flag_set (lag, port_id, true, ing_disable);

            if (ret_val != SAI_STATUS_SUCCESS) {
                break;
            }
        }

        if (egr_disable_attr_present) {
            ret_val
                = sai_l2_lag_port_flag_set (lag, port_id, false, egr_disable);

            if (ret_val != SAI_STATUS_SUCCESS) {
                break;
            }
        }

        port_list.count = 1;
        port_list.list  = &port_id;
        ret_val = sai_lag_rif_notify (lag, SAI_LAG_OPER_ADD_PORTS, &port_list);

        if (ret_val != SAI_STATUS_SUCCESS) {
            break;
        }
    } while (0);

    if ((ret_val != SAI_STATUS_SUCCESS) && (lag_port_added)) {
        sai_l2_remove_lag_port (lag, port_id);
    }

    sai_lag_unlock ();

    return ret_val;
}

static sai_status_t sai_l2_remove_lag_member (sai_object_id_t member_id)
{
    sai_object_list_t port_list;
    sai_object_id_t   lag_id;
    sai_object_id_t   port_id;
    sai_status_t      rc;

    sai_lag_lock();

    do {

        rc = sai_lag_get_info_from_member_id (member_id, &lag_id, &port_id);

        if (rc != SAI_STATUS_SUCCESS) {
            break;
        }

        if (!sai_is_port_valid (port_id)) {
            SAI_LAG_LOG_ERR("Invalid port");
            rc = SAI_STATUS_INVALID_PARAMETER;
            break;
        }

        if (!sai_is_lag_created (lag_id)) {
            SAI_LAG_LOG_WARN ("lag id not found 0x%"PRIx64"", lag_id);
            rc = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if (!sai_is_port_lag_member (lag_id, port_id)) {
            SAI_LAG_LOG_WARN ("Port 0x%"PRIx64" not a member "
                              "of lag 0x%"PRIx64"", port_id, lag_id);
            rc = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        sai_l2_remove_lag_port (lag_id, port_id);

        port_list.count = 1;
        port_list.list  = &port_id;
        sai_lag_rif_notify (lag_id,
                            SAI_LAG_OPER_DEL_PORTS, &port_list);

    } while (0);

    sai_lag_unlock ();

    return rc;
}

static sai_status_t sai_l2_set_lag_member_attribute(sai_object_id_t  member_id,
                                                    const sai_attribute_t *attr)
{
    sai_object_id_t lag_id;
    sai_object_id_t port_id;
    sai_status_t    rc;

    STD_ASSERT ((attr != NULL));

    sai_lag_lock();
    rc = sai_lag_get_info_from_member_id (member_id, &lag_id, &port_id);
    sai_lag_unlock ();

    if (rc != SAI_STATUS_SUCCESS) {
        return rc;
    }

    sai_lag_lock();

    switch (attr->id) {

        case SAI_LAG_MEMBER_ATTR_LAG_ID:
        case SAI_LAG_MEMBER_ATTR_PORT_ID:
            rc = SAI_STATUS_INVALID_ATTRIBUTE_0;
            break;

        case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
            rc = sai_l2_lag_port_flag_set (lag_id, port_id,
                                           false, attr->value.booldata);
            break;

        case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
            rc = sai_l2_lag_port_flag_set (lag_id, port_id,
                                           true, attr->value.booldata);
            break;

        default:
            SAI_LAG_LOG_ERR ("Unknown attr :%d for lag member id 0x%"PRIx64"",
                             attr->id, member_id);
            rc = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
            break;
    }

    sai_lag_unlock ();

    return rc;
}

static sai_status_t sai_l2_get_lag_member_attribute(sai_object_id_t  member_id,
                                                    uint32_t         attr_count,
                                                    sai_attribute_t *attr_list)
{
    sai_object_id_t lag_id;
    sai_object_id_t port_id;
    sai_status_t    rc = SAI_STATUS_SUCCESS;
    uint_t          attr_idx;

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    }

    sai_lag_lock ();
    rc = sai_lag_get_info_from_member_id (member_id, &lag_id, &port_id);
    sai_lag_unlock ();

    if (rc != SAI_STATUS_SUCCESS) {
        return rc;
    }

    sai_lag_lock ();

    for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {

        rc = SAI_STATUS_SUCCESS;

        switch (attr_list [attr_idx].id) {

            case SAI_LAG_MEMBER_ATTR_LAG_ID:
                attr_list [attr_idx].value.oid = lag_id;
                break;

            case SAI_LAG_MEMBER_ATTR_PORT_ID:
                attr_list [attr_idx].value.oid = port_id;
                break;

            case SAI_LAG_MEMBER_ATTR_EGRESS_DISABLE:
                rc = sai_lag_member_get_disable_status (lag_id, port_id,
                                                        false,
                                                        &attr_list [attr_idx]
                                                        .value.booldata);
                break;

            case SAI_LAG_MEMBER_ATTR_INGRESS_DISABLE:
                rc = sai_lag_member_get_disable_status (lag_id, port_id,
                                                        true,
                                                        &attr_list [attr_idx]
                                                        .value.booldata);
                break;

            default:
                rc = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
                break;
        }

        if (rc != SAI_STATUS_SUCCESS) {
            break;
        }
    }

    sai_lag_unlock ();

    return rc;
}

static sai_lag_api_t sai_lag_method_table =
{
    sai_l2_create_lag,
    sai_l2_remove_lag,
    sai_l2_set_lag_attribute,
    sai_l2_get_lag_attribute,
    sai_l2_create_lag_member,
    sai_l2_remove_lag_member,
    sai_l2_set_lag_member_attribute,
    sai_l2_get_lag_member_attribute,
};

sai_lag_api_t  *sai_lag_api_query (void)
{
    return (&sai_lag_method_table);
}
