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
 * @file  sai_qos_sched_group.c
 *
 * @brief This file contains function definitions for SAI QOS default
 *        scheduler groups initilization and hiearchy creation and SAI
 *        scheduler group functionality API implementation.
 */

#include "sai_qos_common.h"
#include "sai_qos_util.h"
#include "sai_qos_api_utils.h"
#include "sai_qos_mem.h"
#include "sai_switch_utils.h"
#include "sai_common_utils.h"
#include "sai_gen_utils.h"
#include "sai_common_infra.h"

#include "saistatus.h"
#include "saitypes.h"

#include "std_assert.h"
#include "std_bit_masks.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

static sai_status_t sai_qos_sched_group_node_init (dn_sai_qos_sched_group_t *p_sg_node)
{
    std_dll_init (&p_sg_node->hqos_info.child_sched_group_dll_head);

    std_dll_init (&p_sg_node->hqos_info.child_queue_dll_head);

    p_sg_node->hqos_info.parent_sched_group_id = SAI_NULL_OBJECT_ID;

    p_sg_node->child_offset = SAI_QOS_CHILD_INDEX_INVALID;

    p_sg_node->hqos_info.max_childs = sai_switch_max_hierarchy_node_childs_get();

    p_sg_node->hqos_info.child_index_bitmap =
        std_bitmap_create_array (p_sg_node->hqos_info.max_childs);

    if (p_sg_node->hqos_info.child_index_bitmap == NULL) {
        SAI_SCHED_GRP_LOG_ERR ("Failed to create Hierarchy child index bitmap"
                               "array of size %d.", p_sg_node->hqos_info.max_childs);

        return SAI_STATUS_NO_MEMORY;
    }

    return SAI_STATUS_SUCCESS;
}

static bool sai_qos_sched_group_is_in_use (dn_sai_qos_sched_group_t *p_sg_node)
{

    STD_ASSERT (p_sg_node != NULL);

    /* Verify Any childs are associted to scheduler group. */

    if (sai_qos_sched_group_get_first_child_sched_group (p_sg_node)
        != NULL) {
        SAI_SCHED_GRP_LOG_ERR ("Scheduler group in use, Scheduler group "
                               "has child scheduler group.");
        return true;
    }

    if (sai_qos_sched_group_get_first_child_queue (p_sg_node)
        != NULL) {
        SAI_SCHED_GRP_LOG_ERR ("Scheduler group in use, Scheduler group "
                               "has child queue");
        return true;
    }

    /* Verify Scheduler group is child of any parent. */
    if (p_sg_node->hqos_info.parent_sched_group_id != SAI_NULL_OBJECT_ID)
        return true;

    return false;
}

sai_status_t sai_qos_sched_group_remove_configs (dn_sai_qos_sched_group_t *p_sg_node)
{
    sai_status_t sai_rc;
    sai_status_t rev_sai_rc;
    sai_attribute_t set_attr;
    sai_object_id_t sched_id = SAI_NULL_OBJECT_ID;

    STD_ASSERT (p_sg_node != NULL);

    sched_id = p_sg_node->scheduler_id;

    if((sched_id != sai_qos_default_sched_id_get()) && (sched_id != SAI_NULL_OBJECT_ID)) {
        set_attr.id = SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID;
        set_attr.value.oid = sai_qos_default_sched_id_get();

        sai_rc = sai_qos_sched_group_scheduler_set (p_sg_node, &set_attr);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Error: Unable to remove scheduler profile from sg:0x%"PRIx64"",
                    p_sg_node->key.sched_group_id);
            set_attr.id = sched_id;
            set_attr.value.oid = sai_qos_default_sched_id_get();
            rev_sai_rc = sai_qos_sched_group_scheduler_set (p_sg_node, &set_attr);
            if(rev_sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Error unable to revert scheduler profile on sg:0x%"PRIx64"",
                        p_sg_node->key.sched_group_id);
            }
            return sai_rc;
        }
        p_sg_node->scheduler_id = sai_qos_default_sched_id_get();
    }
    return SAI_STATUS_SUCCESS;

}
static void sai_qos_sched_group_free_resources (dn_sai_qos_sched_group_t *p_sg_node,
                                          bool is_sg_set_in_npu,
                                          bool is_sg_set_in_port_list)
{
    if (p_sg_node == NULL) {
        return;
    }

    /* Remove Scheduler group from NPU, if it was already applied created.*/
    if (is_sg_set_in_npu) {
        sai_sched_group_npu_api_get()->sched_group_remove (p_sg_node);
    }

    /* Delete Scheduler group node from the PORT's Scheduler group list,
     * if it was already added. */
    if (is_sg_set_in_port_list) {
        sai_qos_port_sched_group_list_update (p_sg_node, false);
    }

    std_bitmaparray_free_data(p_sg_node->hqos_info.child_index_bitmap);

    sai_qos_sched_group_node_free (p_sg_node);

}

