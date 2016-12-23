/*
 * filename: sai_mirror_unit_test_utils.cpp
 * (c) Copyright 2015 Dell Inc. All Rights Reserved.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "gtest/gtest.h"
#include "stdarg.h"
#include "sai_mirror_unit_test.h"
#include "sai_port_breakout_test_utils.h"

extern "C" {
#include "sai.h"
#include "saistatus.h"
#include "saitypes.h"
#include "saiswitch.h"
#include "saimirror.h"
#include "sai_mirror_api.h"
#include "sai_oid_utils.h"
}

#define SAI_MAX_PORTS 106

static uint32_t port_count = 0;

static sai_object_id_t port_list[SAI_MAX_PORTS] = {0};

/*
 * Stubs for Callback functions to be passed from adaptor host/application
 * upon switch initialization API call.
 */
static inline void sai_port_state_evt_callback (uint32_t count,
                                                sai_port_oper_status_notification_t *data)
{
}

static inline void sai_fdb_evt_callback(uint32_t count, sai_fdb_event_notification_data_t *data)
{
}

static inline void sai_switch_operstate_callback (sai_switch_oper_status_t
                                                  switchstate) {
}

/* Packet event callback
 */
static inline void sai_packet_event_callback (const void *buffer,
                                              sai_size_t buffer_size,
                                              uint32_t attr_count,
                                              const sai_attribute_t *attr_list)
{
}

static inline void sai_port_evt_callback (uint32_t count,
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
            port_count = 0;

            printf("PORT DELETE EVENT for  port 0x%"PRIx64" and total ports count is %d \r\n",
                   port_id, port_count);
        } else {
            printf("Invalid PORT EVENT for port 0x%"PRIx64" \r\n", port_id);
        }
    }
}

static inline void  sai_switch_shutdown_callback (void) {
}

sai_object_id_t mirrorTest ::sai_mirror_port_id_get (uint32_t port_index)
{
    if(port_index >= port_count) {
        return 0;
    }

    return port_list [port_index];
}

sai_object_id_t mirrorTest ::sai_mirror_invalid_port_id_get ()
{
    return (port_list[port_count-1] + 1);
}


