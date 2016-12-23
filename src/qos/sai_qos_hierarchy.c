/************************************************************************
* LEGALESE:   "Copyright (c) 2015, Dell Inc. All rights reserved."
*
* This source code is confidential, proprietary, and contains trade
* secrets that are the sole property of Dell Inc.
* Copy and/or distribution of this source code or disassembly or reverse
* engineering of the resultant object code are strictly forbidden without
* the written consent of Dell Inc.
*
************************************************************************/
/**
* @file  sai_qos_hierarchy.c
*
* @brief This file contains function definitions for SAI QOS default
*        scheduler groups initilization and hiearchy creation and SAI
*        scheduler group functionality API implementation.
*
*************************************************************************/

#include "sai_qos_common.h"
#include "sai_qos_util.h"
#include "sai_qos_api_utils.h"
#include "sai_common_infra.h"

#include "saistatus.h"
#include "saitypes.h"

#include "std_assert.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

static inline bool sai_qos_is_child_obj_valid (sai_object_id_t child_id)
{
    return (sai_is_obj_id_queue (child_id) || sai_is_obj_id_scheduler_group (child_id));
}

static sai_status_t sai_qos_sched_group_child_list_validate (sai_object_id_t sg_id,
                                                    uint_t child_count,
                                                    const sai_object_id_t* child_objects,
                                                    bool is_add)
{
    sai_status_t    sai_rc = SAI_STATUS_SUCCESS;
    size_t          child_idx = 0;
    sai_object_id_t child_parent_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t child_id = SAI_NULL_OBJECT_ID;

    if (! sai_is_obj_id_scheduler_group (sg_id)) {
        SAI_SCHED_GRP_LOG_ERR ("Invalid Parent SG 0x%"PRIx64" object id.", sg_id);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    if ((0 == child_count)) {

        SAI_SCHED_GRP_LOG_ERR ("Invalid Child list count: %d.", child_count);

        return SAI_STATUS_INVALID_PARAMETER;
    }

    STD_ASSERT (child_objects != NULL);

    for (child_idx = 0; child_idx < child_count; child_idx++) {

        child_id = child_objects[child_idx];

        if (! sai_qos_is_child_obj_valid (child_id)) {
            SAI_SCHED_GRP_LOG_ERR ("Invalid Child 0x%"PRIx64" object id.",
                                   child_id);

            sai_rc =  SAI_STATUS_INVALID_PARAMETER;
            break;
        }

        sai_rc = sai_qos_child_parent_id_get (child_id, &child_parent_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to get parent id for child 0x%"PRIx64".",
                                   child_id);
            break;
        }

        if (is_add) {
            if (child_parent_id == sg_id) {
                SAI_SCHED_GRP_LOG_ERR ("Duplicate, Child 0x%"PRIx64" already exists same "
                                       "parent 0x%"PRIx64".", child_id,
                                       child_parent_id);

                sai_rc = SAI_STATUS_ITEM_ALREADY_EXISTS;
                break;
            }

            if (child_parent_id != SAI_NULL_OBJECT_ID) {
                SAI_SCHED_GRP_LOG_ERR ("Child 0x%"PRIx64" has different "
                                       "parent 0x%"PRIx64", input parent 0x%"PRIx64".",
                                       child_id, child_parent_id, sg_id);

                sai_rc = SAI_STATUS_OBJECT_IN_USE;
                break;
            }
        } else {
            if (child_parent_id == SAI_NULL_OBJECT_ID) {
                SAI_SCHED_GRP_LOG_ERR ("Child 0x%"PRIx64" has no parent. "
                                       "Invalid inputs.", child_id);

                sai_rc = SAI_STATUS_INVALID_PARAMETER;
                break;
            }

            if (child_parent_id != sg_id) {
                SAI_SCHED_GRP_LOG_ERR ("Child 0x%"PRIx64" has different parent "
                                       "0x%"PRIx64", Input parent 0x%"PRIx64".",
                                       child_id, child_parent_id, sg_id);

                sai_rc = SAI_STATUS_INVALID_PARAMETER;
                break;
            }
        }
    }
    return sai_rc;
}

