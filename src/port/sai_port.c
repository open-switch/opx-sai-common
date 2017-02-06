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

/*
 * SAI internal port event notification handler, which invokes
 * registered SAI port event notification function
 */
static sai_port_event_notification_fn sai_port_event_callback = NULL;

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

sai_status_t sai_port_set_attribute(sai_object_id_t port_id,
                                    const sai_attribute_t *attr)
{
    sai_status_t ret = SAI_STATUS_FAILURE;

    STD_ASSERT(!(attr == NULL));

    SAI_PORT_LOG_TRACE("Attribute %d set for port 0x%"PRIx64"", attr->id, port_id);

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
            case SAI_PORT_ATTR_QOS_TC_TO_DSCP_MAP:
            case SAI_PORT_ATTR_QOS_TC_TO_DOT1P_MAP:
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

    sai_port_unlock();
    return ret;
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
            SAI_PORT_LOG_ERR("Attr get for port id 0x%"PRIx64" failed with err %d",
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
    sai_attr_set.id = SAI_PORT_ATTR_INTERNAL_LOOPBACK;
    sai_attr_set.value.s32 = port_attr_info->internal_loopback;
    ret = sai_port_set_attribute(sai_port_id, &sai_attr_set);
    if (ret != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("SAI Port ID 0x%"PRIx64" default internal loopback %d set failed with err %d",
                          sai_port_id, port_attr_info->internal_loopback, ret);
        return ret;
    }

    memset(&sai_attr_set, 0, sizeof(sai_attribute_t));
    sai_attr_set.id = SAI_PORT_ATTR_FDB_LEARNING;
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

/* Notify Adapter host through the registered port event callback */
void sai_port_event_notification(uint32_t count,
                                 sai_port_event_notification_t *data)
{
    STD_ASSERT(data != NULL);

    /* @todo Invoke other modules for port related changes based on
     * port ADD/DELETE event */

    if(sai_port_event_callback != NULL) {
        sai_port_event_callback(count, data);
    }
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

void sai_port_event_register_callback(sai_port_event_notification_fn port_event_notf_fn)
{
    SAI_PORT_LOG_TRACE("Port Event notification registration");

    sai_port_lock();
    sai_port_event_callback = port_event_notf_fn;
    sai_port_unlock();
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
    sai_port_event_notification(count, data);
}

/*
 * Port Add/Delete event notification to registered Adapter host/Internal SAI modules.
 * This API loops through all ports in the breakout port list and creates notification */
static void sai_port_breakout_add_delete_event_notification(bool internal_notf,
                                                            const sai_port_breakout_t *portbreakout,
                                                            sai_port_event_t event_type)
{
    uint_t port_idx = 0;
    sai_port_event_notification_t data[portbreakout->port_list.count];

    STD_ASSERT(portbreakout != NULL);
    STD_ASSERT(portbreakout->port_list.list != NULL);

    for(port_idx = 0; port_idx < portbreakout->port_list.count; port_idx++) {
        data[port_idx].port_id = portbreakout->port_list.list[port_idx];
        data[port_idx].port_event = event_type;
    }

    if(internal_notf) {
        sai_port_event_internal_notf(portbreakout->port_list.count, data);
    } else {
        /* Adapter host notification */
        sai_port_event_notification(portbreakout->port_list.count, data);
    }
}

/* Port add event notification to registered Adapter host/Internal SAI modules.
 * As breakout will create new ports which are to notified, this API retrieves
 * the new ports and creates ADD notification. */
static void sai_port_breakout_add_event_notification(bool internal_notf,
                                                     const sai_port_breakout_t *portbreakout)
{
    uint_t port_idx = 0, port_count = 0, lane = 0, max_lanes = 0;
    sai_object_id_t port = SAI_NULL_OBJECT_ID;
    sai_object_id_t port_list[SAI_PORT_BREAKOUT_MODE_MAX];
    sai_status_t ret_code = SAI_STATUS_SUCCESS;

    STD_ASSERT(portbreakout != NULL);
    STD_ASSERT(portbreakout->port_list.list != NULL);

    sai_port_breakout_mode_type_t breakout_mode = portbreakout->breakout_mode;

    /* Update the port list on the breakout mode applied */
    if(breakout_mode == SAI_PORT_BREAKOUT_MODE_4_LANE) {

        /* ADD notification for the control port */
        port_list[port_count] = port = portbreakout->port_list.list[port_idx];
        port_count++;

        sai_port_lock();

        do {
            if(sai_port_max_lanes_get(port, &max_lanes) != SAI_STATUS_SUCCESS) {
                SAI_PORT_LOG_ERR("Max port lane get failed for port 0x%"PRIx64"", port);
                ret_code = SAI_STATUS_FAILURE;
                break;
            }

            sai_port_info_t *port_info = sai_port_info_get(port);
            if(port_info == NULL) {
                SAI_PORT_LOG_ERR("port info get returns NULL for port 0x%"PRIx64"", port);
                ret_code = SAI_STATUS_FAILURE;
                break;
            }

            /* ADD notification for the subsidiary ports */
            for (lane = 1; lane < max_lanes; lane++) {
                port_info = sai_port_info_getnext(port_info);

                if(port_info == NULL) {
                    SAI_PORT_LOG_ERR("port info getnext returns NULL");
                    ret_code = SAI_STATUS_FAILURE;
                    break;
                }

                port_list[port_count] = port_info->sai_port_id;
                port_count++;
            }
        }while(0);

        sai_port_unlock();

        if(ret_code != SAI_STATUS_SUCCESS) {
            return;
        }

    } else if(breakout_mode == SAI_PORT_BREAKOUT_MODE_1_LANE) {

        /* Port list need not necessarily have the ports in consecutive order; Only
         * one port among the ports in port list will be valid after break-in.
         * Send ADD notification the first valid in the port list. */

        sai_port_lock();

        for(port_idx = 0; port_idx < portbreakout->port_list.count; port_idx++) {

            /* ADD notification for control and subsidiary ports */
            if(sai_is_port_valid(portbreakout->port_list.list[port_idx])) {

                port_list[port_count] = portbreakout->port_list.list[port_idx];
                port_count++;

                break;
            }
        }
        sai_port_unlock();
    }

    {
        sai_port_event_notification_t data[port_count];
        /* Invoke port event notification based on the notification type */
        for(port_idx = 0; port_idx < port_count; port_idx++) {
            data[port_idx].port_id = port_list[port_idx];
            data[port_idx].port_event = SAI_PORT_EVENT_ADD;
        }

        if(internal_notf) {
            sai_port_event_internal_notf(port_count, data);
        } else {
            sai_port_event_notification(port_count, data);
        }
    }
}

/* Post Breakout, provides ADD/DELETE port event callback notification to adapter host */
static void sai_port_breakout_event_notification(const sai_port_breakout_t *portbreakout)
{
    STD_ASSERT(portbreakout != NULL);

    SAI_PORT_LOG_TRACE("Creating SAI_PORT_EVENT after applying breakout mode %d",
                       portbreakout->breakout_mode);

    /* Delete notification to all ports in the breakout port list */
    sai_port_breakout_add_delete_event_notification(false, portbreakout,
                                                    SAI_PORT_EVENT_DELETE);

    /* Add notification to all the newly created ports */
    sai_port_breakout_add_event_notification(false, portbreakout);
}

/* Validates a given port and its breakout capability */
static sai_status_t sai_port_breakout_mode_validation(sai_object_id_t port,
                                                      sai_port_breakout_mode_type_t breakout_mode)
{
    bool breakout_sup = false;
    sai_status_t ret_code = SAI_STATUS_FAILURE;

    if(!sai_is_obj_id_port(port)) {
        SAI_PORT_LOG_ERR("Port id 0x%"PRIx64" is not a port object", port);
        return SAI_STATUS_INVALID_OBJECT_TYPE;
    }

    if(!sai_is_port_valid(port)) {
        SAI_PORT_LOG_ERR("Port 0x%"PRIx64" is not a valid port", port);
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    if(sai_is_obj_id_cpu_port(port)) {
        SAI_PORT_LOG_ERR("Breakout mode not supported on CPU port 0x%"PRIx64"", port);
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }

    ret_code = sai_is_port_capb_supported(port, sai_port_capb_from_break_mode(breakout_mode),
                                          &breakout_sup);
    if(ret_code != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("Breakout mode %d port capability support get failed "
                         "for port 0x%"PRIx64" with err %d", breakout_mode, port, ret_code);
        return ret_code;
    }

    if(!breakout_sup) {
        SAI_PORT_LOG_ERR("Breakout mode %d not supported on this port 0x%"PRIx64"",
                         breakout_mode, port);
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }

    return ret_code;
}

/* Validates the breakout mode and its corresponding lane count value */
static inline sai_status_t sai_port_breakout_validate(sai_port_breakout_mode_type_t mode,
                                                      uint_t count)
{
    sai_port_lane_count_t lane_count = 0;

    if(mode >= SAI_PORT_BREAKOUT_MODE_MAX) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
    }

    lane_count = sai_port_breakout_lane_count_get(mode);

    if((count == 0) || (count < lane_count)) {
        return SAI_STATUS_INVALID_ATTR_VALUE_0;
    }

    return SAI_STATUS_SUCCESS;
}

/* Invokes breakout mode NPU call and generates Add/Delete port event notification */
static sai_status_t sai_port_npu_breakout_set_event_notify(const sai_port_breakout_t *portbreakout)
{
    sai_status_t ret_code = SAI_STATUS_FAILURE;

    STD_ASSERT(portbreakout != NULL);

    /* Pre-breakout: Notify internal SAI modules with the port(s) to be deleted */
    sai_port_breakout_add_delete_event_notification(true, portbreakout,
                                                    SAI_PORT_EVENT_DELETE);

    sai_port_lock();
    ret_code = sai_port_npu_api_get()->breakout_set(portbreakout);
    sai_port_unlock();

    if(ret_code != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("NPU Breakout mode set failed with error %d", ret_code);

        /* On NPU breakout set failure, send internal notification with ADD for the ports
         * which are deleted earlier */
        sai_port_breakout_add_delete_event_notification(true, portbreakout,
                                                        SAI_PORT_EVENT_ADD);

        return ret_code;
    }

    /* Post-breakout: Notify internal SAI modules with the port(s) added after breakout */
    sai_port_breakout_add_event_notification(true, portbreakout);

    /* Notify Adapter host about the port events after breakout mode Succeeds */
    sai_port_breakout_event_notification(portbreakout);

    return ret_code;
}

sai_status_t sai_port_breakout_set(const sai_port_breakout_t *portbreakout)
{
    uint_t port_idx = 0, port_group = 0, temp_port_group = 0;
    sai_object_id_t *port = NULL;
    bool dyn_breakout = false;
    sai_status_t ret_code = SAI_STATUS_FAILURE;

    STD_ASSERT(portbreakout != NULL);
    STD_ASSERT(portbreakout->port_list.list != NULL);

    dyn_breakout = sai_is_switch_capb_supported(SAI_SWITCH_CAP_DYNAMIC_BREAKOUT_MODE);
    if(!dyn_breakout){
        SAI_PORT_LOG_ERR("Dynamic breakout mode not supported on this switch");
        return SAI_STATUS_ATTR_NOT_SUPPORTED_0;
    }

    /* Validate the breakout mode support on all ports in the list */
    sai_port_breakout_mode_type_t breakout_mode = portbreakout->breakout_mode;

    ret_code = sai_port_breakout_validate(breakout_mode,
                                          portbreakout->port_list.count);
    if(ret_code != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("Breakout mode %d validation failed with err %d",
                         portbreakout->breakout_mode, ret_code);
        return ret_code;
    }

    port = &portbreakout->port_list.list[port_idx];
    sai_port_lock();

    do {
        if(breakout_mode == sai_port_current_breakout_mode_get(*port)) {

            SAI_PORT_LOG_NTC("Breakout mode %d is already enable on port 0x%"PRIx64"",
                               breakout_mode, *port);

            ret_code = SAI_STATUS_ITEM_ALREADY_EXISTS;
            break;
        }

        /* In a multi-lane port, all the lanes of the port are part of the same port group.
         * To apply break-in, all ports in the port list be part of the same port group
         * Get the port group of the first port in the list and validate it against the
         * remaining ports in the list.
         */

        ret_code = sai_port_breakout_mode_validation(*port, breakout_mode);
        if(ret_code != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Port 0x%"PRIx64" validation failed with err %d",
                             *port, ret_code);
            break;
        }

        ret_code = sai_port_port_group_get(*port, &port_group);
        if(ret_code != SAI_STATUS_SUCCESS) {
            SAI_PORT_LOG_ERR("Port group get for port 0x%"PRIx64" failed with err %d",
                             *port, ret_code);
            break;
        }

        for(port_idx = 1; port_idx < portbreakout->port_list.count; ++port_idx, ++port) {

            ret_code = sai_port_breakout_mode_validation(*port, breakout_mode);
            if(ret_code != SAI_STATUS_SUCCESS) {
                SAI_PORT_LOG_ERR("Port 0x%"PRIx64" validation failed with err %d",
                                 *port, ret_code);
                break;
            }

            ret_code = sai_port_port_group_get(*port, &temp_port_group);
            if(ret_code != SAI_STATUS_SUCCESS) {
                SAI_PORT_LOG_ERR("Port group get for port 0x%"PRIx64" failed with err %d",
                                 *port, ret_code);
                break;
            }

            /* Verify the port_group of first port in the list with all other ports */
            if(port_group != temp_port_group) {
                ret_code = SAI_STATUS_INVALID_OBJECT_ID;
                break;
            }
        }

    } while(0);

    sai_port_unlock();

    if(ret_code != SAI_STATUS_SUCCESS) {
        return ret_code;
    }

    ret_code = sai_port_npu_breakout_set_event_notify(portbreakout);
    if(ret_code != SAI_STATUS_SUCCESS) {
        SAI_PORT_LOG_ERR("NPU Breakout mode set and event notify call failed with err %d",
                         ret_code);
    }

    return ret_code;
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

static sai_port_api_t sai_port_method_table =
{
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
