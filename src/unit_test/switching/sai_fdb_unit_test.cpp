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
 * @file sai_fdb_unit_test.cpp
 */

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
#include "sai_l2_unit_test_defs.h"
#include "sai_fdb_unit_test.h"
}

uint32_t port_count = 0;
sai_object_id_t port_list[SAI_MAX_PORTS] = {0};

void sai_fdb_port_evt_callback (uint32_t count,
                            sai_port_event_notification_t *data)
{
    uint32_t port_idx = 0;
    sai_object_id_t port_id = 0;
    sai_port_event_t port_event;

    for(port_idx = 0; port_idx < count; port_idx++) {
        port_id = data[port_idx].port_id;
        port_event = data[port_idx].port_event;

        if(port_event == SAI_PORT_EVENT_ADD) {
            if(port_count < SAI_MAX_PORTS) {
                port_list[port_count] = port_id;
                port_count++;
            }

            printf("PORT ADD EVENT FOR port 0x%"PRIx64" and total ports count is %d \r\n",
                   port_id, port_count);
        } else if(port_event == SAI_PORT_EVENT_DELETE) {

            printf("PORT DELETE EVENT for  port 0x%"PRIx64" and total ports count is %d \r\n",
                   port_id, port_count);
        } else {
            printf("Invalid PORT EVENT for port 0x%"PRIx64" \r\n", port_id);
        }
    }
}


sai_fdb_api_t* fdbInit ::sai_fdb_api_table = NULL;
sai_lag_api_t* fdbInit ::sai_lag_api_table = NULL;
sai_object_id_t fdbInit ::port_id_1 = 0;
sai_object_id_t fdbInit ::port_id_2 = 0;
sai_object_id_t fdbInit ::port_id_3 = 0;
sai_object_id_t fdbInit ::port_id_4 = 0;

void fdbInit ::sai_fdb_entry_create(sai_fdb_entry_type_t type, sai_object_id_t port_id,
                                    sai_packet_action_t action)
{

    sai_fdb_entry_t fdb_entry;
    sai_attribute_t attr_list[SAI_MAX_FDB_ATTRIBUTES];

    if(sai_object_type_query(port_id) == SAI_OBJECT_TYPE_LAG) {
        sai_set_test_fdb_lag_entry(&fdb_entry);
    } else {
        sai_set_test_fdb_entry(&fdb_entry);
    }
    memset(attr_list,0, sizeof(attr_list));
    attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    attr_list[0].value.s32 = type;

    attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    attr_list[1].value.oid = port_id;

    attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
    attr_list[2].value.s32 = action;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->create_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 (const sai_attribute_t*)attr_list));
}

/*
 * FDB Create and Get FDB Entry
 */
TEST_F(fdbInit, create_fdb_entry)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t attr_list[SAI_MAX_FDB_ATTRIBUTES];

    sai_set_test_fdb_entry(&fdb_entry);
    memset(attr_list,0, sizeof(attr_list));
    attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    attr_list[0].value.s32 = SAI_FDB_ENTRY_TYPE_DYNAMIC;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              sai_fdb_api_table->create_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 1,
                                 (const sai_attribute_t*)attr_list));
    attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    attr_list[1].value.oid = port_id_1;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              sai_fdb_api_table->create_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 2,
                                 (const sai_attribute_t*)attr_list));

    attr_list[0].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
    attr_list[0].value.s32 = SAI_PACKET_ACTION_FORWARD;
    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              sai_fdb_api_table->create_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 2,
                                 (const sai_attribute_t*)attr_list));

    attr_list[1].id = SAI_FDB_ENTRY_ATTR_TYPE;
    attr_list[1].value.s32 = SAI_FDB_ENTRY_TYPE_DYNAMIC;
    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              sai_fdb_api_table->create_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                  2,
                                 (const sai_attribute_t*)attr_list));


    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_STATIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry);
    memset(get_attr_list,0, sizeof(get_attr_list));
    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));

    ASSERT_EQ(SAI_FDB_ENTRY_TYPE_STATIC, get_attr_list[0].value.s32);
    ASSERT_EQ(port_id_1, get_attr_list[1].value.oid);
    ASSERT_EQ(SAI_PACKET_ACTION_FORWARD, get_attr_list[2].value.s32);

}

/*
 * FDB Create and Get FDB Entry
 */
TEST_F(fdbInit, remove_fdb_entry)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];

    sai_set_test_fdb_entry(&fdb_entry);
    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->remove_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry));
    memset(get_attr_list,0, sizeof(get_attr_list));
    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));

}

