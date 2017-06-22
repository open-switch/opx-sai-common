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
static rbtree_handle global_stp_port_tree = NULL;
static sai_object_id_t g_def_stp_id = 0;
static sai_object_id_t g_stp_vlan_map[SAI_MAX_VLAN_TAG_ID+1] = {0};
static dn_sai_id_gen_info_t sai_port_id_gen_info;

rbtree_handle sai_stp_global_info_tree_get(void)
{
    return stp_info_tree;
}

rbtree_handle sai_stp_port_tree_get(void)
{
    return global_stp_port_tree;
}

bool sai_stp_port_id_in_use(uint64_t obj_id)
{
    dn_sai_stp_port_info_t stp_port_node;

    stp_port_node.stp_port_id =
        sai_uoid_create(SAI_OBJECT_TYPE_STP_PORT,obj_id);
    if(std_rbtree_getexact(
                global_stp_port_tree,(void *)&stp_port_node) == NULL) {
        return true;
    } else {
        return false;
    }
}

static sai_object_id_t sai_stp_port_id_create(void)
{
    if(SAI_STATUS_SUCCESS ==
            dn_sai_get_next_free_id(&sai_port_id_gen_info)) {
        return (sai_uoid_create(SAI_OBJECT_TYPE_STP_PORT,
                    sai_port_id_gen_info.cur_id));
    }
    return SAI_NULL_OBJECT_ID;
}

static inline void sai_stp_port_id_gen_init(void)
{
    sai_port_id_gen_info.cur_id = 0;
    sai_port_id_gen_info.is_wrappped = false;
    sai_port_id_gen_info.mask = SAI_UOID_NPU_OBJ_ID_MASK;
    sai_port_id_gen_info.is_id_in_use = sai_stp_port_id_in_use;
}

sai_object_id_t sai_stp_get_instance_from_vlan_map(sai_vlan_id_t vlan_id)
{
     return g_stp_vlan_map[vlan_id];
}

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
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    dn_sai_stp_info_t   *p_stp_info    = NULL;
    sai_status_t         error         = SAI_STATUS_SUCCESS;
    sai_npu_object_id_t  npu_object_id = 0;

    /* STP instance create does not expect any attribute so validation of
     * attr_count and attr_list is not added
     */
    if(attr_count > 0) {
        SAI_STP_LOG_WARN ("Attribute list is not applicable");
        return SAI_STATUS_FAILURE;
    }
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

        p_stp_info->stp_port_tree = std_rbtree_create_simple ("stp_port_tree",
                STD_STR_OFFSET_OF(dn_sai_stp_port_info_t, port_id),
                STD_STR_SIZE_OF(dn_sai_stp_port_info_t, port_id));

        if (p_stp_info->stp_port_tree == NULL) {
            SAI_STP_LOG_ERR ("STP port tree creation failed for STP session"
                    " 0x%"PRIx64"", p_stp_info->stp_inst_id);
            std_rbtree_destroy (p_stp_info->vlan_tree);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            sai_stp_npu_api_get()->instance_remove (p_stp_info->stp_inst_id);
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info->num_ports = 0;

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
            SAI_STP_LOG_ERR ("Vlans are still attached to STP instance 0x%"PRIx64"", stp_inst_id);
            error = SAI_STATUS_OBJECT_IN_USE;
            break;
        }

        /* Check if any stp port obj is still attached to the session */
        if (p_stp_info->num_ports) {
            SAI_STP_LOG_ERR ("Some stp ports object still exists in STP session"
                    " 0x%"PRIx64"", stp_inst_id);
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

        if (p_stp_info->stp_port_tree) {
            std_rbtree_destroy (p_stp_info->stp_port_tree);
        }

        p_stp_info->num_ports = 0;

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
                SAI_STP_LOG_ERR ("Read-only attribute VLAN list is set for stp"
                        " instance 0x%"PRIx64"", stp_inst_id);
                error = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;

            case SAI_STP_ATTR_PORT_LIST:
                SAI_STP_LOG_ERR ("Read-only attribute Port list is set for stp"
                        " instance 0x%"PRIx64"", stp_inst_id);
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

sai_status_t sai_stp_get_port_list(sai_object_id_t stp_inst_id,
        sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;
    dn_sai_stp_port_info_t *p_stp_port_info = NULL;

    p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
            stp_info_tree, (void *)&stp_inst_id);

    do {
        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if(p_stp_info->num_ports > attr->value.objlist.count) {
            attr->value.objlist.count = p_stp_info->num_ports;
            ret_val = SAI_STATUS_BUFFER_OVERFLOW;
            break;
        }

        attr->value.objlist.count = 0;

        if(p_stp_info->num_ports) {
            for (p_stp_port_info = std_rbtree_getfirst(p_stp_info->stp_port_tree);
                    p_stp_port_info != NULL;
                    p_stp_port_info = std_rbtree_getnext(p_stp_info->stp_port_tree,
                        p_stp_port_info))
            {
                attr->value.objlist.list[attr->value.objlist.count] =
                    p_stp_port_info->stp_port_id;
                attr->value.objlist.count++;
            }
        }
    } while(0);

    return ret_val;
}

