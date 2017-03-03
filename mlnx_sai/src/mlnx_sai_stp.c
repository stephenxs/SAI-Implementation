/*
 *  Copyright (C) 2014-2015. Mellanox Technologies, Ltd. ALL RIGHTS RESERVED.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License"); you may
 *    not use this file except in compliance with the License. You may obtain
 *    a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 *    THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 *    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *    LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 *    FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 *    See the Apache Version 2.0 License for specific language governing
 *    permissions and limitations under the License.
 *
 */

#include "sai_windows.h"
#include "sai.h"
#include "mlnx_sai.h"
#include "assert.h"

#undef  __MODULE__
#define __MODULE__ SAI_STP


static sx_verbosity_level_t LOG_VAR_NAME(__MODULE__) = SX_VERBOSITY_LEVEL_WARNING;

/*..... Function Prototypes ..................*/
static sai_status_t mlnx_stp_vlanlist_get(_In_ const sai_object_key_t   *key,
                                          _Inout_ sai_attribute_value_t *value,
                                          _In_ uint32_t                  attr_index,
                                          _Inout_ vendor_cache_t        *cache,
                                          _In_ void                     *arg);
static sai_status_t mlnx_stp_ports_get(_In_ const sai_object_key_t   *key,
                                       _Inout_ sai_attribute_value_t *value,
                                       _In_ uint32_t                  attr_index,
                                       _Inout_ vendor_cache_t        *cache,
                                       _In_ void                     *arg);

/* STP instance attributes */
static const sai_attribute_entry_t        stp_attribs[] = {
    { SAI_STP_ATTR_VLAN_LIST, false, false, false, true,
      "List of associated VLANs", SAI_ATTR_VAL_TYPE_VLANLIST },
    { SAI_STP_ATTR_PORT_LIST, false, false, false, true,
      "List of associated ports", SAI_ATTR_VAL_TYPE_OBJLIST },
    { END_FUNCTIONALITY_ATTRIBS_ID, false, false, false, false,
      "", SAI_ATTR_VAL_TYPE_UNDETERMINED }
};
static const sai_vendor_attribute_entry_t stp_vendor_attribs[] = {
    { SAI_STP_ATTR_VLAN_LIST,
      { false, false, false, true },
      { false, false, false, true },
      mlnx_stp_vlanlist_get, NULL,
      NULL, NULL },
    { SAI_STP_ATTR_PORT_LIST,
      { false, false, false, true },
      { false, false, false, true },
      mlnx_stp_ports_get, NULL,
      NULL, NULL }
};
static sai_status_t mlnx_stp_port_stp_id_get(_In_ const sai_object_key_t   *key,
                                             _Inout_ sai_attribute_value_t *value,
                                             _In_ uint32_t                  attr_index,
                                             _Inout_ vendor_cache_t        *cache,
                                             _In_ void                     *arg);
static sai_status_t mlnx_stp_port_port_id_get(_In_ const sai_object_key_t   *key,
                                              _Inout_ sai_attribute_value_t *value,
                                              _In_ uint32_t                  attr_index,
                                              _Inout_ vendor_cache_t        *cache,
                                              _In_ void                     *arg);
static sai_status_t mlnx_stp_port_state_get(_In_ const sai_object_key_t   *key,
                                            _Inout_ sai_attribute_value_t *value,
                                            _In_ uint32_t                  attr_index,
                                            _Inout_ vendor_cache_t        *cache,
                                            _In_ void                     *arg);
static sai_status_t mlnx_stp_port_state_set(_In_ const sai_object_key_t      *key,
                                            _In_ const sai_attribute_value_t *value,
                                            void                             *arg);

