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
 * @file  sai_qos_wred.c
 *
 * @brief This file contains function definitions for SAI Wred
 *        functionality API implementation.
 */

#include "sai_npu_qos.h"
#include "sai_qos_common.h"
#include "sai_qos_util.h"
#include "sai_qos_api_utils.h"
#include "sai_gen_utils.h"
#include "sai_qos_mem.h"
#include "sai_common_infra.h"

#include "sai.h"
#include "saiwred.h"

#include "std_type_defs.h"
#include "std_utils.h"
#include "std_assert.h"
#include <stdlib.h>
#include <inttypes.h>

void sai_qos_wred_free_resources(dn_sai_qos_wred_t *p_wred_node)
{
    SAI_WRED_LOG_TRACE("Freeing the wred node");
    sai_qos_wred_node_free(p_wred_node);
}

static bool sai_qos_wred_is_object_in_use(dn_sai_qos_wred_t *p_wred_node)
{
    STD_ASSERT(p_wred_node != NULL);

    if((sai_qos_queue_node_from_wred_get(p_wred_node)) != NULL){
        return true ;
    }
    return false;
}

static sai_packet_color_t sai_qos_wred_attr_to_color(uint32_t attr_id)
{
    if(attr_id == SAI_WRED_ATTR_GREEN_ENABLE ||
       attr_id == SAI_WRED_ATTR_GREEN_MIN_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_GREEN_MAX_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_GREEN_DROP_PROBABILITY)
    {
        return SAI_PACKET_COLOR_GREEN;
    }

    if(attr_id == SAI_WRED_ATTR_RED_ENABLE ||
       attr_id == SAI_WRED_ATTR_RED_MIN_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_RED_MAX_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_RED_DROP_PROBABILITY)
    {
        return SAI_PACKET_COLOR_RED;
    }
    if(attr_id == SAI_WRED_ATTR_YELLOW_ENABLE ||
       attr_id == SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD ||
       attr_id == SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY)
    {
        return SAI_PACKET_COLOR_YELLOW;
    }
    return SAI_QOS_MAX_PACKET_COLORS;
}
static sai_status_t sai_qos_wred_attr_set(dn_sai_qos_wred_t *p_wred_node,
                                      const sai_attribute_t *p_attr)
{
    sai_packet_color_t color = SAI_WRED_INVALID_COLOR;
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    STD_ASSERT(p_wred_node != NULL);

    STD_ASSERT(p_attr != NULL);

    SAI_WRED_LOG_TRACE ("Setting Wred attribute id: %d.",
                        p_attr->id);

    switch (p_attr->id)
    {
        case SAI_WRED_ATTR_GREEN_ENABLE:
        case SAI_WRED_ATTR_YELLOW_ENABLE:
        case SAI_WRED_ATTR_RED_ENABLE:
            color = sai_qos_wred_attr_to_color(p_attr->id);
            p_wred_node->threshold[color].enable = p_attr->value.booldata;
            break;

        case SAI_WRED_ATTR_GREEN_MIN_THRESHOLD:
        case SAI_WRED_ATTR_YELLOW_MIN_THRESHOLD:
        case SAI_WRED_ATTR_RED_MIN_THRESHOLD:
            color = sai_qos_wred_attr_to_color(p_attr->id);
            p_wred_node->threshold[color].min_limit = p_attr->value.u32;
            break;


        case SAI_WRED_ATTR_GREEN_MAX_THRESHOLD:
        case SAI_WRED_ATTR_YELLOW_MAX_THRESHOLD:
        case SAI_WRED_ATTR_RED_MAX_THRESHOLD:
            color = sai_qos_wred_attr_to_color(p_attr->id);
            p_wred_node->threshold[color].max_limit = p_attr->value.u32;
            break;

        case SAI_WRED_ATTR_GREEN_DROP_PROBABILITY:
        case SAI_WRED_ATTR_YELLOW_DROP_PROBABILITY:
        case SAI_WRED_ATTR_RED_DROP_PROBABILITY:
            color = sai_qos_wred_attr_to_color(p_attr->id);
            p_wred_node->threshold[color].drop_probability = p_attr->value.u32;
            break;

        case SAI_WRED_ATTR_WEIGHT:
            p_wred_node->weight = p_attr->value.u32;
            break;

        case SAI_WRED_ATTR_ECN_MARK_ENABLE:
            p_wred_node->ecn_mark_enable = p_attr->value.booldata;
            break;

        default:
            SAI_WRED_LOG_ERR ("Attribute id: %d is not a known attribute "
                                 "for Wred.", p_attr->id);

            sai_rc = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
            break;
    }

    return sai_rc;
}

