/************************************************************************
* * LEGALESE:   "Copyright (c) 2017, Dell Inc. All rights reserved."
* *
* * This source code is confidential, proprietary, and contains trade
* * secrets that are the sole property of Dell Inc.
* * Copy and/or distribution of this source code or disassembly or reverse
* * engineering of the resultant object code are strictly forbidden without
* * the written consent of Dell Inc.
* *
************************************************************************/
/**
* @file sai_bridge_debug.c
*
* @brief This file contains implementation of SAI Bridge Debug APIs
*************************************************************************/

#include <stdio.h>
#include <inttypes.h>
#include "saibridge.h"
#include "saitypes.h"
#include "saistatus.h"
#include "sai_bridge_common.h"
#include "sai_bridge_api.h"
#include "sai_bridge_npu_api.h"
#include "sai_bridge_main.h"
#include "sai_debug_utils.h"
#include "sai_common_infra.h"
#include "sai_switch_utils.h"

void sai_bridge_dump_bridge_info(sai_object_id_t bridge_id)
{
    dn_sai_bridge_info_t *p_bridge_info = NULL;
    sai_status_t          sai_rc = SAI_STATUS_FAILURE;
    uint_t                bridge_port_count = 0;
    uint_t                idx = 0;

    sai_rc = sai_bridge_cache_read(bridge_id, &p_bridge_info);
    if((sai_rc != SAI_STATUS_SUCCESS) || (p_bridge_info == NULL)) {
        SAI_DEBUG("Error %d in getting bridge info", sai_rc);
        return;
    }
    SAI_DEBUG("Bridge ID 0x%"PRIx64"", p_bridge_info->bridge_id);
    SAI_DEBUG("Bridge type %d", p_bridge_info->bridge_type);
    SAI_DEBUG("Max learned address %d", p_bridge_info->max_learned_address);
    SAI_DEBUG("Learn disable %d", p_bridge_info->learn_disable);
    SAI_DEBUG("Switch object id 0x%"PRIx64"", p_bridge_info->switch_obj_id);
    SAI_DEBUG("Ref count is %d", p_bridge_info->ref_count);

    sai_rc = sai_bridge_map_get_port_count(bridge_id, &bridge_port_count);
    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting bridge port count", sai_rc);
    } else {
        SAI_DEBUG("Bridge port count %d", bridge_port_count);
        sai_object_id_t obj_list[bridge_port_count];

        sai_rc = sai_bridge_map_port_list_get(p_bridge_info->bridge_id, &bridge_port_count,
                                              obj_list);

        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_DEBUG("Error %d in getting bridge port list", sai_rc);
        }
        for(idx = 0; idx < bridge_port_count; idx++) {
            SAI_DEBUG("Bridge port %d - 0x%"PRIx64"", idx,obj_list[idx]);
        }
    }

    SAI_DEBUG("*************");
    SAI_DEBUG("Hardware info");
    SAI_DEBUG("*************");

    sai_bridge_npu_api_get()->bridge_dump_hw_info(p_bridge_info);
}

void sai_brige_dump_default_bridge(void)
{
    sai_object_id_t bridge_id;
    sai_status_t    sai_rc;

    sai_rc = sai_bridge_default_id_get(&bridge_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting default bridge id", sai_rc);
        return;
    }

    sai_bridge_dump_bridge_info(bridge_id);
}

