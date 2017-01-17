LIBRARIES=sai-common

UNIT_TEST += sai_switch_unit_test
sai_switch_unit_test_SRCS= unit_test/sai_infra_unit_test.cpp unit_test/sai_switch_unit_test.cpp unit_test/port/sai_port_breakout_test_utils.cpp
sai_switch_unit_test_LDFLAGS= -lsai-common
sai_switch_unit_test_CPPFLAGS=-Iunit_test/port

UNIT_TEST += sai_stp_unit_test
sai_stp_unit_test_SRCS= unit_test/switching/sai_stp_unit_test.cpp
sai_stp_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_port_unit_test
sai_port_unit_test_SRCS= unit_test/port/sai_port_unit_test.cpp unit_test/port/sai_port_breakout_test_utils.cpp
sai_port_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_fdb_unit_test
sai_fdb_unit_test_SRCS+= unit_test/switching/sai_fdb_unit_test.cpp
sai_fdb_unit_test_SRCS+= unit_test/switching/internal/sai_fdb_unit_test_internal.cpp
sai_fdb_unit_test_CPPFLAGS= -Iunit_test/switching
sai_fdb_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_vlan_unit_test
sai_vlan_unit_test_SRCS+= unit_test/switching/sai_vlan_unit_test.cpp
sai_vlan_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_lag_unit_test
sai_lag_unit_test_SRCS+= unit_test/switching/sai_lag_unit_test.cpp
sai_lag_unit_test_SRCS+= unit_test/switching/internal/sai_lag_unit_test_internal.cpp
sai_lag_unit_test_CPPFLAGS= -Iunit_test/switching
sai_lag_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_vrf_unit_test
sai_l3_vrf_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_vrf_unit_test.cpp
sai_l3_vrf_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_rif_unit_test
sai_l3_rif_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_rif_unit_test.cpp
sai_l3_rif_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_route_unit_test
sai_l3_route_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_route_unit_test.cpp
sai_l3_route_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_nexthop_unit_test
sai_l3_nexthop_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_nexthop_unit_test.cpp
sai_l3_nexthop_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_neighbor_unit_test
sai_l3_neighbor_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_neighbor_unit_test.cpp
sai_l3_neighbor_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_l3_nexthopgroup_unit_test
sai_l3_nexthopgroup_unit_test_SRCS= unit_test/routing/sai_l3_unit_test_utils.cpp unit_test/routing/sai_l3_nexthopgroup_unit_test.cpp
sai_l3_nexthopgroup_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_acl_table_unit_test
sai_acl_table_unit_test_SRCS= unit_test/acl/sai_acl_unit_test_utils.cpp unit_test/acl/sai_acl_table_unit_test.cpp unit_test/routing/sai_l3_unit_test_utils.cpp
sai_acl_table_unit_test_LDFLAGS= -lsai-common
sai_acl_table_unit_test_CPPFLAGS=-Iunit_test/routing

UNIT_TEST += sai_acl_rule_unit_test
sai_acl_rule_unit_test_SRCS= unit_test/acl/sai_acl_unit_test_utils.cpp unit_test/acl/sai_acl_rule_unit_test.cpp unit_test/routing/sai_l3_unit_test_utils.cpp
sai_acl_rule_unit_test_SRCS+= unit_test/udf/sai_udf_unit_test_utils.cpp
sai_acl_rule_unit_test_LDFLAGS= -lsai-common
sai_acl_rule_unit_test_CPPFLAGS=-Iunit_test/routing
sai_acl_rule_unit_test_CPPFLAGS+=-Iunit_test/udf

UNIT_TEST += sai_acl_counter_unit_test
sai_acl_counter_unit_test_SRCS= unit_test/acl/sai_acl_unit_test_utils.cpp unit_test/acl/sai_acl_counter_unit_test.cpp
sai_acl_counter_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_mirror_unit_test
sai_mirror_unit_test_SRCS = unit_test/mirroring/sai_mirror_unit_test.cpp unit_test/mirroring/sai_mirror_unit_test_utils.cpp unit_test/port/sai_port_breakout_test_utils.cpp
sai_mirror_unit_test_LDFLAGS= -lsai-common
sai_mirror_unit_test_CPPFLAGS=-Iunit_test/port

