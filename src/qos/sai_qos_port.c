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
 * @file  sai_qos_port.c
 *
 * @brief This file contains function definitions for SAI Qos port
 *        Initialization functionality API implementation and
 *        port specific qos settings.
 */

#include "sai_qos_common.h"
#include "sai_qos_util.h"
#include "sai_qos_buffer_util.h"
#include "sai_qos_api_utils.h"
#include "sai_port_utils.h"
#include "sai_qos_mem.h"
#include "sai_common_infra.h"

#include "saistatus.h"

#include "std_assert.h"

#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

static sai_status_t sai_qos_port_node_insert_into_port_db (sai_object_id_t port_id,
                                                dn_sai_qos_port_t *p_qos_port_node)
{
    sai_port_application_info_t  *p_port_node = NULL;

    STD_ASSERT (p_qos_port_node != NULL);

    p_port_node = sai_port_application_info_create_and_get (port_id);

    if (p_port_node == NULL) {
        SAI_QOS_LOG_ERR ("Port Info is NULL for port 0x%"PRIx64"", port_id);
        return SAI_STATUS_FAILURE;
    }

    p_port_node->qos_port_db = (void *)(p_qos_port_node);

    return SAI_STATUS_SUCCESS;
}

static void sai_qos_port_node_remove_from_port_db (sai_object_id_t port_id)
{
    sai_port_application_info_t  *p_port_node = NULL;

    p_port_node = sai_port_application_info_get (port_id);

    STD_ASSERT (p_port_node != NULL);

    STD_ASSERT (p_port_node->qos_port_db != NULL);

    p_port_node->qos_port_db = NULL;

    sai_port_application_info_remove(p_port_node);
}

static sai_status_t sai_qos_port_sched_group_dll_head_init (
                              dn_sai_qos_port_t *p_qos_port_node)
{
    uint_t        h_levels_per_port = sai_switch_max_hierarchy_levels_get ();
    uint_t        level = 0;

    if (! sai_qos_is_hierarchy_qos_supported ())
        return SAI_STATUS_SUCCESS;

    p_qos_port_node->sched_group_dll_head =
        ((std_dll_head *) calloc (h_levels_per_port, sizeof (std_dll_head)));

    if (NULL == p_qos_port_node->sched_group_dll_head) {
        SAI_QOS_LOG_CRIT ("Port sched group list memory allocation failed.");
        return SAI_STATUS_NO_MEMORY;
    }

    for (level = 0; level < h_levels_per_port; level++) {
        std_dll_init ((std_dll_head *)&p_qos_port_node->sched_group_dll_head[level]);
    }

    return SAI_STATUS_SUCCESS;
}

static void sai_qos_port_sched_group_dll_head_free (
                              dn_sai_qos_port_t *p_qos_port_node)
{
    if (! sai_qos_is_hierarchy_qos_supported ())
        return;

    STD_ASSERT(p_qos_port_node != NULL);

    free (p_qos_port_node->sched_group_dll_head);
    p_qos_port_node->sched_group_dll_head = NULL;
}

static sai_status_t sai_qos_port_node_init (dn_sai_qos_port_t *p_qos_port_node)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    STD_ASSERT(p_qos_port_node != NULL);

    p_qos_port_node->is_app_hqos_init = false;

    std_dll_init (&p_qos_port_node->queue_dll_head);
    std_dll_init (&p_qos_port_node->pg_dll_head);

    sai_rc = sai_qos_port_sched_group_dll_head_init (p_qos_port_node);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_ERR ("Failed to init scheduler group list.");
    }

    return sai_rc;
}

