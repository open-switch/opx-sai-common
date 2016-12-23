/************************************************************************
 * LEGALESE:   "Copyright (c) 2015, Dell Inc. All rights reserved."
 *
 * This source code is confidential, proprietary, and contains trade
 * secrets that are the sole property of Dell Inc.
 * Copy and/or distribution of this source code or disassembly or reverse
 * engineering of the resultant object code are strictly forbidden without
 * the written consent of Dell Inc.
 *
 ************************************************************************/
/*!
 * \file   sai_switch.c
 * \brief  SAI Switch functionality
 * \date   12-2014
 * */

#include "std_type_defs.h"
#include "sai.h"
#include "std_utils.h"
#include "sai_modules_init.h"
#include "sai_infra_api.h"
#include "sai_common_infra.h"
#include "sai_switch_utils.h"
#include "std_assert.h"
#include "sai_port_main.h"
#include "sai_npu_switch.h"
#include "sai_switch_utils.h"
#include "sai_gen_utils.h"
#include "sai_fdb_api.h"
#include "sai_fdb_main.h"
#include "sai_vlan_api.h"
#include "sai_l3_api.h"
#include "sai_shell.h"
#include "sai_stp_api.h"
#include "sai_l3_api_utils.h"
#include "sai_hostif_api.h"
#include "sai_common_infra.h"
#include "sai_switch_init_config.h"
#include "sai_qos_api_utils.h"
#include "sai_hash_api_utils.h"

static sai_status_t sai_switch_mac_address_set(const sai_mac_t *mac)
{
    sai_status_t status = SAI_STATUS_SUCCESS;

    STD_ASSERT(mac != NULL);
    sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get();

    status = sai_router_npu_api_get()->router_mac_set (mac);

    if (status != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_ERR("Failed to configure Router MAC address.");
        return status;
    }

    memcpy(sai_switch_info_ptr->switch_mac_addr, mac,
           sizeof(sai_switch_info_ptr->switch_mac_addr));

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_switch_common_counter_refresh_interval_set(const sai_attribute_value_t *value)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    STD_ASSERT(value != NULL);

    SAI_SWITCH_LOG_TRACE("SAI NPU counter refresh interval %d seconds set", value->u32);
    sai_switch_lock();

    ret_val = sai_switch_npu_api_get()->counter_refresh_interval_set(value->u32);
    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_switch_unlock();

        SAI_SWITCH_LOG_ERR("SAI NPU counter refresh interval %d set failed with error %d",
                           value->u32, ret_val);
        return ret_val;
    }

    /* Update the cache with configured counter refresh interval */
    sai_switch_counter_refresh_interval_set(value->u32);

    sai_switch_unlock();

    return ret_val;
}

/* Read the counter interval from the cached switch info */
static sai_status_t sai_switch_common_counter_refresh_interval_get(sai_attribute_value_t *value)
{
    STD_ASSERT(value != NULL);

    sai_switch_lock();
    /* @todo relook if NPU support reading counter interval from SDK */
    value->u32 = sai_switch_counter_refresh_interval_get();
    sai_switch_unlock();

    SAI_SWITCH_LOG_TRACE("SAI counter refresh interval is  %d seconds", value->u32);

    return SAI_STATUS_SUCCESS;
}

static inline sai_status_t sai_switch_get_lag_attribute (sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    STD_ASSERT (attr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
            ret_val = sai_switch_npu_api_get()->lag_hash_algorithm_get ((sai_hash_algorithm_t *)
                                                                        &attr->value.s32);
            break;

        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
            ret_val = sai_switch_npu_api_get()->lag_hash_seed_value_get ((sai_switch_hash_seed_t *)
                                                                         (&attr->value.u32));
            break;

        case SAI_SWITCH_ATTR_LAG_MEMBERS:
            ret_val = sai_switch_npu_api_get()->lag_max_members_get (&attr->value.u32);
            break;

        case SAI_SWITCH_ATTR_NUMBER_OF_LAGS:
            ret_val = sai_switch_npu_api_get()->lag_max_number_get (&attr->value.u32);
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            break;
    }
    return ret_val;
}

static sai_status_t sai_switch_ecmp_max_paths_attr_set (uint32_t max_paths)
{
    sai_status_t status = sai_router_ecmp_max_paths_set (max_paths);

    if (status == SAI_STATUS_SUCCESS) {

        sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get();

        sai_switch_info_ptr->max_ecmp_paths = max_paths;

    } else {

        SAI_SWITCH_LOG_ERR ("Failed to set ECMP max paths attribute.");
    }

    return status;
}

/* Inline functions to handle get and set attributes */
static inline sai_status_t sai_switch_get_ecmp_attribute (sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    uint_t       max_ecmp = 0;

    STD_ASSERT(attr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            ret_val = sai_switch_npu_api_get()->ecmp_hash_algorithm_get ((sai_hash_algorithm_t *)
                                                                         &attr->value.s32);
            break;

        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
            ret_val = sai_switch_npu_api_get()->ecmp_hash_seed_value_get ((sai_switch_hash_seed_t *)
                                                                          &attr->value.u32);
            break;

        case SAI_SWITCH_ATTR_ECMP_MEMBERS:
            ret_val = sai_switch_max_ecmp_paths_get(&max_ecmp);
            attr->value.u32 = max_ecmp;
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            break;
    }
    return ret_val;
}

