/*
 * filename:
"src/unit_test/sai_bridge_unit_test.cpp
 * (c) Copyright 2017 Dell Inc. All Rights Reserved.
 */

/*
 * sai_bridge_unit_test.cpp
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
#include "saitypes.h"
#include "saibridge.h"
#include "saiswitch.h"
#include "sai_l2_unit_test_defs.h"
}

#define SAI_MAX_PORTS  256

static uint32_t bridge_port_count = 0;
static sai_object_id_t bridge_port_list[SAI_MAX_PORTS] = {0};
static sai_object_id_t switch_id = 0;
static sai_object_id_t default_bridge_id = 0;
static sai_object_id_t port_list[SAI_MAX_PORTS] = {0};
static uint32_t port_count = 0;

bool sai_bridge_ut_check_if_port_has_bridge_port(sai_bridge_api_t* p_sai_bridge_api_tbl,
                                                         sai_object_id_t port_id)
{
    uint32_t bridge_port_idx = 0;
    sai_attribute_t attr;

    for(bridge_port_idx = 0 ; bridge_port_idx < bridge_port_count; bridge_port_idx++) {
        attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;

        p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_list[bridge_port_idx], 1,
                                                        &attr);
        if(attr.value.oid == port_id) {
            printf("Port 0x%" PRIx64 " - Bridge port 0x%" PRIx64 "\r\n",port_id,
                   bridge_port_list[bridge_port_idx]);
            return true;
        }
    }
    printf("Port 0x%" PRIx64 " has no bridge port\r\n", port_id);
    return false;

}

sai_status_t sai_bridge_ut_get_port_from_bridge_port(sai_bridge_api_t *p_sai_bridge_api_tbl,
                                                     sai_object_id_t bridge_port_id,
                                                     sai_object_id_t *port_id)
{
    sai_attribute_t bridge_port_attr[1];
    sai_status_t    sai_rc;

    bridge_port_attr[0].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;

    sai_rc = p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1,
                                                             bridge_port_attr);

    *port_id = bridge_port_attr[0].value.oid;
    return sai_rc;

}

class bridgeTest : public ::testing::Test
{
    protected:
        static void SetUpTestCase()
        {

            /*
             * Query and populate the SAI Switch API Table.
             */
            EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
                    (SAI_API_SWITCH, (static_cast<void**>
                                      (static_cast<void*>(&p_sai_switch_api_tbl)))));

            ASSERT_TRUE (p_sai_switch_api_tbl != NULL);

            sai_attribute_t sai_attr_set[7];
            uint32_t attr_count = 7;

            memset(sai_attr_set,0, sizeof(sai_attr_set));

            sai_attr_set[0].id = SAI_SWITCH_ATTR_INIT_SWITCH;
            sai_attr_set[0].value.booldata = 1;

            sai_attr_set[1].id = SAI_SWITCH_ATTR_SWITCH_PROFILE_ID;
            sai_attr_set[1].value.u32 = 0;

            sai_attr_set[2].id = SAI_SWITCH_ATTR_FDB_EVENT_NOTIFY;
            sai_attr_set[2].value.ptr = (void *)sai_fdb_evt_callback;

            sai_attr_set[3].id = SAI_SWITCH_ATTR_PORT_STATE_CHANGE_NOTIFY;
            sai_attr_set[3].value.ptr = (void *)sai_port_state_evt_callback;

            sai_attr_set[4].id = SAI_SWITCH_ATTR_PACKET_EVENT_NOTIFY;
            sai_attr_set[4].value.ptr = (void *)sai_packet_event_callback;

            sai_attr_set[5].id = SAI_SWITCH_ATTR_SWITCH_STATE_CHANGE_NOTIFY;
            sai_attr_set[5].value.ptr = (void *)sai_switch_operstate_callback;

            sai_attr_set[6].id = SAI_SWITCH_ATTR_SHUTDOWN_REQUEST_NOTIFY;
            sai_attr_set[6].value.ptr = (void *)sai_switch_shutdown_callback;

            ASSERT_TRUE(p_sai_switch_api_tbl->create_switch != NULL);

            EXPECT_EQ (SAI_STATUS_SUCCESS,
                    (p_sai_switch_api_tbl->create_switch (&switch_id , attr_count,
                                                          sai_attr_set)));

            sai_api_query (SAI_API_BRIDGE, (static_cast<void**>
                        (static_cast<void*>(&p_sai_bridge_api_tbl))));

            ASSERT_TRUE (p_sai_bridge_api_tbl != NULL);

            EXPECT_TRUE(p_sai_bridge_api_tbl->create_bridge != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->remove_bridge != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->set_bridge_attribute != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->get_bridge_attribute != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->create_bridge_port != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->remove_bridge_port != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->set_bridge_port_attribute != NULL);
            EXPECT_TRUE(p_sai_bridge_api_tbl->get_bridge_port_attribute != NULL);

            sai_attribute_t attr;
            sai_status_t ret;
            memset (&attr, 0, sizeof (attr));

            attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;

            ret = p_sai_switch_api_tbl->get_switch_attribute(switch_id,1,&attr);
            ASSERT_EQ (SAI_STATUS_SUCCESS, ret);

            default_bridge_id = attr.value.oid;

            attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
            attr.value.objlist.count = SAI_MAX_PORTS;
            attr.value.objlist.list = bridge_port_list;

            ret = p_sai_bridge_api_tbl->get_bridge_attribute(default_bridge_id, 1, &attr);
            ASSERT_EQ (SAI_STATUS_SUCCESS, ret);

            bridge_port_count = attr.value.objlist.count;

            memset (&attr, 0, sizeof (attr));

            attr.id = SAI_SWITCH_ATTR_PORT_LIST;
            attr.value.objlist.count = SAI_MAX_PORTS;
            attr.value.objlist.list  = port_list;

            ret = p_sai_switch_api_tbl->get_switch_attribute(0,1,&attr);
            port_count = attr.value.objlist.count;

            EXPECT_EQ (SAI_STATUS_SUCCESS,ret);
        }

        static sai_bridge_api_t* p_sai_bridge_api_tbl;
        static sai_switch_api_t *p_sai_switch_api_tbl;
};

