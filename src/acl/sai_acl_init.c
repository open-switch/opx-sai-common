/************************************************************************
 * * LEGALESE:   "Copyright (c) 2015, Dell Inc. All rights reserved."
 * *
 * * This source code is confidential, proprietary, and contains trade
 * * secrets that are the sole property of Dell Inc.
 * * Copy and/or distribution of this source code or disassembly or reverse
 * * engineering of the resultant object code are strictly forbidden without
 * * the written consent of Dell Inc.
 * *
 * ************************************************************************/
/**
 * * @file sai_acl_init.c
 * *
 * * @brief This file contains functions to initialize SAI ACL
 * *        data structures.
 * *
 * *************************************************************************/
#include "sai_acl_utils.h"
#include "sai_common_acl.h"
#include "saistatus.h"
#include "sai_modules_init.h"
#include "sai_acl_type_defs.h"
#include "std_rbtree.h"
#include "std_struct_utils.h"
#include "std_type_defs.h"
#include "std_assert.h"
#include "std_mutex_lock.h"
#include <stdlib.h>
#include <string.h>

static acl_node_pt acl_node = NULL;
static sai_acl_table_id_node_t sai_acl_table_id_generator[SAI_ACL_TABLE_ID_MAX];
static std_mutex_lock_create_static_init_fast(acl_lock);
static sai_acl_api_t sai_acl_method_table =
{
    sai_create_acl_table,
    sai_delete_acl_table,
    sai_set_acl_table,
    sai_get_acl_table,
    sai_create_acl_rule,
    sai_delete_acl_rule,
    sai_set_acl_rule,
    sai_get_acl_rule,
    sai_create_acl_counter,
    sai_delete_acl_counter,
    sai_set_acl_cntr,
    sai_get_acl_cntr
};

/**************************************************************************
 *                          ACCESSOR FUNCTION
 **************************************************************************/

sai_acl_api_t* sai_acl_api_query(void)
{
    return (&sai_acl_method_table);
}

acl_node_pt sai_acl_get_acl_node(void)
{
    STD_ASSERT(acl_node != NULL);

    return acl_node;
}

sai_acl_table_id_node_t *sai_acl_get_table_id_gen(void)
{
    return sai_acl_table_id_generator;
}

void sai_acl_lock(void)
{
    std_mutex_lock (&acl_lock);
}

void sai_acl_unlock(void)
{
    std_mutex_unlock (&acl_lock);
}

static void sai_acl_table_id_generate(void)
{
    uint_t table_idx = 0;

    memset(&sai_acl_table_id_generator, 0,
           sizeof(sai_acl_table_id_node_t)*SAI_ACL_TABLE_ID_MAX);

    for (table_idx = 0; table_idx < SAI_ACL_TABLE_ID_MAX; table_idx++) {
        sai_acl_table_id_generator[table_idx].table_in_use = false;
        sai_acl_table_id_generator[table_idx].table_id = table_idx + 1;
    }
}

static void sai_acl_node_free(acl_node_pt acl_node)
{
    STD_ASSERT(acl_node != NULL);
    if (acl_node->sai_acl_table_tree) {
            std_rbtree_destroy(acl_node->sai_acl_table_tree);
    }

    if (acl_node->sai_acl_rule_tree) {
        std_rbtree_destroy(acl_node->sai_acl_rule_tree);
    }

    if (acl_node->sai_acl_counter_tree) {
        std_rbtree_destroy(acl_node->sai_acl_counter_tree);
    }

    free(acl_node);
}

static acl_node_pt sai_acl_node_alloc(void)
{
    return ((acl_node_pt)calloc(1,sizeof(sai_acl_node_t)));
}

sai_status_t sai_acl_init(void)
{
    sai_status_t rc = SAI_STATUS_SUCCESS;
    acl_node = sai_acl_node_alloc();

    if (acl_node == NULL) {
        SAI_ACL_LOG_CRIT ("Allocation of Memory failed for "
                         "ACL Node during ACL Init");
        return SAI_STATUS_NO_MEMORY;
    }

    do {
        acl_node->sai_acl_table_tree = std_rbtree_create_simple(
                        "acl_table_tree",
                        STD_STR_OFFSET_OF(sai_acl_table_t, table_key),
                        STD_STR_SIZE_OF(sai_acl_table_t, table_key));

        if (acl_node->sai_acl_table_tree == NULL) {
            SAI_ACL_LOG_CRIT ("Creation of ACL Table tree node failed");
            rc = SAI_STATUS_UNINITIALIZED;
            break;
        }

        acl_node->sai_acl_rule_tree = std_rbtree_create_simple(
                                     "acl_rule_tree",
                                     STD_STR_OFFSET_OF(sai_acl_rule_t, rule_key),
                                     STD_STR_SIZE_OF(sai_acl_rule_t, rule_key));

        if (acl_node->sai_acl_rule_tree == NULL) {
            SAI_ACL_LOG_CRIT ("Creation of ACL Rule tree node failed");
            rc = SAI_STATUS_UNINITIALIZED;
            break;
        }

        acl_node->sai_acl_counter_tree = std_rbtree_create_simple(
                             "acl_counter_tree",
                              STD_STR_OFFSET_OF(sai_acl_counter_t, counter_key),
                              STD_STR_SIZE_OF(sai_acl_counter_t, counter_key));

        if (acl_node->sai_acl_counter_tree == NULL) {
            SAI_ACL_LOG_CRIT ("Creation of ACL Counter tree node failed");
            rc = SAI_STATUS_UNINITIALIZED;
            break;
        }

        sai_acl_table_id_generate();

    } while(0);

    if (rc != SAI_STATUS_SUCCESS) {
        sai_acl_node_free(acl_node);
    }

    SAI_ACL_LOG_INFO ("SAI ACL Data structures Init complete");

    return rc;
}