/* STP Port object attributes */
static const sai_attribute_entry_t        stp_port_attribs[] = {
    { SAI_STP_PORT_ATTR_STP, true, true, false, true,
      "STP instance id", SAI_ATTR_VAL_TYPE_OID },
    { SAI_STP_PORT_ATTR_PORT, true, true, false, true,
      "Port object id", SAI_ATTR_VAL_TYPE_OID },
    { SAI_STP_PORT_ATTR_STATE, true, true, true, true,
      "STP Port state", SAI_ATTR_VAL_TYPE_S32 },
    { END_FUNCTIONALITY_ATTRIBS_ID, false, false, false, false,
      "", SAI_ATTR_VAL_TYPE_UNDETERMINED }
};
static const sai_vendor_attribute_entry_t stp_port_vendor_attribs[] = {
    { SAI_STP_PORT_ATTR_STP,
      { true, false, false, true },
      { true, false, false, true },
      mlnx_stp_port_stp_id_get, NULL,
      NULL, NULL },
    { SAI_STP_PORT_ATTR_PORT,
      { true, false, false, true },
      { true, false, false, true },
      mlnx_stp_port_port_id_get, NULL,
      NULL, NULL },
    { SAI_STP_PORT_ATTR_STATE,
      { true, false, true, true },
      { true, false, true, true },
      mlnx_stp_port_state_get, NULL,
      mlnx_stp_port_state_set, NULL }
};
static void stp_id_to_str(_In_ sai_object_id_t sai_stp_id, _Out_ char *key_str)
{
    uint32_t     data;
    sai_status_t status;

    status = mlnx_object_to_type(sai_stp_id, SAI_OBJECT_TYPE_STP,
                                 &data, NULL);
    if (SAI_ERR(status)) {
        snprintf(key_str, MAX_KEY_STR_LEN, "Invalid STP instance id");
    } else {
        snprintf(key_str, MAX_KEY_STR_LEN, "STP instance id [%u]", (sx_mstp_inst_id_t)data);
    }
}

/* Generate instance id (will be passed to SDK API) */
static sai_status_t create_stp_id(_Out_ sx_mstp_inst_id_t* sx_stp_id)
{
    sai_status_t      status;
    sx_mstp_inst_id_t ii;
    mlnx_mstp_inst_t *stp_db_entry;

    SX_LOG_ENTER();

    /* look for unused element */
    for (ii = SX_MSTP_INST_ID_MIN; ii <= SX_MSTP_INST_ID_MAX; ii++) {
        stp_db_entry = get_stp_db_entry(ii);
        if (stp_db_entry->is_used == false) {
            /* return instance id */
            *sx_stp_id            = ii;
            stp_db_entry->is_used = true;
            SX_LOG_DBG("Generated STP id [%u]\n", ii);
            status = SAI_STATUS_SUCCESS;
            goto out;
        }
    }

    /* if no free id found, return errorcode */
    SX_LOG_ERR("STP instances DB is full\n");
    status = SAI_STATUS_TABLE_FULL;

out:
    SX_LOG_EXIT();
    return status;
}

/* remove instance id from database */
static void remove_stp_id(_In_ sx_mstp_inst_id_t sx_stp_id)
{
    mlnx_mstp_inst_t *stp_db_entry;

    SX_LOG_ENTER();

    SX_LOG_NTC("Removing instance id [%u] from STP db \n", sx_stp_id);

    stp_db_entry          = get_stp_db_entry(sx_stp_id);
    stp_db_entry->is_used = false;

    SX_LOG_EXIT();
}

/**
 * @brief Create stp instance with default port state as blocking.
 *
 * @param[out] sai_stp_id stp instance id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Value of attributes
 *
 * @return #SAI_STATUS_SUCCESS if operation is successful otherwise a different
 * error code is returned.
 */
static sai_status_t mlnx_create_stp(_Out_ sai_object_id_t      *sai_stp_id,
                                    _In_ sai_object_id_t        switch_id,
                                    _In_ uint32_t               attr_count,
                                    _In_ const sai_attribute_t *attr_list)
{
    sx_mstp_inst_id_t sx_stp_id = SX_MSTP_INST_ID_MAX + 1;
    sx_status_t       status;

    SX_LOG_ENTER();

    if (sai_stp_id == NULL) {
        SX_LOG_ERR("NULL object id\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    status = check_attribs_metadata(attr_count, attr_list, stp_attribs,
                                    stp_vendor_attribs, SAI_COMMON_API_CREATE);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed attribs check\n");
        return status;
    }

    assert(NULL != g_sai_db_ptr);
    sai_db_write_lock();

    status = create_stp_id(&sx_stp_id);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to generate STP instance id\n");
        goto out;
    }

    SX_LOG_DBG("Creating new STP instance [%u]\n", sx_stp_id);

    status = sx_api_mstp_inst_set(gh_sdk, SX_ACCESS_CMD_ADD,
                                  DEFAULT_ETH_SWID, sx_stp_id);
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        remove_stp_id(sx_stp_id);
        status = sdk_to_sai(status);
        goto out;
    }

    status = mlnx_create_object(SAI_OBJECT_TYPE_STP, sx_stp_id,
                                NULL, sai_stp_id);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to create object of stp_id [%u]\n", sx_stp_id);
        goto out;
    }

out:
    sai_db_unlock();
    SX_LOG_EXIT();
    return status;
}