sai_bridge_api_t* bridgeTest::p_sai_bridge_api_tbl = NULL;
sai_switch_api_t* bridgeTest::p_sai_switch_api_tbl = NULL;

/*
 * Bridge defaults
 */
TEST_F(bridgeTest, test_defaults)
{
    uint32_t port_idx = 0;

    ASSERT_NE(default_bridge_id, SAI_NULL_OBJECT_ID);

    for(port_idx =0; port_idx < port_count; port_idx++) {
        ASSERT_EQ(true,
                  sai_bridge_ut_check_if_port_has_bridge_port(p_sai_bridge_api_tbl,
                                                              port_list[port_idx]));
    }
}

TEST_F(bridgeTest, 1q_bridge_create)
{
    sai_attribute_t attr;
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;

    /* First try to override the switch attribute */
    attr.id = SAI_SWITCH_ATTR_DEFAULT_1Q_BRIDGE_ID;
    attr.value.oid = SAI_NULL_OBJECT_ID;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_switch_api_tbl->set_switch_attribute(switch_id, &attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
               p_sai_switch_api_tbl->get_switch_attribute(switch_id,1,&attr));

    ASSERT_EQ(default_bridge_id, attr.value.oid);

    /* Create 1Q Bridge */
    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1Q;

    ASSERT_EQ(SAI_STATUS_ITEM_ALREADY_EXISTS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr));

    ASSERT_EQ(SAI_STATUS_OBJECT_IN_USE,
              p_sai_bridge_api_tbl->remove_bridge(default_bridge_id));
}