static sai_status_t sai_switch_get_gen_attribute(sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    uint_t       max_vrf = 0;

    STD_ASSERT(attr != NULL);
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_SWITCHING_MODE:
            ret_val = sai_switch_npu_api_get()->switching_mode_get(
                           (sai_switch_switching_mode_t *)&attr->value.s32);
            break;
        case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
            ret_val = sai_switch_mac_address_get(&attr->value.mac);
            break;
        case SAI_SWITCH_ATTR_PORT_NUMBER:
            attr->value.u32 = sai_switch_get_max_lport();
            ret_val = SAI_STATUS_SUCCESS;
            break;
        case SAI_SWITCH_ATTR_PORT_MAX_MTU:
            attr->value.u32 = sai_switch_get_max_port_mtu();
            ret_val = SAI_STATUS_SUCCESS;
            break;
        case SAI_SWITCH_ATTR_PORT_LIST:
            ret_val = sai_port_list_get((sai_object_list_t *)
                                        &attr->value.objlist);
            break;
        case SAI_SWITCH_ATTR_CPU_PORT:
            attr->value.oid = sai_switch_cpu_port_obj_id_get();
            ret_val = SAI_STATUS_SUCCESS;
            break;
        case SAI_SWITCH_ATTR_MAX_VIRTUAL_ROUTERS:
            ret_val = sai_switch_max_virtual_routers_get(&max_vrf);
            attr->value.u32 = max_vrf;
            break;
        case SAI_SWITCH_ATTR_ON_LINK_ROUTE_SUPPORTED:
            break;
        case SAI_SWITCH_ATTR_OPER_STATUS:
            ret_val = sai_switch_oper_status_get((sai_switch_oper_status_t *)
                                                 &attr->value.s32);
            break;
        case SAI_SWITCH_ATTR_MAX_TEMP:
            ret_val = sai_switch_npu_api_get()->switch_temp_get(&attr->value);
            break;
        case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
            ret_val = sai_switch_common_counter_refresh_interval_get(&attr->value);
            break;
        default:
            SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list", attr->id);
            return SAI_STATUS_INVALID_ATTRIBUTE_0;
    }

    return ret_val;
}

static sai_status_t sai_switch_get_l2_attribute(sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;

    STD_ASSERT(attr != NULL);
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            ret_val = sai_l2_mcast_cpu_flood_enable_get((bool*)&attr->value.booldata);
            break;
        case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
            ret_val = sai_l2_bcast_cpu_flood_enable_get((bool*)&attr->value.booldata);
            break;
        case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
            ret_val = sai_l2_fdb_get_switch_max_learned_address(&(attr->value.u32));
            break;
        case SAI_SWITCH_ATTR_FDB_TABLE_SIZE:
            ret_val = sai_l2_get_fdb_table_size(attr);
            break;
        case SAI_SWITCH_ATTR_FDB_AGING_TIME:
            ret_val = sai_l2_fdb_get_aging_time(&(attr->value.u32));
            break;
        case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_ACTION:
            ret_val = sai_l2_fdb_ucast_miss_action_get(attr);
            break;
        case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_ACTION:
            ret_val = sai_l2_fdb_bcast_miss_action_get(attr);
            break;
        case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_ACTION:
            ret_val = sai_l2_fdb_mcast_miss_action_get(attr);
            break;
        case SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID:
            ret_val = sai_l2_stp_default_instance_id_get(attr);
            break;
        default:
            SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list", attr->id);
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
    }
    return ret_val;
}

static void sai_switch_info_update_l2_attribute (const sai_attribute_t *attr)
{
    sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get ();

    STD_ASSERT (attr != NULL);

    switch (attr->id) {
        case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
            sai_switch_info_ptr->isBcastCpuFloodEnable =
                (bool) attr->value.booldata;
            break;

        case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            sai_switch_info_ptr->isMcastCpuFloodEnable =
                (bool) attr->value.booldata;
            break;

        case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
            sai_switch_info_ptr->max_mac_learn_limit =
                (uint32_t) attr->value.u32;
            break;

        case SAI_SWITCH_ATTR_FDB_AGING_TIME:
            sai_switch_info_ptr->fdb_aging_time = (uint32_t) attr->value.u32;
            break;

        case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_ACTION:
            sai_switch_info_ptr->fdbUcastMissPktAction =
                (int32_t) attr->value.s32;
            break;

        case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_ACTION:
            sai_switch_info_ptr->fdbBcastMissPktAction =
                (int32_t) attr->value.s32;
            break;

        case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_ACTION:
            sai_switch_info_ptr->fdbMcastMissPktAction =
                (int32_t) attr->value.s32;
            break;
    }
}