static dn_sai_qos_hierarchy_info_t *sai_qos_sched_group_hierarchy_info_get (
                                                          sai_object_id_t sg_id)
{
    dn_sai_qos_sched_group_t    *p_parent_sg_node = NULL;

    p_parent_sg_node = sai_qos_sched_group_node_get (sg_id);

    if (NULL == p_parent_sg_node) {
        SAI_SCHED_GRP_LOG_ERR ("Parent Scheduler group 0x%"PRIx64" does not "
                               "exist in tree.", sg_id);
        return NULL;
    }

    return &p_parent_sg_node->hqos_info;
}

static sai_status_t sai_qos_sched_group_and_child_nodes_update (
                                          sai_object_id_t sg_id,
                                          uint_t child_count,
                                          const sai_object_id_t* child_objects,
                                          bool is_add)
{
    dn_sai_qos_sched_group_t    *p_child_sg_node = NULL;
    dn_sai_qos_queue_t          *p_child_queue_node = NULL;
    std_dll_head                *p_child_dll_head;
    std_dll                     *p_child_dll_glue;
    sai_object_type_t            child_obj_type = 0;
    dn_sai_qos_hierarchy_info_t *p_parent_hqos_info = NULL;
    size_t                       child_idx = 0;
    sai_object_id_t              child_id = SAI_NULL_OBJECT_ID;

    p_parent_hqos_info =
        sai_qos_sched_group_hierarchy_info_get (sg_id);

    STD_ASSERT (p_parent_hqos_info != NULL);

    for (child_idx = 0; child_idx < child_count; child_idx++) {

        child_id = child_objects[child_idx];

        child_obj_type = sai_uoid_obj_type_get (child_id);

        switch (child_obj_type)
        {
            case SAI_OBJECT_TYPE_QUEUE:
                p_child_queue_node = sai_qos_queue_node_get (child_id);

                if (NULL == p_child_queue_node) {
                    SAI_SCHED_GRP_LOG_ERR ("Child Queue 0x%"PRIx64" does not "
                                           "exist in tree.", child_id);

                    return SAI_STATUS_INVALID_OBJECT_ID;
                }

                p_child_dll_glue = &p_child_queue_node->child_queue_dll_glue;
                p_child_dll_head = &p_parent_hqos_info->child_queue_dll_head;
                p_child_queue_node->parent_sched_group_id =
                    (is_add ? sg_id : SAI_NULL_OBJECT_ID);

                break;

            case SAI_OBJECT_TYPE_SCHEDULER_GROUP:
                p_child_sg_node = sai_qos_sched_group_node_get (child_id);

                if (NULL == p_child_sg_node) {
                    SAI_SCHED_GRP_LOG_ERR ("Child Scheduler group 0x%"PRIx64" does not "
                                           "exist in tree.", child_id);

                    return SAI_STATUS_INVALID_OBJECT_ID;
                }

                p_child_dll_glue = &p_child_sg_node->child_sched_group_dll_glue;
                p_child_dll_head = &p_parent_hqos_info->child_sched_group_dll_head;

                p_child_sg_node->hqos_info.parent_sched_group_id =
                    (is_add ? sg_id : SAI_NULL_OBJECT_ID);

                break;

            default:
                return SAI_STATUS_INVALID_PARAMETER;
        }

        if (is_add) {
            std_dll_insertatback (p_child_dll_head, p_child_dll_glue);
            p_parent_hqos_info->child_count++;
        } else {
            std_dll_remove (p_child_dll_head, p_child_dll_glue);
            p_parent_hqos_info->child_count--;
        }
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_add_child_object_to_group (sai_object_id_t sg_id,
                                            uint32_t child_count,
                                            const sai_object_id_t* child_objects)
{
    sai_status_t  sai_rc              = SAI_STATUS_SUCCESS;
    uint_t        added_child_count   = 0;
    uint_t        removed_child_count = 0;

    SAI_SCHED_GRP_LOG_TRACE ("SAI Add childs to parent, "
                             "sg_id: 0x%"PRIx64", child_count: %d.",
                             sg_id, child_count);

    sai_qos_lock ();

    do {
        sai_rc = sai_qos_sched_group_child_list_validate (sg_id, child_count,
                                                     child_objects, true);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Input parameters validation failed.");

            break;
        }

        sai_rc = sai_sched_group_npu_api_get()->add_child_to_group (sg_id,
                                                                    child_count,
                                                                    child_objects,
                                                                    &added_child_count);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Add child objects to parent sg failed in NPU,"
                                   "Remove successfuly added childs in list");
            sai_sched_group_npu_api_get ()->remove_child_from_group (sg_id,
                                            added_child_count, child_objects,
                                            &removed_child_count);
            break;
        }

        SAI_SCHED_GRP_LOG_TRACE ("Added child objects to SG in NPU.");

        /* Update parent HQos information */
        sai_rc = sai_qos_sched_group_and_child_nodes_update (sg_id, child_count,
                                                             child_objects, true);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to update the child and parent hierarchy "
                                   "information.");
            break;
        }
    } while (0);

    sai_qos_unlock ();

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_TRACE ("Add child objects to parent SG 0x%"PRIx64" "
                                 "success.", sg_id);
    } else {
        SAI_SCHED_GRP_LOG_ERR ("Failed to add child objects to parent "
                               "SG 0x%"PRIx64" failed.", sg_id);
    }
    return sai_rc;
}

