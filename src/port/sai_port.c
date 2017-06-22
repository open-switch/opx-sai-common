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
 * @file sai_port.c
 *
 * @brief This file contains port level functionality for SAI common component.
 *        Adapter Host use these API's for port level Attribute Set/Get, get
 *        port counter statistics and get port state change notifications.
 *
 */

#include "sai_switch_utils.h"
#include "sai_port_utils.h"
#include "sai_npu_port.h"
#include "sai_mirror_defs.h"
#include "sai_mirror_api.h"
#include "sai_qos_api_utils.h"
#include "sai_qos_util.h"
#include "sai_samplepacket_api.h"
#include "sai_common_infra.h"
#include "sai_common_utils.h"
#include "sai_port_common.h"

#include "saiport.h"
#include "saitypes.h"
#include "saistatus.h"
#include "saimirror.h"

#include "std_type_defs.h"
#include "std_assert.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* Internal port event notification handler list for all SAI module,
 * used to invoke all registered internal modules notification function */
static sai_port_event_internal_notf_t port_event_list[SAI_MODULE_MAX];

static sai_status_t sai_port_set_drop_untagged (sai_object_id_t port_id,
                                                const sai_attribute_t *in_attr)
{
    sai_status_t    rc;
    sai_attribute_t attr_drop_untag;

    /* Retrieve the old drop_untagged val to check if this is a duplicate set */
    attr_drop_untag.id = SAI_PORT_ATTR_DROP_UNTAGGED;

    rc = sai_port_attr_info_cache_get (port_id, &attr_drop_untag);

    if (rc != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR ("Port drop_untagged get from cache failed for "
                          "port 0x%"PRIx64" with err: 0x%x", port_id, rc);
        return rc;
    }

    /* Value being set is same as the current value. So do nothing. */
    if (attr_drop_untag.value.booldata == in_attr->value.booldata) {
        SAI_PORT_LOG_TRACE ("Drop untagged (%d) same as old value for "
                            "port 0x%"PRIx64"", in_attr->value.booldata,
                            port_id);
        return SAI_STATUS_SUCCESS;
    }

    rc = sai_port_npu_api_get()->port_set_attribute (port_id, in_attr);

    if (rc != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR ("Drop untagged (%d) set failed in NPU for "
                          "port id 0x%"PRIx64" with err 0x%x",
                          in_attr->value.booldata, port_id, rc);

        return rc;
    }

    SAI_PORT_LOG_TRACE ("Drop untagged set of port 0x%"PRIx64" is %d",
                        port_id, in_attr->value.booldata);

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_port_set_drop_tagged (sai_object_id_t port_id,
                                              const sai_attribute_t *in_attr)
{
    sai_status_t    rc;
    sai_attribute_t attr_drop_tag;

    /* Retrieve the old drop_tagged val to check if this is a duplicate set */
    attr_drop_tag.id = SAI_PORT_ATTR_DROP_TAGGED;

    rc = sai_port_attr_info_cache_get (port_id, &attr_drop_tag);

    if (rc != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR ("Port drop_tagged get from cache failed for port "
                          "0x%"PRIx64" with err: 0x%x", port_id, rc);
        return rc;
    }

    /* Value being set is same as the current value. So do nothing. */
    if (attr_drop_tag.value.booldata == in_attr->value.booldata) {
        SAI_PORT_LOG_TRACE ("Drop tagged (%d) same as old value for port "
                            "0x%"PRIx64"", in_attr->value.booldata, port_id);
        return SAI_STATUS_SUCCESS;
    }

    rc = sai_port_npu_api_get()->port_set_attribute (port_id, in_attr);

    if (rc != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR ("Drop tagged (%d) set failed in NPU for "
                          "port id 0x%"PRIx64" with err 0x%x",
                          in_attr->value.booldata, port_id, rc);

        return rc;
    }

    SAI_PORT_LOG_TRACE ("Drop tagged set of port 0x%"PRIx64" is %d",
                        port_id, in_attr->value.booldata);

    return SAI_STATUS_SUCCESS;
}

static sai_status_t sai_port_apply_attribute (sai_object_id_t port_id,
                                    const sai_attribute_t *attr)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    STD_ASSERT(!(attr == NULL));

    SAI_PORT_LOG_TRACE("Attribute %d set for port 0x%"PRIx64"", attr->id, port_id);

    do {
        if(!sai_is_obj_id_port(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not a port object", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_TYPE;
            break;
        }

        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        switch (attr->id)
        {
            case SAI_PORT_ATTR_DROP_UNTAGGED:
                ret = sai_port_set_drop_untagged (port_id, attr);

                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR ("Drop Untagged set on port "
                                      "id 0x%"PRIx64" failed", port_id);
                }
                break;

            case SAI_PORT_ATTR_DROP_TAGGED:
                ret = sai_port_set_drop_tagged (port_id, attr);

                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR ("Drop Tagged set on port "
                                      "id 0x%"PRIx64" failed", port_id);
                }
                break;

            case SAI_PORT_ATTR_INGRESS_MIRROR_SESSION:
                ret = sai_mirror_handle_per_port (port_id, attr,
                                                  SAI_MIRROR_DIR_INGRESS);
                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Ingress Mirror set on port id 0x%"PRIx64" failed", port_id);
                }
                break;

            case SAI_PORT_ATTR_EGRESS_MIRROR_SESSION:
                ret = sai_mirror_handle_per_port (port_id, attr,
                                                  SAI_MIRROR_DIR_EGRESS);
                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Egress Mirror set on port id 0x%"PRIx64" failed", port_id);
                }
                break;

            case SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE:
                ret = sai_samplepacket_handle_per_port (port_id, attr,
                                                        SAI_SAMPLEPACKET_DIR_INGRESS);
                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Ingress SamplePacket set on port "
                                     "id 0x%"PRIx64" failed", port_id);
                }
                break;

            case SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE:
                ret = sai_samplepacket_handle_per_port (port_id, attr,
                                                        SAI_SAMPLEPACKET_DIR_EGRESS);
                if (ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Egress SamplePacket set on port "
                                     "id 0x%"PRIx64" failed", port_id);
                }
                break;
                /** Fall through */
            case SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP:
            case SAI_PORT_ATTR_QOS_DOT1P_TO_TC_AND_COLOR_MAP:
            case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DOT1P_MAP:
            case SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DSCP_MAP:
            case SAI_PORT_ATTR_QOS_DSCP_TO_TC_AND_COLOR_MAP:
            case SAI_PORT_ATTR_QOS_DOT1P_TO_TC_MAP:
            case SAI_PORT_ATTR_QOS_DOT1P_TO_COLOR_MAP:
            case SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP:
            case SAI_PORT_ATTR_QOS_DSCP_TO_COLOR_MAP:
            case SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP:
            case SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP:
                ret = sai_qos_map_on_port_set(port_id, attr,
                                              sai_get_map_type_from_port_attr(attr->id));
                if(ret != SAI_STATUS_SUCCESS)
                {
                    SAI_PORT_LOG_ERR("Map set failed for map %d set failed on "
                                     "portid 0x%"PRIx64"",attr->id, port_id);
                }
                break;

            case SAI_PORT_ATTR_QOS_SCHEDULER_PROFILE_ID:
                ret = sai_qos_port_scheduler_set(port_id, attr);
                if(ret != SAI_STATUS_SUCCESS)
                {
                    SAI_PORT_LOG_ERR("Port Scheduler set failed on "
                                     "portid 0x%"PRIx64"", port_id);
                }

                break;
            case SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST:
            case SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST:
                ret = SAI_STATUS_NOT_IMPLEMENTED;
                break;
                /** Fall through **/
            case SAI_PORT_ATTR_FLOOD_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_BROADCAST_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_MULTICAST_STORM_CONTROL_POLICER_ID:
            case SAI_PORT_ATTR_POLICER_ID:
                ret = sai_port_attr_storm_control_policer_set(port_id, attr);
                if(ret != SAI_STATUS_SUCCESS)
                {
                    SAI_PORT_LOG_ERR("Storm control set failed for %d on"
                                     "portid 0x%"PRIx64"",attr->id, port_id);
                }
                break;

            case SAI_PORT_ATTR_QOS_WRED_PROFILE_ID:
                ret = sai_port_attr_wred_profile_set(port_id, attr);
                if(ret != SAI_STATUS_SUCCESS)
                {
                    SAI_PORT_LOG_ERR("Wred set failed for %d on"
                                     "portid 0x%"PRIx64"",attr->id, port_id);
                }
                break;

            case SAI_PORT_ATTR_INGRESS_ACL:
            case SAI_PORT_ATTR_EGRESS_ACL:
                ret = SAI_STATUS_SUCCESS;
                break;

            default:
                ret = sai_port_npu_api_get()->port_set_attribute(port_id, attr);
                if(ret != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Attr set for port id 0x%"PRIx64" failed with err %d",
                                     port_id, ret);
                }
                break;
        }

        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("SAI Error code is %d", ret);
            break;
        }

        /* Update the port attribute info cache with the new attribute value */
        ret = sai_port_attr_info_cache_set(port_id, attr);
        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Attr cache info update failed for port id 0x%"PRIx64" with err %d",
                             port_id, ret);
            break;
        }

    } while(0);

    return ret;
}

