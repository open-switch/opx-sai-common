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
 * @file sai_fdb_unit_test.h
 */

#ifndef __SAI_FDB_UNIT_TEST_H__
#define __SAI_FDB_UNIT_TEST_H__

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gtest/gtest.h"
#include "inttypes.h"

extern "C" {
#include "sai.h"
#include "saifdb.h"
#include "saitypes.h"
#include "saiswitch.h"
#include "sailag.h"
}

#define SAI_MAX_PORTS  256
#define SAI_MAX_FDB_ATTRIBUTES 3

extern uint32_t port_count;
extern sai_object_id_t port_list[];;

void sai_fdb_port_evt_callback (uint32_t count,
                                          sai_port_event_notification_t *data);

static inline void sai_set_test_fdb_entry(sai_fdb_entry_t* fdb_entry)
{
    memset(fdb_entry,0, sizeof(sai_fdb_entry_t));
    fdb_entry->mac_address[5] = 0xa;
    fdb_entry->vlan_id = SAI_GTEST_VLAN;
}

static inline void sai_set_test_fdb_lag_entry(sai_fdb_entry_t* fdb_entry)
{
    memset(fdb_entry,0, sizeof(sai_fdb_entry_t));
    fdb_entry->mac_address[5] = 0xb;
    fdb_entry->vlan_id = SAI_GTEST_VLAN;
}
/*
 * API query is done while running the first test case and
 * the method table is stored in sai_fdb_api_table so
 * that its available for the rest of the test cases which
 * use the method table
 */
class fdbInit : public ::testing::Test
{
    public:
        void sai_fdb_entry_create(sai_fdb_entry_type_t type, sai_object_id_t port_id,
                                    sai_packet_action_t action);
        void sai_fdb_create_registered_entry (sai_fdb_entry_t entry,
                                                sai_fdb_entry_type_t type,
                                                sai_object_id_t port_id,
                                                sai_packet_action_t action);
    protected:
        static void SetUpTestCase()
        {
            sai_switch_api_t *p_sai_switch_api_tbl = NULL;
            sai_switch_notification_t notification;
            memset (&notification, 0, sizeof(sai_switch_notification_t));

            /*
             * Query and populate the SAI Switch API Table.
             */
            EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
                       (SAI_API_SWITCH, (static_cast<void**>
                                         (static_cast<void*>(&p_sai_switch_api_tbl)))));

            ASSERT_TRUE (p_sai_switch_api_tbl != NULL);

            /*
             * Switch Initialization.
             * Fill in notification callback routines with stubs.
             */
            notification.on_switch_state_change = sai_switch_operstate_callback;
            notification.on_fdb_event = sai_fdb_evt_callback;
            notification.on_port_state_change = sai_port_state_evt_callback;
            notification.on_switch_shutdown_request = sai_switch_shutdown_callback;
            notification.on_port_event = sai_fdb_port_evt_callback;
            notification.on_packet_event = sai_packet_event_callback;

            ASSERT_TRUE(p_sai_switch_api_tbl->initialize_switch != NULL);

            EXPECT_EQ (SAI_STATUS_SUCCESS,
                       (p_sai_switch_api_tbl->initialize_switch (0, NULL, NULL,
                                                                 &notification)));

            ASSERT_EQ(NULL,sai_api_query(SAI_API_FDB,
                                         (static_cast<void**>(static_cast<void*>(&sai_fdb_api_table)))));

            ASSERT_TRUE(sai_fdb_api_table != NULL);


            EXPECT_TRUE(sai_fdb_api_table->create_fdb_entry != NULL);
            EXPECT_TRUE(sai_fdb_api_table->remove_fdb_entry != NULL);
            EXPECT_TRUE(sai_fdb_api_table->get_fdb_entry_attribute != NULL);
            EXPECT_TRUE(sai_fdb_api_table->set_fdb_entry_attribute != NULL);
            EXPECT_TRUE(sai_fdb_api_table->flush_fdb_entries != NULL);

            ASSERT_EQ(NULL,sai_api_query(SAI_API_LAG,
                                         (static_cast<void**>(static_cast<void*>(&sai_lag_api_table)))));

            ASSERT_TRUE(sai_lag_api_table != NULL);

            EXPECT_TRUE(sai_lag_api_table->create_lag != NULL);
            EXPECT_TRUE(sai_lag_api_table->remove_lag != NULL);
            EXPECT_TRUE(sai_lag_api_table->set_lag_attribute != NULL);
            EXPECT_TRUE(sai_lag_api_table->get_lag_attribute != NULL);


            port_id_1 = port_list[0];
            port_id_2 = port_list[port_count-1];
            port_id_3 = port_list[1];
            port_id_4 = port_list[2];
        }
        static sai_fdb_api_t* sai_fdb_api_table;
        static sai_lag_api_t* sai_lag_api_table;
        static sai_object_id_t  port_id_1;
        static sai_object_id_t  port_id_2;
        static sai_object_id_t  port_id_3;
        static sai_object_id_t  port_id_4;
};

#endif