static sai_status_t sai_qos_sched_group_node_insert_to_tree (
                                      dn_sai_qos_sched_group_t *p_sg_node)
{
    rbtree_handle   sg_tree = NULL;
    t_std_error     err_rc = STD_ERR_OK;

    STD_ASSERT (p_sg_node != NULL);

    sg_tree = sai_qos_access_global_config()->scheduler_group_tree;

    STD_ASSERT (sg_tree != NULL);

    err_rc = std_rbtree_insert (sg_tree, p_sg_node);

    if (STD_IS_ERR(err_rc)) {
        SAI_SCHED_GRP_LOG_ERR ("Failed to insert Scheduler group node for "
                               " SGID 0x%"PRIx64" into Scheduler group Tree",
                               p_sg_node->key.sched_group_id);

        return SAI_STATUS_FAILURE;
    }

    return SAI_STATUS_SUCCESS;
}

static void sai_qos_sched_group_node_remove_from_tree (
                                         dn_sai_qos_sched_group_t *p_sg_node)
{
    rbtree_handle   sg_tree = NULL;

    STD_ASSERT (p_sg_node != NULL);

    sg_tree = sai_qos_access_global_config()->scheduler_group_tree;

    STD_ASSERT (sg_tree != NULL);

    std_rbtree_remove (sg_tree, p_sg_node);

    return;
}

static void sai_qos_sched_group_attr_set (dn_sai_qos_sched_group_t *p_sg_node,
                                          uint_t attr_count,
                                          const sai_attribute_t *p_attr_list,
                                          dn_sai_operations_t op_type)
{
    const sai_attribute_t  *p_attr = NULL;
    uint_t                 list_index = 0;

    if ((op_type != SAI_OP_CREATE) && (op_type != SAI_OP_SET))
        return;

    STD_ASSERT(p_sg_node != NULL);
    STD_ASSERT(p_attr_list != NULL);

    SAI_SCHED_GRP_LOG_TRACE ("Set attributes for Scheduler group, "
                             "attribute count %d op_type %d.", attr_count,
                             op_type);

    for (list_index = 0, p_attr = p_attr_list;
         list_index < attr_count; ++list_index, ++p_attr) {

        switch (p_attr->id)
        {
            case SAI_SCHEDULER_GROUP_ATTR_PORT_ID:
                p_sg_node->port_id = p_attr->value.oid;
                break;

            case SAI_SCHEDULER_GROUP_ATTR_LEVEL:
                 p_sg_node->hierarchy_level = p_attr->value.u32;
                break;

            case SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID:
                if(p_attr->value.oid == SAI_NULL_OBJECT_ID) {
                    p_sg_node->scheduler_id = sai_qos_default_sched_id_get();
                } else {
                    p_sg_node->scheduler_id = p_attr->value.oid;
                }
                break;

            case SAI_SCHEDULER_GROUP_ATTR_MAX_CHILDS:
                p_sg_node->max_childs = p_attr->value.u32;
                break;

            default:
                SAI_SCHED_GRP_LOG_TRACE ("Attribute id: %d - read-only attribute.",
                                         p_attr->id);
                break;
        }
    }
    return;
}