static sai_status_t sai_qos_port_remove_configs (dn_sai_qos_port_t *p_qos_port_node)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;
    sai_status_t     rev_sai_rc;
    uint_t           map_type;
    uint_t           rev_map_type;
    uint_t           policer_type;
    uint_t           rev_policer_type;
    sai_object_id_t  wred_id;
    sai_object_id_t  buffer_profile_id;
    sai_object_id_t  maps_id [SAI_QOS_MAX_QOS_MAPS_TYPES];
    sai_object_id_t  policer_id [SAI_QOS_POLICER_TYPE_MAX];
    sai_attribute_t  set_attr;
    bool             wred_removed = false;
    bool             buffer_profile_removed = false;
    bool             maps_removed = false;
    bool             policer_removed = false;


    STD_ASSERT(p_qos_port_node != NULL);

    memcpy(maps_id, p_qos_port_node->maps_id, sizeof(maps_id[0])*SAI_QOS_MAX_QOS_MAPS_TYPES);
    memcpy(policer_id, p_qos_port_node->policer_id, sizeof(policer_id[0])*SAI_QOS_POLICER_TYPE_MAX);
    buffer_profile_id = p_qos_port_node->buffer_profile_id;
    wred_id = p_qos_port_node->wred_id;

    do {
        if(p_qos_port_node->buffer_profile_id != SAI_NULL_OBJECT_ID) {
            sai_rc = sai_qos_obj_update_buffer_profile(p_qos_port_node->port_id, SAI_NULL_OBJECT_ID);

            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_ERR ("Error: Unable to remove buffer profile from qos_port:0x%"PRIx64"",
                        p_qos_port_node->port_id);
                break;
            }
            p_qos_port_node->buffer_profile_id = SAI_NULL_OBJECT_ID;
            buffer_profile_removed = true;
        }

        if(p_qos_port_node->wred_id != SAI_NULL_OBJECT_ID) {
            set_attr.id = SAI_PORT_ATTR_QOS_WRED_PROFILE_ID;
            set_attr.value.oid = SAI_NULL_OBJECT_ID;

            sai_rc = sai_port_attr_wred_profile_set (p_qos_port_node->port_id, &set_attr);


            if(sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_ERR ("Error: Unable to remove WRED profile from qos_port:0x%"PRIx64"",
                        p_qos_port_node->port_id);
                break;
            }
            p_qos_port_node->wred_id = SAI_NULL_OBJECT_ID;
            wred_removed = true;
        }

        for (map_type = 0; map_type < SAI_QOS_MAX_QOS_MAPS_TYPES; map_type++) {
            if(p_qos_port_node->maps_id[map_type] != SAI_NULL_OBJECT_ID) {
                set_attr.value.oid = SAI_NULL_OBJECT_ID;
                sai_rc = sai_qos_map_on_port_set(p_qos_port_node->port_id, &set_attr, map_type);
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    break;
                }
                p_qos_port_node->maps_id[map_type] = SAI_NULL_OBJECT_ID;
                maps_removed = true;
            }
        }

        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }

        for (policer_type = 0; policer_type < SAI_QOS_POLICER_TYPE_MAX; policer_type++) {
            if(p_qos_port_node->policer_id[policer_type] != SAI_NULL_OBJECT_ID) {
                sai_rc = sai_get_port_attr_from_storm_control_type(policer_type, &(set_attr.id));
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_QOS_LOG_ERR("Error: Unable to find policer id for type %d", policer_type);
                    break;
                }
                set_attr.value.oid = SAI_NULL_OBJECT_ID;

                sai_rc = sai_port_attr_storm_control_policer_set (p_qos_port_node->port_id,
                                                                  &set_attr);
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    break;
                }
                p_qos_port_node->policer_id[policer_type] = SAI_NULL_OBJECT_ID;
                policer_removed = true;
            }
        }
        if(sai_rc != SAI_STATUS_SUCCESS) {
            break;
        }
    }while (0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        /* Reverting Buffer profile */
        if(buffer_profile_removed) {
            rev_sai_rc = sai_qos_obj_update_buffer_profile(p_qos_port_node->port_id, buffer_profile_id);

            if(rev_sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_ERR ("Error: Unable to revert buffer profile on port:0x%"PRIx64""
                        " Error %d",p_qos_port_node->port_id, rev_sai_rc);
            } else {
                p_qos_port_node->buffer_profile_id = buffer_profile_id;
            }
        }

        /* Reverting wred profile */
        if (wred_removed) {
            set_attr.id = SAI_PORT_ATTR_QOS_WRED_PROFILE_ID;
            set_attr.value.oid = wred_id;

            rev_sai_rc = sai_port_attr_wred_profile_set (p_qos_port_node->port_id, &set_attr);


            if(rev_sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_ERR ("Error: Unable to revert WRED profile on port:0x%"PRIx64""
                        " Error %d", p_qos_port_node->port_id, rev_sai_rc);
            } else {
                p_qos_port_node->wred_id = wred_id;
            }
        }

        if(maps_removed) {
            for (rev_map_type = 0; rev_map_type < map_type; rev_map_type++) {
                if(maps_id[rev_map_type] != SAI_NULL_OBJECT_ID) {
                    set_attr.value.oid = maps_id[rev_map_type];
                    rev_sai_rc = sai_qos_map_on_port_set(p_qos_port_node->port_id, &set_attr, rev_map_type);
                    if(rev_sai_rc != SAI_STATUS_SUCCESS) {
                        SAI_QOS_LOG_ERR ("Error: Unable to revert map type %d on port:0x%"PRIx64""
                                " Error %d", rev_map_type, p_qos_port_node->port_id, rev_sai_rc);
                    } else {
                        p_qos_port_node->maps_id[rev_map_type] = maps_id[rev_map_type];
                    }
                }
            }

        }

        if(policer_removed) {
            for (rev_policer_type = 0; rev_policer_type < policer_type; rev_policer_type++) {
                if(policer_id[rev_policer_type] != SAI_NULL_OBJECT_ID) {
                    sai_rc = sai_get_port_attr_from_storm_control_type(rev_policer_type, &(set_attr.id));
                    if(sai_rc != SAI_STATUS_SUCCESS) {
                        SAI_QOS_LOG_ERR("Error: Unable to find policer id for type %d", policer_type);
                        break;
                    }
                    set_attr.value.oid = policer_id[rev_policer_type];

                    rev_sai_rc = sai_port_attr_storm_control_policer_set (p_qos_port_node->port_id,
                                                                          &set_attr);
                    if(rev_sai_rc != SAI_STATUS_SUCCESS) {
                        SAI_QOS_LOG_ERR ("Error: Unable to revert policer type %d on port:0x%"PRIx64""
                                " Error %d", rev_policer_type, p_qos_port_node->port_id, rev_sai_rc);
                    } else {
                        p_qos_port_node->policer_id[rev_policer_type] = policer_id[rev_policer_type];
                    }
                }
            }

        }

    }
    return sai_rc;
}