void mirrorTest ::SetUpTestCase (void) {
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
     */
    ASSERT_TRUE(p_sai_switch_api_tbl->initialize_switch != NULL);

    /*
     * Fill in notification callback routines with stubs.
     */
    notification.on_switch_state_change = sai_switch_operstate_callback;
    notification.on_fdb_event = sai_fdb_evt_callback;
    notification.on_port_state_change = sai_port_state_evt_callback;
    notification.on_switch_shutdown_request = sai_switch_shutdown_callback;
    //notification.on_packet_rx = sai_packet_rx_callback;
    notification.on_port_event = sai_port_evt_callback;
    notification.on_packet_event = sai_packet_event_callback;

    EXPECT_EQ (SAI_STATUS_SUCCESS,
               (p_sai_switch_api_tbl->initialize_switch (0, NULL, NULL,
                                                         &notification)));
    /*
     * Query and populate the SAI Port API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_PORT, (static_cast<void**>
                                         (static_cast<void*>
                                          (&p_sai_port_api_tbl)))));

    ASSERT_TRUE (p_sai_port_api_tbl != NULL);

    /*
     * Query and populate the SAI Mirror API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_MIRROR, (static_cast<void**>
                                         (static_cast<void*>
                                          (&p_sai_mirror_api_tbl)))));

    ASSERT_TRUE (p_sai_mirror_api_tbl != NULL);

    /*
     * Query and populate the SAI Acl API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_ACL, (static_cast<void**>
                                         (static_cast<void*>
                                          (&p_sai_acl_api_tbl)))));

    ASSERT_TRUE (p_sai_acl_api_tbl != NULL);

    /*
     * Query and populate the SAI Acl API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_VLAN, (static_cast<void**>
                                         (static_cast<void*>
                                          (&p_sai_vlan_api_tbl)))));

    ASSERT_TRUE (p_sai_vlan_api_tbl != NULL);

    /*
     * Query and populate the SAI Acl API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_LAG, (static_cast<void**>
                                         (static_cast<void*>
                                          (&p_sai_lag_api_table)))));

    ASSERT_TRUE (p_sai_lag_api_table != NULL);

    sai_mirror_first_port = port_list[port_count - 2];
    sai_mirror_second_port = sai_mirror_port_id_get (17);
    sai_mirror_third_port = sai_mirror_port_id_get (9);
    sai_monitor_port = port_list[port_count - 1];
    sai_second_monitor_port = sai_mirror_port_id_get (13);
    sai_invalid_port = sai_mirror_invalid_port_id_get();

    EXPECT_EQ (SAI_STATUS_SUCCESS,p_sai_vlan_api_tbl->create_vlan(SAI_MIRROR_VLAN_2));
    EXPECT_EQ (SAI_STATUS_SUCCESS,p_sai_vlan_api_tbl->create_vlan(SAI_MIRROR_VLAN_3));
}

sai_switch_api_t* mirrorTest ::p_sai_switch_api_tbl = NULL;
sai_port_api_t* mirrorTest ::p_sai_port_api_tbl = NULL;
sai_mirror_api_t* mirrorTest ::p_sai_mirror_api_tbl = NULL;
sai_acl_api_t* mirrorTest ::p_sai_acl_api_tbl = NULL;
sai_vlan_api_t* mirrorTest ::p_sai_vlan_api_tbl = NULL;
sai_lag_api_t* mirrorTest ::p_sai_lag_api_table = NULL;
sai_object_id_t mirrorTest ::sai_mirror_first_port = 0;
sai_object_id_t mirrorTest ::sai_mirror_second_port = 0;
sai_object_id_t mirrorTest ::sai_mirror_third_port = 0;
sai_object_id_t mirrorTest ::sai_monitor_port = 0;
sai_object_id_t mirrorTest ::sai_second_monitor_port = 0;
sai_object_id_t mirrorTest ::sai_invalid_port = 0;

sai_status_t mirrorTest ::sai_test_mirror_session_create (sai_object_id_t *p_session_id,
        uint32_t attr_count,
        sai_attribute_t *p_attr_list)
{
    sai_status_t     sai_rc = SAI_STATUS_SUCCESS;


    printf ("Testing Session Create API with attribute count: %d\r\n", attr_count);

    sai_rc = p_sai_mirror_api_tbl->create_mirror_session (p_session_id,attr_count,
            p_attr_list);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("SAI Mirror session Creation API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("SAI Mirror session Creation API success, session ID: %lu\r\n", *p_session_id);
    }

    return sai_rc;
}

sai_status_t mirrorTest ::sai_test_mirror_session_destroy (sai_object_id_t session_id) {
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    sai_rc = p_sai_mirror_api_tbl->remove_mirror_session (session_id);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("Mirror Session destroy API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("Mirror Session API success for session id: %lu\r\n", session_id);
    }

    return sai_rc;
}

sai_status_t mirrorTest ::sai_test_mirror_session_ingress_port_add (sai_object_id_t port_id,
            sai_object_list_t *obj_list) {

    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_INGRESS_MIRROR_SESSION;
    memcpy (&attr.value.objlist,obj_list,sizeof (sai_object_list_t));
    sai_rc = p_sai_port_api_tbl->set_port_attribute (port_id,&attr);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("Set port attribute API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("Set port attribute API success for port id: %lu\r\n", port_id);
    }

    return sai_rc;
}

sai_status_t mirrorTest ::sai_test_mirror_session_ingress_port_get (sai_object_id_t port_id,
            sai_object_list_t *obj_list) {

    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_INGRESS_MIRROR_SESSION;
    memcpy (&attr.value.objlist,obj_list,sizeof (sai_object_list_t));
    sai_rc = p_sai_port_api_tbl->get_port_attribute (port_id, 1, &attr);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("Get port attribute API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("Get port attribute API success for port id: %lu\r\n", port_id);
    }

    return sai_rc;
}

sai_status_t mirrorTest ::sai_test_mirror_session_egress_port_add (sai_object_id_t port_id,
            sai_object_list_t *obj_list) {

    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_EGRESS_MIRROR_SESSION;
    memcpy (&attr.value.objlist,obj_list,sizeof (sai_object_list_t));
    sai_rc = p_sai_port_api_tbl->set_port_attribute (port_id,&attr);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("Set port attribute API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("Set port attribute API success for port id: %lu\r\n", port_id);
    }

    return sai_rc;
}

sai_status_t mirrorTest ::sai_test_mirror_session_egress_port_get (sai_object_id_t port_id,
            sai_object_list_t *obj_list) {

    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_attribute_t attr;

    attr.id = SAI_PORT_ATTR_EGRESS_MIRROR_SESSION;

    memcpy (&attr.value.objlist,obj_list,sizeof (sai_object_list_t));
    sai_rc = p_sai_port_api_tbl->get_port_attribute (port_id, 1, &attr);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        printf ("Get port attribute API failed with error: %d\r\n", sai_rc);
    } else {
        printf ("Get port attribute API success for port id: %lu\r\n", port_id);
    }

    return sai_rc;
}

void mirrorTest ::sai_test_mirror_port_breakout (sai_object_id_t session_id)
{
    sai_status_t ret = SAI_STATUS_FAILURE;
    sai_object_list_t obj_list;
    sai_object_id_t *sessions = NULL;
    bool is_supported =  false;
    uint32_t idx;
    sai_object_id_t breakout_port = SAI_NULL_OBJECT_ID;

    /*set the breakout mode*/
    for(idx = 0; idx < port_count; idx++) {
        sai_check_supported_breakout_mode (p_sai_port_api_tbl,
                port_list[idx], SAI_PORT_BREAKOUT_MODE_4_LANE,
                &is_supported);
        if(is_supported) {
            breakout_port = port_list[idx];
            break;
        }
    }
    if(breakout_port == SAI_NULL_OBJECT_ID) {
        printf("Breakout is not supported on any port");
        return;
    }
    ret = sai_port_break_out_mode_set (p_sai_switch_api_tbl,
                                       p_sai_port_api_tbl,
                                       breakout_port,
                                       SAI_PORT_BREAKOUT_MODE_4_LANE);
    ASSERT_EQ(ret, SAI_STATUS_SUCCESS);

    obj_list.count = 1;
    sessions = (sai_object_id_t *) calloc (1, sizeof(sai_object_id_t));
    sessions[0] = session_id;
    obj_list.list = sessions;
    ret = sai_test_mirror_session_ingress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[1], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[2], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[3], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[1], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[2], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[3], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    obj_list.count = 0;
    ret = sai_test_mirror_session_ingress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[1], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[2], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_ingress_port_add (port_list[3], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[1], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[2], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[3], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    /*get the mode and check*/
    ret = sai_port_break_out_mode_get(p_sai_port_api_tbl,
                                      port_list[0],
                                      SAI_PORT_BREAKOUT_MODE_4_LANE);
    ASSERT_EQ(ret, SAI_STATUS_SUCCESS);

    /*set the breakin mode*/
    ret = sai_port_break_in_mode_set(p_sai_switch_api_tbl,
                                     p_sai_port_api_tbl,
                                     4,
                                     port_list,
                                     SAI_PORT_BREAKOUT_MODE_1_LANE);
    ASSERT_EQ(ret, SAI_STATUS_SUCCESS);

    /*get the mode and check*/
    ret = sai_port_break_out_mode_get(p_sai_port_api_tbl,
                                      port_list[0],
                                      SAI_PORT_BREAKOUT_MODE_1_LANE);
    ASSERT_EQ(ret, SAI_STATUS_SUCCESS);

    obj_list.count = 1;
    sessions[0] = session_id;
    obj_list.list = sessions;
    ret = sai_test_mirror_session_ingress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[0], &obj_list);

    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    obj_list.count = 0;
    ret = sai_test_mirror_session_ingress_port_add (port_list[0], &obj_list);
    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    ret = sai_test_mirror_session_egress_port_add (port_list[0], &obj_list);
    EXPECT_EQ (SAI_STATUS_SUCCESS, ret);

    free (sessions);
}

void mirrorTest ::TearDownTestCase (void)
{
    EXPECT_EQ (SAI_STATUS_SUCCESS,p_sai_vlan_api_tbl->remove_vlan(SAI_MIRROR_VLAN_2));
    EXPECT_EQ (SAI_STATUS_SUCCESS,p_sai_vlan_api_tbl->remove_vlan(SAI_MIRROR_VLAN_3));
}