static sai_status_t sai_qos_sched_group_attributes_validate (
                                     uint_t attr_count,
                                     const sai_attribute_t *attr_list,
                                     dn_sai_operations_t op_type)
{
    sai_status_t                    sai_rc = SAI_STATUS_SUCCESS;
    uint_t                          max_vendor_attr_count = 0;
    const dn_sai_attribute_entry_t *p_vendor_attr = NULL;

    SAI_SCHED_GRP_LOG_TRACE ("Parsing attributes for Scheduler group, "
                             "attribute count %d op_type %d.", attr_count,
                             op_type);

    if (attr_count == 0)
        return SAI_STATUS_INVALID_PARAMETER;

    sai_sched_group_npu_api_get()->attribute_table_get(&p_vendor_attr,
                                                       &max_vendor_attr_count);

    STD_ASSERT(p_vendor_attr != NULL);
    STD_ASSERT(max_vendor_attr_count > 0);

    sai_rc = sai_attribute_validate (attr_count, attr_list, p_vendor_attr,
                                     op_type, max_vendor_attr_count);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Attribute validation failed for %d "
                               "operation", op_type);
    }

    return sai_rc;
}

/* Attribute validation should happen before this function */
static sai_status_t sai_qos_sched_group_port_attribute_get (
                              uint_t attr_count, const sai_attribute_t *p_attr_list,
                              sai_object_id_t *port_id)
{
    const sai_attribute_t *p_attr = NULL;
    uint_t                 list_idx = 0;

    STD_ASSERT(p_attr_list != NULL);

    for (list_idx = 0, p_attr = p_attr_list;
         list_idx < attr_count; ++list_idx, ++p_attr) {

        if (p_attr->id == SAI_SCHEDULER_GROUP_ATTR_PORT_ID) {
            *port_id = p_attr->value.oid;
            return SAI_STATUS_SUCCESS;
        }
    }

    return SAI_STATUS_FAILURE;
}

/* Attribute validation should happen before this function */
static sai_status_t sai_qos_port_hierarchy_update_on_first_application_sched_group (
                                                            sai_object_id_t port_id)
{
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_port_t *p_qos_port_node = NULL;

    /* Qos Init Not done, No Need to do any cleanup */
    if (! sai_qos_is_init_complete ())
        return sai_rc;

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    /* Delete existing hierarchy if first hqos sched group create from app */
    if (! p_qos_port_node->is_app_hqos_init) {

        sai_rc = sai_qos_port_hierarchy_deinit (port_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("SAI QOS port 0x%"PRIx64" hierarchy de-init "
                             "failed.", port_id);
            return sai_rc;
        }
        p_qos_port_node->is_app_hqos_init = true;
    }

    return sai_rc;
}

static sai_status_t sai_qos_port_application_sched_group_handle (uint_t attr_count,
                                                 const sai_attribute_t *attr_list)

{
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    sai_object_id_t    port_id = SAI_NULL_OBJECT_ID;
    bool               is_app_hqos_init_started = false;

    sai_rc = sai_qos_sched_group_port_attribute_get (attr_count, attr_list,
                                                     &port_id);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port is not part of the Input parameters for "
                               "Scheduler group creation.");

        return sai_rc;
    }

    sai_rc = sai_qos_port_hierarchy_init_from_application_status_get (port_id,
                                                            &is_app_hqos_init_started);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port hierarchy init from application status get failed.");

        return sai_rc;
    }

    if (! is_app_hqos_init_started) {
        sai_rc = sai_qos_port_hierarchy_update_on_first_application_sched_group (port_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Default hierarchy remove failed on creation of "
                                   "first app scheduler group creation.");

            return sai_rc;
        }
    }

    return sai_rc;
}

/* Attribute validation happen before this function */
static bool sai_qos_sched_group_is_duplicate_set (dn_sai_qos_sched_group_t *p_sg_node,
                                                  const sai_attribute_t *p_attr)
{
    bool is_duplicate = false;

    STD_ASSERT(p_sg_node != NULL);
    STD_ASSERT(p_attr != NULL);

    SAI_SCHED_GRP_LOG_TRACE ("Verify duplicate set attributes value, ID: %d.",
                             p_attr->id);

    switch (p_attr->id)
    {
        case SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID:
            if (p_sg_node->scheduler_id == p_attr->value.oid)
                is_duplicate = true;
            break;

        default:
            SAI_SCHED_GRP_LOG_TRACE ("Attribute id: %d - read-only attribute.",
                                     p_attr->id);
            break;
    }

    return is_duplicate;
}