static sai_status_t sai_switch_cache_lag_hash_info (const sai_attribute_t *attr)
{
    sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get ();

    STD_ASSERT (attr != NULL);
    STD_ASSERT (sai_switch_info_ptr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
            sai_switch_info_ptr->lag_hash_algo = attr->value.s32;
            break;

        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
            sai_switch_info_ptr->lag_hash_seed = attr->value.u32;
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_switch_cache_ecmp_hash_info (const sai_attribute_t *attr)
{
    sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get ();

    STD_ASSERT (attr != NULL);
    STD_ASSERT (sai_switch_info_ptr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            sai_switch_info_ptr->ecmp_hash_algo = attr->value.s32;
            break;

        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
            sai_switch_info_ptr->ecmp_hash_seed = attr->value.u32;
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            return SAI_STATUS_UNKNOWN_ATTRIBUTE_0;
    }

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_switch_set_lag_attribute (const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;

    STD_ASSERT (attr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
            ret_val = sai_switch_npu_api_get()->lag_hash_algorithm_set ((sai_hash_algorithm_t)
                                                                        attr->value.s32);
            break;

        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
            ret_val = sai_switch_npu_api_get()->lag_hash_seed_value_set ((sai_switch_hash_seed_t)
                                                                         attr->value.u32);
            break;

        case SAI_SWITCH_ATTR_LAG_MEMBERS:
        case SAI_SWITCH_ATTR_NUMBER_OF_LAGS:
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            break;
    }

    if (ret_val == SAI_STATUS_SUCCESS)
    {
        sai_switch_cache_lag_hash_info (attr);
    }

    return ret_val;
}

static sai_status_t sai_switch_set_ecmp_attribute (const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;

    STD_ASSERT(attr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            ret_val = sai_switch_npu_api_get()->ecmp_hash_algorithm_set ((sai_hash_algorithm_t)
                                                                         attr->value.s32);
            break;

        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
            ret_val = sai_switch_npu_api_get()->ecmp_hash_seed_value_set ((sai_switch_hash_seed_t)
                                                                          attr->value.u32);
            break;

        case SAI_SWITCH_ATTR_ECMP_MEMBERS:
            ret_val = sai_switch_ecmp_max_paths_attr_set (attr->value.u32);
            break;

        default:
            SAI_SWITCH_LOG_ERR ("Invalid Attribute Id %d in list", attr->id);
            break;
    }

    if (ret_val == SAI_STATUS_SUCCESS)
    {
        sai_switch_cache_ecmp_hash_info (attr);
    }
    return ret_val;
}

static sai_status_t sai_switch_set_gen_attribute(const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_ATTR_NOT_SUPPORTED_0;

    STD_ASSERT(attr != NULL);
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_SWITCHING_MODE:
            ret_val = sai_switch_npu_api_get()->switching_mode_set(
                                 (sai_switch_switching_mode_t)attr->value.s32);
            break;
        case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
            ret_val = sai_switch_mac_address_set(&attr->value.mac);
            break;
        case SAI_SWITCH_ATTR_PORT_BREAKOUT:
            ret_val = sai_port_breakout_set((sai_port_breakout_t *)
                                            &attr->value.portbreakout);
            break;
        case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
            ret_val = sai_switch_common_counter_refresh_interval_set(&attr->value);
            break;
        default:
            SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list", attr->id);
            return SAI_STATUS_INVALID_ATTRIBUTE_0;
    }
    return ret_val;
}

static sai_status_t sai_switch_set_l2_attribute(const sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_UNKNOWN_ATTRIBUTE_0;

    STD_ASSERT(attr != NULL);
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
            ret_val = sai_l2_bcast_cpu_flood_enable_set((bool)
                                                        attr->value.booldata);
            break;
        case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            ret_val = sai_l2_mcast_cpu_flood_enable_set((bool)
                                                        attr->value.booldata);
            break;
        case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
            ret_val = sai_l2_fdb_set_switch_max_learned_address(attr->value.u32);
            break;
        case SAI_SWITCH_ATTR_FDB_AGING_TIME:
            ret_val = sai_l2_fdb_set_aging_time(attr->value.u32);
            break;
        case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_ACTION:
            ret_val = sai_l2_fdb_ucast_miss_action_set(attr);
            break;
        case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_ACTION:
            ret_val = sai_l2_fdb_bcast_miss_action_set(attr);
            break;
        case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_ACTION:
            ret_val = sai_l2_fdb_mcast_miss_action_set(attr);
            break;
        default:
            SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list", attr->id);
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
    }

    /* Update the new attribute setting in the switch info DB */
    if (ret_val == SAI_STATUS_SUCCESS) {
        sai_switch_info_update_l2_attribute (attr);
    }

    return ret_val;
}

static sai_status_t sai_switch_get_hostif_attribute(sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_FAILURE;

    STD_ASSERT(attr != NULL);
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
            ret_val = sai_hostif_get_default_trap_group(attr);
            break;
        default:
            SAI_SWITCH_LOG_ERR("Invalid hostif switch attribute %d", attr->id);
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
    }

    return ret_val;
}