TEST_F(bridgeTest, 1q_bridge_attribute_set)
{
    sai_attribute_t attr;
    sai_attribute_t get_attr;
    sai_status_t    sai_rc;

    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(default_bridge_id, &attr));

    attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
    attr.value.objlist.count = bridge_port_count;
    attr.value.objlist.list = bridge_port_list;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(default_bridge_id, &attr));

    attr.id = SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES;
    attr.value.s32 = 10;
    sai_rc = p_sai_bridge_api_tbl->set_bridge_attribute(default_bridge_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_attribute(default_bridge_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES is set to 10 successfully\r\n");
    }

    attr.id = SAI_BRIDGE_ATTR_LEARN_DISABLE;
    attr.value.booldata = true;
    sai_rc = p_sai_bridge_api_tbl->set_bridge_attribute(default_bridge_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_ATTR_LEARN_DISABLE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_ATTR_LEARN_DISABLE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_attribute(default_bridge_id, 1, &get_attr));
        ASSERT_EQ(attr.value.booldata, get_attr.value.booldata);
        printf("Attribute SAI_BRIDGE_ATTR_LEARN_DISABLE is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_ATTR_END;
    ASSERT_EQ(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(default_bridge_id, &attr));

}

TEST_F(bridgeTest, 1q_bridge_port_create_remove)
{
    sai_object_id_t port_id;
    sai_object_id_t bridge_port_id;
    sai_attribute_t attr[3];

    bridge_port_id = bridge_port_list[0];

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              sai_bridge_ut_get_port_from_bridge_port(p_sai_bridge_api_tbl, bridge_port_id,
                                                      &port_id));


    attr[0].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_PORT;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 1, attr));

    attr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr[1].value.oid = port_id;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 2, attr));

    attr[2].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    attr[2].value.oid = default_bridge_id;

    ASSERT_EQ(SAI_STATUS_ITEM_ALREADY_EXISTS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 3, attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_list[0]));


    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 3, attr));

    bridge_port_list[0] = bridge_port_id;
    printf("Bridge port id 0x%" PRIx64 " is created for port 0x%" PRIx64 "",
           port_id, bridge_port_id);
}

TEST_F(bridgeTest, 1q_bridge_port_attribute_set)
{
    sai_attribute_t attr;
    sai_attribute_t get_attr;
    sai_status_t    sai_rc;
    sai_object_id_t bridge_port_id = bridge_port_list[0];

    attr.id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_PORT_TYPE_SUB_PORT;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr.value.oid = port_list[1];

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    attr.value.oid = SAI_GTEST_VLAN;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_RIF_ID;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_TUNNEL_ID;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES;
    attr.value.s32 = 10;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES is set to 10 successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    attr.value.s32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE is set to drop successfully\r\n");
    }
    attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION;
    attr.value.s32 = SAI_PACKET_ACTION_DROP;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting "
               "SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION "
               "is set to drop successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    attr.value.booldata = true;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_ADMIN_STATE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.booldata, get_attr.value.booldata);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_ADMIN_STATE is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    attr.value.booldata = true;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_END;
    ASSERT_EQ(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

}

TEST_F(bridgeTest, 1d_bridge_create_remove)
{
    sai_attribute_t attr;
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;

    attr.id = SAI_BRIDGE_ATTR_LEARN_DISABLE;
    attr.value.booldata = true;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr));

    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));

    ASSERT_EQ(SAI_STATUS_INVALID_OBJECT_ID,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));
}

TEST_F(bridgeTest, 1d_bridge_attribute_set)
{
    sai_attribute_t attr;
    sai_attribute_t get_attr;
    sai_status_t    sai_rc;
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;

    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr));

    attr.id = SAI_BRIDGE_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_TYPE_1Q;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(bridge_id, &attr));

    attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
    attr.value.objlist.count = bridge_port_count;
    attr.value.objlist.list = bridge_port_list;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(bridge_id, &attr));

    attr.id = SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES;

    attr.value.s32 = 10;
    sai_rc = p_sai_bridge_api_tbl->set_bridge_attribute(bridge_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_ATTR_MAX_LEARNED_ADDRESSES is set to 10 successfully\r\n");
    }

    attr.id = SAI_BRIDGE_ATTR_LEARN_DISABLE;
    attr.value.booldata = true;
    sai_rc = p_sai_bridge_api_tbl->set_bridge_attribute(bridge_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_ATTR_LEARN_DISABLE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_ATTR_LEARN_DISABLE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));
        ASSERT_EQ(attr.value.booldata, get_attr.value.booldata);
        printf("Attribute SAI_BRIDGE_ATTR_LEARN_DISABLE is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_ATTR_END;
    ASSERT_EQ(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_attribute(bridge_id, &attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));

}