static sai_status_t sai_qos_sched_group_create (
                          sai_object_id_t *sg_obj_id, uint32_t attr_count,
                          const sai_attribute_t *attr_list)
{
    sai_status_t               sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_sched_group_t  *p_sg_node = NULL;
    sai_object_id_t            sg_oid = SAI_NULL_OBJECT_ID;
    bool                       is_sg_set_in_npu = false;
    bool                       is_sg_set_in_port_list = false;

    SAI_SCHED_GRP_LOG_TRACE ("Scheduler group Creation, attr_count: %d.",
                             attr_count);

    STD_ASSERT (sg_obj_id != NULL);
    STD_ASSERT (attr_list != NULL);

    sai_rc = sai_qos_sched_group_attributes_validate (attr_count, attr_list,
                                                      SAI_OP_CREATE);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Input parameters validation failed for "
                               "Scheduler group creation.");
        return sai_rc;
    }

    sai_rc = sai_qos_port_application_sched_group_handle (attr_count, attr_list);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Failed to handle the port first Scheduler "
                               "group creation.");
        return sai_rc;
    }

    sai_qos_lock ();

    do {
        p_sg_node = sai_qos_sched_grp_node_alloc ();

        if (NULL == p_sg_node) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group node memory allocation failed.");
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }

        sai_rc = sai_qos_sched_group_node_init (p_sg_node);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group node init failed.");
            break;
        }

        sai_qos_sched_group_attr_set (p_sg_node, attr_count, attr_list,
                                      SAI_OP_CREATE);

        sai_rc = sai_sched_group_npu_api_get()->sched_group_create (p_sg_node,
                                                                    &sg_oid);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group creation failed in NPU.");
            break;
        }

        SAI_SCHED_GRP_LOG_TRACE ("Scheduler group Created in NPU.");
        is_sg_set_in_npu = true;

        *sg_obj_id = sg_oid;
        p_sg_node->key.sched_group_id = sg_oid;

        /* Add the Queue node to PORT's Queue list */
        sai_rc = sai_qos_port_sched_group_list_update (p_sg_node, true);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to add Scheduler group node to the "
                                   "scheduler group List in PORT node");
            break;
        }

        is_sg_set_in_port_list = true;

        sai_rc = sai_qos_sched_group_node_insert_to_tree (p_sg_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group insertion to tree failed.");
            break;
        }
    } while (0);

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_INFO ("Scheduler group Obj Id 0x%"PRIx64" created.",
                                 sg_oid);
    } else {
        SAI_SCHED_GRP_LOG_ERR ("Failed to create Scheduler group.");
        sai_qos_sched_group_free_resources (p_sg_node, is_sg_set_in_npu,
                                            is_sg_set_in_port_list);
    }

    sai_qos_unlock ();
    return sai_rc;
}

static sai_status_t sai_qos_sched_group_remove (sai_object_id_t sg_id)
{
    sai_status_t               sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_sched_group_t  *p_sg_node = NULL;

    SAI_SCHED_GRP_LOG_TRACE ("Scheduler group remove ID 0x%"PRIx64".", sg_id);

    if (! sai_is_obj_id_scheduler_group (sg_id)) {
        SAI_SCHED_GRP_LOG_ERR ("0x%"PRIx64" is not a valid scheduler group obj id.",
                               sg_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_qos_lock ();

    do {
        p_sg_node = sai_qos_sched_group_node_get (sg_id);

        if (NULL == p_sg_node) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" "
                                   "does not exist in tree.",
                                   sg_id);

            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;

            break;
        }

        SAI_SCHED_GRP_LOG_TRACE ("Scheduler group remove. Port ID 0x%"PRIx64", "
                                 "Level %d SGID 0x%"PRIx64"", p_sg_node->port_id,
                                 p_sg_node->hierarchy_level, sg_id);

        sai_rc = sai_qos_sched_group_remove_configs (p_sg_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" configs can't be deleted, ", sg_id);
            break;
        }

        if (sai_qos_sched_group_is_in_use (p_sg_node)) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" can't be deleted, "
                                   "It is in use.", sg_id);

            sai_rc = SAI_STATUS_OBJECT_IN_USE;

            break;
        }

        sai_rc = sai_sched_group_npu_api_get()->sched_group_remove (p_sg_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" deletion "
                                   "failed in NPU.", sg_id);

            break;
        }

        /* Delete from the PORT's scheduler group list */
        sai_qos_port_sched_group_list_update (p_sg_node, false);

        sai_qos_sched_group_node_remove_from_tree (p_sg_node);

        sai_qos_sched_group_node_free (p_sg_node);

    } while (0);

    sai_qos_unlock ();

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_INFO ("Scheduler group 0x%"PRIx64" removed.", sg_id);
    } else {
        SAI_SCHED_GRP_LOG_ERR ("Failed to remove Scheduler group 0x%"PRIx64".",
                               sg_id);
    }

    return sai_rc;
}