static sai_status_t sai_switch_get_qos_attribute(sai_attribute_t *attr)
{
    sai_status_t ret_val = SAI_STATUS_SUCCESS;

    STD_ASSERT(attr != NULL);

    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_TRAFFIC_CLASSES:
            attr->value.u32 = sai_switch_max_traffic_class_get ();
            break;

        case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUP_HIERARCHY_LEVELS:
            attr->value.u32 = sai_switch_max_hierarchy_levels_get ();
            break;

        case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUPS_PER_HIERARCHY_LEVEL:
            ret_val = SAI_STATUS_ATTR_NOT_IMPLEMENTED_0;
            break;

        case SAI_SWITCH_ATTR_QOS_DEFAULT_TC:
            attr->value.u32 = sai_switch_default_tc_get ();
            break;

        case SAI_SWITCH_ATTR_TOTAL_BUFFER_SIZE:
            attr->value.u32 = sai_switch_max_buffer_size_get ();
            break;

        case SAI_SWITCH_ATTR_INGRESS_BUFFER_POOL_NUM:
            attr->value.u32 = sai_switch_ing_max_buf_pools_get ();
            break;

        case SAI_SWITCH_ATTR_EGRESS_BUFFER_POOL_NUM:
            attr->value.u32 = sai_switch_egr_max_buf_pools_get ();
            break;

        case SAI_SWITCH_ATTR_NUMBER_OF_UNICAST_QUEUES:
            attr->value.u32 = sai_switch_max_uc_queues_get ();
            break;

        case SAI_SWITCH_ATTR_NUMBER_OF_MULTICAST_QUEUES:
            attr->value.u32 = sai_switch_max_mc_queues_get ();
            break;

        case SAI_SWITCH_ATTR_NUMBER_OF_QUEUES:
            attr->value.u32 = sai_switch_max_queues_get ();
            break;

        case SAI_SWITCH_ATTR_NUMBER_OF_CPU_QUEUES:
            attr->value.u32 = sai_switch_num_cpu_queues_get();
            break;

        default:
            SAI_SWITCH_LOG_ERR("Invalid qos switch attribute %d", attr->id);
            ret_val = SAI_STATUS_INVALID_ATTRIBUTE_0;
            break;
    }

    return ret_val;
}

static sai_status_t sai_switch_get_acl_attribute(sai_attribute_t *attr)
{
    sai_status_t sai_rc = SAI_STATUS_SUCCESS;

    STD_ASSERT(attr != NULL);
    sai_rc = sai_acl_npu_api_get()->get_acl_attribute(attr);

    if (sai_rc != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_ERR("Failed to fetch ACL priority");
    }

    return sai_rc;
}