static sai_status_t sai_port_set_attribute(sai_object_id_t port_id,
                                    const sai_attribute_t *attr)
{
    sai_status_t sai_rc = SAI_STATUS_FAILURE;
    sai_port_lock();

    sai_rc = sai_port_apply_attribute(port_id, attr);

    sai_port_unlock();
    return sai_rc;
}

sai_status_t sai_port_get_attribute(sai_object_id_t port_id,
                                    uint32_t attr_count,
                                    sai_attribute_t *attr_list)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    if(!attr_count) {
        SAI_PORT_LOG_ERR("Attr get: number of attributes is zero");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    STD_ASSERT(!(attr_list ==NULL));

    SAI_PORT_LOG_TRACE("Attribute get for %d attributes of port 0x%"PRIx64"",
                       attr_count, port_id);

    sai_port_lock();
    do {
        if(!sai_is_obj_id_port(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not a port object", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_TYPE;
            break;
        }

        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        ret = sai_port_npu_api_get()->port_get_attribute(port_id, attr_count, attr_list);
        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_TRACE("Attr get for port id 0x%"PRIx64" failed with err %d",
                                port_id, ret);
            break;
        }
    } while(0);

    sai_port_unlock();
    return ret;
}

sai_status_t sai_port_get_stats(sai_object_id_t port_id,
                                const sai_port_stat_t *counter_ids,
                                uint32_t number_of_counters,
                                uint64_t* counters)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    if(number_of_counters == 0) {
        SAI_PORT_LOG_ERR("Stat get: number of counters is zero");
        return SAI_STATUS_INVALID_PARAMETER;
    }
    STD_ASSERT(!(counter_ids == NULL));
    STD_ASSERT(!(counters == NULL));

    SAI_PORT_LOG_TRACE("Stats get for %d counters of port 0x%"PRIx64"",
                       number_of_counters, port_id);

    sai_port_lock();
    do {
        if(!sai_is_obj_id_port(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not a port object", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_TYPE;
            break;
        }

        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        ret = sai_port_npu_api_get()->port_get_stats(port_id, counter_ids,
                                                     number_of_counters, counters);
        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Stats get for port id 0x%"PRIx64" failed with err %d",
                             port_id, ret);
            break;
        }
    } while(0);

    sai_port_unlock();
    return ret;
}