UNIT_TEST += sai_hostif_unit_test
sai_hostif_unit_test_SRCS= unit_test/hostintf/sai_hostif_unit_test.cpp
sai_hostif_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_samplepacket_unit_test
sai_samplepacket_unit_test_SRCS= unit_test/samplepacket/sai_samplepacket_unit_test.cpp unit_test/samplepacket/sai_samplepacket_unit_test_utils.cpp
sai_samplepacket_unit_test_SRCS+=unit_test/port/sai_port_breakout_test_utils.cpp
sai_samplepacket_unit_test_LDFLAGS= -lsai-common
sai_samplepacket_unit_test_CPPFLAGS=-Iunit_test/port

UNIT_TEST += sai_qos_scheduler_group_unit_test
sai_qos_scheduler_group_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_qos_scheduler_group_unit_test.cpp
sai_qos_scheduler_group_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_qos_queue_unit_test
sai_qos_queue_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_qos_queue_unit_test.cpp
sai_qos_queue_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_qos_scheduler_unit_test
sai_qos_scheduler_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_qos_scheduler_unit_test.cpp
sai_qos_scheduler_unit_test_LDFLAGS= -lsai-common


UNIT_TEST += sai_qos_maps_unit_test
sai_qos_maps_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_qos_maps_unit_test.cpp
sai_qos_maps_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_qos_policer_unit_test
sai_qos_policer_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_policer_unit_test.cpp
sai_qos_policer_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_qos_wred_unit_test
sai_qos_wred_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_wred_unit_test.cpp
sai_qos_wred_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_qos_buffer_unit_test
sai_qos_buffer_unit_test_SRCS = unit_test/qos/sai_qos_unit_test_utils.cpp unit_test/qos/sai_qos_buffer_unit_test.cpp
sai_qos_buffer_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_udf_unit_test
sai_udf_unit_test_SRCS = unit_test/udf/sai_udf_unit_test.cpp unit_test/udf/sai_udf_unit_test_utils.cpp
sai_udf_unit_test_LDFLAGS= -lsai-common

UNIT_TEST += sai_hash_unit_test
sai_hash_unit_test_SRCS = unit_test/hash/sai_hash_unit_test.cpp unit_test/hash/sai_hash_unit_test_utils.cpp unit_test/udf/sai_udf_unit_test_utils.cpp
sai_hash_unit_test_LDFLAGS= -lsai-common
sai_hash_unit_test_CPPFLAGS=-Iunit_test/udf

UNIT_TEST += sai_ip_tunnel_unit_test
sai_ip_tunnel_unit_test_SRCS = unit_test/tunnel/sai_ip_tunnel_unit_test.cpp
sai_ip_tunnel_unit_test_SRCS += unit_test/tunnel/sai_tunnel_unit_test_utils.cpp unit_test/routing/sai_l3_unit_test_utils.cpp
sai_ip_tunnel_unit_test_LDFLAGS= -lsai-common
sai_ip_tunnel_unit_test_CPPFLAGS=-Iunit_test/routing

SWITCHINFRA_SRCS:=$(wildcard switchinfra/*.c)

PORT_SRCS:=$(wildcard port/*.c)

SWITCHING_SRCS:=$(wildcard switching/*.c)

ROUTING_SRCS:=$(wildcard routing/*.c)

SHELL_SRCS:=$(wildcard shell/*.c)

ACL_SRCS:=$(wildcard acl/*.c)

QOS_SRCS:=$(wildcard qos/*.c)

MIRRORING_SRCS:=$(wildcard mirroring/*.c)

HOSTIF_SRCS:=$(wildcard hostintf/*.c)

SAMPLEPACKET_SRCS:=$(wildcard samplepacket/*.c)

UDF_SRCS:=$(wildcard udf/*.c)

HASH_SRCS:=$(wildcard hash/*.c)

TUNNEL_SRCS:=$(wildcard tunnel/*.c)

sai-common_SRCS=$(ROUTING_SRCS)

sai-common_SRCS+= ${SWITCHINFRA_SRCS} ${ACL_SRCS}

sai-common_SRCS+= $(PORT_SRCS)

sai-common_SRCS+= ${SWITCHING_SRCS}

sai-common_SRCS+= ${MIRRORING_SRCS}

sai-common_SRCS+= ${SHELL_SRCS}

sai-common_SRCS+= ${HOSTIF_SRCS}

sai-common_SRCS+= ${QOS_SRCS}

sai-common_SRCS+= ${SAMPLEPACKET_SRCS}

sai-common_SRCS+= ${UDF_SRCS}

sai-common_SRCS+= ${HASH_SRCS}

sai-common_SRCS+= ${TUNNEL_SRCS}

sai-common_LDFLAGS+= -ldn_common -levent_log -lsai_common_utils -ldl

include ${MAKE_INC}/workspace.mak