TEST_F(fdbInit, flush_all_fdb_entries_by_port)
{

    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[2];

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);

    sai_set_test_fdb_entry(&fdb_entry);
    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_PORT_ID;
    flush_attr[0].value.oid = port_id_1;

    flush_attr[1].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[1].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(2,
                                 (const sai_attribute_t*)flush_attr));

    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));
}

TEST_F(fdbInit, flush_all_fdb_entries_by_lag)
{

    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[2];
    sai_object_id_t lag_id;
    sai_object_id_t lag_member_id_1;
    sai_object_id_t lag_member_id_2;
    sai_attribute_t lag_attr[2];

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_lag_api_table->create_lag (&lag_id, 0, NULL));

    memset(lag_attr, 0, sizeof(lag_attr));

    lag_attr[0].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    lag_attr[0].value.oid = lag_id;
    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_3;


    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_1,
                                                     2, lag_attr));

    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_4;
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_2,
                                                     2, lag_attr));

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, lag_id, SAI_PACKET_ACTION_FORWARD);

    sai_set_test_fdb_lag_entry(&fdb_entry);

    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_PORT_ID;
    flush_attr[0].value.oid = lag_id;

    flush_attr[1].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[1].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(2,
                                 (const sai_attribute_t*)flush_attr));

    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));

    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_1));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_2));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag(lag_id));
}

TEST_F(fdbInit, flush_all_fdb_entries_by_vlan)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[2];

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_2, SAI_PACKET_ACTION_FORWARD);

    sai_set_test_fdb_entry(&fdb_entry);
    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_VLAN_ID;
    flush_attr[0].value.u16 =  SAI_GTEST_VLAN;

    flush_attr[1].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[1].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;


    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(2,
                                 (const sai_attribute_t*)flush_attr));
    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));
}

TEST_F(fdbInit, flush_all_fdb_entries_by_port_vlan)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[3];

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry);
    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_VLAN_ID;
    flush_attr[0].value.u16 =  SAI_GTEST_VLAN;

    flush_attr[1].id = SAI_FDB_FLUSH_ATTR_PORT_ID;
    flush_attr[1].value.oid =  port_id_1;

    flush_attr[2].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[2].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;


    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(3,
                                 (const sai_attribute_t*)flush_attr));

    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));
}

TEST_F(fdbInit, flush_all_fdb_entries_by_lag_vlan)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[3];
    sai_object_id_t lag_id;
    sai_object_id_t lag_member_id_1;
    sai_object_id_t lag_member_id_2;
    sai_attribute_t lag_attr[2];

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_lag_api_table->create_lag (&lag_id, 0, NULL));

    memset(lag_attr, 0, sizeof(lag_attr));

    lag_attr[0].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    lag_attr[0].value.oid = lag_id;
    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_3;


    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_1,
                                                     2, lag_attr));

    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_4;
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_2,
                                                     2, lag_attr));

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, lag_id, SAI_PACKET_ACTION_FORWARD);

    sai_set_test_fdb_lag_entry(&fdb_entry);

    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_VLAN_ID;
    flush_attr[0].value.u16 =  SAI_GTEST_VLAN;

    flush_attr[1].id = SAI_FDB_FLUSH_ATTR_PORT_ID;
    flush_attr[1].value.oid =  lag_id;

    flush_attr[2].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[2].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;


    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(3,
                                 (const sai_attribute_t*)flush_attr));

    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));

    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_1));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_2));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag(lag_id));
}

TEST_F(fdbInit, flush_all_fdb_entries)
{
    sai_fdb_entry_t fdb_entry_1;
    sai_fdb_entry_t fdb_entry_2;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[1];
    sai_object_id_t lag_id;
    sai_object_id_t lag_member_id_1;
    sai_object_id_t lag_member_id_2;
    sai_attribute_t lag_attr[2];

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_lag_api_table->create_lag (&lag_id, 0, NULL));

    memset(lag_attr, 0, sizeof(lag_attr));

    lag_attr[0].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    lag_attr[0].value.oid = lag_id;
    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_3;


    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_1,
                                                     2, lag_attr));

    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_4;
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_2,
                                                     2, lag_attr));

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, lag_id, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry_2);
    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry_1);

    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[0].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;


    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->flush_fdb_entries(1,
                                 (const sai_attribute_t*)flush_attr));

    get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
    get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry_1,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));

    ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry_2,
                                 SAI_MAX_FDB_ATTRIBUTES,
                                 get_attr_list));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_1));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_2));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag(lag_id));
}