static sai_status_t sai_qos_wred_check_mandatory_attr(dn_sai_qos_wred_t *p_wred_node)
{
    STD_ASSERT(p_wred_node != NULL);
    size_t color = 0;
    uint_t buff_size = sai_qos_wred_npu_api_get()->wred_max_buf_size_get();

    for(color = 0; color < SAI_QOS_MAX_PACKET_COLORS; color ++){
        if(p_wred_node->threshold[color].enable &&
           (p_wred_node->threshold[color].min_limit == buff_size ||
            p_wred_node->threshold[color].max_limit == buff_size))
        {
            return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
        }
    }
    return SAI_STATUS_SUCCESS;
}

static void sai_qos_wred_fill_in_defaults(dn_sai_qos_wred_t *p_wred_node)
{
    STD_ASSERT(p_wred_node != NULL);
    size_t color = 0;
    uint_t buff_size = sai_qos_wred_npu_api_get()->wred_max_buf_size_get();

    for(color = 0; color < SAI_QOS_MAX_PACKET_COLORS; color ++)
    {
        p_wred_node->threshold[color].max_limit = buff_size;
        p_wred_node->threshold[color].min_limit = buff_size;
        p_wred_node->threshold[color].drop_probability = SAI_MAX_WRED_DROP_PROBABILITY;
    }
}

static sai_status_t sai_wred_parse_update_attributes(dn_sai_qos_wred_t *p_wred_node,
                                                     uint32_t attr_count,
                                                     const sai_attribute_t *attr_list,
                                                     dn_sai_operations_t op_type)
{
    sai_status_t               sai_rc = SAI_STATUS_SUCCESS;
    uint_t                     list_idx = 0;
    uint_t                     max_vendor_attr_count = 0;
    const dn_sai_attribute_entry_t   *p_vendor_attr = NULL;

    STD_ASSERT(p_wred_node != NULL);

    sai_qos_wred_npu_api_get()->attribute_table_get
        (&p_vendor_attr, &max_vendor_attr_count);

    STD_ASSERT(p_vendor_attr != NULL);
    STD_ASSERT(max_vendor_attr_count != 0);

    sai_rc = sai_attribute_validate(attr_count,attr_list, p_vendor_attr,
                                    op_type, max_vendor_attr_count);

    if(sai_rc != SAI_STATUS_SUCCESS){
        SAI_WRED_LOG_ERR("Attribute validation failed for %d operation",
                            op_type);
        return sai_rc;
    }

    for (list_idx = 0; list_idx < attr_count; list_idx++)
    {
        if(op_type != SAI_OP_GET){
            sai_rc = sai_qos_wred_attr_set(p_wred_node, &attr_list[list_idx]);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                sai_rc = sai_get_indexed_ret_val(sai_rc, list_idx);
                return sai_rc;
            }
        }
    }

    if(op_type != SAI_OP_GET){
        sai_rc = sai_qos_wred_check_mandatory_attr(p_wred_node);
    }

    return sai_rc;
}

static sai_status_t sai_qos_wred_create(sai_object_id_t *wred_id,
                                    uint32_t attr_count,
                                    const sai_attribute_t *attr_list)
{
    sai_status_t           sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_wred_t      *p_wred_node = NULL;
    sai_npu_object_id_t    hw_wred_id = 0;

    STD_ASSERT(wred_id != NULL);

    sai_qos_lock();

    do
    {
        if((p_wred_node = sai_qos_wred_node_alloc()) == NULL){
            SAI_WRED_LOG_ERR("Failed to allocate memory for wred node.");
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }

        sai_qos_wred_fill_in_defaults(p_wred_node);

        sai_rc = sai_wred_parse_update_attributes(p_wred_node, attr_count,
                                                  attr_list, SAI_OP_CREATE);
        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Failed to parse attributes for wred");
            break;
        }

        sai_rc = sai_qos_wred_npu_api_get()->wred_create(p_wred_node,
                                                           &hw_wred_id);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Npu wred create failed for wred");
            break;
        }

        SAI_WRED_LOG_TRACE("Wred created in NPU 0x%"PRIx64"",hw_wred_id);

        *wred_id = sai_uoid_create(SAI_OBJECT_TYPE_WRED,
                                  hw_wred_id);

        SAI_WRED_LOG_TRACE("Wred object id is 0x%"PRIx64"",*wred_id);

        p_wred_node->key.wred_id = *wred_id;

        std_dll_init(&p_wred_node->queue_dll_head);

        std_dll_init(&p_wred_node->port_dll_head);

        if (sai_qos_wred_node_insert(p_wred_node) != STD_ERR_OK){
            SAI_WRED_LOG_ERR("Wred id insertion failed in RB tree");
            sai_qos_wred_npu_api_get()->wred_remove(p_wred_node);
            sai_rc = SAI_STATUS_FAILURE;
            break;
        }

    }while(0);

    if(sai_rc == SAI_STATUS_SUCCESS){
        SAI_WRED_LOG_TRACE("Wred created successfully");
    }
    else{
        SAI_WRED_LOG_ERR("Wred create failed");
        sai_qos_wred_free_resources(p_wred_node);
    }

    sai_qos_unlock();
    return sai_rc;
}

