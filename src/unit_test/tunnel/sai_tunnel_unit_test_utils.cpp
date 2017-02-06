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
 * @file  sai_tunnel_unit_test_utils.cpp
 *
 * @brief This file contains utility and helper function definitions for
 *        testing the SAI Tunnel functionalities.
 */

#include "gtest/gtest.h"

#include "sai_tunnel_unit_test.h"
#include "sai_l3_unit_test_utils.h"

extern "C" {
#include "sai.h"
#include "saitypes.h"
#include "saistatus.h"
#include "saiswitch.h"
#include "saitunnel.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <arpa/inet.h>
}

/* Definition for the data members */
sai_switch_api_t* saiTunnelTest::p_sai_switch_api_tbl = NULL;
sai_tunnel_api_t* saiTunnelTest::p_sai_tunnel_api_tbl = NULL;
sai_object_id_t saiTunnelTest::dflt_vr_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_overlay_vr_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_port_rif_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_vlan_rif_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_overlay_rif_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_underlay_port_nh_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_underlay_vlan_nh_id = SAI_NULL_OBJECT_ID;
sai_object_id_t saiTunnelTest::dflt_underlay_nhg_id = SAI_NULL_OBJECT_ID;
const char *  saiTunnelTest::nh_ip_str_1 = "50.1.1.1";
const char *  saiTunnelTest::nh_ip_str_2 = "60.1.1.1";
unsigned int saiTunnelTest::port_count = 0;
sai_object_id_t saiTunnelTest::port_list[SAI_TEST_MAX_PORTS] = {0};

