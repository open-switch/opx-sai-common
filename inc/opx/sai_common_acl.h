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
 * @file sai_common_acl.h
 *
 * @brief This file contains SAI ACL common definitions and prototypes
 *
 */

#ifndef _SAI_COMMON_ACL_H
#define _SAI_COMMON_ACL_H

#include "sai_acl_type_defs.h"
#include "saistatus.h"
#include "std_type_defs.h"
#include "std_rbtree.h"
#include "std_struct_utils.h"
#include "std_assert.h"
#include "string.h"

#define SAI_ACL_TABLE_ATTR_START SAI_ACL_TABLE_ATTR_STAGE
#define SAI_ACL_TABLE_ATTR_END SAI_ACL_TABLE_ATTR_FIELD_END

#define SAI_ACL_RULE_ATTR_START SAI_ACL_ENTRY_ATTR_TABLE_ID
#define SAI_ACL_RULE_ATTR_FIELD_START SAI_ACL_ENTRY_ATTR_FIELD_START
#define SAI_ACL_RULE_ATTR_ACTION_START SAI_ACL_ENTRY_ATTR_ACTION_START
#define SAI_ACL_RULE_ATTR_FIELD_END SAI_ACL_ENTRY_ATTR_FIELD_END
#define SAI_ACL_RULE_ATTR_ACTION_END SAI_ACL_ENTRY_ATTR_ACTION_END
#define SAI_ACL_RULE_ATTR_END SAI_ACL_RULE_ATTR_ACTION_END

#define SAI_ACL_CNTR_ATTR_START SAI_ACL_COUNTER_ATTR_TABLE_ID
#define SAI_ACL_CNTR_ATTR_END SAI_ACL_COUNTER_ATTR_BYTES

#define SAI_ACL_DEFAULT_SET_ATTR_COUNT (1)

#define SAI_ACL_RULE_DLL_GLUE_OFFSET \
         STD_STR_OFFSET_OF(sai_acl_rule_t, acl_rule_priority)

#define SAI_ACL_RULE_DLL_GLUE_SIZE \
         STD_STR_SIZE_OF(sai_acl_rule_t, acl_rule_priority)

#define SAI_ACL_COUNTER_NUM_PACKETS_AND_BYTES (2)
#define SAI_ACL_COUNTER_NUM_PACKETS_OR_BYTES (1)

/**************************************************************************
 *                     Static Inline Functions
 **************************************************************************/

static inline bool sai_acl_valid_stage(sai_acl_stage_t stage)
{
    if((stage == SAI_ACL_STAGE_INGRESS) ||
       (stage == SAI_ACL_STAGE_EGRESS) ||
       (stage == SAI_ACL_STAGE_SUBSTAGE_INGRESS_PRE_L2) ||
       (stage == SAI_ACL_STAGE_SUBSTAGE_INGRESS_POST_L3)) {
        return true;
    }
    return false;
}

static inline bool sai_acl_table_valid_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_TABLE_ATTR_START) &&
        (id <= SAI_ACL_TABLE_ATTR_END)) {
        return true;
    }
    return false;
}

static inline bool sai_acl_table_field_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_TABLE_ATTR_FIELD_START) &&
        (id <= SAI_ACL_TABLE_ATTR_FIELD_END)) {
        return true;
    }
    return false;
}

static inline sai_acl_table_t *sai_acl_table_find(rbtree_handle table_tree,
                                                  sai_object_id_t table_id)
{
    sai_acl_table_t tmp_acl_table;

    memset (&tmp_acl_table, 0, sizeof(sai_acl_table_t));
    tmp_acl_table.table_key.acl_table_id = table_id;

    STD_ASSERT(table_tree != NULL);
    return ((sai_acl_table_t *)std_rbtree_getexact(table_tree,
                                                   &tmp_acl_table));
}