TEST_F(bridgeTest, 1d_bridge_port_create_remove)
{
    sai_object_id_t port_id = port_list[0];
    sai_vlan_id_t   vlan_id = SAI_GTEST_VLAN;
    sai_object_id_t bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t bridge_port_id_1 = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr[4];
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;

    attr[0].id = SAI_BRIDGE_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr[0]));


    attr[0].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_SUB_PORT;

    attr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr[1].value.oid = port_id;


    attr[2].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    attr[2].value.oid = bridge_id;

    ASSERT_EQ(SAI_STATUS_MANDATORY_ATTRIBUTE_MISSING,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 3, attr));

    attr[3].id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    attr[3].value.u16 = vlan_id;

    attr[2].value.oid = default_bridge_id;

    ASSERT_NE(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 4, attr));

    attr[2].value.oid = bridge_id;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 4, attr));
    printf("Bridge port id 0x%" PRIx64 " is created for port 0x%" PRIx64 " vlan %d\r\n",
           port_id, bridge_port_id, vlan_id);

    ASSERT_EQ(SAI_STATUS_ITEM_ALREADY_EXISTS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id_1, switch_id, 4, attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id));

    ASSERT_EQ(SAI_STATUS_INVALID_OBJECT_ID,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id));
    /* Check again for non duplicate creation */

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id_1, switch_id, 4, attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id_1));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));

}

TEST_F(bridgeTest, 1d_bridge_port_list_get)
{
    sai_object_id_t port_id = port_list[0];
    sai_object_id_t port_id_1 = port_list[1];
    sai_vlan_id_t   vlan_id = SAI_GTEST_VLAN;
    sai_object_id_t bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t bridge_port_id_1 = SAI_NULL_OBJECT_ID;
    sai_attribute_t attr[4];
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t tmp_bridge_port_list[2];
    sai_attribute_t get_attr;

    attr[0].id = SAI_BRIDGE_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &attr[0]));


    attr[0].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_SUB_PORT;

    attr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr[1].value.oid = port_id;


    attr[2].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    attr[2].value.oid = bridge_id;


    attr[3].id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    attr[3].value.u16 = vlan_id;

    get_attr.id = SAI_BRIDGE_ATTR_PORT_LIST;
    get_attr.value.objlist.count = 2;
    get_attr.value.objlist.list = tmp_bridge_port_list;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));

    ASSERT_EQ(get_attr.value.objlist.count, 0);

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 4, attr));

    printf("Bridge port id 0x%" PRIx64 " is created for port 0x%" PRIx64 " vlan %d\r\n",
           port_id, bridge_port_id, vlan_id);

    get_attr.value.objlist.count = 2;
    get_attr.value.objlist.list = tmp_bridge_port_list;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));

    ASSERT_EQ(get_attr.value.objlist.count, 1);
    ASSERT_EQ(tmp_bridge_port_list[0], bridge_port_id);

    attr[1].value.oid = port_id_1;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id_1, switch_id, 4, attr));

    printf("Bridge port id 0x%" PRIx64 " is created for port 0x%" PRIx64 " vlan %d\r\n",
           port_id_1, bridge_port_id_1, vlan_id);

    get_attr.value.objlist.count = 2;
    get_attr.value.objlist.list = tmp_bridge_port_list;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));

    ASSERT_EQ(get_attr.value.objlist.count, 2);

    ASSERT_TRUE(((bridge_port_id_1 == tmp_bridge_port_list[0]) &&
                 (bridge_port_id == tmp_bridge_port_list[1])) ||
                ((bridge_port_id_1 == tmp_bridge_port_list[1]) &&
                 (bridge_port_id == tmp_bridge_port_list[0])));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id));

    get_attr.value.objlist.count = 2;
    get_attr.value.objlist.list = tmp_bridge_port_list;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));

    ASSERT_EQ(get_attr.value.objlist.count, 1);
    ASSERT_EQ(tmp_bridge_port_list[0], bridge_port_id_1);

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id_1));

    get_attr.value.objlist.count = 2;
    get_attr.value.objlist.list = tmp_bridge_port_list;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->get_bridge_attribute(bridge_id, 1, &get_attr));

    ASSERT_EQ(get_attr.value.objlist.count, 0);

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));

}