TEST_F(fdbInit, flush_supported_types)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr_list[SAI_MAX_FDB_ATTRIBUTES];
    sai_attribute_t flush_attr[1];
    sai_status_t ret;

    sai_set_test_fdb_entry(&fdb_entry);

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    memset(get_attr_list,0, sizeof(get_attr_list));
    memset(flush_attr,0, sizeof(flush_attr));

    flush_attr[0].id = SAI_FDB_FLUSH_ATTR_ENTRY_TYPE;
    flush_attr[0].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_DYNAMIC;

    ret = sai_fdb_api_table->flush_fdb_entries(1,
                                 (const sai_attribute_t*)flush_attr);

    if (ret == SAI_STATUS_SUCCESS) {
        printf("Flush by dynamic is supported\r\n");
        get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
        get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
        get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

        ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
                sai_fdb_api_table->get_fdb_entry_attribute(
                    (const sai_fdb_entry_t*)&fdb_entry,
                    SAI_MAX_FDB_ATTRIBUTES,
                    get_attr_list));
    } else if (ret == SAI_STATUS_NOT_SUPPORTED) {
        printf("Flush by dynamic is not supported\r\n");
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    } else {
        printf("Flush by dynamic returns %d\r\n", ret);
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    }

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_STATIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    flush_attr[0].value.s32 = SAI_FDB_FLUSH_ENTRY_TYPE_STATIC;

    ret = sai_fdb_api_table->flush_fdb_entries(1,
                                 (const sai_attribute_t*)flush_attr);

    if (ret == SAI_STATUS_SUCCESS) {
        printf("Flush by static is supported\r\n");
        get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
        get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
        get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

        ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
                sai_fdb_api_table->get_fdb_entry_attribute(
                    (const sai_fdb_entry_t*)&fdb_entry,
                    SAI_MAX_FDB_ATTRIBUTES,
                    get_attr_list));
    } else if (ret == SAI_STATUS_NOT_SUPPORTED) {
        printf("Flush by static is not supported\r\n");
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    } else {
        printf("Flush by static returns %d\r\n", ret);
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    }

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_STATIC, port_id_1, SAI_PACKET_ACTION_FORWARD);

    ret = sai_fdb_api_table->flush_fdb_entries(0, NULL);

    if (ret == SAI_STATUS_SUCCESS) {
        printf("Flush by all types is supported\r\n");
        get_attr_list[0].id = SAI_FDB_ENTRY_ATTR_TYPE;
        get_attr_list[1].id = SAI_FDB_ENTRY_ATTR_PORT_ID;
        get_attr_list[2].id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

        ASSERT_EQ(SAI_STATUS_ADDR_NOT_FOUND,
                sai_fdb_api_table->get_fdb_entry_attribute(
                    (const sai_fdb_entry_t*)&fdb_entry,
                    SAI_MAX_FDB_ATTRIBUTES,
                    get_attr_list));
    } else if (ret == SAI_STATUS_NOT_SUPPORTED) {
        printf("Flush by all types is not supported\r\n");
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    } else {
        printf("Flush by all types returns %d\r\n", ret);
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                sai_fdb_api_table->remove_fdb_entry(
                    (const sai_fdb_entry_t*)&fdb_entry));
    }
}
/*
 * FDB Create and Get FDB Entry
 */
TEST_F(fdbInit, set_fdb_entry)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t get_attr;
    sai_attribute_t set_attr;
    sai_object_id_t lag_id;
    sai_object_id_t lag_member_id_1;
    sai_object_id_t lag_member_id_2;
    sai_attribute_t lag_attr[2];

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_lag_api_table->create_lag (&lag_id, 0, NULL));

    memset(lag_attr, 0, sizeof(lag_attr));

    lag_attr[0].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    lag_attr[0].value.oid = lag_id;
    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_3;


    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_1,
                                                     2, lag_attr));

    lag_attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    lag_attr[1].value.oid = port_id_4;
    ASSERT_EQ (SAI_STATUS_SUCCESS,
               sai_lag_api_table->create_lag_member (&lag_member_id_2,
                                                     2, lag_attr));


    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry);

    /*Changing type*/
    memset(&set_attr,0, sizeof(set_attr));
    set_attr.id = SAI_FDB_ENTRY_ATTR_TYPE;
    set_attr.value.s32 = SAI_FDB_ENTRY_TYPE_STATIC;

    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->set_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 (const sai_attribute_t*)&set_attr));

    memset(&get_attr,0, sizeof(get_attr));
    get_attr.id = SAI_FDB_ENTRY_ATTR_TYPE;
    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 1, &get_attr));
    EXPECT_EQ(SAI_FDB_ENTRY_TYPE_STATIC, get_attr.value.s32);

    /*Changing action*/
    memset(&set_attr,0, sizeof(set_attr));
    set_attr.id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
    set_attr.value.s32 = SAI_PACKET_ACTION_DROP;

    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->set_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 (const sai_attribute_t*)&set_attr));

    memset(&get_attr,0, sizeof(get_attr));
    get_attr.id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;
    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 1, &get_attr));
    EXPECT_EQ(SAI_PACKET_ACTION_DROP, get_attr.value.s32);

    /*Changing port*/
    memset(&set_attr,0, sizeof(set_attr));
    set_attr.id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    set_attr.value.oid = port_id_2;

    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->set_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 (const sai_attribute_t*)&set_attr));

    memset(&get_attr,0, sizeof(get_attr));
    get_attr.id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 1, &get_attr));
    EXPECT_EQ(port_id_2, get_attr.value.oid);

    /*Change port to lag*/
    memset(&set_attr,0, sizeof(set_attr));
    set_attr.id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    set_attr.value.oid = lag_id;

    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->set_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 (const sai_attribute_t*)&set_attr));

    memset(&get_attr,0, sizeof(get_attr));
    get_attr.id = SAI_FDB_ENTRY_ATTR_PORT_ID;
    EXPECT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->get_fdb_entry_attribute(
                                 (const sai_fdb_entry_t*)&fdb_entry,
                                 1, &get_attr));
    EXPECT_EQ(lag_id, get_attr.value.oid);

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_fdb_api_table->remove_fdb_entry(
                                 (const sai_fdb_entry_t*)&fdb_entry));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_1));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag_member(lag_member_id_2));
    ASSERT_EQ(SAI_STATUS_SUCCESS, sai_lag_api_table->remove_lag(lag_id));
}