/**
 * @brief Remove stp instance.
 *
 * @param[in] sai_stp_id Stp instance id
 *
 * @return #SAI_STATUS_SUCCESS if operation is successful otherwise a different
 * error code is returned.
 */
static sai_status_t mlnx_remove_stp(_In_ sai_object_id_t sai_stp_id)
{
    sx_status_t       status;
    uint32_t          data;
    sx_mstp_inst_id_t sx_stp_id;
    sx_mstp_inst_id_t def_stp_id;
    uint32_t          vlan_cnt;      /* number of VLANs associated */

    SX_LOG_ENTER();

    status = mlnx_object_to_type(sai_stp_id, SAI_OBJECT_TYPE_STP,
                                 &data, NULL);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to get STP instance id of object [%" PRIx64 "]\n", sai_stp_id);
        return status;
    }

    sx_stp_id = (sx_mstp_inst_id_t)data;

    SX_LOG_NTC("Removing STP number [%u]\n", sx_stp_id);

    assert(NULL != g_sai_db_ptr);
    sai_db_read_lock();

    def_stp_id = mlnx_stp_get_default_stp();
    sai_db_unlock();

    if (sx_stp_id == def_stp_id) {
        SX_LOG_ERR("Removing default STP is not permitted\n");
        return SAI_STATUS_INVALID_OBJECT_ID;
    }

    /* check for VLANs associated */
    status = sx_api_mstp_inst_vlan_list_get(gh_sdk, DEFAULT_ETH_SWID, sx_stp_id, NULL, &vlan_cnt);
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        return sdk_to_sai(status);
    } else if (vlan_cnt != 0) {
        SX_LOG_ERR("Failed to remove STP number [%u]: it still has some VLANs\n", sx_stp_id);
        return SAI_STATUS_OBJECT_IN_USE;
    }

    /* remove STP instance */
    status = sx_api_mstp_inst_set(gh_sdk, SX_ACCESS_CMD_DELETE, DEFAULT_ETH_SWID, sx_stp_id);
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        return sdk_to_sai(status);
    }

    assert(NULL != g_sai_db_ptr);
    sai_db_write_lock();

    /* remove instance id from db */
    remove_stp_id(sx_stp_id);

    sai_db_unlock();

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Set the attribute of STP instance.
 *
 * @param[in] sai_stp_id Stp instance id
 * @param[in] attr attribute value
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
sai_status_t mlnx_set_stp_attribute(_In_ sai_object_id_t sai_stp_id, _In_ const sai_attribute_t *attr)
{
    const sai_object_key_t key = {.key.object_id = sai_stp_id };
    char                   key_str[MAX_KEY_STR_LEN];
    sai_status_t           status;

    SX_LOG_ENTER();

    stp_id_to_str(sai_stp_id, key_str);

    status = sai_set_attribute(&key, key_str, stp_attribs, stp_vendor_attribs, attr);
    if (SAI_ERR(status)) {
        return status;
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Get the attribute of STP instance.
 *
 * @param[in] sai_stp_id stp instance id
 * @param[in] attr_count number of the attribute
 * @param[in] attr_list attribute value
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
static sai_status_t mlnx_get_stp_attribute(_In_ const sai_object_id_t sai_stp_id,
                                           _In_ uint32_t              attr_count,
                                           _Inout_ sai_attribute_t   *attr_list)
{
    const sai_object_key_t key = { .key.object_id = sai_stp_id };
    char                   key_str[MAX_KEY_STR_LEN];
    sai_status_t           status;

    SX_LOG_ENTER();

    stp_id_to_str(sai_stp_id, key_str);

    status = sai_get_attributes(&key, key_str, stp_attribs, stp_vendor_attribs, attr_count, attr_list);
    if (SAI_ERR(status)) {
        return status;
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/* vlanlist getter */
static sai_status_t mlnx_stp_vlanlist_get(_In_ const sai_object_key_t   *key,
                                          _Inout_ sai_attribute_value_t *value,
                                          _In_ uint32_t                  attr_index,
                                          _Inout_ vendor_cache_t        *cache,
                                          _In_ void                     *arg)
{
    sx_status_t           status;
    const sai_object_id_t sai_stp_id = key->key.object_id;
    sx_mstp_inst_id_t     sx_stp_id;
    uint32_t              data;
    mlnx_mstp_inst_t     *stp_db_entry;

    SX_LOG_ENTER();

    /* Get sx_stp_id */
    status = mlnx_object_to_type(sai_stp_id, SAI_OBJECT_TYPE_STP,
                                 &data, NULL);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to get stp_id of object [%" PRIx64 "]\n", sai_stp_id);
        return status;
    }

    sx_stp_id = (sx_mstp_inst_id_t)data;

    /* validate STP id */
    if (!SX_MSTP_INST_ID_CHECK_RANGE(sx_stp_id)) {
        SX_LOG_ERR("Invalid STP id: should be within a range [%u - %u]\n",
                   SX_MSTP_INST_ID_MIN, SX_MSTP_INST_ID_MAX);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    /* Get number of VLANS added to specified STP instance */
    assert(NULL != g_sai_db_ptr);
    sai_db_read_lock();

    stp_db_entry = get_stp_db_entry(sx_stp_id);

    /* Check if user has got enough memory to store the vlanlist */
    if (value->vlanlist.count < stp_db_entry->vlan_count) {
        SX_LOG_ERR("Not enough memory to store %u VLANs\n", stp_db_entry->vlan_count);
        status = SAI_STATUS_BUFFER_OVERFLOW;
        goto out;
    }

    /* Call SDK API to read VLAN list */
    status = sx_api_mstp_inst_vlan_list_get(gh_sdk, DEFAULT_ETH_SWID, sx_stp_id,
                                            value->vlanlist.list,
                                            &value->vlanlist.count);
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        status = sdk_to_sai(status);
        goto out;
    }

out:
    sai_db_unlock();
    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/* port list getter */
static sai_status_t mlnx_stp_ports_get(_In_ const sai_object_key_t   *key,
                                       _Inout_ sai_attribute_value_t *value,
                                       _In_ uint32_t                  attr_index,
                                       _Inout_ vendor_cache_t        *cache,
                                       _In_ void                     *arg)
{
    const sai_object_id_t sai_stp_id  = key->key.object_id;
    uint32_t              ports_count = 0;
    sai_object_id_t      *ports_list  = NULL;
    sx_mstp_inst_id_t     sx_stp_id;
    sai_status_t          status;
    mlnx_port_config_t   *port;
    uint32_t              data;
    uint32_t              idx;
    uint32_t              ii = 0;
    uint16_t              vid;

    SX_LOG_ENTER();

    /* Get sx_stp_id */
    status = mlnx_object_to_type(sai_stp_id, SAI_OBJECT_TYPE_STP,
                                 &data, NULL);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to get stp_id of object [%" PRIx64 "]\n", sai_stp_id);
        return status;
    }

    sx_stp_id = (sx_mstp_inst_id_t)data;

    /* validate STP id */
    if (!SX_MSTP_INST_ID_CHECK_RANGE(sx_stp_id)) {
        SX_LOG_ERR("Invalid STP id: should be within a range [%u - %u]\n",
                   SX_MSTP_INST_ID_MIN, SX_MSTP_INST_ID_MAX);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    sai_db_read_lock();

    mlnx_stp_vlans_foreach(sx_stp_id, vid) {
        mlnx_vlan_ports_foreach(vid, port, idx) {
            ports_count++;
        }
    }

    if (!ports_count) {
        value->objlist.count = 0;
        status               = SAI_STATUS_SUCCESS;
        goto out;
    }

    ports_list = calloc(ports_count, sizeof(sai_object_id_t));
    if (!ports_list) {
        status = SAI_STATUS_NO_MEMORY;
        goto out;
    }

    mlnx_stp_vlans_foreach(sx_stp_id, vid) {
        mlnx_vlan_ports_foreach(vid, port, idx) {
            bool             is_stp_port_exist = false;
            mlnx_object_id_t port_stp_oid;
            sai_object_id_t  oid;
            uint32_t         jj;

            memset(&port_stp_oid, 0, sizeof(port_stp_oid));

            port_stp_oid.id.log_port_id = port->logical;
            port_stp_oid.ext.stp.id     = sx_stp_id;

            status = mlnx_object_id_to_sai(SAI_OBJECT_TYPE_STP_PORT, &port_stp_oid, &oid);
            if (SAI_ERR(status)) {
                goto out;
            }

            /* In case same port is in few VLANs it will appear in the list few times,
             * so let's filter it out */
            for (jj = 0; jj < ii; jj++) {
                if (ports_list[jj] == oid) {
                    is_stp_port_exist = true;
                    break;
                }
            }

            if (!is_stp_port_exist) {
                ports_list[ii++] = oid;
            }
        }
    }

    ports_count = ii;

    status = mlnx_fill_objlist(ports_list, ports_count, &value->objlist);

out:
    sai_db_unlock();
    free(ports_list);
    SX_LOG_EXIT();

    return SAI_STATUS_SUCCESS;
}

/*
 * SAI STP Port
 */

static void stp_port_id_to_str(_In_ sai_object_id_t sai_stp_port_id, _Out_ char *key_str)
{
    mlnx_object_id_t stp_port;
    sai_status_t     status;

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP_PORT, sai_stp_port_id, &stp_port);
    if (SAI_ERR(status)) {
        snprintf(key_str, MAX_KEY_STR_LEN, "Invalid STP Port id");
    } else {
        snprintf(key_str, MAX_KEY_STR_LEN, "STP Port (%u, 0x%x)",
                 stp_port.ext.stp.id,
                 stp_port.id.log_port_id);
    }
}

static sai_status_t sai_stp_port_state_validate(sai_stp_port_state_t state)
{
    switch (state) {
    case SAI_STP_PORT_STATE_LEARNING:
    case SAI_STP_PORT_STATE_FORWARDING:
    case SAI_STP_PORT_STATE_BLOCKING:
        return SAI_STATUS_SUCCESS;

    default:
        SX_LOG_ERR("Invalid port state passed\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }
}

static sx_mstp_inst_port_state_t sai_stp_port_state_to_sdk(sai_stp_port_state_t state)
{
    switch (state) {
    case SAI_STP_PORT_STATE_LEARNING:
        return SX_MSTP_INST_PORT_STATE_LEARNING;

    case SAI_STP_PORT_STATE_FORWARDING:
        return SX_MSTP_INST_PORT_STATE_FORWARDING;

    case SAI_STP_PORT_STATE_BLOCKING:
        return SX_MSTP_INST_PORT_STATE_DISCARDING;
    }

    return SX_MSTP_INST_PORT_STATE_MAX + 1;
}

/**
 * @brief Create stp port object
 *
 * @param[out] stp_port_id stp port id
 * @param[in] attr_count Number of attributes
 * @param[in] attr_list Value of attributes
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
static sai_status_t mlnx_create_stp_port(_Out_ sai_object_id_t      *stp_port_id,
                                         _In_ sai_object_id_t        switch_id,
                                         _In_ uint32_t               attr_count,
                                         _In_ const sai_attribute_t *attr_list)
{
    uint32_t                     stp_index, port_index, state_index;
    char                         list_str[MAX_LIST_VALUE_STR_LEN];
    char                         key_str[MAX_KEY_STR_LEN];
    const sai_attribute_value_t *stp, *port, *state;
    sx_mstp_inst_port_state_t    sx_port_state;
    mlnx_object_id_t             stp_port_obj_id;
    mlnx_object_id_t             port_obj_id;
    mlnx_object_id_t             stp_obj_id;
    sx_status_t                  sx_status;
    sai_status_t                 status;

    SX_LOG_ENTER();

    if (stp_port_id == NULL) {
        SX_LOG_ERR("NULL object id\n");
        return SAI_STATUS_INVALID_PARAMETER;
    }

    status = check_attribs_metadata(attr_count, attr_list, stp_port_attribs,
                                    stp_port_vendor_attribs, SAI_COMMON_API_CREATE);
    if (SAI_ERR(status)) {
        return status;
    }

    sai_attr_list_to_str(attr_count, attr_list, stp_port_attribs, MAX_LIST_VALUE_STR_LEN, list_str);
    SX_LOG_NTC("Create STP Port, %s\n", list_str);

    status = find_attrib_in_list(attr_count, attr_list, SAI_STP_PORT_ATTR_STP, &stp, &stp_index);
    assert(status == SAI_STATUS_SUCCESS);

    status = find_attrib_in_list(attr_count, attr_list, SAI_STP_PORT_ATTR_PORT, &port, &port_index);
    assert(status == SAI_STATUS_SUCCESS);

    status = find_attrib_in_list(attr_count, attr_list, SAI_STP_PORT_ATTR_STATE, &state, &state_index);
    assert(status == SAI_STATUS_SUCCESS);

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP, stp->oid, &stp_obj_id);
    if (SAI_ERR(status)) {
        return status;
    }

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_PORT, port->oid, &port_obj_id);
    if (SAI_ERR(status)) {
        return status;
    }

    status = sai_stp_port_state_validate(state->s32);
    if (SAI_ERR(status)) {
        return status;
    }

    sx_port_state = sai_stp_port_state_to_sdk(state->s32);

    sx_status = sx_api_mstp_inst_port_state_set(gh_sdk, DEFAULT_ETH_SWID,
                                                stp_obj_id.id.stp_inst_id,
                                                port_obj_id.id.log_port_id,
                                                sx_port_state);
    if (SX_ERR(sx_status)) {
        SX_LOG_ERR("Failed to set stp port state (%u) - %s\n", sx_port_state,
                   SX_STATUS_MSG(sx_status));

        return sdk_to_sai(status);
    }

    memset(&stp_port_obj_id, 0, sizeof(stp_port_obj_id));

    stp_port_obj_id.id.log_port_id = port_obj_id.id.log_port_id;
    stp_port_obj_id.ext.stp.id     = stp_obj_id.id.stp_inst_id;

    status = mlnx_object_id_to_sai(SAI_OBJECT_TYPE_STP_PORT, &stp_port_obj_id, stp_port_id);
    if (SAI_ERR(status)) {
        return status;
    }

    stp_port_id_to_str(*stp_port_id, key_str);
    SX_LOG_NTC("Created STP Port %s\n", key_str);

    SX_LOG_EXIT();
    return status;
}

/**
 * @brief Remove stp port object.
 *
 * @param[in] stp_port_id stp object id
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
static sai_status_t mlnx_remove_stp_port(_In_ sai_object_id_t stp_port_id)
{
    return SAI_STATUS_NOT_SUPPORTED;
}

/**
 * @brief Bulk stp ports creation.
 *
 * @param[in] switch_id SAI Switch object id
 * @param[in] object_count Number of objects to create
 * @param[in] attr_count List of attr_count. Caller passes the number
 *         of attribute for each object to create.
 * @param[in] attrs List of attributes for every object.
 * @param[in] type bulk operation type.
 *
 * @param[out] object_id List of object ids returned
 * @param[out] object_statuses List of status for every object. Caller needs to allocate the buffer.
 *
 * @return #SAI_STATUS_SUCCESS on success when all objects are created or #SAI_STATUS_FAILURE when
 * any of the objects fails to create. When there is failure, Caller is expected to go through the
 * list of returned statuses to find out which fails and which succeeds.
 */
sai_status_t mlnx_create_stp_ports(_In_ sai_object_id_t    switch_id,
                                   _In_ uint32_t           object_count,
                                   _In_ uint32_t          *attr_count,
                                   _In_ sai_attribute_t  **attrs,
                                   _In_ sai_bulk_op_type_t type,
                                   _Out_ sai_object_id_t  *object_id,
                                   _Out_ sai_status_t     *object_statuses)
{
    return SAI_STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief Bulk stp ports removal.
 *
 * @param[in] object_count Number of objects to create
 * @param[in] object_id List of object ids
 * @param[in] type bulk operation type.
 * @param[out] object_statuses List of status for every object. Caller needs to allocate the buffer.
 *
 * @return #SAI_STATUS_SUCCESS on success when all objects are removed or #SAI_STATUS_FAILURE when
 * any of the objects fails to remove. When there is failure, Caller is expected to go through the
 * list of returned statuses to find out which fails and which succeeds.
 */
sai_status_t mlnx_remove_stp_ports(_In_ uint32_t           object_count,
                                   _In_ sai_object_id_t   *object_id,
                                   _In_ sai_bulk_op_type_t type,
                                   _Out_ sai_status_t     *object_statuses)
{
    return SAI_STATUS_NOT_IMPLEMENTED;
}

/**
 * @brief STP id
 * @type sai_object_id_t
 * @objects SAI_OBJECT_TYPE_STP
 * @flags MANDATORY_ON_CREATE | CREATE_ONLY
 */
static sai_status_t mlnx_stp_port_stp_id_get(_In_ const sai_object_key_t   *key,
                                             _Inout_ sai_attribute_value_t *value,
                                             _In_ uint32_t                  attr_index,
                                             _Inout_ vendor_cache_t        *cache,
                                             _In_ void                     *arg)
{
    mlnx_object_id_t stp_port;
    sai_status_t     status;

    SX_LOG_ENTER();

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP_PORT, key->key.object_id, &stp_port);
    if (SAI_ERR(status)) {
        goto out;
    }

    status = mlnx_create_object(SAI_OBJECT_TYPE_STP,
                                stp_port.ext.stp.id,
                                NULL, &value->oid);
out:
    SX_LOG_EXIT();
    return status;
}

/**
 * @brief Port id
 * @type sai_object_id_t
 * @objects SAI_OBJECT_TYPE_PORT
 * @flags MANDATORY_ON_CREATE | CREATE_ONLY
 */
static sai_status_t mlnx_stp_port_port_id_get(_In_ const sai_object_key_t   *key,
                                              _Inout_ sai_attribute_value_t *value,
                                              _In_ uint32_t                  attr_index,
                                              _Inout_ vendor_cache_t        *cache,
                                              _In_ void                     *arg)
{
    mlnx_object_id_t stp_port;
    mlnx_object_id_t port;
    sai_status_t     status;

    SX_LOG_ENTER();

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP_PORT, key->key.object_id, &stp_port);
    if (SAI_ERR(status)) {
        goto out;
    }

    memset(&port, 0, sizeof(port));

    port.id.log_port_id = stp_port.id.log_port_id;

    status = mlnx_object_id_to_sai(SAI_OBJECT_TYPE_PORT, &port, &value->oid);

out:
    SX_LOG_EXIT();
    return status;
}

/**
 * @brief STP port state
 * @type sai_stp_port_state_t
 * @flags MANDATORY_ON_CREATE | CREATE_AND_SET
 */
static sai_status_t mlnx_stp_port_state_get(_In_ const sai_object_key_t   *key,
                                            _Inout_ sai_attribute_value_t *value,
                                            _In_ uint32_t                  attr_index,
                                            _Inout_ vendor_cache_t        *cache,
                                            _In_ void                     *arg)
{
    sx_mstp_inst_port_state_t sx_port_state;
    mlnx_object_id_t          stp_port;
    sx_status_t               status;

    SX_LOG_ENTER();

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP_PORT, key->key.object_id, &stp_port);
    if (SAI_ERR(status)) {
        return status;
    }

    status = sx_api_mstp_inst_port_state_get(gh_sdk, DEFAULT_ETH_SWID,
                                             stp_port.ext.stp.id,
                                             stp_port.id.log_port_id,
                                             &sx_port_state);
    if (SX_ERR(status)) {
        SX_LOG_ERR("Failed to get stp state - %s\n", SX_STATUS_MSG(status));
        return sdk_to_sai(status);
    }

    switch (sx_port_state) {
    case SX_MSTP_INST_PORT_STATE_LEARNING:
        value->s32 = SAI_STP_PORT_STATE_LEARNING;
        break;

    case SX_MSTP_INST_PORT_STATE_FORWARDING:
        value->s32 = SAI_STP_PORT_STATE_FORWARDING;
        break;

    case SX_MSTP_INST_PORT_STATE_DISCARDING:
        value->s32 = SAI_STP_PORT_STATE_BLOCKING;
        break;

    default:
        SX_LOG_ERR("Invalid port state - %u\n", sx_port_state);
        return SAI_STATUS_INVALID_PARAMETER;
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief STP port state
 * @type sai_stp_port_state_t
 * @flags MANDATORY_ON_CREATE | CREATE_AND_SET
 */
static sai_status_t mlnx_stp_port_state_set(_In_ const sai_object_key_t      *key,
                                            _In_ const sai_attribute_value_t *value,
                                            void                             *arg)
{
    sx_mstp_inst_port_state_t sx_port_state;
    mlnx_object_id_t          stp_port;
    sx_status_t               status;

    SX_LOG_ENTER();

    status = sai_to_mlnx_object_id(SAI_OBJECT_TYPE_STP_PORT, key->key.object_id, &stp_port);
    if (SAI_ERR(status)) {
        return status;
    }

    status = sai_stp_port_state_validate(value->s32);
    if (SAI_ERR(status)) {
        return status;
    }

    sx_port_state = sai_stp_port_state_to_sdk(value->s32);

    status = sx_api_mstp_inst_port_state_set(gh_sdk, DEFAULT_ETH_SWID,
                                             stp_port.ext.stp.id,
                                             stp_port.id.log_port_id,
                                             sx_port_state);

    if (SX_ERR(status)) {
        SX_LOG_ERR("Failed to set stp port state (%u) - %s\n", sx_port_state,
                   SX_STATUS_MSG(status));

        return sdk_to_sai(status);
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Set the attribute of STP port.
 *
 * @param[in] stp_port_id stp port id
 * @param[in] attr attribute value
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
static sai_status_t mlnx_set_stp_port_attribute(_In_ sai_object_id_t stp_port_id, _In_ const sai_attribute_t *attr)
{
    const sai_object_key_t key = {.key.object_id = stp_port_id };
    char                   key_str[MAX_KEY_STR_LEN];
    sai_status_t           status;

    SX_LOG_ENTER();

    stp_port_id_to_str(stp_port_id, key_str);

    status = sai_set_attribute(&key, key_str, stp_port_attribs, stp_port_vendor_attribs, attr);
    if (SAI_ERR(status)) {
        return status;
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/**
 * @brief Get the attribute of STP port.
 *
 * @param[in] stp_port_id stp port id
 * @param[in] attr_count number of the attribute
 * @param[in] attr_list attribute value
 * @return SAI_STATUS_SUCCESS if operation is successful otherwise a different
 *  error code is returned.
 */
static sai_status_t mlnx_get_stp_port_attribute(_In_ sai_object_id_t     stp_port_id,
                                                _In_ uint32_t            attr_count,
                                                _Inout_ sai_attribute_t *attr_list)
{
    const sai_object_key_t key = { .key.object_id = stp_port_id };
    char                   key_str[MAX_KEY_STR_LEN];
    sai_status_t           status;

    SX_LOG_ENTER();

    stp_port_id_to_str(stp_port_id, key_str);

    status = sai_get_attributes(&key, key_str, stp_port_attribs, stp_port_vendor_attribs, attr_count, attr_list);
    if (SAI_ERR(status)) {
        return status;
    }

    SX_LOG_EXIT();
    return SAI_STATUS_SUCCESS;
}

/* STP initializer. */
/* Called when switch is starting. */
sai_status_t mlnx_stp_initialize()
{
    sx_status_t status;

    SX_LOG_ENTER();

    /* set MSTP mode */
    status = sx_api_mstp_mode_set(gh_sdk, DEFAULT_ETH_SWID, SX_MSTP_MODE_MSTP);
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        return sdk_to_sai(status);
    }

    /* Generate default STP instance id */
    SX_LOG_DBG("Generating default STP id\n");

    status = create_stp_id(&g_sai_db_ptr->def_stp_id);
    if (SAI_ERR(status)) {
        SX_LOG_ERR("Failed to generate default STP id\n");
        goto out;
    }

    SX_LOG_DBG("Default STP id = %u\n", mlnx_stp_get_default_stp());

    /* Create default STP instance */
    status = sx_api_mstp_inst_set(gh_sdk, SX_ACCESS_CMD_ADD,
                                  DEFAULT_ETH_SWID, mlnx_stp_get_default_stp());
    if (SX_ERR(status)) {
        SX_LOG_ERR("%s\n", SX_STATUS_MSG(status));
        remove_stp_id(mlnx_stp_get_default_stp());
        status = sdk_to_sai(status);
        goto out;
    }

    /* init VLAN db with INVALID STPs */
    sai_vlan_id_t ii;
    mlnx_vlan_id_foreach(ii) {
        mlnx_vlan_stp_id_set(ii, SAI_INVALID_STP_INSTANCE);
    }

    /* Add VLAN 1 to default STP */
    status = mlnx_vlan_stp_bind(DEFAULT_VLAN, mlnx_stp_get_default_stp());
    if (SAI_ERR(status)) {
        remove_stp_id(mlnx_stp_get_default_stp());
        SX_LOG_ERR("Failed to add VLAN %u to default STP\n", DEFAULT_VLAN);
        goto out;
    }

out:
    SX_LOG_EXIT();
    return status;
}

sai_status_t mlnx_stp_log_set(sx_verbosity_level_t level)
{
    LOG_VAR_NAME(__MODULE__) = level;

    if (gh_sdk) {
        return sdk_to_sai(sx_api_mstp_log_verbosity_level_set(gh_sdk, SX_LOG_VERBOSITY_BOTH,
                                                              level, level));
    } else {
        return SAI_STATUS_SUCCESS;
    }
}

sx_mstp_inst_id_t mlnx_stp_get_default_stp()
{
    return (g_sai_db_ptr->def_stp_id);
}

mlnx_mstp_inst_t * get_stp_db_entry(sx_mstp_inst_id_t sx_stp_id)
{
    return (&g_sai_db_ptr->mlnx_mstp_inst_db[sx_stp_id - SX_MSTP_INST_ID_MIN]);
}

const sai_stp_api_t mlnx_stp_api = {
    mlnx_create_stp,
    mlnx_remove_stp,
    mlnx_set_stp_attribute,
    mlnx_get_stp_attribute,
    mlnx_create_stp_port,
    mlnx_remove_stp_port,
    mlnx_set_stp_port_attribute,
    mlnx_get_stp_port_attribute,
    mlnx_create_stp_ports,
    mlnx_remove_stp_ports,
};