static sai_status_t sai_qos_wred_remove(sai_object_id_t wred_id)
{
    dn_sai_qos_wred_t  *p_wred_node = NULL;
    sai_status_t      sai_rc = SAI_STATUS_SUCCESS;

    if(!sai_is_obj_id_wred(wred_id)) {
        SAI_WRED_LOG_ERR("Passed object is not wred object");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    SAI_WRED_LOG_TRACE("Removing wred id 0x%"PRIx64"",wred_id);
    sai_qos_lock();
    do
    {
        p_wred_node = sai_qos_wred_node_get(wred_id);

        if(!p_wred_node){
            SAI_WRED_LOG_ERR("Wred_id 0x%"PRIx64" not present in tree",
                             wred_id);
            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        if(sai_qos_wred_is_object_in_use(p_wred_node)){
            SAI_WRED_LOG_WARN("Wred_id 0x%"PRIx64" is in use",
                              wred_id);
            sai_rc = SAI_STATUS_OBJECT_IN_USE;
            break;
        }
        sai_rc = sai_qos_wred_npu_api_get()->wred_remove(p_wred_node);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Npu wred remove failed for wredid 0x%"PRIx64"",
                             wred_id);
            break;
        }

        sai_qos_wred_node_remove(wred_id);
        sai_qos_wred_free_resources(p_wred_node);
    }while(0);

    sai_qos_unlock();
    return sai_rc;
}

static void sai_wred_queue_list_error_recovery(dn_sai_qos_wred_t *p_wred_old, uint_t count)
{
    STD_ASSERT(p_wred_old != NULL);
    dn_sai_qos_queue_t *p_queue_node = NULL;
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    SAI_WRED_LOG_TRACE("Recovering queues with wredid 0x%"PRIx64" to old values",
                       p_wred_old->key.wred_id);

    p_queue_node = sai_qos_queue_node_from_wred_get(p_wred_old);

    while((p_queue_node != NULL) && (count > 0)){
        SAI_WRED_LOG_TRACE("Updating queue 0x%"PRIx64"",p_queue_node->key.queue_id);
        sai_rc = sai_qos_wred_npu_api_get()->wred_on_queue_set
            (p_queue_node->key.queue_id, p_wred_old, true);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Npu update failed ");
            break;
        }
        p_queue_node = sai_qos_next_queue_node_from_wred_get(p_wred_old, p_queue_node);
        count --;
    }
}