sai_status_t sai_port_clear_stats(sai_object_id_t port_id,
                                  const sai_port_stat_t *counter_ids,
                                  uint32_t number_of_counters)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    if(number_of_counters == 0) {
        SAI_PORT_LOG_ERR("Stat clear: number of counters is zero");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    STD_ASSERT(!(counter_ids == NULL));

    SAI_PORT_LOG_TRACE("Clear Statistics counters of port 0x%"PRIx64"", port_id);

    sai_port_lock();
    do {
        if(!sai_is_obj_id_port(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not a port object", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_TYPE;
            break;
        }

        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        ret = sai_port_npu_api_get()->port_clear_stats(port_id, counter_ids,
                                                       number_of_counters);
        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Stats clear for port id 0x%"PRIx64" failed with err %d",
                             port_id, ret);
            break;
        }
    } while(0);

    sai_port_unlock();
    return ret;
}

sai_status_t sai_port_clear_all_stats(sai_object_id_t port_id)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    SAI_PORT_LOG_TRACE("Clear Statistics counters of port 0x%"PRIx64"", port_id);

    sai_port_lock();
    do {
        if(!sai_is_obj_id_port(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not a port object", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_TYPE;
            break;
        }

        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            ret = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        ret = sai_port_npu_api_get()->port_clear_all_stats(port_id);
        if(ret != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Stats clear for port id 0x%"PRIx64" failed with err %d",
                             port_id, ret);
            break;
        }
    } while(0);

    sai_port_unlock();
    return ret;
}

