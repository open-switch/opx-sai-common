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
 * @file sai_stp.c
 *
 * @brief This file contains public API handling for the SAI STP
 *        functionality
 */

#include "saitypes.h"
#include "saistatus.h"
#include "saistp.h"

#include "sai_stp_defs.h"
#include "sai_stp_api.h"
#include "sai_npu_stp.h"
#include "sai_stp_api.h"
#include "sai_port_common.h"
#include "sai_port_utils.h"
#include "sai_switch_utils.h"
#include "sai_vlan_api.h"
#include "sai_vlan_common.h"
#include "sai_oid_utils.h"
#include "sai_common_infra.h"
#include "sai_stp_util.h"
#include "sai_debug_utils.h"

#include "std_rbtree.h"
#include "std_assert.h"
#include "std_type_defs.h"
#include "std_struct_utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static rbtree_handle stp_info_tree = NULL;
static sai_object_id_t g_def_stp_id = 0;
static sai_object_id_t g_stp_vlan_map[SAI_MAX_VLAN_TAG_ID+1] = {0};

static bool sai_stp_is_valid_stp_instance (sai_object_id_t stp_inst_id)
{
    dn_sai_stp_info_t   *p_stp_info = NULL;
    bool                 ret        = true;

    sai_stp_lock();

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            ret = false;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                stp_info_tree, (void *)&stp_inst_id);

        if (!(p_stp_info)) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            ret = false;
            break;
        }
    } while (0);

    sai_stp_unlock();

    return ret;
}