static bool sai_qos_port_is_in_use (dn_sai_qos_port_t *p_qos_port_node)
{
    uint_t level;

    STD_ASSERT (p_qos_port_node != NULL);

    /* Verify Any childs are associated to scheduler group. */
    for (level = 0; level < sai_switch_max_hierarchy_levels_get (); level++) {
        if (sai_qos_port_get_first_sched_group (p_qos_port_node, level) != NULL) {
            SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" has scheduler group and cannot be removed",
                             p_qos_port_node->port_id);
            return true;
        }
    }

    if (sai_qos_port_get_first_queue (p_qos_port_node) != NULL) {
        SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" has queue and cannot be removed",
                         p_qos_port_node->port_id);
        return true;
    }

    if (sai_qos_port_get_first_pg (p_qos_port_node) != NULL) {
        SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" has pg and cannot be removed",
                         p_qos_port_node->port_id);
        return true;
    }

    return false;
}

static void sai_qos_port_free_resources (dn_sai_qos_port_t *p_qos_port_node)
{
    if (p_qos_port_node == NULL) {
        return;
    }

    /* TODO - Delete Port node from the WRED's port list,
     * if it was already added.
     *  sai_qos_wred_port_list_update (p_qos_port_node, false); */

    sai_qos_port_sched_group_dll_head_free (p_qos_port_node);

    sai_qos_port_node_free (p_qos_port_node);

    return;
}