static sai_status_t sai_qos_sched_group_attribute_set (sai_object_id_t sg_id,
                                                       const sai_attribute_t *p_attr)
{
    sai_status_t                sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_sched_group_t    *p_sg_node = NULL;
    uint_t                       attr_count = 1;

    STD_ASSERT (p_attr != NULL);

    SAI_SCHED_GRP_LOG_TRACE ("Setting Attribute ID %d on Scheduler group 0x%"PRIx64".",
                             p_attr->id, sg_id);

    if (! sai_is_obj_id_scheduler_group (sg_id)) {
        SAI_SCHED_GRP_LOG_ERR ("%"PRIx64" is not a valid Scheduler group obj id.",
                               sg_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_qos_lock ();

    do {
        p_sg_node = sai_qos_sched_group_node_get (sg_id);

        if (NULL == p_sg_node) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" "
                                   "does not exist in tree.", sg_id);

            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;

            break;
        }

        sai_rc = sai_qos_sched_group_attributes_validate (attr_count, p_attr,
                                                          SAI_OP_SET);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Input parameters validation failed for "
                                   "Scheduler group attributes set.");

            break;
        }

        if (sai_qos_sched_group_is_duplicate_set (p_sg_node, p_attr)) {
            SAI_SCHED_GRP_LOG_TRACE ("Duplicate set value for Attribute ID %d.",
                                     p_attr->id);
            break;
        }

        switch (p_attr->id)
        {
            case SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID:
                sai_rc = sai_qos_sched_group_scheduler_set (p_sg_node, p_attr);
                break;

            default:
                sai_rc = sai_sched_group_npu_api_get()->sched_group_attribute_set(p_sg_node,
                                                                                  attr_count,
                                                                                  p_attr);
                if (sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_SCHED_GRP_LOG_ERR ("Failed to set Scheduler group Attribute ID: %d "
                                           "in NPU, Error: %d.", p_attr->id, sai_rc);
                }
                break;
        }

        sai_qos_sched_group_attr_set (p_sg_node, attr_count, p_attr, SAI_OP_SET);

    } while (0);

    sai_qos_unlock ();

    return sai_rc;
}

static sai_status_t sai_qos_sched_group_attribute_get (sai_object_id_t sg_id,
                                                       uint32_t attr_count,
                                                       sai_attribute_t *p_attr_list)
{
    sai_status_t                sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_sched_group_t    *p_sg_node = NULL;

    STD_ASSERT (p_attr_list != NULL);

    SAI_SCHED_GRP_LOG_TRACE ("Getting Attributes for scheduler group 0x%"PRIx64", "
                             "attr_count %d.", sg_id, attr_count);

    if (! sai_is_obj_id_scheduler_group (sg_id)) {
        SAI_SCHED_GRP_LOG_ERR ("%"PRIx64" is not a valid Scheduler group obj id.",
                               sg_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_qos_lock ();

    do {
        p_sg_node = sai_qos_sched_group_node_get (sg_id);

        if (NULL == p_sg_node) {
            SAI_SCHED_GRP_LOG_ERR ("Scheduler group 0x%"PRIx64" "
                                   "does not exist in tree.", sg_id);

            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;

            break;
        }


        sai_rc = sai_qos_sched_group_attributes_validate (attr_count, p_attr_list,
                                                          SAI_OP_GET);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Input parameters validation failed for "
                                   "Scheduler group attributes get.");

            break;
        }

        sai_rc = sai_sched_group_npu_api_get()->sched_group_attribute_get (p_sg_node,
                                                                           attr_count,
                                                                           p_attr_list);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to get Scheduler group Attributes from "
                                   "NPU, Error: %d.", sai_rc);

            break;
        }
    } while (0);

    sai_qos_unlock ();

    return sai_rc;
}