static inline sai_acl_counter_t *sai_acl_cntr_find(rbtree_handle counter_tree,
                                     sai_object_id_t counter_id)
{
    sai_acl_counter_t tmp_acl_cntr;

    memset (&tmp_acl_cntr, 0, sizeof(sai_acl_counter_t));
    tmp_acl_cntr.counter_key.counter_id = counter_id;

    STD_ASSERT(counter_tree != NULL);
    return ((sai_acl_counter_t *)std_rbtree_getexact(counter_tree,
                                                     &tmp_acl_cntr));
}

static inline bool sai_acl_rule_valid_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_RULE_ATTR_START) &&
        (id <= SAI_ACL_RULE_ATTR_END)) {
        return true;
    }
    return false;
}

static inline bool sai_acl_rule_field_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_RULE_ATTR_FIELD_START) &&
        (id <= SAI_ACL_RULE_ATTR_FIELD_END)) {
        return true;
    }
    return false;
}

static inline bool sai_acl_rule_action_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_RULE_ATTR_ACTION_START) &&
        (id <= SAI_ACL_RULE_ATTR_ACTION_END)) {
        return true;
    }
    return false;
}

static inline bool sai_acl_cntr_valid_attr_range(sai_attr_id_t id)
{
    if ((id >= SAI_ACL_CNTR_ATTR_START) &&
        (id <= SAI_ACL_CNTR_ATTR_END)) {
        return true;
    }
    return false;
}

/**************************************************************************
 *                     Function Prototypes
 **************************************************************************/

acl_node_pt sai_acl_get_acl_node(void);
sai_acl_table_id_node_t *sai_acl_get_table_id_gen(void);
sai_status_t sai_attach_cntr_to_acl_rule(sai_acl_rule_t *acl_rule);
sai_status_t sai_detach_cntr_from_acl_rule(sai_acl_rule_t *acl_rule);
sai_status_t sai_create_acl_table(sai_object_id_t *acl_table_id,
                                  uint32_t attr_count,
                                  const sai_attribute_t *attr_list);
sai_status_t sai_delete_acl_table(sai_object_id_t table_id);
sai_status_t sai_get_acl_table(sai_object_id_t table_id,
                               uint32_t attr_count,
                               sai_attribute_t *attr_list);
sai_status_t sai_set_acl_table(sai_object_id_t table_id,
                               const sai_attribute_t *attr);
sai_status_t sai_create_acl_rule(sai_object_id_t *acl_rule_id,
                                 uint32_t attr_count,
                                 const sai_attribute_t *attr_list);
sai_status_t sai_delete_acl_rule(sai_object_id_t acl_id);
sai_status_t sai_set_acl_rule(sai_object_id_t acl_id,
                              const sai_attribute_t *attr);
sai_status_t sai_get_acl_rule(sai_object_id_t acl_id,
                              uint32_t attr_count,
                              sai_attribute_t *attr_list);
sai_status_t sai_create_acl_counter(sai_object_id_t *acl_counter_id,
                                    uint32_t attr_count,
                                    const sai_attribute_t *attr_list);
sai_status_t sai_delete_acl_counter(sai_object_id_t acl_counter_id);
sai_status_t sai_set_acl_cntr(sai_object_id_t acl_counter_id,
                              const sai_attribute_t *attr);
sai_status_t sai_get_acl_cntr(sai_object_id_t acl_counter_id,
                              uint32_t attr_count,
                              sai_attribute_t *attr_list);
sai_status_t  sai_acl_rule_policer_update(sai_acl_rule_t *acl_rule_modify,
                                          sai_acl_rule_t *acl_rule_present);
sai_status_t sai_attach_policer_to_acl_rule(sai_acl_rule_t *acl_rule);
sai_status_t sai_detach_policer_from_acl_rule(sai_acl_rule_t *acl_rule);

void sai_acl_lock(void);
void sai_acl_unlock(void);

#endif  /* _SAI_COMMON_ACL_H */