sai_status_t sai_qos_port_queue_list_update (dn_sai_qos_queue_t *p_queue_node,
                                             bool is_add)
{
    sai_object_id_t     port_id = 0;
    dn_sai_qos_port_t  *p_qos_port_node = NULL;

    STD_ASSERT (p_queue_node != NULL);

    port_id = p_queue_node->port_id;

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_QOS_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                         port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    if (is_add) {
        std_dll_insertatback (&p_qos_port_node->queue_dll_head,
                              &p_queue_node->port_dll_glue);
    } else {
        std_dll_remove (&p_qos_port_node->queue_dll_head,
                        &p_queue_node->port_dll_glue);
    }

    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_qos_port_sched_group_list_update (
                  dn_sai_qos_sched_group_t *p_sg_node, bool is_add)
{
    sai_object_id_t     port_id = 0;
    dn_sai_qos_port_t  *p_qos_port_node = NULL;
    uint_t              level = 0;

    STD_ASSERT (p_sg_node != NULL);

    port_id = p_sg_node->port_id;
    level   = p_sg_node->hierarchy_level;

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_QOS_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                         port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    if (is_add) {
        std_dll_insertatback (&p_qos_port_node->sched_group_dll_head [level],
                              &p_sg_node->port_dll_glue);
    } else {
        std_dll_remove (&p_qos_port_node->sched_group_dll_head [level],
                        &p_sg_node->port_dll_glue);
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_qos_port_global_init (sai_object_id_t port_id)
{
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_port_t  *p_qos_port_node = NULL;

    SAI_QOS_LOG_TRACE ("Qos Port 0x%"PRIx64" Global Init.", port_id);

    sai_qos_lock ();

    do {
        p_qos_port_node = sai_qos_port_node_alloc ();

        if (NULL == p_qos_port_node) {
            SAI_QOS_LOG_ERR ("Qos Port memory allocation failed.");
            sai_rc = SAI_STATUS_NO_MEMORY;
            break;
        }

        p_qos_port_node->port_id = port_id;

        sai_rc = sai_qos_npu_api_get()->qos_port_init (p_qos_port_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("Qos Port init failed in NPU.");
            break;
        }

        SAI_QOS_LOG_TRACE ("Port Init completed in NPU.");

        sai_rc = sai_qos_port_node_init (p_qos_port_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("Failed to init qos port node init.");
            break;
        }

        sai_rc = sai_qos_port_node_insert_into_port_db (port_id,
                                                        p_qos_port_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QUEUE_LOG_ERR ("Queue insertion to tree failed.");
            break;
        }

    } while (0);

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Qos Port 0x%"PRIx64" global init success.",
                           port_id);
    } else {
        SAI_QUEUE_LOG_ERR ("Failed qos port global init.");
        sai_qos_port_free_resources (p_qos_port_node);
    }

    sai_qos_unlock ();

    return sai_rc;
}

static sai_status_t sai_qos_port_global_deinit (sai_object_id_t port_id)
{
    sai_status_t        sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_port_t  *p_qos_port_node = NULL;

    SAI_QOS_LOG_TRACE ("Qos Port 0x%"PRIx64" Global De-Init.", port_id);

    if (! sai_is_obj_id_port (port_id)) {
        SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" is not a valid port obj id.",
                           port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_qos_lock ();

    do {
        p_qos_port_node = sai_qos_port_node_get (port_id);

        if (sai_qos_port_is_in_use (p_qos_port_node)) {
            SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" can't be deleted, since port is "
                    "in use.", port_id);

            sai_rc =  SAI_STATUS_OBJECT_IN_USE;
            break;
        }
        if (NULL == p_qos_port_node) {
            SAI_QOS_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                               port_id);

            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;

            break;
        }

        /* TODO - Check this is needed or not
        sai_rc = sai_npu_port_deinit (p_qos_port_node);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("Qos Port 0x%"PRIx64" De-init failed in NPU.",
                                port_id);
            break;
        }
        */

        sai_qos_port_node_remove_from_port_db (port_id);

        sai_qos_port_free_resources (p_qos_port_node);

    } while (0);

    sai_qos_unlock ();

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Qos Port 0x%"PRIx64" De-Initialized.", port_id);
    } else {
        SAI_QOS_LOG_ERR ("Failed to De-Initialize Qos Port 0x%"PRIx64".", port_id);
    }


    return sai_rc;
}