sai_status_t sai_remove_child_object_from_group (sai_object_id_t sg_id,
                                                  uint32_t child_count,
                                                  const sai_object_id_t* child_objects)
{
    sai_status_t sai_rc              = SAI_STATUS_SUCCESS;
    uint_t       removed_child_count = 0;
    uint_t       added_child_count   = 0;

    SAI_SCHED_GRP_LOG_TRACE ("SAI Remove childs from parent, "
                             "sg_id: 0x%"PRIx64", child_count: %d.",
                             sg_id, child_count);

    sai_qos_lock ();

    do {
        sai_rc = sai_qos_sched_group_child_list_validate (sg_id, child_count,
                                                          child_objects, false);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Input parameters validation failed.");

            break;
        }

        sai_rc = sai_sched_group_npu_api_get ()->remove_child_from_group (sg_id,
                                                                         child_count,
                                                                         child_objects,
                                                                         &removed_child_count);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Remove child objects from parent failed in NPU, "
                                   "Add back successfully removed childs");
            sai_sched_group_npu_api_get()->add_child_to_group (sg_id,
                                                removed_child_count, child_objects,
                                                &added_child_count);
            break;
        }

        SAI_SCHED_GRP_LOG_TRACE ("Removed child objets from parent SG in NPU.");

        /* Update parent HQos information */
        sai_rc = sai_qos_sched_group_and_child_nodes_update (sg_id, child_count,
                                                        child_objects, false);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to update the child and parent hierarchy "
                                   "information.");
            break;
        }
    } while (0);

    sai_qos_unlock ();

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_TRACE ("Remove child objects from parent SG 0x%"PRIx64" "
                                 "success.", sg_id);
    } else {
        SAI_SCHED_GRP_LOG_ERR ("Failed to remove child objects from parent SG "
                               "0x%"PRIx64" failed.", sg_id);
    }
    return sai_rc;
}
static sai_status_t sai_set_default_scheduler(uint_t child_count,
                                              sai_object_id_t *child_list)
{
    uint_t idx = 0;
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_attribute_t attr;
    dn_sai_qos_queue_t       *p_queue_node = NULL;
    dn_sai_qos_sched_group_t *p_sg_node = NULL;

    STD_ASSERT(child_list != NULL);

    if(sai_qos_default_sched_id_get() == SAI_NULL_OBJECT_ID){
        SAI_SCHED_GRP_LOG_ERR("Default scheduler id is a NULL object");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for(idx = 0; idx < child_count; idx ++){
        if(sai_is_obj_id_scheduler_group(child_list[idx])){
            p_sg_node = sai_qos_sched_group_node_get(child_list[idx]);

            if(p_sg_node == NULL){
                SAI_SCHED_GRP_LOG_ERR("SG id 0x%"PRIx64" not in tree",
                                      child_list[idx]);
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            attr.id = SAI_SCHEDULER_GROUP_ATTR_SCHEDULER_PROFILE_ID;
            attr.value.oid = sai_qos_default_sched_id_get();

            sai_rc = sai_qos_sched_group_scheduler_set(p_sg_node, &attr);

            if(sai_rc != SAI_STATUS_SUCCESS){
                SAI_SCHED_GRP_LOG_ERR("Default scheduler set failed for sgid 0x%"PRIx64"",
                                      child_list[idx]);
                return sai_rc;

            }
            p_sg_node->scheduler_id = sai_qos_default_sched_id_get();

        }else if(sai_is_obj_id_queue((child_list[idx]))){
            p_queue_node = sai_qos_queue_node_get(child_list[idx]);

            if(p_queue_node == NULL){
                SAI_SCHED_GRP_LOG_ERR("Queue id 0x%"PRIx64" not in tree",
                                      child_list[idx]);
                return SAI_STATUS_INVALID_OBJECT_ID;
            }
            attr.id = SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID;
            attr.value.oid = sai_qos_default_sched_id_get();

            sai_rc = sai_qos_queue_scheduler_set(p_queue_node, &attr);

            if(sai_rc != SAI_STATUS_SUCCESS){
                SAI_SCHED_GRP_LOG_ERR("Default scheduler set failed for qid0x%"PRIx64"",
                                      child_list[idx]);
                return sai_rc;
            }
            p_queue_node->scheduler_id = sai_qos_default_sched_id_get();
        }else{
            return SAI_STATUS_INVALID_OBJECT_ID;
        }
    }

    return sai_rc;
}

static sai_status_t sai_qos_port_hierarchy_create (sai_object_id_t port_id,
                                                   dn_sai_qos_hierarchy_t *p_default_hqos)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;
    uint_t           level = 0;
    uint_t           child_count = 0;
    uint_t           sg_groups = 0;
    uint_t           count = 0;
    uint_t           child_idx = 0;
    uint_t           child_type = 0;
    uint_t           queue_type = 0;
    uint_t           parent_idx = 0;
    uint_t           child_level = 0;
    sai_object_id_t  parent_sg_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t  child_sg_id = SAI_NULL_OBJECT_ID;
    dn_sai_qos_queue_t *p_queue_node = NULL;
    dn_sai_qos_port_t *p_qos_port_node = NULL;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" default hierarchy create for "
                             "non leaf levels.", port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                               port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    for (level = 0; level < sai_switch_max_hierarchy_levels_get(); level++) {
        for(sg_groups = 0; sg_groups < p_default_hqos->level_info[level].num_sg_groups;
            sg_groups ++){
            parent_idx = p_default_hqos->level_info[level].sg_info[sg_groups].node_id;
            SAI_SCHED_GRP_LOG_TRACE("Level %d sg_group %d node %d",
                                    level, sg_groups, parent_idx);
            sai_rc = sai_qos_indexed_sched_group_id_get(port_id, level, parent_idx,
                                                        &parent_sg_id);

            if(sai_rc != SAI_STATUS_SUCCESS){
                SAI_SCHED_GRP_LOG_ERR("Failed to get parent sgid for level %d sggroup %d node %d",
                                      level, sg_groups, parent_idx);
                return sai_rc;
            }

            child_count = p_default_hqos->level_info[level].sg_info[sg_groups].num_children;
            SAI_SCHED_GRP_LOG_TRACE("Level %d sg_group %d node %d childcount %d",
                                    level, sg_groups, parent_idx, child_count);

            if(child_count == 0){
                continue;
            }
            sai_object_id_t child_list [child_count];

            for(count = 0; count < child_count; count ++){
                child_idx = p_default_hqos->level_info[level].sg_info[sg_groups].
                    child_info[count].child_index;
                child_type = p_default_hqos->level_info[level].sg_info[sg_groups].
                    child_info[count].type;
                child_level = p_default_hqos->level_info[level].sg_info[sg_groups].
                    child_info[count].level;

                SAI_SCHED_GRP_LOG_TRACE("childtype is %d childidx %d childlevel %d",child_type,
                                      child_idx, child_level);
                if(child_type == CHILD_TYPE_SCHEDULER){
                    sai_rc = sai_qos_indexed_sched_group_id_get(port_id, child_level, child_idx,
                                                                &child_sg_id);
                    SAI_SCHED_GRP_LOG_TRACE("Child sgid is 0x%"PRIx64"",child_sg_id);
                    child_list[count] = child_sg_id;
                }else if(child_type == CHILD_TYPE_QUEUE){
                    queue_type = p_default_hqos->level_info[level].sg_info[sg_groups].
                        child_info[count].queue_type;

                    if(queue_type == SAI_QUEUE_TYPE_UNICAST){
                        p_queue_node = sai_qos_port_get_indexed_uc_queue_object(p_qos_port_node,
                                                                                child_idx);
                    } else if(queue_type == SAI_QUEUE_TYPE_MULTICAST){
                        p_queue_node = sai_qos_port_get_indexed_mc_queue_object(p_qos_port_node,
                                                                                child_idx);
                    } else{
                        p_queue_node = sai_qos_port_get_indexed_queue_object(p_qos_port_node,
                                                                             child_idx);
                    }
                    if(p_queue_node == NULL){
                        SAI_SCHED_GRP_LOG_ERR("Queue node not in tree");
                        return SAI_STATUS_ITEM_NOT_FOUND;
                    }
                    child_list[count] = p_queue_node->key.queue_id;
                }
                if (sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_SCHED_GRP_LOG_ERR ("Failed to get scheduler "
                                           "group id per port 0x%"PRIx64" level %d "
                                           "idx %d", port_id, level, child_idx);
                    return sai_rc;
                }
            }
            sai_rc = sai_add_child_object_to_group (parent_sg_id, child_count,
                                                    child_list);
            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Failed to add child to parent SG 0x%"PRIx64" "
                                       "for port 0x%"PRIx64" level %d.",
                                       parent_sg_id, port_id, level);
                return sai_rc;
            }

            sai_rc = sai_set_default_scheduler(child_count, child_list);
            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Failed to set default scheduler");
                return sai_rc;
            }
        }
    }
    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" default hierarchy create"
                             "success.", port_id);
    return sai_rc;
}
static sai_status_t sai_qos_hierarchy_create (sai_object_id_t port_id)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;

    SAI_SCHED_GRP_LOG_TRACE ("Port default hierarchy create.");

    /* Create 1 scheduler group at each level other than the leaf.
     * Leaf level, NPU suport seperate unicast and multicast queues
     * on port create logical pairs */

    if(!sai_is_obj_id_cpu_port(port_id)){
        sai_rc = sai_qos_port_hierarchy_create (port_id, sai_qos_default_hqos_get());
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to hierarchy for port 0x%"PRIx64".", port_id);
            return sai_rc;
        }
    }else{
        sai_rc = sai_qos_port_hierarchy_create (port_id, sai_qos_default_cpu_hqos_get());
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to hierarchy for port 0x%"PRIx64".", port_id);
            return sai_rc;
        }
    }
    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" Hierarchy create success.",
                             port_id);

    return sai_rc;
}

