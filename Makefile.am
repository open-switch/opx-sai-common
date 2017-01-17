ACLOCAL_AMFLAGS=-I m4
SUBDIRS = inc

lib_LTLIBRARIES = libopx_sai_common.la

libopx_sai_common_la_SOURCES = \
src/acl/sai_acl_counter.c src/acl/sai_acl_debug.c \
src/acl/sai_acl_init.c src/acl/sai_acl_policer.c \
src/acl/sai_acl_rule.c src/acl/sai_acl_rule_utils.c \
src/acl/sai_acl_table.c \
src/hash/sai_hash_obj.c \
src/hostintf/sai_hostintf.c  src/hostintf/sai_hostintf_debug.c \
src/hostintf/sai_hostintf_utils.c \
src/mirroring/sai_mirror_common.c  src/mirroring/sai_mirror_debug.c \
src/mirroring/sai_mirror_port.c  src/mirroring/sai_mirror_utils.c \
src/port/sai_port.c \
src/qos/sai_qos_buffer.c src/qos/sai_qos_init.c  src/qos/sai_qos_pfc_maps.c \
src/qos/sai_qos_port.c src/qos/sai_qos_scheduler.c src/qos/sai_qos_buffer_common.c \
src/qos/sai_qos_maps.c src/qos/sai_qos_pg.c src/qos/sai_qos_queue.c \
src/qos/sai_qos_scheduler_util.c src/qos/sai_qos_hierarchy.c src/qos/sai_qos_map_utils.c  \
src/qos/sai_qos_policer.c src/qos/sai_qos_sched_group.c  src/qos/sai_qos_wred.c \
src/routing/sai_l3_encap_next_hop.c  src/routing/sai_l3_neighbor.c  \
src/routing/sai_l3_next_hop_group.c  src/routing/sai_l3_router_interface.c \
src/routing/sai_l3_mem.c  src/routing/sai_l3_next_hop.c  src/routing/sai_l3_route.c \
src/routing/sai_l3_vrf.c src/samplepacket/sai_samplepacket_common.c  \
src/samplepacket/sai_samplepacket_debug.c  src/samplepacket/sai_samplepacket_port.c \
src/samplepacket/sai_samplepacket_utils.c \
src/shell/sai_shell.c  src/shell/sai_shell_init.c \
src/switchinfra/sai_func_query.c src/switchinfra/sai_switch.c \
src/switchinfra/sai_switch_init_config.c \
src/switching/sai_fdb.c  src/switching/sai_lag.c  src/switching/sai_stp.c  \
src/switching/sai_stp_utils.c  src/switching/sai_vlan.c \
src/tunnel/sai_tunnel_obj.c  src/tunnel/sai_tunnel_term_obj.c \
src/udf/sai_udf.c  src/udf/sai_udf_group.c  src/udf/sai_udf_utils.c

libopx_sai_common_la_CFLAGS= -I$(top_srcdir)/inc/opx -I$(includedir)/opx
libopx_sai_common_la_LDFLAGS= -shared -version-info 1:1:0
libopx_sai_common_la_LIBADD= -lopx_common -lopx_logging -lopx_sai_common_utils -lpthread -ldl