static sai_status_t sai_qos_port_handle_init_failure (sai_object_id_t port_id,
                                                      bool is_global_port_init,
                                                      bool is_port_queue_init,
                                                      bool is_port_hierarchy_init)
{

    if (is_port_hierarchy_init) {
        /* De-Initialize Existing Hierarchy on port */
        sai_qos_port_hierarchy_deinit (port_id);
    }

    if (is_port_queue_init) {
        /* De-Initialize all queues on port */
        sai_qos_port_queue_all_deinit (port_id);
    }

    if (is_global_port_init) {
        sai_qos_port_global_deinit (port_id);
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_qos_port_handle_deinit_failure (sai_object_id_t port_id)
{
    /* TODO */
    return SAI_STATUS_SUCCESS;
}

sai_status_t sai_qos_port_init (sai_object_id_t port_id)
{
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    bool               is_port_global_init = false;
    bool               is_port_queue_init = false;
    bool               is_port_hierarchy_init = false;
    dn_sai_qos_port_t  *p_qos_port_node = NULL;

    p_qos_port_node = sai_qos_port_node_get (port_id);


    if(p_qos_port_node != NULL) {
         return SAI_STATUS_SUCCESS;
    }

    SAI_QOS_LOG_TRACE ("Qos Port 0x%"PRIx64" Init.", port_id);

    do {
        sai_rc = sai_qos_port_global_init (port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_CRIT ("SAI QOS port 0x%"PRIx64" global init failed.",
                               port_id);
            break;
        }

        is_port_global_init = true;

        /* Initialize all queues on port */
        sai_rc = sai_qos_port_queue_all_init (port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_CRIT ("SAI QOS port 0x%"PRIx64" queue all init failed.",
                               port_id);
            break;
        }

        is_port_queue_init = true;

        if (sai_qos_is_hierarchy_qos_supported ()) {
            /* Initialize Default Scheduler groups on port */
            sai_rc = sai_qos_port_default_hierarchy_init (port_id);

            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_CRIT ("SAI QOS port 0x%"PRIx64" default hierarchy "
                                   "init failed.", port_id);
                break;
            }
            is_port_hierarchy_init = true;
        }

        sai_rc = sai_qos_port_create_all_pg (port_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_CRIT ("SAI QOS port 0x%"PRIx64" pg init failed.", port_id);
            break;
        }
    } while (0);

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Qos Port 0x%"PRIx64" init success.", port_id);
    } else {
        SAI_QUEUE_LOG_ERR ("Failed Qos Port 0x%"PRIx64" init.", port_id);

        sai_qos_port_handle_init_failure (port_id, is_port_global_init,
                                          is_port_queue_init,
                                          is_port_hierarchy_init);

    }
    return sai_rc;
}