void sai_bridge_dump_bridge_port_info(sai_object_id_t bridge_port_id)
{
    sai_status_t               sai_rc;
    dn_sai_bridge_port_info_t *p_bridge_port_info = NULL;
    sai_object_id_t            attach_id;
    uint16_t                   vlan_id;

    sai_rc = sai_bridge_port_cache_read(bridge_port_id, &p_bridge_port_info);
    if((sai_rc != SAI_STATUS_SUCCESS) || (p_bridge_port_info == NULL)) {
        SAI_DEBUG("Error %d in getting bridge_port info", sai_rc);
        return;
    }
    SAI_DEBUG("Bridge port ID 0x%"PRIx64"", p_bridge_port_info->bridge_port_id);
    SAI_DEBUG("Bridge ID 0x%"PRIx64"", p_bridge_port_info->bridge_id);
    SAI_DEBUG("Bridge port type %d", p_bridge_port_info->bridge_port_type);
    SAI_DEBUG("Max learned address %d", p_bridge_port_info->max_learned_address);
    SAI_DEBUG("Learn mode %d", p_bridge_port_info->fdb_learn_mode);
    SAI_DEBUG("Learn limit violation action %d", p_bridge_port_info->learn_limit_violation_action);
    SAI_DEBUG("Admin state %d", p_bridge_port_info->admin_state);
    SAI_DEBUG("Ingress filtering %d", p_bridge_port_info->ingress_filtering);
    SAI_DEBUG("Switch object id 0x%"PRIx64"", p_bridge_port_info->switch_obj_id);
    SAI_DEBUG("Ref count is %d", p_bridge_port_info->ref_count);

    switch(p_bridge_port_info->bridge_port_type) {

        case SAI_BRIDGE_PORT_TYPE_PORT:
            attach_id = sai_bridge_port_info_get_port_id(p_bridge_port_info);
            SAI_DEBUG("Attach port id 0x%"PRIx64"",attach_id);
            break;

        case SAI_BRIDGE_PORT_TYPE_SUB_PORT:
            attach_id = sai_bridge_port_info_get_port_id(p_bridge_port_info);
            vlan_id = sai_bridge_port_info_get_vlan_id(p_bridge_port_info);
            SAI_DEBUG("Attach port id 0x%"PRIx64"",attach_id);
            SAI_DEBUG("Attach vlan id %d",vlan_id);
            break;

        case SAI_BRIDGE_PORT_TYPE_1Q_ROUTER:
        case SAI_BRIDGE_PORT_TYPE_1D_ROUTER:
            attach_id = sai_bridge_port_info_get_rif_id(p_bridge_port_info);
            SAI_DEBUG("Attach router interface id 0x%"PRIx64"",attach_id);
            break;

        case SAI_BRIDGE_PORT_TYPE_TUNNEL:
            attach_id = sai_bridge_port_info_get_tunnel_id(p_bridge_port_info);
            SAI_DEBUG("Attach tunnel id 0x%"PRIx64"",attach_id);
            break;
    }

    SAI_DEBUG("*************");
    SAI_DEBUG("Hardware info");
    SAI_DEBUG("*************");
    sai_bridge_npu_api_get()->bridge_port_dump_hw_info(p_bridge_port_info);
}

void sai_bridge_dump_all_bridge_ids(void)
{
    uint_t          count = sai_bridge_total_count();
    sai_object_id_t bridge_id[count];
    sai_status_t    sai_rc;
    uint_t          idx = 0;

    sai_rc = sai_bridge_list_get(&count, bridge_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting bridge list",sai_rc);
    }

    for(idx=0; idx< count; idx++) {
        SAI_DEBUG("Bridge ID %d - 0x%"PRIx64"", idx, bridge_id[idx]);
    }

}

void sai_bridge_dump_all_bridge_info(void)
{
    uint_t          count = sai_bridge_total_count();
    sai_object_id_t bridge_id[count];
    sai_status_t    sai_rc;
    uint_t          idx = 0;

    sai_rc = sai_bridge_list_get(&count, bridge_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting bridge list",sai_rc);
    }

    for(idx=0; idx< count; idx++) {
        sai_bridge_dump_bridge_info(bridge_id[idx]);
    }

}

void sai_bridge_dump_all_bridge_port_ids(void)
{
    uint_t          count = sai_bridge_port_total_count();
    sai_object_id_t bridge_port_id[count];
    sai_status_t    sai_rc;
    uint_t          idx = 0;

    sai_rc = sai_bridge_port_list_get(&count, bridge_port_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting bridge port list",sai_rc);
    }

    for(idx=0; idx< count; idx++) {
        SAI_DEBUG("Bridge Port ID %d - 0x%"PRIx64"", idx, bridge_port_id[idx]);
    }
}