/* configure sai port with default value of attributes */
static sai_status_t sai_port_set_attribute_default(sai_object_id_t sai_port_id)
{
    sai_port_attr_info_t *port_attr_info = NULL;
    sai_status_t ret = SAI_STATUS_FAILURE;
    sai_attribute_t sai_attr_set;

    if(sai_is_port_valid(sai_port_id) == false) {
        SAI_PORT_LOG_ERR("Port 0x%"PRIx64" is not valid port", sai_port_id);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    port_attr_info = sai_port_attr_info_get(sai_port_id);
    STD_ASSERT(port_attr_info != NULL);

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_ADMIN_STATE;
    sai_attr_set.value.booldata = port_attr_info->admin_state;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default admin state %d set failed with err %d",
                          sai_port_id, port_attr_info->admin_state, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_FULL_DUPLEX_MODE;
    sai_attr_set.value.booldata = port_attr_info->duplex;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default duplex state %d set failed with err %d",
                          sai_port_id, port_attr_info->duplex, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_AUTO_NEG_MODE;
    sai_attr_set.value.booldata = port_attr_info->autoneg;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default autoneg state %d set failed with err %d",
                          sai_port_id, port_attr_info->autoneg, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_INGRESS_FILTERING;
    sai_attr_set.value.booldata = port_attr_info->ingress_filtering;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default ingress filter %d set failed with err %d",
                          sai_port_id, port_attr_info->ingress_filtering, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_DROP_UNTAGGED;
    sai_attr_set.value.booldata = port_attr_info->drop_untagged;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default drop untagged %d set failed with err %d",
                          sai_port_id, port_attr_info->drop_untagged, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_DROP_TAGGED;
    sai_attr_set.value.booldata = port_attr_info->drop_tagged;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default drop tagged %d set failed with err %d",
                          sai_port_id, port_attr_info->drop_tagged, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_UPDATE_DSCP;
    sai_attr_set.value.booldata = port_attr_info->update_dscp;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default dscp %d set failed with err %d",
                          sai_port_id, port_attr_info->update_dscp, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE;
    sai_attr_set.value.s32 = port_attr_info->internal_loopback;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default internal loopback %d set failed with err %d",
                          sai_port_id, port_attr_info->internal_loopback, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_FDB_LEARNING_MODE;
    sai_attr_set.value.s32 = port_attr_info->fdb_learning;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default fdb learning mode %d set failed with err %d",
                          sai_port_id, port_attr_info->fdb_learning, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_MTU;
    sai_attr_set.value.u32 = port_attr_info->mtu;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default mtu %d set failed with err %d",
                          sai_port_id, port_attr_info->mtu, ret);
        return ret;
    }

    return ret;
}

/* Port Link State change notification registration callback
 * Null input will unregister from callback */
void sai_port_state_register_callback(sai_port_state_change_notification_fn port_state_notf_fn)
{
    SAI_PORT_LOG_TRACE("Port Link state change notification registration");

    sai_port_lock();
    sai_port_npu_api_get()->reg_link_state_cb(port_state_notf_fn);
    sai_port_unlock();
}

/* Port Module's internal port_event notification handler */
static void sai_port_event_internal_notif_handler(uint32_t count,
                                                  sai_port_event_notification_t *data)
{
    uint32_t port_idx = 0;
    sai_status_t ret = SAI_STATUS_FAILURE;
    for(port_idx = 0; port_idx < count; port_idx++) {
        if (data[port_idx].port_event == SAI_PORT_EVENT_ADD) {
            ret = sai_port_set_attribute_default(data[port_idx].port_id);
            if (ret != SAI_STATUS_SUCCESS) {
                SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default configuration failed with err %d",
                                 data[port_idx].port_id, ret);
            }
        }
    }
    sai_port_npu_api_get()->switching_mode_update(count, data);
}

/* Per SAI module registration for port event notification.
 * Port_event is set to NULL for unregistering from the event
 */