static sai_status_t sai_qos_non_default_configs_remove (sai_object_id_t port_id)
{
    sai_status_t              sai_rc = SAI_STATUS_SUCCESS;
    int                       level = 0;
    dn_sai_qos_sched_group_t *p_sg_node = NULL;
    dn_sai_qos_sched_group_t *p_next_sg_node = NULL;
    dn_sai_qos_port_t        *p_qos_port_node = NULL;
    dn_sai_qos_queue_t       *p_queue_node = NULL;
    dn_sai_qos_queue_t       *p_next_queue_node = NULL;


    SAI_SCHED_LOG_TRACE ("Port  0x%"PRIx64" non default configs remove", port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {

        SAI_SCHED_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                            port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_rc = sai_qos_port_remove_configs (p_qos_port_node);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" can't be deleted, since port configs cannot be "
                         "removed.", port_id);

        return sai_rc;
    }

    for (p_queue_node = sai_qos_port_get_first_queue (p_qos_port_node);
         p_queue_node != NULL; p_queue_node = p_next_queue_node) {

            p_next_queue_node = sai_qos_port_get_next_queue (p_qos_port_node, p_queue_node);
            SAI_SCHED_LOG_TRACE ("queue  0x%"PRIx64" hierarchy remove for non default scheduler "
                                 "0x%"PRIx64"",p_queue_node->key.queue_id, p_queue_node->scheduler_id);
            sai_rc = sai_qos_queue_remove_configs (p_queue_node);
            if(sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_QOS_LOG_ERR("Error - Unable to remove non default scheduler for"
                                       " queue 0x%"PRIx64"",p_queue_node->key.queue_id);
            }

    }

    if(sai_qos_is_hierarchy_qos_supported()) {
        for (level = sai_switch_leaf_hierarchy_level_get();
                level >= 1; level--) {

            for (p_sg_node = sai_qos_port_get_first_sched_group (p_qos_port_node, level);
                    (p_sg_node != NULL); p_sg_node = p_next_sg_node) {

                p_next_sg_node = sai_qos_port_get_next_sched_group (p_qos_port_node, p_sg_node);

                sai_rc = sai_qos_sched_group_remove_configs (p_sg_node);

                if(sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_QOS_LOG_ERR("Error - Unable to remove non default scheduler for"
                            " scheduler group 0x%"PRIx64"",p_sg_node->key.sched_group_id);
                }


            }
        }
    }
    SAI_SCHED_LOG_TRACE ("Port 0x%"PRIx64"  non default configs remove "
                         "success.", port_id);

    return sai_rc;
}

sai_status_t sai_qos_port_deinit (sai_object_id_t port_id)
{
    sai_status_t       sai_rc = SAI_STATUS_SUCCESS;
    dn_sai_qos_port_t *p_qos_port_node = NULL;

    SAI_QOS_LOG_TRACE ("Qos Port 0x%"PRIx64" De-Init.", port_id);

    if (! sai_is_obj_id_port (port_id)) {
        SAI_QOS_LOG_ERR ("Port 0x%"PRIx64" is not a valid port obj id.",
                           port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    SAI_QOS_LOG_TRACE ("Qos Port De-Init. Port ID 0x%"PRIx64".", port_id);

    p_qos_port_node = sai_qos_port_node_get (port_id);

    if (NULL == p_qos_port_node) {
        SAI_QOS_LOG_ERR ("Qos Port 0x%"PRIx64" does not exist in tree.",
                         port_id);

        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    do {

        sai_rc = sai_qos_non_default_configs_remove (port_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("Failed to remove non default scheduler port 0x%"PRIx64".",
                    port_id);
            break;
        }

        if (sai_qos_is_hierarchy_qos_supported ()) {
            /* De-Initialize Existing Hierarchy on port */
            sai_rc = sai_qos_port_hierarchy_deinit (port_id);

            if (sai_rc != SAI_STATUS_SUCCESS) {
                SAI_QOS_LOG_ERR ("SAI QOS port 0x%"PRIx64" hierarchy de-init "
                                 "failed.", port_id);
                break;
            }
        }

        /* De-Initialize all queues on port */
        sai_rc = sai_qos_port_queue_all_deinit (port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("SAI QOS port 0x%"PRIx64" queue all de-init failed.",
                             port_id);
            break;
        }

        sai_rc = sai_qos_port_destroy_all_pg (port_id);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("SAI QOS port 0x%"PRIx64" pg all de-init failed.",
                             port_id);
            break;
        }

        sai_rc = sai_qos_port_global_deinit (port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("SAI QOS port 0x%"PRIx64" global de-init failed.",
                             port_id);
            break;
        }
    } while (0);

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Qos Port 0x%"PRIx64" de-init success.", port_id);
    } else {
        SAI_QUEUE_LOG_ERR ("Failed qos port 0x%"PRIx64" de-init.", port_id);
        sai_qos_port_handle_deinit_failure (port_id);
    }

    return sai_rc;
}

sai_status_t sai_qos_port_all_init (void)
{
    sai_status_t    sai_rc = SAI_STATUS_FAILURE;
    sai_port_info_t *port_info = NULL;
    sai_object_id_t cpu_port_id = 0;

    SAI_QOS_LOG_TRACE ("Port All Qos Init.");

    cpu_port_id = sai_switch_cpu_port_obj_id_get();

    sai_rc = sai_qos_port_init (cpu_port_id);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_CRIT ("SAI QOS CPU Port 0x%"PRIx64" init failed.",
                          cpu_port_id);
        return sai_rc;
    }

    for (port_info = sai_port_info_getfirst(); (port_info != NULL);
         port_info = sai_port_info_getnext(port_info)) {

        if (! sai_is_port_valid (port_info->sai_port_id)) {
            continue;
        }

        sai_rc = sai_qos_port_init (port_info->sai_port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_CRIT ("SAI QOS Port 0x%"PRIx64" init failed.",
                               port_info->sai_port_id);
            break;
        }
    }

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Port All Qos Init success.");
    } else {
        SAI_QUEUE_LOG_ERR ("Failed Port All Qos Init.");
    }

    return sai_rc;
}