static sai_status_t sai_qos_leaf_level_hierarchy_remove (sai_object_id_t port_id)
{
    sai_status_t              sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    dn_sai_qos_queue_t       *p_queue_node = NULL;

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {

        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                               port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" leaf level hierarchy remove.",
                             port_id);

    for (p_queue_node = sai_qos_port_get_first_queue (p_qos_port_node);
         p_queue_node != NULL; p_queue_node =
         sai_qos_port_get_next_queue (p_qos_port_node, p_queue_node)) {

        if (p_queue_node->parent_sched_group_id == SAI_NULL_OBJECT_ID)
            continue;

        sai_rc = sai_remove_child_object_from_group (p_queue_node->parent_sched_group_id,
                                                      SAI_QOS_CHILD_COUNT_ONE,
                                                      &p_queue_node->key.queue_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_SCHED_GRP_LOG_ERR ("Failed to remove child 0x%"PRIx64" from "
                                   "parent 0x%"PRIx64" for port 0x%"PRIx64".",
                                   &p_queue_node->key.queue_id,
                                   p_queue_node->parent_sched_group_id,
                                   port_id);
            return sai_rc;
        }
    }

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" leaf hierarchy remove success.",
                             port_id);

    return sai_rc;
}

static sai_status_t sai_qos_non_leaf_level_hierarchy_remove (sai_object_id_t port_id)
{
    sai_status_t              sai_rc = SAI_STATUS_SUCCESS;
    int                       level = 0;
    dn_sai_qos_sched_group_t *p_sg_node = NULL;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    sai_object_id_t           parent_sg_id = SAI_NULL_OBJECT_ID;

    SAI_SCHED_GRP_LOG_TRACE ("Port  0x%"PRIx64" hierarchy remove for non "
                             "leaf levels.", port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {

        SAI_SCHED_GRP_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                               port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    for (level = sai_switch_leaf_hierarchy_level_get();
         level >= 1; level--) {

        SAI_SCHED_GRP_LOG_TRACE ("level %d.", level);

        for (p_sg_node =
             sai_qos_port_get_first_sched_group (p_qos_port_node, level);
             (p_sg_node != NULL);
             p_sg_node = sai_qos_port_get_next_sched_group (p_qos_port_node, p_sg_node)) {

            parent_sg_id = p_sg_node->hqos_info.parent_sched_group_id;

            if (parent_sg_id == SAI_NULL_OBJECT_ID)
                continue;

            sai_rc = sai_remove_child_object_from_group (parent_sg_id, SAI_QOS_CHILD_COUNT_ONE,
                                                          &p_sg_node->key.sched_group_id);
            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_SCHED_GRP_LOG_ERR ("Failed to remove child 0x%"PRIx64" from "
                                       "parent 0x%"PRIx64" for port 0x%"PRIx64" "
                                       "level %d.", p_sg_node->key.sched_group_id,
                                       parent_sg_id, port_id, level);
                return sai_rc;
            }
        }
    }

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" hierarchy remove for non leaf "
                             "levels success.", port_id);

    return sai_rc;
}

