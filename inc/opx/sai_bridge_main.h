/************************************************************************
* * LEGALESE:   "Copyright (c) 2017, Dell Inc. All rights reserved."
* *
* * This source code is confidential, proprietary, and contains trade
* * secrets that are the sole property of Dell Inc.
* * Copy and/or distribution of this source code or disassembly or reverse
* * engineering of the resultant object code are strictly forbidden without
* * the written consent of Dell Inc.
* *
*************************************************************************/
/**
* @file sai_bridge_main.h
*
* @brief Private header file contains the declarations for APIs used in
*        sai_bridge.c
*
*************************************************************************/


#ifndef __SAI_BRIDGE_MAIN_H__
#define __SAI_BRIDGE_MAIN_H__
#include "saitypes.h"
#include "sai_bridge_common.h"

#define SAI_BRIDGE_DEF_BRIDGE_PORT_ATTR_COUNT 3
sai_status_t sai_bridge_init (void);

sai_status_t sai_bridge_default_id_get (sai_object_id_t *default_bridge_id);

/**
 * @brief Register callback for bridge port event
 *
 * @param[in] module_id Module Identifier
 * @return Pointer to attached port info structure
 */
sai_status_t sai_bridge_port_event_cb_register(sai_module_t module_id,
                                               uint_t bridge_port_type_bmp,
                                               sai_bridge_port_event_cb_fn bridge_port_cb);


void sai_bridge_dump_bridge_info(sai_object_id_t bridge_id);

void sai_brige_dump_default_bridge(void);

void sai_bridge_dump_bridge_port_info(sai_object_id_t bridge_port_id);

void sai_bridge_dump_all_bridge_ids(void);

void sai_bridge_dump_all_bridge_info(void);

void sai_bridge_dump_all_bridge_port_ids(void);

void sai_bridge_dump_all_bridge_port_info(void);
#endif