TEST_F(bridgeTest, 1d_bridge_port_attribute_set)
{
    sai_attribute_t attr;
    sai_attribute_t get_attr;
    sai_status_t    sai_rc;
    sai_object_id_t bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_attribute_t c_attr[4];
    sai_object_id_t bridge_id = SAI_NULL_OBJECT_ID;
    sai_object_id_t port_id = port_list[0];
    sai_vlan_id_t   vlan_id = SAI_GTEST_VLAN;

    c_attr[0].id = SAI_BRIDGE_ATTR_TYPE;
    c_attr[0].value.s32 = SAI_BRIDGE_TYPE_1D;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge(&bridge_id, switch_id, 1, &c_attr[0]));

    c_attr[0].id = SAI_BRIDGE_PORT_ATTR_TYPE;
    c_attr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_SUB_PORT;

    c_attr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    c_attr[1].value.oid = port_id;


    c_attr[2].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    c_attr[2].value.oid = bridge_id;


    c_attr[3].id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    c_attr[3].value.u16 = vlan_id;

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->create_bridge_port(&bridge_port_id, switch_id, 4, c_attr));


    attr.id = SAI_BRIDGE_PORT_ATTR_TYPE;
    attr.value.s32 = SAI_BRIDGE_PORT_TYPE_TUNNEL;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr.value.oid = port_list[1];

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    attr.value.oid = SAI_GTEST_VLAN;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_RIF_ID;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_TUNNEL_ID;

    ASSERT_EQ(SAI_STATUS_INVALID_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    attr.id = SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES;
    attr.value.s32 = 10;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_MAX_LEARNED_ADDRESSES is set to 10 successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
    attr.value.s32 = SAI_BRIDGE_PORT_FDB_LEARNING_MODE_DROP;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_MODE is set to drop successfully\r\n");
    }
    attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION;
    attr.value.s32 = SAI_PACKET_ACTION_DROP;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting "
               "SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_FDB_LEARNING_LIMIT_VIOLATION_PACKET_ACTION "
               "is set to drop successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
    attr.value.booldata = true;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_ADMIN_STATE\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_ADMIN_STATE;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.booldata, get_attr.value.booldata);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_ADMIN_STATE is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
    attr.value.booldata = true;

    sai_rc = p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        printf("Error %d is returned on setting SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING\r\n",
               sai_rc);
    } else {
        get_attr.id = SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING;
        ASSERT_EQ(SAI_STATUS_SUCCESS,
                  p_sai_bridge_api_tbl->get_bridge_port_attribute(bridge_port_id, 1, &get_attr));
        ASSERT_EQ(attr.value.s32, get_attr.value.s32);
        printf("Attribute SAI_BRIDGE_PORT_ATTR_INGRESS_FILTERING is set to true successfully\r\n");
    }

    attr.id = SAI_BRIDGE_PORT_ATTR_END;
    ASSERT_EQ(SAI_STATUS_UNKNOWN_ATTRIBUTE_0,
              p_sai_bridge_api_tbl->set_bridge_port_attribute(bridge_port_id, &attr));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge_port(bridge_port_id));

    ASSERT_EQ(SAI_STATUS_SUCCESS,
              p_sai_bridge_api_tbl->remove_bridge(bridge_id));

}