static sai_status_t sai_switch_modules_initialize(void)
{
    sai_status_t ret_val = SAI_STATUS_UNINITIALIZED;

    if((ret_val = sai_port_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI port init failed with err %d",ret_val);
        return ret_val;
    }

    if((ret_val = sai_fdb_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI FDB init failed with err %d",ret_val);
        return ret_val;
    }

    if((ret_val = sai_vlan_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI VLAN init failed with err %d",ret_val);
        return ret_val;
    }

    if((ret_val = sai_lag_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI LAG init failed with err %d",ret_val);
        return ret_val;
    }

    if((ret_val = sai_shell_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Shell init failed with err %d",ret_val);
        return ret_val;
    }

    if ((ret_val = sai_router_init ()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Router Initialization failed with error: %d",
                            ret_val);
        return ret_val;
    }

    if ((ret_val = sai_acl_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI ACL init failed with error %d",ret_val);
        return ret_val;
    }
    if ((ret_val = sai_stp_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI STP init failed with error %d",ret_val);
        return ret_val;
    }

    if ((ret_val = sai_mirror_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Mirror init failed with error %d",ret_val);
        return ret_val;
    }
    if ((ret_val = sai_hostintf_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Host Interface Init failed with error %d",ret_val);
        return ret_val;
    }
    if ((ret_val = sai_qos_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Qos init failed with error %d",ret_val);
        return ret_val;
    }

    if ((ret_val = sai_samplepacket_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI SamplePacket init failed with error %d",
                            ret_val);
        return ret_val;
    }

    if ((ret_val = sai_udf_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI UDF init failed with error %d", ret_val);
        return ret_val;
    }

    if ((ret_val = sai_hash_obj_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Hash object init failed with error %d", ret_val);
        return ret_val;
    }

    if ((ret_val = sai_tunnel_init()) != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI Tunnel init failed with error %d", ret_val);
        return ret_val;
    }
    sai_port_init_event_notification();

    return ret_val;
}

static sai_status_t sai_switch_attribute_config(sai_switch_profile_id_t profile_id)
{
    int key_len = 0;
    uint_t value = 0;
    const char *key = NULL;
    const char *value_str = NULL;
    int ret = 0;

    const service_method_table_t *service_method_table = sai_service_method_table_get();
    if (service_method_table == NULL) {
        return SAI_STATUS_NOT_SUPPORTED;
    }

    if (service_method_table->profile_get_next_value == NULL) {
        return SAI_STATUS_NOT_SUPPORTED;
    }

    do {
        ret = service_method_table->profile_get_next_value(profile_id,
                                                           &key,
                                                           &value_str);
        if ((ret == 0) && (key != NULL) && (value_str != NULL)) {
            key_len = strlen(key);
            value = (uint_t) strtol(value_str, NULL, 0);
            if (strncmp(key, SAI_KEY_FDB_TABLE_SIZE, key_len) == 0) {
                sai_switch_fdb_table_size_set(value);
                SAI_SWITCH_LOG_TRACE("FDB table size is %d", value);
            } else if (strncmp(key, SAI_KEY_L3_ROUTE_TABLE_SIZE, key_len) == 0) {
                sai_switch_l3_table_size_set(value);
                SAI_SWITCH_LOG_TRACE("L3 table size is %d", value);
            } else if (strncmp(key, SAI_KEY_L3_NEIGHBOR_TABLE_SIZE, key_len) == 0) {
                sai_switch_neighbor_table_size_set(value);
                SAI_SWITCH_LOG_TRACE("Neighbor table size is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_LAG_MEMBERS, key_len) == 0) {
                sai_switch_num_lag_members_set(value);
                SAI_SWITCH_LOG_TRACE("Number of LAG Members is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_LAGS, key_len) == 0) {
                sai_switch_num_lag_set(value);
                SAI_SWITCH_LOG_TRACE("Number of LAG count is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_ECMP_MEMBERS, key_len) == 0) {
                sai_switch_num_ecmp_members_set(value);
                SAI_SWITCH_LOG_TRACE("Number of ECMP Members is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_ECMP_GROUPS, key_len) == 0) {
                sai_switch_num_ecmp_groups_set(value);
                SAI_SWITCH_LOG_TRACE("Number of ECMP Groups is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_UNICAST_QUEUES, key_len) == 0) {
                sai_switch_num_unicast_queues_set(value);
                SAI_SWITCH_LOG_TRACE("Number of Unicast Queues is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_MULTICAST_QUEUES, key_len) == 0) {
                sai_switch_num_multicast_queues_set(value);
                SAI_SWITCH_LOG_TRACE("Number of Multiicast Queues is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_QUEUES, key_len) == 0) {
                sai_switch_num_queues_set(value);
                SAI_SWITCH_LOG_TRACE("Number of Queues is %d", value);
            } else if (strncmp(key, SAI_KEY_NUM_CPU_QUEUES, key_len) == 0) {
                sai_switch_num_cpu_queues_set(value);
                SAI_SWITCH_LOG_TRACE("Number of CPU Queues is %d", value);
            } else {
                /* unsupported switch attribute */
                continue;
            }
        }
    } while (ret != -1);

    return SAI_STATUS_SUCCESS;
}

static const char *sai_switch_init_config_file_get(sai_switch_profile_id_t profile_id)
{
    const service_method_table_t *service_method_table = sai_service_method_table_get();
    if (service_method_table == NULL) {
        return NULL;
    }

    if (service_method_table->profile_get_value == NULL) {
        return NULL;
    }

    return service_method_table->profile_get_value(profile_id, SAI_KEY_INIT_CONFIG_FILE);
}

sai_status_t sai_switch_initialize(sai_switch_profile_id_t profile_id,
                                   char* switch_hardware_id,
                                   char* microcode_module_name,
                                   sai_switch_notification_t* switch_notifications)
{
    sai_switch_id_t switch_id = 0;
    sai_status_t ret_val = SAI_STATUS_UNINITIALIZED;
    sai_switch_info_t *sai_switch_info = NULL;
    const char *sai_init_config_file = NULL;

    STD_ASSERT(switch_notifications != NULL);
    SAI_SWITCH_LOG_TRACE("SDK Init: Profileid %d hwid %s microcode %s",
                         profile_id,switch_hardware_id,microcode_module_name);

    ret_val = sai_npu_api_initialize (SAI_DEFAULT_NPU_LIB_NAME);
    if(ret_val != SAI_STATUS_SUCCESS) {
        SAI_SWITCH_LOG_CRIT("SAI NPU API method table query failed");
        return ret_val;
    }

    sai_switch_populate_event_callbacks(switch_notifications);

    if(switch_notifications->on_switch_state_change != NULL)
    {
        /* Switch state notification registration */
        sai_switch_state_register_callback(switch_notifications->on_switch_state_change);
    }

    if(switch_notifications->on_port_event != NULL)
    {
        /* Port ADD/DELETE event registration */
        sai_port_event_register_callback(switch_notifications->on_port_event);
    }

    do {

        sai_switch_info = sai_switch_info_alloc();
        STD_ASSERT(sai_switch_info != NULL);

        /* TODO: switch identifier should be derived from switch_hardware_id
         * string; format of the switch_hardware_id is not yet finalized
         */
        switch_id = SAI_DEFAULT_SWTICH_ID;

        sai_init_config_file = sai_switch_init_config_file_get(profile_id);
        if (sai_init_config_file == NULL) {
            SAI_SWITCH_LOG_TRACE("Using default switch init config file %s", SAI_INIT_CONFIG_FILE);
            ret_val = sai_switch_init_config(switch_id, SAI_INIT_CONFIG_FILE);
        } else {
            SAI_SWITCH_LOG_TRACE("Using switch init config file %s", sai_init_config_file);
            ret_val = sai_switch_init_config(switch_id, sai_init_config_file);
        }
        if(ret_val != SAI_STATUS_SUCCESS) {
            SAI_SWITCH_LOG_ERR("Switch init config info initialize failed with err %d",ret_val);
            break;
        }

        ret_val = sai_switch_attribute_config(profile_id);
        if (ret_val == SAI_STATUS_NOT_SUPPORTED) {
            SAI_SWITCH_LOG_TRACE("Switch init attributes configuration unsupported");
        }

        sai_log_init ();

        if((ret_val = sai_switch_npu_api_get()->switch_init(switch_id))
            != SAI_STATUS_SUCCESS) {
            SAI_SWITCH_LOG_ERR("SAI Switch initialize failed with err %d",ret_val);
            SAI_SWITCH_LOG_CRIT("SAI Switch initialize failed with err %d",ret_val);
            break;
        }

        if((ret_val = sai_switch_modules_initialize()) != SAI_STATUS_SUCCESS) {
            SAI_SWITCH_LOG_CRIT("SAI Internal modules initialize failed with er %d",ret_val);
            break;
        }

        if(switch_notifications->on_port_state_change != NULL)
        {
            sai_port_state_register_callback
                (switch_notifications->on_port_state_change);
        }

        if(switch_notifications->on_packet_event != NULL)
        {
            sai_hostif_rx_register_callback
                (switch_notifications->on_packet_event);
        }

        if(switch_notifications->on_fdb_event != NULL)
        {
            ret_val = sai_l2_fdb_register_callback
                (switch_notifications->on_fdb_event);
            break;
        }
    } while(0);

    if(ret_val != SAI_STATUS_SUCCESS) {
        sai_switch_oper_status_set(SAI_SWITCH_OPER_STATUS_FAILED);
        SAI_SWITCH_LOG_CRIT("SAI switch Initialization failed with err %d", ret_val);
        return ret_val;
    }

    sai_switch_oper_status_set(SAI_SWITCH_OPER_STATUS_UP);

    return ret_val;
}

void sai_switch_shutdown(bool warm_restart_hint)

{
    /*TODO: To be filled in later.
    */
    sai_npu_api_uninitialize();
}

static void sai_switch_default_tc_update(uint_t default_tc)
{
    sai_switch_info_t *sai_switch_info_ptr = sai_switch_info_get ();

    sai_switch_info_ptr->default_tc = default_tc;
}

sai_status_t sai_switch_set_attribute(const sai_attribute_t *attr)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;

    STD_ASSERT(attr != NULL);
    SAI_SWITCH_LOG_TRACE("Attribute set for %d value %ld",
                         attr->id, attr->value);
    /* TODO: actual APIS for each attribute to be filled later
    */
    switch(attr->id)
    {
        case SAI_SWITCH_ATTR_SWITCHING_MODE:
        case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
        case SAI_SWITCH_ATTR_PORT_BREAKOUT:
        case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
            sai_rc = sai_switch_set_gen_attribute(attr);
            break;

        case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
        case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
        case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
        case SAI_SWITCH_ATTR_FDB_AGING_TIME:
        case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_ACTION:
        case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_ACTION:
        case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_ACTION:
            sai_rc = sai_switch_set_l2_attribute(attr);
            break;

        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
        case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
        case SAI_SWITCH_ATTR_LAG_MEMBERS:
        case SAI_SWITCH_ATTR_NUMBER_OF_LAGS:
            sai_rc = sai_switch_set_lag_attribute (attr);
            break;

        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
        case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
        case SAI_SWITCH_ATTR_ECMP_MEMBERS:
            sai_rc = sai_switch_set_ecmp_attribute (attr);
            break;

        case SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID:
            /* Default STP Instance Id is a READ-ONLY attribute */
            SAI_SWITCH_LOG_ERR("Setting a READ-ONLY attribute %d", attr->id);
            return SAI_STATUS_INVALID_ATTRIBUTE_0;

        case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
            /* Default HostIf Trap Group is a READ-ONLY attribute */
            SAI_SWITCH_LOG_ERR("Setting a Host Interface READ-ONLY "
                               "attribute %d", attr->id);
            return SAI_STATUS_INVALID_ATTRIBUTE_0;

        case SAI_SWITCH_ATTR_QOS_DEFAULT_TC:
            sai_rc = sai_switch_set_qos_default_tc(attr->value.u32);
            if(sai_rc == SAI_STATUS_SUCCESS){
                sai_switch_default_tc_update(attr->value.u32);
            }
            break;

        case SAI_SWITCH_ATTR_ECMP_HASH:
            SAI_SWITCH_LOG_ERR ("Default ECMP Hash attribute is READ-ONLY");
            return SAI_STATUS_INVALID_ATTRIBUTE_0;

        case SAI_SWITCH_ATTR_LAG_HASH:
            SAI_SWITCH_LOG_ERR ("Default LAG Hash attribute is READ-ONLY");
            return SAI_STATUS_INVALID_ATTRIBUTE_0;

        case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
        case SAI_SWITCH_ATTR_ECMP_HASH_IPV4_IN_IPV4:
        case SAI_SWITCH_ATTR_LAG_HASH_IPV4:
        case SAI_SWITCH_ATTR_LAG_HASH_IPV6:
        case SAI_SWITCH_ATTR_LAG_HASH_IPV4_IN_IPV4:
            sai_rc = dn_sai_switch_hash_attr_set (attr->id, attr->value.oid);
            break;

        /** @TODO Deprecated. To be cleaned up */
        case SAI_SWITCH_ATTR_LAG_HASH_ALGO:
        case SAI_SWITCH_ATTR_LAG_HASH_SEED:
        case SAI_SWITCH_ATTR_LAG_HASH_FIELDS:
        case SAI_SWITCH_ATTR_ECMP_HASH_ALGO:
        case SAI_SWITCH_ATTR_ECMP_HASH_SEED:
        case SAI_SWITCH_ATTR_ECMP_HASH_FIELDS:
            sai_rc = SAI_STATUS_SUCCESS;
            break;

        default:
            SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list", attr->id);
            return SAI_STATUS_INVALID_ATTRIBUTE_0;
    }
    return sai_rc;
}


sai_status_t sai_switch_get_attribute(sai_uint32_t attr_count,
                                      sai_attribute_t *attr_list)
{
    unsigned int attr_id_loop = 0;
    sai_attribute_t *p_attr = NULL;
    sai_status_t sai_rc = SAI_STATUS_FAILURE;

    /* TODO: actual APIS for each attribute to be filled later
    */
    STD_ASSERT(attr_list != NULL);
    if(attr_count == 0)
    {
        SAI_SWITCH_LOG_ERR("Attribute count is 0");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    for(attr_id_loop = 0; attr_id_loop < attr_count; attr_id_loop ++)
    {
        p_attr = &attr_list[attr_id_loop];

        if(p_attr == NULL)
        {
            SAI_SWITCH_LOG_TRACE("Array entry %d is NULL",
                                 attr_id_loop);
            continue;
        }

        SAI_SWITCH_LOG_TRACE("Attribute get for id %d", p_attr->id);

        switch(p_attr->id)
        {
            case SAI_SWITCH_ATTR_PORT_NUMBER:
            case SAI_SWITCH_ATTR_PORT_LIST:
            case SAI_SWITCH_ATTR_CPU_PORT:
            case SAI_SWITCH_ATTR_PORT_MAX_MTU:
            case SAI_SWITCH_ATTR_MAX_VIRTUAL_ROUTERS:
            case SAI_SWITCH_ATTR_ON_LINK_ROUTE_SUPPORTED:
            case SAI_SWITCH_ATTR_OPER_STATUS:
            case SAI_SWITCH_ATTR_MAX_TEMP:
            case SAI_SWITCH_ATTR_SWITCHING_MODE:
            case SAI_SWITCH_ATTR_SRC_MAC_ADDRESS:
            case SAI_SWITCH_ATTR_COUNTER_REFRESH_INTERVAL:
                sai_rc = sai_switch_get_gen_attribute(p_attr);
                break;

            case SAI_SWITCH_ATTR_MCAST_CPU_FLOOD_ENABLE:
            case SAI_SWITCH_ATTR_BCAST_CPU_FLOOD_ENABLE:
            case SAI_SWITCH_ATTR_MAX_LEARNED_ADDRESSES:
            case SAI_SWITCH_ATTR_FDB_TABLE_SIZE:
            case SAI_SWITCH_ATTR_FDB_AGING_TIME:
            case SAI_SWITCH_ATTR_FDB_UNICAST_MISS_ACTION:
            case SAI_SWITCH_ATTR_FDB_BROADCAST_MISS_ACTION:
            case SAI_SWITCH_ATTR_FDB_MULTICAST_MISS_ACTION:
            case SAI_SWITCH_ATTR_DEFAULT_STP_INST_ID:
                sai_rc = sai_switch_get_l2_attribute(p_attr);
                break;

            case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_ALGORITHM:
            case SAI_SWITCH_ATTR_LAG_DEFAULT_HASH_SEED:
            case SAI_SWITCH_ATTR_LAG_MEMBERS:
            case SAI_SWITCH_ATTR_NUMBER_OF_LAGS:
                sai_rc = sai_switch_get_lag_attribute (p_attr);
                break;

            case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_ALGORITHM:
            case SAI_SWITCH_ATTR_ECMP_DEFAULT_HASH_SEED:
            case SAI_SWITCH_ATTR_ECMP_MEMBERS:
                sai_rc = sai_switch_get_ecmp_attribute (p_attr);
                break;

            case SAI_SWITCH_ATTR_DEFAULT_TRAP_GROUP:
                sai_rc = sai_switch_get_hostif_attribute(p_attr);
                break;

            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_TRAFFIC_CLASSES:
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUP_HIERARCHY_LEVELS:
            case SAI_SWITCH_ATTR_QOS_MAX_NUMBER_OF_SCHEDULER_GROUPS_PER_HIERARCHY_LEVEL:
            case SAI_SWITCH_ATTR_QOS_DEFAULT_TC:
            case SAI_SWITCH_ATTR_TOTAL_BUFFER_SIZE:
            case SAI_SWITCH_ATTR_INGRESS_BUFFER_POOL_NUM:
            case SAI_SWITCH_ATTR_EGRESS_BUFFER_POOL_NUM:
            case SAI_SWITCH_ATTR_NUMBER_OF_UNICAST_QUEUES:
            case SAI_SWITCH_ATTR_NUMBER_OF_MULTICAST_QUEUES:
            case SAI_SWITCH_ATTR_NUMBER_OF_QUEUES:
            case SAI_SWITCH_ATTR_NUMBER_OF_CPU_QUEUES:
                sai_rc = sai_switch_get_qos_attribute(p_attr);
                break;

            case SAI_SWITCH_ATTR_ACL_TABLE_MINIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_ACL_TABLE_MAXIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_ACL_ENTRY_MINIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_ACL_ENTRY_MAXIMUM_PRIORITY:
            case SAI_SWITCH_ATTR_FDB_DST_USER_META_DATA_RANGE:
            case SAI_SWITCH_ATTR_ROUTE_DST_USER_META_DATA_RANGE:
            case SAI_SWITCH_ATTR_NEIGHBOR_DST_USER_META_DATA_RANGE:
            case SAI_SWITCH_ATTR_PORT_USER_META_DATA_RANGE:
            case SAI_SWITCH_ATTR_VLAN_USER_META_DATA_RANGE:
            case SAI_SWITCH_ATTR_ACL_USER_META_DATA_RANGE:
                sai_rc = sai_switch_get_acl_attribute(p_attr);
                break;

            case SAI_SWITCH_ATTR_ECMP_HASH:
            case SAI_SWITCH_ATTR_LAG_HASH:
            case SAI_SWITCH_ATTR_ECMP_HASH_IPV4:
            case SAI_SWITCH_ATTR_ECMP_HASH_IPV6:
            case SAI_SWITCH_ATTR_ECMP_HASH_IPV4_IN_IPV4:
            case SAI_SWITCH_ATTR_LAG_HASH_IPV4:
            case SAI_SWITCH_ATTR_LAG_HASH_IPV6:
            case SAI_SWITCH_ATTR_LAG_HASH_IPV4_IN_IPV4:
                sai_rc = dn_sai_switch_hash_attr_get (p_attr);
                break;

            /** @TODO Deprecated. To be cleaned up */
            case SAI_SWITCH_ATTR_LAG_HASH_ALGO:
            case SAI_SWITCH_ATTR_LAG_HASH_SEED:
            case SAI_SWITCH_ATTR_LAG_HASH_FIELDS:
            case SAI_SWITCH_ATTR_ECMP_HASH_ALGO:
            case SAI_SWITCH_ATTR_ECMP_HASH_SEED:
            case SAI_SWITCH_ATTR_ECMP_HASH_FIELDS:
                sai_rc = SAI_STATUS_SUCCESS;
                break;

            default:
                SAI_SWITCH_LOG_ERR("Invalid Attribute Id %d in list",p_attr->id);
                return sai_get_indexed_ret_val(SAI_STATUS_INVALID_ATTRIBUTE_0,
                                               attr_id_loop);
        }
        if(sai_rc != SAI_STATUS_SUCCESS) {
            return sai_get_indexed_ret_val(sai_rc, attr_id_loop);
        }
    }

    return sai_rc;
}

/* SAI connect and disconnect currently not supported */
sai_status_t sai_connect_switch(sai_switch_profile_id_t profile_id,
                                char* switch_hardware_id,
                                sai_switch_notification_t* switch_notifications)
{
    STD_ASSERT(switch_notifications != NULL);
    SAI_SWITCH_LOG_TRACE("SDK Connect: Profileid %d Hwid %s",
                         profile_id,switch_hardware_id);

    return SAI_STATUS_SUCCESS;
}

void sai_disconnect_switch(void)
{
    SAI_SWITCH_LOG_INFO("SDK Disconnect");
}

static sai_switch_api_t sai_switch_method_table =
{
    sai_switch_initialize,
    sai_switch_shutdown,
    sai_connect_switch,
    sai_disconnect_switch,
    sai_switch_set_attribute,
    sai_switch_get_attribute,
};

/* Switch functions passed to the method table */

sai_switch_api_t* sai_switch_api_query(void)
{
    return (&sai_switch_method_table);
}