sai_status_t sai_qos_port_all_deinit (void)
{
    sai_status_t       sai_rc = SAI_STATUS_FAILURE;
    dn_sai_qos_port_t *p_qos_port_node = NULL;
    dn_sai_qos_port_t *p_next_qos_port_node = NULL;
    sai_object_id_t    cpu_port_id = 0;
    sai_object_id_t    port_id = 0;

    SAI_QOS_LOG_TRACE ("Port All Qos De-Init.");

    cpu_port_id = sai_switch_cpu_port_obj_id_get();

    sai_rc = sai_qos_port_deinit (cpu_port_id);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_CRIT ("SAI QOS CPU Port 0x%"PRIx64" de-init failed.",
                          cpu_port_id);
        return sai_rc;
    }

    for (p_qos_port_node = sai_qos_port_node_get_first();
        (p_qos_port_node != NULL);
         p_qos_port_node = p_next_qos_port_node) {

        p_next_qos_port_node = sai_qos_port_node_get_next (p_qos_port_node);
        /**
         * Reenable this once fanout problem is resolved.
        if (! sai_is_port_valid (port_info->sai_port_id)) {
            continue;
        }
        */


        port_id = p_qos_port_node->port_id;

        sai_rc = sai_qos_port_deinit (port_id);

        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_QOS_LOG_ERR ("SAI QOS Port 0x%"PRIx64" de-init failed.", port_id);
            break;
        }
    }

    if (sai_rc == SAI_STATUS_SUCCESS) {
        SAI_QOS_LOG_INFO ("Port All Qos De-Init success.");
    } else {
        SAI_QUEUE_LOG_ERR ("Failed Port All Qos De-Init.");
    }

    return sai_rc;
}
sai_status_t sai_qos_port_buffer_profile_set (sai_object_id_t port_id,
                                              sai_object_id_t profile_id)
{
    dn_sai_qos_port_t *p_port_node = NULL;
    sai_status_t sai_rc;

    sai_qos_lock();
    p_port_node = sai_qos_port_node_get(port_id);

    if (p_port_node == NULL) {
        sai_qos_unlock ();
        SAI_BUFFER_LOG_ERR ("Object id 0x%"PRIx64" not found", port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }
    sai_rc = sai_qos_obj_update_buffer_profile(port_id, profile_id);

    sai_qos_unlock();
    return sai_rc;
}