static sai_status_t sai_wred_update_queue_list(dn_sai_qos_wred_t *p_wred_new)
{
    STD_ASSERT(p_wred_new != NULL);
    dn_sai_qos_queue_t *p_queue_node = NULL;
    dn_sai_qos_wred_t *p_wred_node_old = NULL;
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    uint_t count = 0;

    p_wred_node_old = sai_qos_wred_node_get(p_wred_new->key.wred_id);

    if(p_wred_node_old == NULL){
        SAI_WRED_LOG_ERR("Passed wred node is not found in tree");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    SAI_WRED_LOG_TRACE("Updating queues with wredid 0x%"PRIx64"",p_wred_new->key.wred_id);

    p_queue_node = sai_qos_queue_node_from_wred_get(p_wred_node_old);

    while(p_queue_node != NULL){
        SAI_WRED_LOG_TRACE("Updating queue 0x%"PRIx64"",p_queue_node->key.queue_id);
        sai_rc = sai_qos_wred_npu_api_get()->wred_on_queue_set
            (p_queue_node->key.queue_id, p_wred_new, true);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Npu update failed ");
            sai_wred_queue_list_error_recovery(p_wred_node_old, count);
            break;
        }
        p_queue_node = sai_qos_next_queue_node_from_wred_get(p_wred_node_old, p_queue_node);
        count ++;
    }

    return sai_rc;
}
static sai_status_t sai_qos_wred_attribute_set(sai_object_id_t wred_id,
                                              const sai_attribute_t *p_attr)
{
    dn_sai_qos_wred_t   wred_new_node;
    dn_sai_qos_wred_t   *p_wred_exist_node = NULL;
    sai_status_t        sai_rc = SAI_STATUS_SUCCESS;
    uint_t              attr_count = 1;

    STD_ASSERT (p_attr != NULL);

    SAI_WRED_LOG_TRACE("Setting attribute Id: %d on Wred Id 0x%"PRIx64"",
                          p_attr->id, wred_id);

    if(!sai_is_obj_id_wred(wred_id)) {
        SAI_WRED_LOG_ERR("Passed object is not wred object");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    sai_qos_lock();

    do
    {
        p_wred_exist_node = sai_qos_wred_node_get(wred_id);

        if(p_wred_exist_node == NULL){
            SAI_WRED_LOG_ERR("Wred node not found");
            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        memset(&wred_new_node, 0, sizeof(dn_sai_qos_wred_t));
        wred_new_node.key.wred_id = wred_id;

        memcpy(&wred_new_node, p_wred_exist_node, sizeof(dn_sai_qos_wred_t));

        sai_rc = sai_wred_parse_update_attributes(&wred_new_node,
                                                     attr_count,
                                                     p_attr, SAI_OP_SET);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Validation failed for attribute Id %d",
                                p_attr->id);
            break;
        }

        sai_rc = sai_qos_wred_npu_api_get()->wred_attr_set(&wred_new_node, attr_count, p_attr);

        if(sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("NPU set failed for attribute Id %d"
                                "on wredid 0x%"PRIx64"",p_attr->id, wred_id);
            break;
        }
        SAI_WRED_LOG_TRACE("NPU attribute set success for id %d"
                              "for wred 0x%"PRIx64"", p_attr->id, wred_id);

        if(!sai_qos_wred_npu_api_get()->wred_is_hw_object()){
            sai_rc = sai_wred_update_queue_list(&wred_new_node);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_WRED_LOG_ERR("Error: Unable to update queuelist");
                break;
            }
        }
        sai_rc = sai_qos_wred_attr_set(p_wred_exist_node, p_attr);

    }while(0);

    sai_qos_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS){
        SAI_WRED_LOG_INFO("Set attribute success for wred 0x%"PRIx64" for attr %d",
                              p_wred_exist_node->key.wred_id, p_attr->id);
    }

    return sai_rc;
}