uint_t sai_qos_dlft_sched_groups_per_level_get (sai_object_id_t port_id,
                                                uint_t level)
{
    if (level == sai_switch_leaf_hierarchy_level_get ()) {
        if (sai_qos_queue_is_seperate_ucast_and_mcast_supported (port_id))
            return sai_switch_max_uc_queues_per_port_get(port_id);
    }
    return SAI_QOS_DLFT_SCHED_GROUPS_PER_LEVEL;
}
sai_status_t sai_qos_indexed_sched_group_id_get (sai_object_id_t port_id,
                                                 uint_t level, uint_t child_idx,
                                                 sai_object_id_t *sg_grp_id)
{
    dn_sai_qos_sched_group_t *p_sg_node = NULL;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    uint_t                    sg_count = 0;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" level %d scheduler group id list "
                             "get, Index of schduler group %d",
                             port_id, level, child_idx);


    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                               port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    p_sg_node = sai_qos_port_get_first_sched_group (p_qos_port_node, level);

    while(p_sg_node != NULL)
    {
        if(sg_count == child_idx){
            *sg_grp_id = p_sg_node->key.sched_group_id;
            return SAI_STATUS_SUCCESS;
        }
        sg_count ++;
        p_sg_node = sai_qos_port_get_next_sched_group (p_qos_port_node, p_sg_node);
    }

    SAI_SCHED_GRP_LOG_ERR ("Required scheduler group not exits in "
                           "port 0x%"PRIx64" and level %d, "
                           "childidx %d sg present %d.",
                           port_id, level, child_idx, sg_count);
    return SAI_STATUS_FAILURE;
}