sai_status_t sai_port_event_internal_notif_register(sai_module_t module_id,
                                                    sai_port_event_notification_fn port_event)
{
    if(module_id >= SAI_MODULE_MAX) {
        SAI_PORT_LOG_ERR("Invalid module id %d for port event registration", module_id);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    SAI_PORT_LOG_TRACE("SAI module %d updates the port event internal notification", module_id);

    sai_port_lock();
    port_event_list[module_id].port_event = port_event;
    sai_port_unlock();

    return SAI_STATUS_SUCCESS;
}

/* Notify all the registered modules with port events  */
static void sai_port_event_internal_notf(uint32_t count,
                                         sai_port_event_notification_t *data)
{
    sai_module_t mod_idx = 0;

    STD_ASSERT(data != NULL);

    for(mod_idx = 0; mod_idx < SAI_MODULE_MAX; mod_idx++) {

        if(port_event_list[mod_idx].port_event != NULL) {
            port_event_list[mod_idx].port_event(count, data);
        }
    }
}

void sai_port_init_event_notification(void)
{
    uint32_t count = 0;
    uint32_t max_count = sai_switch_get_max_lport();
    sai_port_event_notification_t data[max_count];
    sai_port_info_t *port_info = NULL;

    sai_port_lock();
    count = 0;
    /* After NPU Switch Initialization, default port's event default notification for
     * all valid logical Ports. CPU port is not part of this event notification */
    for (port_info = sai_port_info_getfirst(); (port_info != NULL);
         port_info = sai_port_info_getnext(port_info)) {
        if(sai_is_port_valid(port_info->sai_port_id)) {
            data[count].port_id = port_info->sai_port_id;
            data[count].port_event = SAI_PORT_EVENT_ADD;
            SAI_PORT_LOG_NTC("SAI port 0x%"PRIx64" is Added", port_info->sai_port_id);
            count++;
        }
    }
    sai_port_unlock();

    sai_port_event_internal_notf(count, data);
}

void sai_ports_linkscan_enable(void)
{
    sai_port_info_t *port_info = NULL;

    sai_port_lock();
    for (port_info = sai_port_info_getfirst(); (port_info != NULL);
         port_info = sai_port_info_getnext(port_info)) {
        if (sai_is_port_valid(port_info->sai_port_id)) {
            if ((sai_port_npu_api_get()->linkscan_mode_set != NULL) &&
                    (sai_port_npu_api_get()->linkscan_mode_set(port_info->sai_port_id, true)
                            != SAI_STATUS_SUCCESS)) {
                SAI_PORT_LOG_ERR("Failed to enable linkcan on port 0x%"PRIx64" in npu",
                                 port_info->sai_port_id);
            }
        }
    }
    sai_port_unlock();
}

/* Validates the breakout mode and its corresponding lane count value */
static inline sai_status_t sai_port_breakout_validate(sai_port_breakout_mode_type_t mode,
                                                      uint_t count)
{
    sai_port_lane_count_t lane_count = 0;

    if(mode >= SAI_PORT_BREAKOUT_MODE_TYPE_MAX) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
    }

    lane_count = sai_port_breakout_lane_count_get(mode);

    if((count == 0) || (count < lane_count)) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
    }

    return SAI_STATUS_SUCCESS;
}

/* Get the list of valid logical ports in the switch */
sai_status_t sai_port_list_get(sai_object_list_t *objlist)
{
    sai_status_t ret_code = SAI_STATUS_SUCCESS;
    sai_object_list_t port_list;

    STD_ASSERT(objlist != NULL);
    STD_ASSERT(objlist->list != NULL);

    port_list.list = (sai_object_id_t *) calloc(sai_switch_get_max_lport(),
                                                sizeof(sai_object_id_t));
    if(port_list.list == NULL) {
        SAI_SWITCH_LOG_ERR ("Allocation of Memory failed for port object list");
        return SAI_STATUS_NO_MEMORY;
    }

    sai_port_lock();
    sai_port_logical_list_get(&port_list);
    sai_port_unlock();

    do {
        if(port_list.count == 0) { /* Not likely */
            ret_code = SAI_STATUS_INVALID_ATTR_VALUE_0;
            break;
        }

        if(objlist->count < port_list.count) {
            SAI_SWITCH_LOG_ERR("get port list count %d is less than "
                               "actual port list count %d",
                               objlist->count, port_list.count);

            objlist->count = port_list.count;
            ret_code = SAI_STATUS_BUFFER_OVERFLOW;
            break;
        }

        memcpy(objlist->list, port_list.list, (port_list.count * sizeof(sai_object_id_t)));

        /* Update the object list count with actual port list count */
        objlist->count = port_list.count;
    } while(0);

    free(port_list.list);
    port_list.list = NULL;
    port_list.count = 0;

    return ret_code;
}

sai_status_t sai_port_init(void)
{
    sai_status_t ret_code = SAI_STATUS_SUCCESS;

    /* Initialize port attributes default value */
    sai_port_attr_defaults_init();

    /* SAI port module registration for port_event */
    ret_code = sai_port_event_internal_notif_register(SAI_MODULE_PORT,
                                                      sai_port_event_internal_notif_handler);

    if(ret_code != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("Internal port event notif register failed for PORT module with err %d",
                         ret_code);
    }

    return ret_code;
}