static sai_status_t sai_qos_wred_attribute_get(sai_object_id_t wred_id,
                                       uint32_t attr_count,
                                       sai_attribute_t *attr_list)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    dn_sai_qos_wred_t  *p_wred_node = NULL;

    STD_ASSERT(attr_list != NULL);

    SAI_WRED_LOG_TRACE("Get wred attributes for id 0x%"PRIx64"",wred_id);

    if ((attr_count == 0) || (attr_count > SAI_QOS_WRED_MAX_ATTR_COUNT)){
        SAI_WRED_LOG_ERR("Invalid number of attributes for qos wred");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    if(!sai_is_obj_id_wred(wred_id)) {
        SAI_WRED_LOG_ERR("Passed object is not wred object");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    sai_qos_lock();

    do
    {
        p_wred_node = sai_qos_wred_node_get(wred_id);

        if(p_wred_node == NULL){
            SAI_WRED_LOG_ERR("Passed wred node is not found in tree");
            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        sai_rc = sai_wred_parse_update_attributes(p_wred_node, attr_count,
                                                  attr_list, SAI_OP_GET);

        if (sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Validation failed for attribute");
            break;
        }

        sai_rc = sai_qos_wred_npu_api_get()->wred_attr_get(p_wred_node,
                                                      attr_count, attr_list);

        if (sai_rc != SAI_STATUS_SUCCESS){
            SAI_WRED_LOG_ERR("Npu get failed for wredid 0x%"PRIx64"",
                                wred_id);
            break;
        }
    } while (0);

    sai_qos_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS){
        SAI_WRED_LOG_TRACE("Get attribute success for wred id 0x%"PRIx64"",
                           wred_id);
    }
    else
    {
        SAI_WRED_LOG_ERR("Get attribute failed for wred id 0x%"PRIx64"",
                           wred_id);
    }
    return sai_rc;
}

static void sai_wred_queue_node_update(dn_sai_qos_queue_t *p_queue_node,
                                               dn_sai_qos_wred_t *p_wred_node,
                                               bool is_add)
{
    dn_sai_qos_wred_t *p_wred_node_old = NULL;
    STD_ASSERT(p_queue_node != NULL);
    STD_ASSERT(p_wred_node != NULL);

    if(is_add){
        SAI_WRED_LOG_TRACE("Add wred 0x%"PRIx64" on queue 0x%"PRIx64"",
                              p_wred_node->key.wred_id,p_queue_node->key.queue_id);
        if(p_queue_node->wred_id != SAI_NULL_OBJECT_ID){
            SAI_WRED_LOG_TRACE("Removing existing wredid of type");
            p_wred_node_old = sai_qos_wred_node_get(p_queue_node->wred_id);

            if(p_wred_node_old != NULL){
                std_dll_remove(&p_wred_node_old->queue_dll_head,
                               &p_queue_node->wred_dll_glue);
            }

        }
        std_dll_insertatback(&p_wred_node->queue_dll_head,
                             &p_queue_node->wred_dll_glue);
        p_queue_node->wred_id = p_wred_node->key.wred_id;
    }else {
        SAI_WRED_LOG_TRACE("Remove wred 0x%"PRIx64" on queue 0x%"PRIx64"",
                              p_wred_node->key.wred_id,p_queue_node->key.queue_id);
        std_dll_remove(&p_wred_node->queue_dll_head,
                       &p_queue_node->wred_dll_glue);

        p_queue_node->wred_id = SAI_NULL_OBJECT_ID;
    }
}
sai_status_t sai_qos_wred_set_on_queue(sai_object_id_t queue_id,
                                       const sai_attribute_t *attr)
{
    STD_ASSERT(attr != NULL);

    bool is_add = false;
    dn_sai_qos_wred_t *p_wred_node = NULL;
    dn_sai_qos_queue_t *p_queue_node = NULL;
    sai_status_t sai_rc =  SAI_STATUS_SUCCESS;

    SAI_WRED_LOG_TRACE("Set wred id 0x%"PRIx64" on queue 0x%"PRIx64"",attr->value.oid,queue_id);

    p_queue_node = sai_qos_queue_node_get(queue_id);

    if(p_queue_node == NULL){
        SAI_WRED_LOG_ERR("Queue node not in db");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    SAI_WRED_LOG_TRACE("queue_type %d",p_queue_node->queue_type);

    if(p_queue_node->queue_type != SAI_QUEUE_TYPE_UNICAST){
        SAI_WRED_LOG_ERR("Queue type is not unicast");
        return SAI_STATUS_INVALID_ATTRIBUTE_0;
    }
    if((attr->value.oid != SAI_NULL_OBJECT_ID) && (!sai_is_obj_id_wred(attr->value.oid))) {
        SAI_WRED_LOG_ERR("Object passed is not wred");
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    if(attr->value.oid != SAI_NULL_OBJECT_ID){
        is_add = true;

        p_wred_node = sai_qos_wred_node_get(attr->value.oid);

        if(p_wred_node == NULL) {
            SAI_WRED_LOG_ERR("Wred node is NULL");
            return SAI_STATUS_INVALID_OBJECT_ID;
        }

    }else {
        p_wred_node = sai_qos_wred_node_get(p_queue_node->wred_id);
        if(p_wred_node == NULL) {
            SAI_WRED_LOG_ERR("Wred node is NULL");
            return SAI_STATUS_INVALID_OBJECT_ID;
        }
        SAI_WRED_LOG_TRACE(" Setting null object id for wredid 0x%"PRIx64"",
                              p_queue_node->wred_id);
        is_add = false;
    }

    sai_rc = sai_qos_wred_npu_api_get()->wred_on_queue_set(queue_id,
                                                           p_wred_node,
                                                           is_add);

    if(sai_rc == SAI_STATUS_SUCCESS){
        sai_wred_queue_node_update(p_queue_node,
                                   p_wred_node,
                                   is_add);
    }

    return sai_rc;
}

sai_status_t sai_port_attr_wred_profile_set(sai_object_id_t port_id,
                                            const sai_attribute_t *p_attr)
{
    STD_ASSERT(p_attr != NULL);

    if(!sai_qos_wred_npu_api_get()->wred_on_port_supported()) {
        return SAI_STATUS_NOT_SUPPORTED;
    }

    return SAI_STATUS_SUCCESS;
}
/* API method table for Qos wred to be returned during query.
 **/
static sai_wred_api_t sai_qos_wred_method_table = {
    sai_qos_wred_create,
    sai_qos_wred_remove,
    sai_qos_wred_attribute_set,
    sai_qos_wred_attribute_get,
};

sai_wred_api_t *sai_wred_api_query (void)
{
    return &sai_qos_wred_method_table;
}