sai_status_t sai_stp_instance_create (sai_object_id_t *stp_inst_id,
                                      uint32_t attr_count,
                                      const sai_attribute_t *attr_list)
{
    dn_sai_stp_info_t   *p_stp_info    = NULL;
    sai_status_t         error         = SAI_STATUS_SUCCESS;
    sai_npu_object_id_t  npu_object_id = 0;

    /* STP instance create does not expect any attribute so validation of
     * attr_count and attr_list is not added
     */
    SAI_STP_LOG_WARN ("Attribute list is not applicable");
    sai_stp_lock();

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = sai_stp_info_node_alloc ();
        if (!(p_stp_info)) {
            SAI_STP_LOG_ERR ("Memory allocation failed");
            error = SAI_STATUS_NO_MEMORY;
            break;
        }

        if ((error = sai_stp_npu_api_get()->instance_create(&npu_object_id)) !=
                     SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP instance create failed");
            break;
        }

        p_stp_info->stp_inst_id = sai_uoid_create (SAI_OBJECT_TYPE_STP, npu_object_id);

        if (std_rbtree_insert (stp_info_tree, (void *) p_stp_info) != STD_ERR_OK) {
            SAI_STP_LOG_ERR ("Node insertion failed for session 0x%"PRIx64"",
                                                                p_stp_info->stp_inst_id);
            sai_stp_npu_api_get()->instance_remove (p_stp_info->stp_inst_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info->vlan_tree = std_rbtree_create_simple ("vlan_tree",
                0, sizeof(sai_vlan_id_t));

        if (p_stp_info->vlan_tree == NULL) {
            SAI_STP_LOG_ERR ("Vlan tree creations failed for session 0x%"PRIx64"",
                                                                p_stp_info->stp_inst_id);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            sai_stp_npu_api_get()->instance_remove (p_stp_info->stp_inst_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        *stp_inst_id = p_stp_info->stp_inst_id;

        SAI_STP_LOG_TRACE ("Created STP instance 0x%"PRIx64"", p_stp_info->stp_inst_id);

    } while (0);

    if ((p_stp_info) && (error != SAI_STATUS_SUCCESS)) {
        sai_stp_info_node_free ((dn_sai_stp_info_t *)p_stp_info);
    }

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_instance_remove (sai_object_id_t stp_inst_id)
{
    dn_sai_stp_info_t   *p_stp_info = NULL;
    sai_status_t         error      = SAI_STATUS_SUCCESS;

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Destroy STP instance 0x%"PRIx64"", stp_inst_id);

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                                     stp_info_tree, (void *)&stp_inst_id);
        if (!(p_stp_info)) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        /* Check if any vlan is still attached to the session */

        if (std_rbtree_getfirst(p_stp_info->vlan_tree) != NULL) {
            SAI_STP_LOG_ERR ("Vlans are still attached to stp instance 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_OBJECT_IN_USE;
            break;
        }

        if ((error = sai_stp_npu_api_get()->instance_remove (p_stp_info->stp_inst_id))
             != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP instance destroy failed 0x%"PRIx64"", stp_inst_id);
            break;
        }

        if (p_stp_info->vlan_tree) {
            std_rbtree_destroy (p_stp_info->vlan_tree);
        }

        if (std_rbtree_remove (stp_info_tree , (void *)p_stp_info) != p_stp_info) {
            SAI_STP_LOG_ERR ("STP instance node remove failed 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        sai_stp_info_node_free (p_stp_info);

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_vlan_add (sai_object_id_t stp_inst_id,
                               sai_vlan_id_t vlan_id)
{
    dn_sai_stp_info_t *p_stp_info   = NULL;
    sai_vlan_id_t     *p_vlan_node  = NULL;
    sai_status_t       error        = SAI_STATUS_SUCCESS;
    bool               is_vlan_node_created = false;

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Add vlan 0x%"PRIx64" to stp instance 0x%"PRIx64"", vlan_id,
                           stp_inst_id);
    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                                        stp_info_tree, (void *)&stp_inst_id);
        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        if (!(p_stp_info->vlan_tree)) {
            SAI_STP_LOG_ERR ("Vlans tree initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_vlan_node = std_rbtree_getexact (p_stp_info->vlan_tree,
                                            (void *) &vlan_id);
        if (p_vlan_node == NULL) {
            p_vlan_node = sai_stp_vlan_node_alloc();
            if (p_vlan_node == NULL) {
                SAI_STP_LOG_ERR ("Memory allocation failed for vlan 0x%"PRIx64"",
                        vlan_id);
                error = SAI_STATUS_NO_MEMORY;
                break;
            }
            is_vlan_node_created = true;
        } else {
            SAI_STP_LOG_ERR ("Vlan 0x%"PRIx64" already exists", vlan_id);
            error = SAI_STATUS_ITEM_ALREADY_EXISTS;
            break;
        }

        *p_vlan_node = vlan_id;

        if ((error = sai_stp_npu_api_get()->vlan_add (stp_inst_id, vlan_id))
             != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("Vlan %u addition to stp instance 0x%"PRIx64" failed",
                    vlan_id, stp_inst_id);
            break;
        }

        if (std_rbtree_insert(p_stp_info->vlan_tree, (void *)p_vlan_node) != STD_ERR_OK) {
            sai_stp_npu_api_get()->vlan_remove (stp_inst_id, vlan_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

    } while (0);

    if ((is_vlan_node_created) && (p_vlan_node) && (error != SAI_STATUS_SUCCESS)) {
        sai_stp_vlan_node_free (p_vlan_node);
    }

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_vlan_remove  (sai_object_id_t stp_inst_id,
                                   sai_vlan_id_t vlan_id)
{
    dn_sai_stp_info_t *p_stp_info   = NULL;
    sai_vlan_id_t     *p_vlan_node  = NULL;
    sai_status_t       error        = SAI_STATUS_SUCCESS;

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Remove vlan 0x%"PRIx64" from stp instance 0x%"PRIx64"",
                           vlan_id, stp_inst_id);

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                                  stp_info_tree, (void *)&stp_inst_id);
        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        if (!p_stp_info->vlan_tree) {
            SAI_STP_LOG_ERR ("Vlans tree initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_vlan_node = std_rbtree_getexact (p_stp_info->vlan_tree,
                                            (void *) &vlan_id);
        if (p_vlan_node == NULL) {
            SAI_STP_LOG_ERR ("Vlan not found 0x%"PRIx64"", vlan_id);
            error = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if ((error = sai_stp_npu_api_get()->vlan_remove (stp_inst_id, vlan_id))
             != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("Vlan 0x%"PRIx64" removal from session 0x%"PRIx64" failed",
                                   vlan_id, stp_inst_id);
            break;
        }

        if (std_rbtree_remove (p_stp_info->vlan_tree, (void *)p_vlan_node) !=
                p_vlan_node) {
            SAI_STP_LOG_ERR ("Vlan node remove failed 0x%"PRIx64"", vlan_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        sai_stp_vlan_node_free (p_vlan_node);

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_attribute_set (sai_object_id_t stp_inst_id,
                                    const sai_attribute_t *attr)
{
    sai_status_t error = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;

    STD_ASSERT (attr != NULL);

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Attribute set for stp_inst_id 0x%"PRIx64"", stp_inst_id);

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                                  stp_info_tree, (void *)&stp_inst_id);
        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        switch (attr->id)
        {
            case SAI_STP_ATTR_VLAN_LIST:
                SAI_STP_LOG_ERR ("Read-only attribute is set for stp instance 0x%"PRIx64"",
                                  stp_inst_id);
                error = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;
            default:
                error = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
                break;
        }

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_attribute_get (sai_object_id_t stp_inst_id,
                                    uint32_t attr_count,
                                    sai_attribute_t *attr_list)
{
    sai_status_t       error      = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;

    STD_ASSERT (attr_list != NULL);
    STD_ASSERT (attr_count);

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Attribute get for stp_inst_id 0x%"PRIx64"", stp_inst_id);

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                stp_info_tree, (void *)&stp_inst_id);

        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        error = sai_stp_npu_api_get()->attribute_get (stp_inst_id, attr_count,
                                                      attr_list);

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_port_state_set (sai_object_id_t stp_inst_id,
                                     sai_object_id_t port_id,
                                     sai_port_stp_state_t port_state)
{
    sai_status_t       error      = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("STP port state %d set for port 0x%"PRIx64" in stp_inst_id 0x%"PRIx64"",
                        port_state, port_id, stp_inst_id);

    do {
        if(!(sai_is_port_valid(port_id))) {
            SAI_STP_LOG_ERR("Port id 0x%"PRIx64" is not valid", port_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                stp_info_tree, (void *)&stp_inst_id);

        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        error = sai_stp_npu_api_get()->port_state_set (stp_inst_id, port_id, port_state);
        if (error != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP port state set failed for port 0x%"PRIx64"", port_id);
            break;
        }

    } while (0);

    sai_stp_unlock();
    return error;
}

sai_status_t sai_stp_port_state_get (sai_object_id_t stp_inst_id,
                                     sai_object_id_t port_id,
                                     sai_port_stp_state_t *port_state)
{
    sai_status_t       error      = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;

    STD_ASSERT (port_state != NULL);

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("STP port state get for stp_inst_id 0x%"PRIx64"", stp_inst_id);

    do {
        if(!(sai_is_port_valid(port_id))) {
            SAI_STP_LOG_ERR("Port id 0x%"PRIx64" is not valid", port_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
                stp_info_tree, (void *)&stp_inst_id);

        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        error = sai_stp_npu_api_get()->port_state_get (stp_inst_id, port_id,
                                                       port_state);
        if (error != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP port state get failed for port 0x%"PRIx64"", port_id);
            break;
        }

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_vlan_destroy (sai_vlan_id_t vlan_id)
{
    sai_status_t ret = SAI_STATUS_SUCCESS;

    /* Remove the STP instance associated to a vlan during its removal */
    ret = sai_stp_vlan_remove (g_stp_vlan_map[vlan_id], vlan_id);

    if (ret != SAI_STATUS_SUCCESS) {
        SAI_STP_LOG_ERR ("Vlan %u removal from associated stp failed", vlan_id);
        return ret;
    }

    g_stp_vlan_map[vlan_id] = 0;

    return ret;
}

sai_status_t sai_stp_vlan_handle (sai_vlan_id_t vlan_id, sai_object_id_t stp_inst_id)
{
    sai_status_t ret = SAI_STATUS_SUCCESS;

    if (!(sai_stp_is_valid_stp_instance (stp_inst_id))) {
        return SAI_STATUS_INVALID_OBJECT_ID;
    }


    do {

        ret = sai_stp_vlan_remove (g_stp_vlan_map[vlan_id], vlan_id);

        if (ret != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("Vlan %u removal to old stp 0x%"PRIx64" failed", vlan_id,
                              g_stp_vlan_map[vlan_id]);
            break;
        }

        ret = sai_stp_vlan_add (stp_inst_id, vlan_id);

        if (ret != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("Vlan %u addition to new stp 0x%"PRIx64" failed", vlan_id, stp_inst_id);
            sai_stp_vlan_add (g_stp_vlan_map[vlan_id], vlan_id);
            break;
        }

        g_stp_vlan_map[vlan_id] = stp_inst_id;

    } while (0);

    return ret;
}

sai_status_t sai_stp_default_stp_vlan_update (sai_vlan_id_t vlan_id)
{
    sai_status_t ret = SAI_STATUS_SUCCESS;

    if (g_stp_vlan_map[vlan_id] != 0) {
        ret = sai_stp_vlan_remove (g_stp_vlan_map[vlan_id], vlan_id);

        if (ret != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("Vlan %u removal from stp failed", vlan_id);
            return ret;
        }
        g_stp_vlan_map[vlan_id] = 0;
    }

    ret = sai_stp_vlan_add (g_def_stp_id, vlan_id);

    if (ret != SAI_STATUS_SUCCESS) {
        SAI_STP_LOG_ERR ("Vlan %u addition to default stp failed", vlan_id);
        return ret;
    }
    g_stp_vlan_map[vlan_id] = g_def_stp_id;

    return ret;
}

static sai_status_t sai_internal_instance_update_cache (sai_object_id_t stp_inst_id,
                                                 sai_vlan_id_t vlan_id,
                                                 bool add_vlan_to_npu)
{
    dn_sai_stp_info_t *p_stp_info  = NULL;
    sai_status_t       error       = SAI_STATUS_SUCCESS;
    sai_vlan_id_t     *p_vlan_node = NULL;

    sai_stp_lock();

    do {
        if (!stp_info_tree) {
            SAI_STP_LOG_ERR ("STP initialization not done");
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info = sai_stp_info_node_alloc ();
        if (!(p_stp_info)) {
            SAI_STP_LOG_ERR ("Memory allocation failed");
            error = SAI_STATUS_NO_MEMORY;
            break;
        }

        p_stp_info->stp_inst_id = stp_inst_id;

        if (std_rbtree_insert (stp_info_tree, (void *) p_stp_info) != STD_ERR_OK) {
            SAI_STP_LOG_ERR ("Node insertion failed for session 0x%"PRIx64"",
                              p_stp_info->stp_inst_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info->vlan_tree = std_rbtree_create_simple ("vlan_tree",
                0, sizeof(sai_vlan_id_t));

        if (p_stp_info->vlan_tree == NULL) {
            SAI_STP_LOG_ERR ("Vlan tree creations failed for session 0x%"PRIx64"",
                    p_stp_info->stp_inst_id);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_vlan_node = sai_stp_vlan_node_alloc();
        if (p_vlan_node == NULL) {
            SAI_STP_LOG_ERR ("Memory allocation failed for vlan 0x%"PRIx64"",
                    vlan_id);
            std_rbtree_destroy (p_stp_info->vlan_tree);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            error = SAI_STATUS_NO_MEMORY;
            break;
        }

        *p_vlan_node = vlan_id;

        if (add_vlan_to_npu) {
            if ((error = sai_stp_npu_api_get()->vlan_add (stp_inst_id, vlan_id))
                 != SAI_STATUS_SUCCESS) {
                SAI_STP_LOG_ERR ("Vlan addition failed for port 0x%"PRIx64"",
                        vlan_id);
                std_rbtree_destroy (p_stp_info->vlan_tree);
                std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
                break;
            }
        }

        if (std_rbtree_insert(p_stp_info->vlan_tree, (void *)p_vlan_node) != STD_ERR_OK) {
            if (add_vlan_to_npu) {
                sai_stp_npu_api_get()->vlan_remove (p_stp_info->stp_inst_id, vlan_id);
            }
            std_rbtree_destroy (p_stp_info->vlan_tree);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            error = SAI_STATUS_FAILURE;
            break;
        }

        g_stp_vlan_map[vlan_id] = stp_inst_id;

    } while (0);

    if ((p_vlan_node) && (error != SAI_STATUS_SUCCESS)) {
        sai_stp_vlan_node_free (p_vlan_node);
    }

    if ((p_stp_info) && (error != SAI_STATUS_SUCCESS)) {
        sai_stp_info_node_free ((dn_sai_stp_info_t *)p_stp_info);
    }

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_init (void)
{
    sai_status_t        ret         = SAI_STATUS_SUCCESS;
    sai_npu_object_id_t def_stp_id  = 0;
    sai_object_id_t     l3_stp_inst = 0;
    sai_npu_object_id_t l3_stp_id   = 0;

    do {
        sai_stp_mutex_lock_init ();

        stp_info_tree = std_rbtree_create_simple ("stp_info_tree",
                        STD_STR_OFFSET_OF(dn_sai_stp_info_t, stp_inst_id),
                        STD_STR_SIZE_OF(dn_sai_stp_info_t, stp_inst_id));

        if (stp_info_tree == NULL) {
            SAI_STP_LOG_ERR ("STP instances tree create failed");
            ret = SAI_STATUS_FAILURE;
            break;
        }

        ret = sai_stp_npu_api_get()->stp_init(&def_stp_id, &l3_stp_id);

        if (ret != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP NPU Initialization failed %d", ret);
            break;
        }

        g_def_stp_id = sai_uoid_create (SAI_OBJECT_TYPE_STP, def_stp_id);

        ret = sai_internal_instance_update_cache (g_def_stp_id, SAI_INIT_DEFAULT_VLAN,
                                                  false);
        if (ret != SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP default instance to cache failed %d", ret);
            break;
        }

        if (sai_is_internal_vlan_id_initialized() == true) {
            l3_stp_inst = sai_uoid_create (SAI_OBJECT_TYPE_STP, l3_stp_id);
            ret = sai_internal_instance_update_cache (l3_stp_inst, sai_internal_vlan_id_get(), true);
            if (ret != SAI_STATUS_SUCCESS) {
                SAI_STP_LOG_ERR ("STP l3 instance to cache failed %d", ret);
                break;
            }
        }

    } while (0);

    if ((ret != SAI_STATUS_SUCCESS) && (stp_info_tree)) {
        std_rbtree_destroy (stp_info_tree);
    }

    return ret;
}

sai_status_t sai_stp_vlan_stp_get (sai_vlan_id_t vlan_id,
                                   sai_object_id_t *p_stp_id)
{
    sai_status_t        ret           = SAI_STATUS_SUCCESS;
    sai_npu_object_id_t npu_object_id = 0;

    STD_ASSERT (p_stp_id != NULL);

    ret = sai_stp_npu_api_get()->vlan_stp_get (vlan_id, &npu_object_id);

    if (ret != SAI_STATUS_SUCCESS) {
        SAI_STP_LOG_ERR ("STP instance get on vlan %u failed", vlan_id);
        return ret;
    }

    *p_stp_id = sai_uoid_create (SAI_OBJECT_TYPE_STP, npu_object_id);

    return ret;
}

sai_status_t sai_l2_stp_default_instance_id_get (sai_attribute_t *attr)
{
    sai_status_t    ret        = SAI_STATUS_SUCCESS;
    sai_object_id_t def_stp_id = 0;

    STD_ASSERT (attr != NULL);

    ret = sai_stp_npu_api_get()->default_instance_get (&def_stp_id);

    if (ret != SAI_STATUS_SUCCESS) {
        SAI_STP_LOG_ERR ("Default stp instance get failed %d", ret);
        return ret;
    }

    attr->value.oid = sai_uoid_create (SAI_OBJECT_TYPE_STP, def_stp_id);

    return ret;
}

static sai_stp_api_t sai_stp_method_table =
{
    sai_stp_instance_create,
    sai_stp_instance_remove,
    sai_stp_attribute_set,
    sai_stp_attribute_get,
    sai_stp_port_state_set,
    sai_stp_port_state_get,
};

sai_stp_api_t* sai_stp_api_query(void)
{
    return (&sai_stp_method_table);
}

void sai_stp_dump (void)
{
    dn_sai_stp_info_t *p_stp_info = NULL;
    dn_sai_stp_info_t tmp_stp_info;
    sai_vlan_id_t *p_vlan_node = NULL;
    for (p_stp_info = std_rbtree_getfirst(stp_info_tree); p_stp_info != NULL;
            p_stp_info = std_rbtree_getnext (stp_info_tree, p_stp_info))
    {
        memcpy (&tmp_stp_info, p_stp_info, sizeof(dn_sai_stp_info_t));
        SAI_DEBUG ("STP instance 0x%"PRIx64"", p_stp_info->stp_inst_id);
        for (p_vlan_node = std_rbtree_getfirst(p_stp_info->vlan_tree); p_vlan_node != NULL;
                p_vlan_node = std_rbtree_getnext (p_stp_info->vlan_tree, (void*)p_vlan_node))
        {
            SAI_DEBUG ("Vlan %u is  associated to stp instance 0x%"PRIx64"", *p_vlan_node,
                        p_stp_info->stp_inst_id);
        }
    }
}