static sai_status_t sai_qos_hierarchy_remove (sai_object_id_t port_id)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" hierarchy remove.", port_id);

    sai_rc = sai_qos_leaf_level_hierarchy_remove (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Failed to remove leaf hierarchy for port 0x%"PRIx64".",
                               port_id);
        return sai_rc;
    }

    sai_rc = sai_qos_non_leaf_level_hierarchy_remove (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Failed to remove non leaf hierarchy for port 0x%"PRIx64".",
                               port_id);
        return sai_rc;
    }

    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" Hierarchy remove success.",
                             port_id);

    return sai_rc;
}

sai_status_t sai_qos_port_default_hierarchy_init (sai_object_id_t port_id)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" Default Hierarchy Init.", port_id);

    sai_rc = sai_qos_port_sched_groups_init (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port Scheduler groups init failed "
                               "for port 0x%"PRIx64".", port_id);
        return sai_rc;
    }

    sai_rc = sai_qos_hierarchy_create (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port default hierarchy create failed "
                               "for port 0x%"PRIx64".", port_id);
        return sai_rc;
    }

    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" Default hierarchy Init success.",
                             port_id);

    return sai_rc;
}

sai_status_t sai_qos_port_hierarchy_deinit (sai_object_id_t port_id)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    SAI_SCHED_GRP_LOG_TRACE ("Port 0x%"PRIx64" Hierarchy De-Init.", port_id);

    /* Remove present hierarchy created on port */
    sai_rc = sai_qos_hierarchy_remove (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port hierarchy remove failed "
                               "for port 0x%"PRIx64".", port_id);
        return sai_rc;
    }

    /* De-Initialize the scheduler groups created on port */
    sai_rc = sai_qos_port_sched_groups_deinit (port_id);
    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SCHED_GRP_LOG_ERR ("Port Scheduler groups De-Init failed "
                               "for port 0x%"PRIx64".", port_id);
        return sai_rc;
    }

    SAI_SCHED_GRP_LOG_INFO ("Port 0x%"PRIx64" hierarchy De-Init success.",
                             port_id);
    return sai_rc;
}