char *sai_fdb_get_action_char(sai_packet_action_t action,
                              char *fdb_action_char, unsigned int len)
{
    switch(action) {
        case SAI_PACKET_ACTION_DROP:
            snprintf(fdb_action_char, len, "Drop");
            break;

        case SAI_PACKET_ACTION_FORWARD:
            snprintf(fdb_action_char, len, "Forward");
            break;

        case SAI_PACKET_ACTION_COPY:
            snprintf(fdb_action_char, len, "Copy");
            break;

        case SAI_PACKET_ACTION_COPY_CANCEL:
            snprintf(fdb_action_char, len, "Copy Cancel");
            break;

        case SAI_PACKET_ACTION_TRAP:
            snprintf(fdb_action_char, len, "Trap");
            break;

        case SAI_PACKET_ACTION_LOG:
            snprintf(fdb_action_char, len, "Log");
            break;

        case SAI_PACKET_ACTION_DENY:
            snprintf(fdb_action_char, len, "Deny");
            break;

        case SAI_PACKET_ACTION_TRANSIT:
            snprintf(fdb_action_char, len, "Transit");
            break;

    }
    return fdb_action_char;
}

TEST_F(fdbInit, fdb_entry_supported_actions)
{
    sai_fdb_entry_t fdb_entry;
    sai_attribute_t set_attr;
    sai_status_t ret;
    sai_packet_action_t packet_action[] =
                    {SAI_PACKET_ACTION_DROP, SAI_PACKET_ACTION_FORWARD, SAI_PACKET_ACTION_COPY,
                     SAI_PACKET_ACTION_COPY_CANCEL, SAI_PACKET_ACTION_TRAP, SAI_PACKET_ACTION_LOG,
                     SAI_PACKET_ACTION_DENY, SAI_PACKET_ACTION_TRANSIT };
    unsigned int num_actions = sizeof(packet_action)/sizeof(packet_action[0]);
    unsigned int idx = 0;
    char act_str[20] = {0};

    sai_fdb_entry_create(SAI_FDB_ENTRY_TYPE_DYNAMIC, port_id_1, SAI_PACKET_ACTION_FORWARD);
    sai_set_test_fdb_entry(&fdb_entry);


    /*Changing action*/
    memset(&set_attr,0, sizeof(set_attr));
    set_attr.id = SAI_FDB_ENTRY_ATTR_PACKET_ACTION;

    for(idx = 0; idx < num_actions; idx++) {
        set_attr.value.s32 = packet_action[idx];
        ret = sai_fdb_api_table->set_fdb_entry_attribute(
                (const sai_fdb_entry_t*)&fdb_entry,
                (const sai_attribute_t*)&set_attr);
        sai_fdb_get_action_char(packet_action[idx], act_str, 20);

        if(ret == SAI_STATUS_SUCCESS) {
            printf("%s action is supported\r\n",act_str);
        } else if ((ret == SAI_STATUS_NOT_SUPPORTED) ||
                  (ret == SAI_STATUS_INVALID_ATTR_VALUE_0)) {
            printf("%s action is not supported\r\n",act_str);
        } else {
            printf("%s action return value is %d\r\n",act_str, ret);
        }
    }
    ASSERT_EQ(SAI_STATUS_SUCCESS,
            sai_fdb_api_table->remove_fdb_entry(
                (const sai_fdb_entry_t*)&fdb_entry));
}