sai_status_t sai_port_remove(sai_object_id_t port_id)
{

    sai_status_t sai_rc = SAI_STATUS_SUCCESS;
    sai_port_event_notification_t data;
    bool linkscan_set = false;

    memset (&data, 0, sizeof(data));
    sai_port_lock();
    do {
        if(!sai_is_port_valid(port_id)) {
            SAI_PORT_LOG_ERR("port id 0x%"PRIx64" is not valid", port_id);
            sai_rc = SAI_STATUS_INVALID_OBJECT_ID;
            break;
        }

        if (sai_port_npu_api_get()->linkscan_mode_set != NULL) {
            if (sai_port_npu_api_get()->linkscan_mode_set(port_id, false)
                      != SAI_STATUS_SUCCESS) {
                SAI_PORT_LOG_ERR("Failed to disable linkscan on port 0x%"PRIx64" in npu",
                                 port_id);
            } else {
                linkscan_set = true;
            }
        }

        sai_rc = sai_port_npu_api_get()->npu_port_remove(port_id);
        if(sai_rc != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Failed to remove port id 0x%"PRIx64" in npu",port_id);
            break;
        }

    } while(0);

    sai_port_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        data.port_id = port_id;
        data.port_event = SAI_PORT_EVENT_DELETE;
        sai_port_event_internal_notf (1,&data);
        sai_port_validity_set (port_id, false);
    } else if (linkscan_set) { /* Enable linkscan when port removal failed */

        /* Function pointer check is not required as the flag will be enabled
         * only when it is not NULL  and function call to disable linkscan is
         * successful
         */
        if (sai_port_npu_api_get()->linkscan_mode_set(port_id, true)
                      != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Failed to enable linkscan on port 0x%"PRIx64" in npu",
                             port_id);
        }
    }

    return sai_rc;
}

sai_status_t sai_port_create(sai_object_id_t *port_id, sai_object_id_t switch_id,
                            uint32_t attr_count, const sai_attribute_t *attr_list)
{
    sai_port_info_t *port_info = NULL;
    uint_t attr_idx = 0;
    bool sai_port_created = false;
    sai_port_event_notification_t data;
    sai_status_t sai_rc = SAI_STATUS_FAILURE;

    sai_port_lock ();
    do {
        sai_rc = sai_port_npu_api_get()->npu_port_create(port_id, attr_count, attr_list);
        if (sai_rc != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Port create failed at npu");
            break;
        }

        sai_port_created = true;
        port_info = sai_port_info_get (*port_id);
        if(port_info == NULL) {
            sai_rc = SAI_STATUS_FAILURE;
            break;
        }
        sai_port_validity_set (*port_id, true);

        for (attr_idx = 0; attr_idx < attr_count; attr_idx++) {
            if (attr_list[attr_idx].id != SAI_PORT_ATTR_HW_LANE_LIST) {
                sai_rc = sai_port_apply_attribute(*port_id, &attr_list[attr_idx]);
                if(sai_rc != SAI_STATUS_SUCCESS) {
                    SAI_PORT_LOG_ERR("Error: Unable to set attribute %d on port 0x%"PRIx64"",
                                      attr_list[attr_idx].id, *port_id);
                    break;
                }
            }
        }
    } while(0);

    if(sai_rc != SAI_STATUS_SUCCESS) {
        if(sai_port_created) {
            sai_port_npu_api_get()->npu_port_remove(*port_id);
            sai_port_validity_set (*port_id, false);
        }
    }
    sai_port_unlock();

    if(sai_rc == SAI_STATUS_SUCCESS) {
        data.port_id = *port_id;
        data.port_event = SAI_PORT_EVENT_ADD;
        sai_port_event_internal_notf (1,&data);

        sai_port_lock();
        if ((sai_port_npu_api_get()->linkscan_mode_set != NULL) &&
                 (sai_port_npu_api_get()->linkscan_mode_set(*port_id, true)
                      != SAI_STATUS_SUCCESS)) {
            SAI_PORT_LOG_ERR("Failed to enable linkcan on port 0x%"PRIx64" in npu",
                             *port_id);
        }
        sai_port_unlock();
    }

    return sai_rc;
}

static sai_port_api_t sai_port_method_table =
{
    sai_port_create,
    sai_port_remove,
    sai_port_set_attribute,
    sai_port_get_attribute,
    sai_port_get_stats,
    sai_port_clear_stats,
    sai_port_clear_all_stats,
};

sai_port_api_t* sai_port_api_query(void)
{
    return (&sai_port_method_table);
}