sai_status_t sai_qos_sched_group_id_list_per_level_get (sai_object_id_t port_id,
                                                        uint_t level,
                                                        uint_t sg_id_list_count,
                                                        sai_object_id_t *p_sg_id_list)
{
    dn_sai_qos_sched_group_t *p_sg_node = NULL;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    uint_t                    sg_count = 0;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" level %d scheduler group id list "
                             "get, Req number of scheduler groups %d",
                             port_id, level, sg_id_list_count);

    if (0 == sg_id_list_count)
        return SAI_STATUS_SUCCESS;

    STD_ASSERT (p_sg_id_list != NULL);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                         port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    for (p_sg_node =
             sai_qos_port_get_first_sched_group (p_qos_port_node, level);
             ((p_sg_node != NULL) && (sg_count < sg_id_list_count)); p_sg_node =
             sai_qos_port_get_next_sched_group (p_qos_port_node, p_sg_node)) {
        p_sg_id_list [sg_count] = p_sg_node->key.sched_group_id;
        sg_count++;
    }

    if (sg_id_list_count != sg_count) {
        SAI_SCHED_GRP_LOG_ERR ("Required scheduler group not exits in "
                               "port 0x%"PRIx64" and level %d, "
                               "req count %d sg present %d.",
                               port_id, level, sg_id_list_count, sg_count);
        return SAI_STATUS_FAILURE;
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_qos_port_sched_groups_init (sai_object_id_t port_id)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;
    uint_t           level = 0;
    uint_t           attr_count = 0;
    uint_t           max_childs = 0;
    uint_t           max_groups_per_level = 0;
    uint_t           num_groups = 0;
    sai_attribute_t  attr_list[SAI_SCHED_GROUP_MANDATORY_ATTR_COUNT];
    sai_object_id_t  sg_id = SAI_NULL_OBJECT_ID;
    dn_sai_qos_port_t *p_qos_port_node = NULL;
    dn_sai_qos_hierarchy_t *p_default_hqos = NULL;

    if(sai_is_obj_id_cpu_port(port_id)){
        p_default_hqos = sai_qos_default_cpu_hqos_get();
    }else {
        p_default_hqos = sai_qos_default_hqos_get();
    }

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" Scheduler Groups Init.", port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_SCHED_GRP_LOG_ERR ("Port 0x%"PRIx64" does not exist in tree.",
                               port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    SAI_SCHED_GRP_LOG_TRACE ("Root Scheduler Group Init for port 0x%"PRIx64".",
                             port_id);

    sai_rc = sai_sched_group_npu_api_get()->root_sched_group_init (port_id);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("ROOT Scheduler groups init failed "
                               "for port 0x%"PRIx64".", port_id);
        return sai_rc;
    }

    /* Create 1 scheduler group at each level other than the leaf.
     * Leaf level, NPU suport seperate unicast and multicast queues
     * on port create logical pairs */
    for (level = 0; level < sai_switch_max_hierarchy_levels_get(); level++) {

        max_groups_per_level = p_default_hqos->level_info[level].num_sg_groups;

        SAI_SCHED_GRP_LOG_TRACE ("Create Scheduler Groups for port 0x%"PRIx64""
                                 " level %d Number of groups %d", port_id,
                                 level, max_groups_per_level);

        for(num_groups = 0; num_groups < max_groups_per_level; num_groups ++){
            max_childs = p_default_hqos->level_info[level].
                sg_info[num_groups].num_children;
            attr_count = 0;

            attr_list[attr_count].id = SAI_SCHEDULER_GROUP_ATTR_PORT_ID;
            attr_list[attr_count].value.oid = port_id;
            attr_count ++;

            attr_list[attr_count].id = SAI_SCHEDULER_GROUP_ATTR_LEVEL;
            attr_list[attr_count].value.u32 = level;
            attr_count ++;

            attr_list[attr_count].id = SAI_SCHEDULER_GROUP_ATTR_MAX_CHILDS;
            attr_list[attr_count].value.u32 = max_childs;
            attr_count ++;

            sg_id = 0;

            SAI_SCHED_GRP_LOG_TRACE ("Scheduler group %d max_childs %d",
                                    num_groups, max_childs);

            sai_rc = sai_qos_sched_group_create (&sg_id, attr_count, &attr_list[0]);
            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Failed to create scheduler "
                                       "groups for port 0x%"PRIx64" level %d.",
                                       port_id, level);
                return sai_rc;
            }

            SAI_SCHED_GRP_LOG_TRACE ("Created Scheduler Group 0x%"PRIx64" for "
                                     "port 0x%"PRIx64" level %d",
                                     sg_id, port_id, level);
        }
    }

    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" Scheduler groups Init success.", port_id);

    return sai_rc;
}

sai_status_t sai_qos_port_sched_groups_deinit (sai_object_id_t port_id)
{
    sai_status_t              sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_sched_group_t *p_sg_node = NULL;
    dn_sai_qos_sched_group_t *p_next_sg_node = NULL;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    uint_t                    level = 0;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" Scheduler groups De-Init.",
                             port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {

        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                         port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    for (level = 0; level < sai_switch_max_hierarchy_levels_get (); level++)
    {
        for (p_sg_node =
             sai_qos_port_get_first_sched_group (p_qos_port_node, level);
             p_sg_node != NULL; p_sg_node = p_next_sg_node) {

            p_next_sg_node = sai_qos_port_get_next_sched_group (p_qos_port_node,
                                                                     p_sg_node);
            sai_rc = sai_qos_sched_group_remove (p_sg_node->key.sched_group_id);

            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Scheduler group remove failed "
                                       "for port 0x%"PRIx64", Scheduler group "
                                       "0x%"PRIx64".", port_id,
                                       p_sg_node->key.sched_group_id);
                return sai_rc;
            }
        }
    }

    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" Scheduler groups De-Init success.",
                             port_id);
    return sai_rc;
}


static sai_scheduler_group_api_t sai_qos_sched_group_method_table = {
    sai_qos_sched_group_create,
    sai_qos_sched_group_remove,
    sai_qos_sched_group_attribute_set,
    sai_qos_sched_group_attribute_get,
    sai_add_child_object_to_group,
    sai_remove_child_object_from_group,
};

sai_scheduler_group_api_t *sai_qos_sched_group_api_query (void)
{
    return (&sai_qos_sched_group_method_table);
}