sai_status_t sai_stp_attribute_get (sai_object_id_t stp_inst_id,
                                    uint32_t attr_count,
                                    sai_attribute_t *attr_list)
{
    sai_status_t       error      = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;
    uint32_t attr_index = 0;

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

        for (attr_index = 0; attr_index < attr_count; ++attr_index)
        {
            switch (attr_list[attr_index].id)
            {
                case SAI_STP_ATTR_VLAN_LIST:
                    error = sai_stp_npu_api_get()->attribute_get (stp_inst_id, attr_count,
                            attr_list);
                    break;

                case SAI_STP_ATTR_PORT_LIST:
                    error = sai_stp_get_port_list(stp_inst_id, &attr_list[attr_index]);
                    break;

                default:
                    error = SAI_STATUS_UNKNOWN_ATTRIBUTE_0 + attr_index;
            }

            if(SAI_STATUS_SUCCESS != error) {
                break;
            }
        }

    } while (0);

    sai_stp_unlock();

    return error;
}

sai_status_t sai_stp_port_create(sai_object_id_t *stp_port_id,
        sai_object_id_t switch_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    bool stp_id_attr_present = false;
    bool port_id_attr_present = false;
    bool port_state_attr_present = false;
    uint32_t attr_idx = 0;
    sai_object_id_t stp_inst_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t port_id = SAI_NULL_OBJECT_ID;
    sai_stp_port_state_t port_state = SAI_STP_PORT_STATE_BLOCKING;
    dn_sai_stp_info_t *p_stp_info = NULL;
    dn_sai_stp_port_info_t *p_stp_port_info = NULL;


    STD_ASSERT (stp_port_id != NULL);
    STD_ASSERT (attr_list != NULL);

    if (attr_count > 0) {
        STD_ASSERT ((attr_list != NULL));
    } else {
        SAI_STP_LOG_ERR("STP port create mandatory attribute missing");
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    for(attr_idx = 0; attr_idx < attr_count; attr_idx++) {
        switch(attr_list[attr_idx].id) {
            case SAI_STP_PORT_ATTR_STP:
                stp_inst_id = attr_list[attr_idx].value.oid;

                if(!sai_is_obj_id_stp_instance(stp_inst_id)) {
                    SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + attr_idx;
                }

                p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact(
                        stp_info_tree, (void *)&stp_inst_id);
                if (p_stp_info == NULL) {
                    SAI_STP_LOG_ERR ("STP instance UOID not found 0x%"PRIx64"",
                            stp_inst_id);
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + attr_idx;
                    break;
                }

                stp_id_attr_present = true;
                break;

            case SAI_STP_PORT_ATTR_PORT:
                port_id = attr_list[attr_idx].value.oid;

                if(!sai_is_obj_id_port(port_id)){
                    SAI_VLAN_LOG_ERR("STP invalid port UOID type 0x%"PRIx64"",
                            port_id);
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + attr_idx;
                }

                if(!sai_is_port_valid(port_id)){
                    SAI_VLAN_LOG_ERR("STP invalid port UOID 0x%"PRIx64"", port_id);
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + attr_idx;
                }

                port_id_attr_present = true;
                break;

            case SAI_STP_PORT_ATTR_STATE:
                port_state = attr_list[attr_idx].value.s32;

                if(!sai_stp_port_state_valid(port_state)) {
                    SAI_STP_LOG_ERR ("STP invalid port state");
                    return SAI_STATUS_INVALID_ATTR_VALUE_0 + attr_idx;
                }

                port_state_attr_present = true;
                break;

            default:
                SAI_STP_LOG_ERR("STP port create unknown attribute");
                return SAI_STATUS_UNKNOWN_ATTRIBUTE_0 + attr_idx;
        }
    }

    if(!(stp_id_attr_present) || !(port_id_attr_present) ||
            !(port_state_attr_present)) {
        SAI_STP_LOG_ERR("STP port create mandatory attribute missing for"
                " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                stp_inst_id, port_id);
        return SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING;
    }

    sai_stp_lock();

    do{
        p_stp_port_info = sai_stp_port_node_alloc();
        if (p_stp_port_info == NULL) {
            SAI_STP_LOG_ERR ("STP port node memory allocation failed"
                    " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                    stp_inst_id, port_id);
            ret_val = SAI_STATUS_NO_MEMORY;
            break;
        }

        p_stp_port_info->port_id = port_id;
        if(std_rbtree_getexact(p_stp_info->stp_port_tree,
                p_stp_port_info) != NULL) {
            SAI_STP_LOG_ERR ("Port 0x%"PRIx64" already associated with STP"
                    " instance 0x%"PRIx64"",port_id,stp_inst_id);
            ret_val = SAI_STATUS_ITEM_ALREADY_EXISTS;
            break;
        } else {
            p_stp_port_info->stp_port_id = sai_stp_port_id_create();
            if(SAI_NULL_OBJECT_ID == p_stp_port_info->stp_port_id) {
                SAI_STP_LOG_ERR ("STP port id create failed for"
                        " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                        stp_inst_id, port_id);
                ret_val = SAI_STATUS_FAILURE;
                break;
            }

            p_stp_port_info->stp_inst_id = stp_inst_id;
            p_stp_port_info->port_state = port_state;

            if(std_rbtree_insert(p_stp_info->stp_port_tree, p_stp_port_info) !=
                    STD_ERR_OK) {
                SAI_STP_LOG_ERR ("STP port tree insert failed"
                        " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                        stp_inst_id, port_id);
                ret_val = SAI_STATUS_FAILURE;
                break;
            }
            p_stp_info->num_ports++;

            if(std_rbtree_insert(global_stp_port_tree, p_stp_port_info) !=
                    STD_ERR_OK) {
                std_rbtree_remove(p_stp_info->stp_port_tree, p_stp_port_info);
                p_stp_info->num_ports--;
                SAI_STP_LOG_ERR ("STP port global tree insert failed"
                        " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                        stp_inst_id, port_id);
                ret_val = SAI_STATUS_FAILURE;
                break;
            }

            if((ret_val = sai_stp_npu_api_get()->port_state_set (stp_inst_id,
                        port_id, port_state)) != SAI_STATUS_SUCCESS) {
                std_rbtree_remove(p_stp_info->stp_port_tree, p_stp_port_info);
                p_stp_info->num_ports--;
                std_rbtree_remove(global_stp_port_tree, p_stp_port_info);
                SAI_STP_LOG_ERR ("STP port state set failed for port"
                        " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                        stp_inst_id, port_id);
                break;
            }

        }
    } while(0);

    if(SAI_STATUS_SUCCESS != ret_val) {
        SAI_STP_LOG_ERR("STP port create failed for"
                " STP Inst 0x%"PRIx64" Port 0x%"PRIx64"",
                stp_inst_id, port_id);
        sai_stp_port_node_free(p_stp_port_info);
        p_stp_port_info = NULL;
    } else {
        *stp_port_id = p_stp_port_info->stp_port_id;
    }

    sai_stp_unlock();

    return ret_val;
}

sai_status_t sai_stp_port_remove(sai_object_id_t stp_port_id)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    dn_sai_stp_port_info_t stp_port_info;
    dn_sai_stp_info_t *p_stp_info = NULL;
    dn_sai_stp_port_info_t *p_stp_port_info = NULL;

    if(!sai_is_obj_id_stp_port(stp_port_id)) {
        SAI_STP_LOG_ERR("Invalid STP port object id 0x%"PRIx64" to remove",
                stp_port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_stp_lock();

    do {
        memset(&stp_port_info,0,sizeof(stp_port_info));
        stp_port_info.stp_port_id = stp_port_id;
        if((p_stp_port_info = (dn_sai_stp_port_info_t *)std_rbtree_getexact(
                        global_stp_port_tree,&stp_port_info)) == NULL) {
            SAI_STP_LOG_ERR ("STP port obj 0x%"PRIx64" not found",stp_port_id);
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        if((ret_val = sai_stp_npu_api_get()->port_state_set(
                        p_stp_port_info->stp_inst_id,
                        p_stp_port_info->port_id,
                        SAI_STP_PORT_STATE_BLOCKING)) !=
                SAI_STATUS_SUCCESS) {
            SAI_STP_LOG_ERR ("STP port state default set failed for port"
                    " 0x%"PRIx64" on STP 0x%"PRIx64"",
                    p_stp_port_info->port_id,
                    p_stp_port_info->stp_inst_id);
            break;
        }

        std_rbtree_remove(global_stp_port_tree, p_stp_port_info);

        p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact(
                stp_info_tree, (void *)&p_stp_port_info->stp_inst_id);
        if (p_stp_info == NULL) {
            SAI_STP_LOG_ERR ("STP instance UOID not found 0x%"PRIx64"",
                    p_stp_port_info->stp_inst_id);
            ret_val = SAI_STATUS_FAILURE;
            break;
        } else {
            std_rbtree_remove(p_stp_info->stp_port_tree, p_stp_port_info);
            p_stp_info->num_ports--;
        }
    } while(0);

    if(SAI_STATUS_SUCCESS == ret_val) {
        SAI_STP_LOG_ERR("Failed to remove STP port obj 0x%"PRIx64"",stp_port_id);
        sai_stp_port_node_free(p_stp_port_info);
        p_stp_port_info = NULL;
    }

    sai_stp_unlock();

    return ret_val;
}

sai_status_t sai_stp_port_attribute_set(sai_object_id_t stp_port_id,
        const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    dn_sai_stp_port_info_t stp_port_info;
    dn_sai_stp_port_info_t *p_stp_port_info = NULL;
    sai_stp_port_state_t port_state = SAI_STP_PORT_STATE_BLOCKING;

    STD_ASSERT (attr != NULL);

    if (!sai_is_obj_id_stp_port(stp_port_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP port obj",stp_port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Attribute set for STP port obj 0x%"PRIx64"",stp_port_id);

    do {
        memset(&stp_port_info,0,sizeof(stp_port_info));
        stp_port_info.stp_port_id = stp_port_id;
        if((p_stp_port_info = (dn_sai_stp_port_info_t *)std_rbtree_getexact(
                        global_stp_port_tree,&stp_port_info)) == NULL) {
            SAI_STP_LOG_ERR ("STP port obj 0x%"PRIx64" not found",stp_port_id);
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        switch (attr->id)
        {
            case SAI_STP_PORT_ATTR_STP:
                SAI_STP_LOG_ERR ("Read-only attribute stp is set for stp port"
                        " 0x%"PRIx64"", stp_port_id);
                ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;

            case SAI_STP_PORT_ATTR_PORT:
                SAI_STP_LOG_ERR ("Read-only attribute port is set for stp port"
                        " 0x%"PRIx64"", stp_port_id);
                ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                break;

            case SAI_STP_PORT_ATTR_STATE:
                port_state = attr->value.s32;

                if(!sai_stp_port_state_valid(port_state)) {
                    SAI_STP_LOG_ERR ("STP invalid port state %d is set for stp port"
                            " 0x%"PRIx64"",port_state,stp_port_id);
                    ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
                } else {
                    ret_val = sai_stp_npu_api_get()->port_state_set(
                            p_stp_port_info->stp_inst_id,
                            p_stp_port_info->port_id, attr->value.s32);
                    if (ret_val != SAI_STATUS_SUCCESS) {
                        SAI_STP_LOG_ERR ("STP port state set failed for port"
                                " 0x%"PRIx64"", p_stp_port_info->port_id);
                    } else {
                        p_stp_port_info->port_state = port_state;
                    }
                }
                break;

            default:
                ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
                break;
        }

    } while (0);

    sai_stp_unlock();

    return ret_val;
}

sai_status_t sai_stp_port_attribute_get(sai_object_id_t stp_port_id,
        uint32_t attr_count,
        sai_attribute_t *attr_list)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;
    dn_sai_stp_port_info_t stp_port_info;
    dn_sai_stp_port_info_t *p_stp_port_info = NULL;
    uint32_t attr_index = 0;

    STD_ASSERT(attr_list != NULL);

    if (!sai_is_obj_id_stp_port(stp_port_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP port obj",stp_port_id);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    sai_stp_lock();

    SAI_STP_LOG_TRACE ("Attribute get for STP port obj 0x%"PRIx64"",stp_port_id);

    do {
        memset(&stp_port_info,0,sizeof(stp_port_info));
        stp_port_info.stp_port_id = stp_port_id;
        if((p_stp_port_info = (dn_sai_stp_port_info_t *)std_rbtree_getexact(
                        global_stp_port_tree,&stp_port_info)) == NULL) {
            SAI_STP_LOG_ERR ("STP port id 0x%"PRIx64" info not found in caache",
                    stp_port_id);
            ret_val = SAI_STATUS_ITEM_NOT_FOUND;
            break;
        }

        for(attr_index = 0; attr_index < attr_count; ++attr_index)
        {
            switch(attr_list[attr_index].id)
            {
                case SAI_STP_PORT_ATTR_STP:
                    attr_list[attr_index].value.oid = p_stp_port_info->stp_inst_id;
                    break;

                case SAI_STP_PORT_ATTR_PORT:
                    attr_list[attr_index].value.oid = p_stp_port_info->port_id;
                    break;

                case SAI_STP_PORT_ATTR_STATE:
                    attr_list[attr_index].value.s32 = p_stp_port_info->port_state;
                    break;

                default:
                    ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
                    break;
            }

            if(SAI_STATUS_SUCCESS != ret_val) {
                break;
            }
        }

    } while (0);

    sai_stp_unlock();

    return ret_val;

}

sai_status_t sai_stp_port_bulk_create(
        sai_object_id_t switch_id,
        uint32_t object_count,
        uint32_t *attr_count,
        sai_attribute_t **attrs,
        sai_bulk_op_type_t type,
        sai_object_id_t *object_id,
        sai_status_t *object_statuses)
{
    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t sai_stp_port_bulk_remove(
        uint32_t object_count,
        sai_object_id_t *object_id,
        sai_bulk_op_type_t type,
        sai_status_t *object_statuses)
{
    return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t sai_npu_stp_port_state_get (sai_object_id_t stp_inst_id,
                                         sai_object_id_t port_id,
                                         sai_stp_port_state_t *port_state)
{
    sai_status_t       sai_rc      = SAI_STATUS_SUCCESS;
    dn_sai_stp_info_t *p_stp_info = NULL;

    STD_ASSERT (port_state != NULL);

    if (!sai_is_obj_id_stp_instance (stp_inst_id)) {
        SAI_STP_LOG_ERR ("0x%"PRIx64" is not a valid STP obj", stp_inst_id);

        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }
    SAI_STP_LOG_TRACE ("STP port state get for stp_inst_id 0x%"PRIx64"", stp_inst_id);

    if(!(sai_is_port_valid(port_id))) {
        SAI_STP_LOG_ERR("Port id 0x%"PRIx64" is not valid", port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    p_stp_info = (dn_sai_stp_info_t *) std_rbtree_getexact (
            stp_info_tree, (void *)&stp_inst_id);

    if (p_stp_info == NULL) {
        SAI_STP_LOG_ERR ("STP instance not found 0x%"PRIx64"", stp_inst_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    sai_rc = sai_stp_npu_api_get()->port_state_get (stp_inst_id, port_id,
                                                    port_state);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_STP_LOG_ERR ("STP port state get failed for port 0x%"PRIx64"", port_id);
    }
    return sai_rc;

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

        p_stp_info->stp_port_tree = std_rbtree_create_simple ("stp_port_tree",
                STD_STR_OFFSET_OF(dn_sai_stp_port_info_t, port_id),
                STD_STR_SIZE_OF(dn_sai_stp_port_info_t, port_id));

        if (p_stp_info->stp_port_tree == NULL) {
            SAI_STP_LOG_ERR ("STP port tree creation failed for STP session"
                    " 0x%"PRIx64"", p_stp_info->stp_inst_id);
            std_rbtree_destroy (p_stp_info->vlan_tree);
            std_rbtree_remove (stp_info_tree, (void *) p_stp_info);
            error = SAI_STATUS_FAILURE;
            break;
        }

        p_stp_info->num_ports = 0;

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

        global_stp_port_tree = std_rbtree_create_simple ("global_stp_port_tree",
                        STD_STR_OFFSET_OF(dn_sai_stp_port_info_t, stp_port_id),
                        STD_STR_SIZE_OF(dn_sai_stp_port_info_t, stp_port_id));

        if (global_stp_port_tree == NULL) {
            SAI_STP_LOG_ERR ("STP Port global tree create failed");
            ret = SAI_STATUS_FAILURE;
            break;
        }

        sai_stp_port_id_gen_init();

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

    if (ret != SAI_STATUS_SUCCESS) {
        if(stp_info_tree) {
            std_rbtree_destroy (stp_info_tree);
        }
        if(global_stp_port_tree) {
            std_rbtree_destroy(global_stp_port_tree);
        }
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
    sai_stp_port_create,
    sai_stp_port_remove,
    sai_stp_port_attribute_set,
    sai_stp_port_attribute_get,
    sai_stp_port_bulk_create,
    sai_stp_port_bulk_remove,
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
            SAI_DEBUG ("Vlan %u is associated to stp instance 0x%"PRIx64"", *p_vlan_node,
                        p_stp_info->stp_inst_id);
        }
    }
}
