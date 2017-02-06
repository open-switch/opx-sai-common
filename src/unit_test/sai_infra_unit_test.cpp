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
 * @file std_cfg_file_gtest.cpp
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gtest/gtest.h"

extern "C" {
#include "sai.h"
#include "sai_infra_api.h"
}

#include <iostream>
#include <map>

typedef std::map<std::string, std::string> sai_kv_pair_t;
typedef std::map<std::string, std::string>::iterator kv_iter;
static sai_kv_pair_t kvpair;

const char* profile_get_value(sai_switch_profile_id_t profile_id,
                                 const char* variable)
{
    kv_iter kviter;
    std::string key;

    if (variable ==  NULL)
        return NULL;

    key = variable;

    kviter = kvpair.find(key);
    if (kviter == kvpair.end()) {
        return NULL;
    }
    return kviter->second.c_str();
}

int profile_get_next_value(sai_switch_profile_id_t profile_id,
                           const char** variable,
                           const char** value)
{
    kv_iter kviter;
    std::string key;

    if (variable == NULL || value == NULL) {
        return -1;
    }
    if (*variable == NULL) {
            if (kvpair.size() < 1) {
            return -1;
        }
        kviter = kvpair.begin();
    } else {
        key = *variable;
        kviter = kvpair.find(key);
        if (kviter == kvpair.end()) {
            return -1;
        }
        kviter++;
        if (kviter == kvpair.end()) {
            return -1;
        }
    }
    *variable = (char *)kviter->first.c_str();
    *value = (char *)kviter->second.c_str();
    return 0;
}


void kv_populate(void)
{
    kvpair["SAI_FDB_TABLE_SIZE"] = "16384";
    kvpair["SAI_L3_ROUTE_TABLE_SIZE"] = "8192";
    kvpair["SAI_NUM_CPU_QUEUES"] = "43";
    kvpair["SAI_INIT_CONFIG_FILE"] = "/etc/opx/sai/init.xml";
    kvpair["SAI_NUM_ECMP_MEMBERS"] = "64";
}

/*
 * Pass the service method table and do API intialize.
 */
TEST(sai_unit_test, api_init)
{
    service_method_table_t  sai_service_method_table;
    kv_populate();

    sai_service_method_table.profile_get_value = profile_get_value;
    sai_service_method_table.profile_get_next_value = profile_get_next_value;

    ASSERT_EQ(SAI_STATUS_SUCCESS,sai_api_initialize(0, &sai_service_method_table));
}

/*
 *Obtain the method table for the sai switch api.
 */
TEST(sai_unit_test, api_query)
{
    sai_switch_api_t *sai_switch_api_table = NULL;

    ASSERT_EQ(NULL,sai_api_query(SAI_API_SWITCH,
              (static_cast<void**>(static_cast<void*>(&sai_switch_api_table)))));

    EXPECT_TRUE(sai_switch_api_table->initialize_switch != NULL);
    EXPECT_TRUE(sai_switch_api_table->shutdown_switch != NULL);
    EXPECT_TRUE(sai_switch_api_table->connect_switch != NULL);
    EXPECT_TRUE(sai_switch_api_table->disconnect_switch != NULL);
    EXPECT_TRUE(sai_switch_api_table->set_switch_attribute != NULL);
    EXPECT_TRUE(sai_switch_api_table->get_switch_attribute != NULL);

}

/*
 *Verify if object_type_query returns OBJECT_TYPE_NULL for invalid object id.
 */
TEST(sai_unit_test, sai_object_type_query)
{
    sai_object_id_t  invalid_obj_id = 0;

    ASSERT_EQ(SAI_OBJECT_TYPE_NULL,sai_object_type_query(invalid_obj_id));
}

/*
 * Unintialize the SDK and free up the resources.
 */
TEST(sai_unit_test, api_uninit)
{
    ASSERT_EQ(SAI_STATUS_SUCCESS,sai_api_uninitialize());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