#define TUNNEL_PRINT(msg, ...) \
    printf(msg"\n", ##__VA_ARGS__)

/*
 * Stubs for Callback functions to be passed from adapter host/application.
 */
#ifdef UNREFERENCED_PARAMETER
#elif defined(__GNUC__)
#define UNREFERENCED_PARAMETER(P)   (void)(P)
#else
#define UNREFERENCED_PARAMETER(P)   (P)
#endif

static inline void sai_port_state_evt_callback (
                                     uint32_t count,
                                     sai_port_oper_status_notification_t *data)
{
    UNREFERENCED_PARAMETER(count);
    UNREFERENCED_PARAMETER(data);
}

static inline void sai_port_evt_callback (uint32_t count,
                                          sai_port_event_notification_t *data)
{
    UNREFERENCED_PARAMETER(count);
    UNREFERENCED_PARAMETER(data);
}

static inline void sai_fdb_evt_callback (uint32_t count,
                                         sai_fdb_event_notification_data_t *data)
{
    UNREFERENCED_PARAMETER(count);
    UNREFERENCED_PARAMETER(data);
}

static inline void sai_switch_operstate_callback (sai_switch_oper_status_t
                                                  switchstate)
{
    UNREFERENCED_PARAMETER(switchstate);
}

static inline void sai_packet_event_callback (const void *buffer,
                                              sai_size_t buffer_size,
                                              uint32_t attr_count,
                                              const sai_attribute_t *attr_list)
{
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(buffer_size);
    UNREFERENCED_PARAMETER(attr_count);
    UNREFERENCED_PARAMETER(attr_list);
}

static inline void sai_switch_shutdown_callback (void)
{
}

/* SAI switch initialization */
void saiTunnelTest::SetUpTestCase (void)
{
    sai_switch_notification_t notification;
    sai_status_t              status;
    sai_attribute_t attr;

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
    notification.on_port_event = sai_port_evt_callback;
    notification.on_packet_event = sai_packet_event_callback;

    ASSERT_TRUE(p_sai_switch_api_tbl->initialize_switch != NULL);

    EXPECT_EQ (SAI_STATUS_SUCCESS,
               (p_sai_switch_api_tbl->initialize_switch (0, NULL, NULL,
                                                         &notification)));

    /* Query the Tunnel API method tables */
    sai_test_tunnel_api_table_get ();

    /* Query the L3 API method table */
    saiL3Test::SetUpL3ApiQuery ();

    /* Get the switch port count and port list */
    memset (&attr, 0, sizeof (attr));

    attr.id = SAI_SWITCH_ATTR_PORT_LIST;
    attr.value.objlist.count = SAI_TEST_MAX_PORTS;
    attr.value.objlist.list  = port_list;

    status = p_sai_switch_api_tbl->get_switch_attribute (1, &attr);
    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    port_count = attr.value.objlist.count;

    TUNNEL_PRINT ("Switch port count is %u.", port_count);

    ASSERT_TRUE (port_count != 0);

    /* Setup Default Underlay Router objects */
    sai_test_tunnel_underlay_router_setup ();

    /* Use the default VRF for Overlay Virtual Router instance */
    dflt_overlay_vr_id = dflt_vr_id;

    /* Create a default Overlay RIF */
    /* TODO Change this to loopback type */
    status = saiL3Test::sai_test_rif_create (&dflt_overlay_rif_id,
                                             saiL3Test::default_rif_attr_count,
                                             SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID,
                                             dflt_overlay_vr_id,
                                             SAI_ROUTER_INTERFACE_ATTR_TYPE,
                                             SAI_ROUTER_INTERFACE_TYPE_PORT,
                                             SAI_ROUTER_INTERFACE_ATTR_PORT_ID,
                                             sai_test_tunnel_port_id_get (1));
    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

}

void saiTunnelTest::TearDownTestCase (void)
{
    sai_status_t      status;

    /* Remove Overlay RIF */
    status = saiL3Test::sai_test_rif_remove (dflt_overlay_rif_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    sai_test_tunnel_underlay_router_tear_down ();
}

void saiTunnelTest::sai_test_tunnel_underlay_router_setup (void)
{
    sai_status_t      status;
    sai_object_id_t   nh_list [4];
    unsigned int      ecmp_count = 0;
    unsigned int      test_port_index_1 = 0;
    unsigned int      test_port_index_2 = 1;
    sai_object_id_t   test_port_id_1;
    sai_object_id_t   test_port_id_2;
    const char *      mac_str_1 = "00:22:22:22:22:22";
    const char *      mac_str_2 = "00:44:44:44:44:44";
    const unsigned int vr_attr_count = 1;

    test_port_id_1 = sai_test_tunnel_port_id_get (test_port_index_1);
    test_port_id_2 = sai_test_tunnel_port_id_get (test_port_index_2);

    /* Create Vlan */
    status = saiL3Test::sai_test_vlan_create (test_vlan);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Create a default Virtual Router instance */
    status = saiL3Test::sai_test_vrf_create (&dflt_vr_id, vr_attr_count,
                                             SAI_VIRTUAL_ROUTER_ATTR_SRC_MAC_ADDRESS,
                                             "00:00:00:00:00:b4:b5");

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Create a Port RIF */
    status = saiL3Test::sai_test_rif_create (&dflt_port_rif_id,
                                             saiL3Test::default_rif_attr_count,
                                             SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID,
                                             dflt_vr_id,
                                             SAI_ROUTER_INTERFACE_ATTR_TYPE,
                                             SAI_ROUTER_INTERFACE_TYPE_PORT,
                                             SAI_ROUTER_INTERFACE_ATTR_PORT_ID,
                                             test_port_id_1);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Create a VLAN RIF */
    status = saiL3Test::sai_test_rif_create (&dflt_vlan_rif_id,
                                             saiL3Test::default_rif_attr_count,
                                             SAI_ROUTER_INTERFACE_ATTR_VIRTUAL_ROUTER_ID,
                                             dflt_vr_id,
                                             SAI_ROUTER_INTERFACE_ATTR_TYPE,
                                             SAI_ROUTER_INTERFACE_TYPE_VLAN,
                                             SAI_ROUTER_INTERFACE_ATTR_VLAN_ID,
                                             test_vlan);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Neighbor creation */
    status = saiL3Test::sai_test_neighbor_create (dflt_port_rif_id,
                                                  SAI_IP_ADDR_FAMILY_IPV4,
                                                  nh_ip_str_1,
                                                  saiL3Test::default_neighbor_attr_count,
                                                  SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS,
                                                  mac_str_1);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_neighbor_fdb_entry_create (mac_str_2,
                                                            test_vlan, test_port_id_2);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_neighbor_create (dflt_vlan_rif_id,
                                                  SAI_IP_ADDR_FAMILY_IPV4,
                                                  nh_ip_str_2,
                                                  saiL3Test::default_neighbor_attr_count,
                                                  SAI_NEIGHBOR_ENTRY_ATTR_DST_MAC_ADDRESS,
                                                  mac_str_2);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Next-hop Creation */
    status = saiL3Test::sai_test_nexthop_create (&dflt_underlay_port_nh_id,
                                                 saiL3Test::default_nh_attr_count,
                                                 SAI_NEXT_HOP_ATTR_TYPE,
                                                 SAI_NEXT_HOP_TYPE_IP,
                                                 SAI_NEXT_HOP_ATTR_IP,
                                                 SAI_IP_ADDR_FAMILY_IPV4,
                                                 nh_ip_str_1,
                                                 SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID,
                                                 dflt_port_rif_id);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_nexthop_create (&dflt_underlay_vlan_nh_id,
                                                 saiL3Test::default_nh_attr_count,
                                                 SAI_NEXT_HOP_ATTR_TYPE,
                                                 SAI_NEXT_HOP_TYPE_IP,
                                                 SAI_NEXT_HOP_ATTR_IP,
                                                 SAI_IP_ADDR_FAMILY_IPV4,
                                                 nh_ip_str_2,
                                                 SAI_NEXT_HOP_ATTR_ROUTER_INTERFACE_ID,
                                                 dflt_vlan_rif_id);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);

    /* Next-hop Group Creation */
    nh_list [ecmp_count] = dflt_underlay_port_nh_id;
    ecmp_count++;
    nh_list [ecmp_count] = dflt_underlay_vlan_nh_id;
    ecmp_count++;

    status = saiL3Test::sai_test_nh_group_create (&dflt_underlay_nhg_id, nh_list,
                                                  saiL3Test::default_nh_group_attr_count,
                                                  SAI_NEXT_HOP_GROUP_ATTR_TYPE,
                                                  SAI_NEXT_HOP_GROUP_TYPE_ECMP,
                                                  SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_LIST,
                                                  ecmp_count);

    ASSERT_EQ (SAI_STATUS_SUCCESS, status);
}

void saiTunnelTest::sai_test_tunnel_underlay_router_tear_down (void)
{
    sai_status_t  status;

    /* Remove all Next-hop Groups */
    status = saiL3Test::sai_test_nh_group_remove (dflt_underlay_nhg_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    /* Remove all Next-hops */
    status = saiL3Test::sai_test_nexthop_remove (dflt_underlay_vlan_nh_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_nexthop_remove (dflt_underlay_port_nh_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    /* Remove all Neighbors */
    status = saiL3Test::sai_test_neighbor_remove (dflt_port_rif_id,
                                                  SAI_IP_ADDR_FAMILY_IPV4,
                                                  nh_ip_str_1);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_neighbor_remove (dflt_vlan_rif_id,
                                                  SAI_IP_ADDR_FAMILY_IPV4,
                                                  nh_ip_str_2);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    /* Remove all RIF */
    status = saiL3Test::sai_test_rif_remove (dflt_port_rif_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    status = saiL3Test::sai_test_rif_remove (dflt_vlan_rif_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);

    /* Remove VRF */
    status = saiL3Test::sai_test_vrf_remove (dflt_vr_id);

    EXPECT_EQ (SAI_STATUS_SUCCESS, status);
}

sai_object_id_t saiTunnelTest::sai_test_tunnel_port_id_get (uint32_t port_index)
{
    if(port_index >= port_count) {
        return 0;
    }

    return port_list [port_index];
}


/* SAI Tunnel API Query */
void saiTunnelTest::sai_test_tunnel_api_table_get (void)
{
    /*
     * Query and populate the SAI Tunnel API Table.
     */
    EXPECT_EQ (SAI_STATUS_SUCCESS, sai_api_query
               (SAI_API_TUNNEL, (static_cast<void**>
                                (static_cast<void*>(&p_sai_tunnel_api_tbl)))));

    ASSERT_TRUE (p_sai_tunnel_api_tbl != NULL);
}

void saiTunnelTest::sai_test_tunnel_attr_value_fill (unsigned int attr_count,
                                                     va_list *p_varg_list,
                                                     sai_attribute_t *p_attr_list)
{
    unsigned int     af;
    unsigned int     index;
    const char      *p_ip_str;
    sai_attribute_t *p_attr;

    for (index = 0; index < attr_count; index++) {

        p_attr = &p_attr_list [index];

        p_attr->id = va_arg ((*p_varg_list), unsigned int);

        switch (p_attr->id) {

            case SAI_TUNNEL_ATTR_ENCAP_SRC_IP:
                af       = va_arg ((*p_varg_list), unsigned int);
                p_ip_str = va_arg ((*p_varg_list), const char *);

                if (af == SAI_IP_ADDR_FAMILY_IPV4) {

                    inet_pton (AF_INET, p_ip_str,
                               (void *)&p_attr->value.ipaddr.addr.ip4);
                } else {

                    inet_pton (AF_INET6, p_ip_str,
                               (void *)&p_attr->value.ipaddr.addr.ip6);
                }

                p_attr->value.ipaddr.addr_family = (sai_ip_addr_family_t) af;
                TUNNEL_PRINT ("Attr Index: %d, Set Encap SIP Addr family: %d, "
                              "IP Addr: %s.", index, af, p_ip_str);
                break;

            case SAI_TUNNEL_ATTR_UNDERLAY_INTERFACE:
            case SAI_TUNNEL_ATTR_OVERLAY_INTERFACE:
                p_attr->value.oid = va_arg ((*p_varg_list), unsigned long);
                TUNNEL_PRINT ("Attr Index: %d, Set RIF Id value: 0x%"PRIx64".",
                              index, p_attr->value.oid);
                break;

            case SAI_TUNNEL_ATTR_TYPE:
            case SAI_TUNNEL_ATTR_ENCAP_TTL_MODE:
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_MODE:
            case SAI_TUNNEL_ATTR_ENCAP_ECN_MODE:
            case SAI_TUNNEL_ATTR_DECAP_TTL_MODE:
            case SAI_TUNNEL_ATTR_DECAP_DSCP_MODE:
            case SAI_TUNNEL_ATTR_DECAP_ECN_MODE:
                p_attr->value.s32 = va_arg ((*p_varg_list), unsigned int);
                break;

            case SAI_TUNNEL_ATTR_ENCAP_TTL_VAL:
            case SAI_TUNNEL_ATTR_ENCAP_DSCP_VAL:
                p_attr->value.u8 = va_arg ((*p_varg_list), unsigned int);
                break;

            default:
                p_attr->value.u64 = va_arg ((*p_varg_list), unsigned int);
                TUNNEL_PRINT ("Attr Index: %d, Set Attr Id: %d to value: %ld.",
                              index, p_attr->id, p_attr->value.u64);
                break;
        }
    }
}

sai_status_t saiTunnelTest::sai_test_tunnel_create (
                                               sai_object_id_t *tunnel_id,
                                               unsigned int attr_count, ...)
{
    sai_status_t     status = SAI_STATUS_FAILURE;
    va_list          ap;
    sai_attribute_t  attr_list [max_tunnel_obj_attr_count];

    if (attr_count > max_tunnel_obj_attr_count) {

        TUNNEL_PRINT ("%s(): Attr count %u is greater than Tunnel attr count "
                      "%u.", __FUNCTION__, attr_count, max_tunnel_obj_attr_count);

        return status;
    }

    memset (attr_list, 0, sizeof (attr_list));

    TUNNEL_PRINT ("Testing Tunnel object Creation with attribute count: %u.",
                  attr_count);

    va_start (ap, attr_count);

    sai_test_tunnel_attr_value_fill (attr_count, &ap, attr_list);

    va_end (ap);

    status = p_sai_tunnel_api_tbl->create_tunnel (tunnel_id, attr_count,
                                                  attr_list);

    if (status != SAI_STATUS_SUCCESS) {
        TUNNEL_PRINT ("SAI Tunnel Creation API failed with error: %d.", status);
    } else {
        TUNNEL_PRINT ("SAI Tunnel Creation API success, Tunnel object Id: "
                      "0x%"PRIx64".", *tunnel_id);
    }

    return status;
}

sai_status_t saiTunnelTest::sai_test_tunnel_remove (sai_object_id_t tunnel_id)
{
    sai_status_t       status;

    TUNNEL_PRINT ("Testing Tunnel object Id: 0x%"PRIx64" remove.", tunnel_id);

    status = p_sai_tunnel_api_tbl->remove_tunnel (tunnel_id);

    if (status != SAI_STATUS_SUCCESS) {
        TUNNEL_PRINT ("SAI Tunnel remove API failed with error: %d.", status);
    } else {
        TUNNEL_PRINT ("SAI Tunnel remove API success.");
    }

    return status;
}

void saiTunnelTest::sai_test_tunnel_term_attr_value_fill (unsigned int attr_count,
                                                          va_list *p_varg_list,
                                                          sai_attribute_t *p_attr_list)
{
    unsigned int     af;
    unsigned int     index;
    const char      *p_ip_str;
    sai_attribute_t *p_attr;

    for (index = 0; index < attr_count; index++) {

        p_attr = &p_attr_list [index];

        p_attr->id = va_arg ((*p_varg_list), unsigned int);

        switch (p_attr->id) {

            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_DST_IP:
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_SRC_IP:
                af       = va_arg ((*p_varg_list), unsigned int);
                p_ip_str = va_arg ((*p_varg_list), const char *);

                if (af == SAI_IP_ADDR_FAMILY_IPV4) {

                    inet_pton (AF_INET, p_ip_str,
                               (void *)&p_attr->value.ipaddr.addr.ip4);
                } else {

                    inet_pton (AF_INET6, p_ip_str,
                               (void *)&p_attr->value.ipaddr.addr.ip6);
                }

                p_attr->value.ipaddr.addr_family = (sai_ip_addr_family_t) af;
                TUNNEL_PRINT ("Attr Index: %d, Set Termination IP Addr family: %d, "
                              "IP Addr: %s.", index, af, p_ip_str);
                break;

            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_ACTION_TUNNEL_ID:
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_VR_ID:
                p_attr->value.oid = va_arg ((*p_varg_list), unsigned long);
                TUNNEL_PRINT ("Attr Index: %d, Set OId value: 0x%"PRIx64".",
                              index, p_attr->value.oid);
                break;

            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TYPE:
            case SAI_TUNNEL_TERM_TABLE_ENTRY_ATTR_TUNNEL_TYPE:
                p_attr->value.s32 = va_arg ((*p_varg_list), unsigned int);
                break;

            default:
                p_attr->value.u64 = va_arg ((*p_varg_list), unsigned int);
                TUNNEL_PRINT ("Attr Index: %d, Set Attr Id: %d to value: %ld.",
                              index, p_attr->id, p_attr->value.u64);
                break;
        }
    }
}

sai_status_t saiTunnelTest::sai_test_tunnel_term_entry_create (
                                               sai_object_id_t *tunnel_term_id,
                                               unsigned int attr_count, ...)
{
    sai_status_t     status = SAI_STATUS_FAILURE;
    va_list          ap;
    sai_attribute_t  attr_list [max_tunnel_term_attr_count];

    if (attr_count > max_tunnel_term_attr_count) {

        TUNNEL_PRINT ("%s(): Attr count %u is greater than Max attr count "
                      "%u.", __FUNCTION__, attr_count, max_tunnel_term_attr_count);

        return status;
    }

    memset (attr_list, 0, sizeof (attr_list));

    TUNNEL_PRINT ("Testing Tunnel Termination table entry object Creation "
                  "with attribute count: %u.", attr_count);

    va_start (ap, attr_count);

    sai_test_tunnel_term_attr_value_fill (attr_count, &ap, attr_list);

    va_end (ap);

    status = p_sai_tunnel_api_tbl->create_tunnel_term_table_entry (
                             tunnel_term_id, attr_count, attr_list);

    if (status != SAI_STATUS_SUCCESS) {
        TUNNEL_PRINT ("SAI Tunnel Termination table entry Creation API failed "
                      "with error: %d.", status);
    } else {
        TUNNEL_PRINT ("SAI Tunnel Termination table entry Creation API success"
                      ", Tunnel object Id: 0x%"PRIx64".", *tunnel_term_id);
    }

    return status;
}

sai_status_t saiTunnelTest::sai_test_tunnel_term_entry_remove (
                                             sai_object_id_t tunnel_term_id)
{
    sai_status_t       status;

    TUNNEL_PRINT ("Testing Tunnel Termination table entry object Id: "
                  "0x%"PRIx64" remove.", tunnel_term_id);

    status = p_sai_tunnel_api_tbl->remove_tunnel_term_table_entry (tunnel_term_id);

    if (status != SAI_STATUS_SUCCESS) {
        TUNNEL_PRINT ("SAI Tunnel Termination table entry remove API failed "
                      "with error: %d.", status);
    } else {
        TUNNEL_PRINT ("SAI Tunnel Termination table entry remove API success.");
    }

    return status;
}

