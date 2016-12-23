/*
 * filename:
"src/unit_test/sai_lag_unit_test_internal.cpp
 * (c) Copyright 2015 Dell Inc. All Rights Reserved.
 */

/*
 * sai_lag_unit_test_internal.cpp
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gtest/gtest.h"
#include <inttypes.h>

extern "C" {
#include "sai.h"
#include "sailag.h"
#include "saitypes.h"
#include "sai_lag_callback.h"
#include "sai_l2_unit_test_defs.h"
#include "sai_lag_unit_test.h"
}

static sai_object_id_t     g_lag_id;
static sai_object_id_t     g_rif_id;
static sai_object_list_t   g_port_list;
static sai_object_id_t     g_ports [SAI_MAX_PORTS];
static sai_lag_operation_t g_lag_operation;

static sai_status_t sai_lag_ut_rif_callback (sai_object_id_t          lag_id,
                                             sai_object_id_t          rif_id,
                                             const sai_object_list_t *port_list,
                                             sai_lag_operation_t      operation)
{
    if (port_list->count > (sizeof (g_ports) / sizeof (sai_object_id_t))) {
        return SAI_STATUS_BUFFER_OVERFLOW;
    }

    memset (g_ports, 0, sizeof (g_ports));
    g_port_list.list  = g_ports;

    g_lag_id          = lag_id;
    g_rif_id          = rif_id;
    g_lag_operation   = operation;
    g_port_list.count = port_list->count;
    memcpy (g_port_list.list, port_list->list,
            port_list->count * sizeof (sai_object_id_t));

    return SAI_STATUS_SUCCESS;
}

static sai_status_t
sai_lag_ut_validate_rif_resp (sai_object_id_t     lag_id,
                              sai_object_id_t     rif_id,
                              sai_object_id_t     port_id,
                              sai_lag_operation_t operation)
{
    uint32_t index;

    if (lag_id != g_lag_id) {
        return SAI_STATUS_FAILURE;
    }

    if (rif_id != g_rif_id) {
        return SAI_STATUS_FAILURE;
    }

    if (operation != g_lag_operation) {
        return SAI_STATUS_FAILURE;
    }

    if (lag_id != g_lag_id) {
        return SAI_STATUS_FAILURE;
    }

    for (index = 0; index < g_port_list.count; index++) {
        if (port_id == g_port_list.list [index]) {
            return SAI_STATUS_SUCCESS;
        }
    }

    return SAI_STATUS_FAILURE;
}

TEST_F(lagInit, rif_callback)
{
    sai_object_id_t lag_id_1 = 0;
    sai_object_id_t member_id_1 = 0;
    sai_object_id_t member_id_2 = 0;
    sai_object_id_t vrf_id;
    sai_object_id_t rif_id;

    sai_lag_rif_callback_register (sai_lag_ut_rif_callback);

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag (&lag_id_1, 0, NULL));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_create_virtual_router (sai_vrf_api_table, &vrf_id));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_create_router_interface (sai_router_intf_api_table,
                                                   sai_vlan_api_table,
                                                   &rif_id, vrf_id, lag_id_1));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_add_port_to_lag (sai_lag_api_table, &member_id_1,
                                           lag_id_1, port_id_1,
                                           true, true, false, false));
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_validate_rif_resp(lag_id_1, rif_id,
                                            port_id_1, SAI_LAG_OPER_ADD_PORTS));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_add_port_to_lag (sai_lag_api_table, &member_id_2,
                                           lag_id_1, port_id_2,
                                           false, false, true, true));
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_validate_rif_resp(lag_id_1, rif_id,
                                            port_id_2, SAI_LAG_OPER_ADD_PORTS));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_remove_port_from_lag (sai_lag_api_table,
                                                member_id_1));
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_validate_rif_resp(lag_id_1, rif_id,
                                            port_id_1, SAI_LAG_OPER_DEL_PORTS));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_remove_port_from_lag(sai_lag_api_table, member_id_2));
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_validate_rif_resp(lag_id_1, rif_id,
                                            port_id_2, SAI_LAG_OPER_DEL_PORTS));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_remove_router_interface (sai_router_intf_api_table,
                                                   rif_id));

    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_ut_remove_virtual_router (sai_vrf_api_table, vrf_id));
}