void sai_bridge_dump_all_bridge_port_info(void)
{
    uint_t          count = sai_bridge_port_total_count();
    sai_object_id_t bridge_port_id[count];
    sai_status_t    sai_rc;
    uint_t          idx = 0;

    sai_rc = sai_bridge_port_list_get(&count, bridge_port_id);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in getting bridge port list",sai_rc);
    }

    for(idx=0; idx< count; idx++) {
        sai_bridge_dump_bridge_port_info(bridge_port_id[idx]);
    }
}

void sai_debug_bridge_create()
{
    sai_attribute_t attr[1];
    sai_object_id_t bridge_id;
    sai_status_t    sai_rc;

    attr[0].id = SAI_BRIDGE_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_TYPE_1D;

    sai_rc = sai_bridge_api_query()->create_bridge(&bridge_id, SAI_DEFAULT_SWITCH_ID, 1, attr);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in creating bridge",sai_rc);
        return;
    }
    SAI_DEBUG("Bridge ID created is 0x%"PRIx64"",bridge_id);
}

void sai_debug_lag_create()
{
    sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;

    sai_lag_api_query()->create_lag(&lag_id, SAI_DEFAULT_SWITCH_ID, 0, NULL);

    SAI_DEBUG("LAG ID created is 0x%"PRIx64"", lag_id);

}

void sai_debug_lag_add_port(sai_object_id_t lag_id, sai_object_id_t port_id)
{
    sai_attribute_t attr[2];
    sai_object_id_t lag_member_id = SAI_NULL_OBJECT_ID;

    attr[0].id = SAI_LAG_MEMBER_ATTR_LAG_ID;
    attr[0].value.oid = lag_id;

    attr[1].id = SAI_LAG_MEMBER_ATTR_PORT_ID;
    attr[1].value.oid = port_id;

    sai_lag_api_query()->create_lag_member(&lag_member_id, SAI_DEFAULT_SWITCH_ID,  2, attr);

    SAI_DEBUG("LAG member id created is 0x%"PRIx64"", lag_member_id);
}

void sai_debug_lag_remove_member(sai_object_id_t lag_member_id)
{
    sai_lag_api_query()->remove_lag_member(lag_member_id);
}

void sai_debug_bridge_port_sub_port_create(sai_object_id_t bridge_id,
                                           sai_object_id_t port_id, int vlan_id)
{
    sai_attribute_t attr[4];
    sai_object_id_t bridge_port_id = SAI_NULL_OBJECT_ID;
    sai_status_t    sai_rc;

    attr[0].id = SAI_BRIDGE_ATTR_TYPE;
    attr[0].value.s32 = SAI_BRIDGE_PORT_TYPE_SUB_PORT;

    attr[1].id = SAI_BRIDGE_PORT_ATTR_PORT_ID;
    attr[1].value.oid = port_id;

    attr[2].id = SAI_BRIDGE_PORT_ATTR_VLAN_ID;
    attr[2].value.u16 = vlan_id;

    attr[3].id = SAI_BRIDGE_PORT_ATTR_BRIDGE_ID;
    attr[3].value.oid = bridge_id;

    sai_rc = sai_bridge_api_query()->create_bridge_port(&bridge_port_id, SAI_DEFAULT_SWITCH_ID,
                                                        4, attr);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in creating bridge port",sai_rc);
        return;
    }
    SAI_DEBUG("Bridge port ID created is 0x%"PRIx64"",bridge_port_id);
}

void sai_debug_bridge_port_set_attribute(sai_object_id_t bridge_port_id,
                                         int attr_id, int value)
{

    sai_attribute_t attr;
    sai_status_t sai_rc;

    attr.id = attr_id;
    attr.value.s32 = value;

    sai_rc = sai_bridge_api_query()->set_bridge_port_attribute(bridge_port_id, &attr);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        SAI_DEBUG("Error %d in set bridge port attribute",sai_rc);
    }
}
