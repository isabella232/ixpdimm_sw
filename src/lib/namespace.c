/*
 * Copyright (c) 2015 2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file contains the implementation of App Direct and Storage namespace
 * management functions of the Native API.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "nvm_management.h"
#include <persistence/logging.h>
#include "monitor.h"
#include <persistence/lib_persistence.h>
#include "device_adapter.h"
#include "platform_config_data.h"
#include "utility.h"
#include <file_ops/file_ops_adapter.h>
#include <uid/uid.h>
#include <string/s_str.h>
#include "device_utilities.h"
#include "string/x_str.h"
#include "pool_utilities.h"
#include "capabilities.h"
#include <os/os_adapter.h>
#include "system.h"
#include "nvm_context.h"
#include "namespace_utils.h"

int validate_namespace_block_count(const NVM_UID namespace_uid,
		NVM_UINT64 *block_count, const NVM_BOOL allow_adjustment);

int find_interleave_with_capacity(const struct pool *p_pool,
		const struct nvm_capabilities *p_nvm_caps,
		struct namespace_create_settings *p_settings,
		NVM_UINT32 *namespace_creation_id,
		const struct interleave_format *p_format,
		NVM_BOOL allow_adjustment,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count);

int find_id_for_ns_creation(const struct pool *p_pool,
		const struct nvm_capabilities *p_nvm_caps,
		struct namespace_create_settings *p_settings,
		NVM_UINT32 *p_namespace_creation_id,
		const struct interleave_format *p_format,
		NVM_BOOL allow_adjustment,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count);

NVM_BOOL interleave_meets_app_direct_settings(const struct interleave_set *p_interleave,
		const struct interleave_format *p_format);

NVM_UINT64 get_minimum_ns_size(NVM_UINT8 ways, NVM_UINT64 driver_min_ns_size);

int get_pool_supported_size_ranges(const struct pool *p_pool,
		const struct nvm_capabilities *p_caps,
		struct possible_namespace_ranges *p_range,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count, const NVM_UINT8 ways);

/*
 * Retrieve the number of PM namespaces allocated across all devices.
 */
int nvm_get_namespace_count()
{
	COMMON_LOG_ENTRY();
	int rc = 0; // return count

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(get_namespaces)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Retrieving namespaces is not supported.");
	}
	else
	{
		rc = nvm_get_pool_count();
		if (rc >= 0)
		{
			rc = get_namespace_count();
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Retrieve basic information on all PM namespaces.
 */
int nvm_get_namespaces(struct namespace_discovery *p_namespaces, const NVM_UINT8 count)
{
	COMMON_LOG_ENTRY();
	int rc = 0; // return count

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(get_namespaces)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Retrieving namespaces is not supported.");
	}
	else if (p_namespaces == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_namespaces is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (count == 0)
	{
		COMMON_LOG_ERROR("Invalid parameter, count is 0");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	// read from the cache
	else if ((rc = get_nvm_context_namespaces(p_namespaces, count)) < 0 &&
			rc != NVM_ERR_ARRAYTOOSMALL)
	{
		// zero out return data
		memset(p_namespaces, 0, (sizeof (struct namespace_discovery) * count));

		// get the actual count
		int ns_count = nvm_get_namespace_count();
		if (ns_count <= 0)
		{
			rc = ns_count;
		}
		else
		{
			struct nvm_namespace_discovery *nvm_namespaces = malloc(ns_count * sizeof(struct nvm_namespace_discovery));
			ns_count = get_namespaces(ns_count, nvm_namespaces);
			if (ns_count <= 0)
			{
				rc = ns_count;
			}
			else
			{
				rc = 0;
				for (int i = 0; i < ns_count; i++)
				{
					// check array size
					if (i >= count)
					{
						COMMON_LOG_ERROR("The provided namespace array is too small.");
						rc = NVM_ERR_ARRAYTOOSMALL;
						break;
					}

					// simple copy - same size struct
					memmove(&p_namespaces[i], &nvm_namespaces[i], sizeof (p_namespaces[i]));
					rc++; // return number of namespaces

				} // end for each ns
				if (rc >= 0)
				{
					// update the context
					set_nvm_context_namespaces(p_namespaces, rc);
				}

			} // end get_namespaces succeeded

            free(nvm_namespaces);
		} // end get namespace count succeeded
	} // end valid input parameters

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}


enum encryption_status namespace_encryption_to_enum(const NVM_BOOL passphrase_capable)
{
	COMMON_LOG_ENTRY();

	enum encryption_status enum_val;
	if (passphrase_capable)
	{
		enum_val = NVM_ENCRYPTION_ON;
	}
	else
	{
		enum_val = NVM_ENCRYPTION_OFF;
	}

	COMMON_LOG_EXIT_RETURN_I(enum_val);
	return enum_val;
}

/*
 * Retrieve detailed information about the namespace specified.
 */
int nvm_get_namespace_details(const NVM_UID namespace_uid, struct namespace_details *p_namespace)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != COMMON_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(get_namespace_details)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Retrieving namespaces is not supported.");
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_namespace == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_namespace is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (get_nvm_context_namespace_details(namespace_uid, p_namespace) != NVM_SUCCESS)
	{
		memset(p_namespace, 0, sizeof (*p_namespace));

		// get the namespace from the driver
		struct nvm_namespace_details nvm_details;
		memset(&nvm_details, 0, sizeof (nvm_details));
		struct pool *p_pool = calloc(1, sizeof (struct pool));
		if (!p_pool)
		{
			COMMON_LOG_ERROR("Unable to allocate memory for pool\n");
			rc = NVM_ERR_NOMEMORY;
		}
		else
		{
			if ((rc = get_namespace_details(namespace_uid, &nvm_details)) == NVM_SUCCESS &&
				(rc = get_pool_from_namespace_details(&nvm_details, p_pool)) == NVM_SUCCESS)
			{
				memmove(p_namespace->pool_uid, p_pool->pool_uid, NVM_MAX_UID_LEN);
				s_strcpy(p_namespace->discovery.friendly_name,
						nvm_details.discovery.friendly_name, NVM_NAMESPACE_NAME_LEN);
				memmove(p_namespace->discovery.namespace_uid,
						nvm_details.discovery.namespace_uid, NVM_MAX_UID_LEN);
				p_namespace->block_count = nvm_details.block_count;
				p_namespace->block_size = nvm_details.block_size;
				p_namespace->type = nvm_details.type;
				p_namespace->health = nvm_details.health;
				p_namespace->enabled = nvm_details.enabled;
				p_namespace->btt = nvm_details.btt;
				p_namespace->memory_page_allocation = nvm_details.memory_page_allocation;

				if (p_namespace->type == NAMESPACE_TYPE_APP_DIRECT)
				{
					p_namespace->creation_id.interleave_setid =
						nvm_details.namespace_creation_id.interleave_setid;
					// find the underlying iset
					NVM_BOOL iset_found = 0;
					for (int i = 0; i < p_pool->ilset_count; i++)
					{
						if (p_pool->ilsets[i].driver_id ==
							p_namespace->creation_id.interleave_setid)
						{
							iset_found = 1;
							p_namespace->mirrored = p_pool->ilsets[i].mirrored;
							memmove(&p_namespace->interleave_format,
								&p_pool->ilsets[i].settings,
								sizeof (p_pool->ilsets[i].settings));
							p_namespace->security_features.encryption =
									p_pool->ilsets[i].encryption;
							p_namespace->security_features.erase_capable =
									p_pool->ilsets[i].erase_capable;
							break;
						}
					}
					if (!iset_found)
					{
						COMMON_LOG_ERROR_F(
							"Failed to find the namespace underlying interleave set %d\n",
							p_namespace->creation_id.interleave_setid);
						rc = NVM_ERR_DRIVERFAILED;
					}
				}
				free(p_pool);
			}
			// update the context
			if (NVM_SUCCESS == rc)
			{
				set_nvm_context_namespace_details(namespace_uid, p_namespace);
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to validate namespace enable state
 */
int validate_ns_enabled_state(enum namespace_enable_state enabled)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if ((enabled != NAMESPACE_ENABLE_STATE_ENABLED) &&
			(enabled != NAMESPACE_ENABLE_STATE_DISABLED))
	{
		COMMON_LOG_ERROR("Invalid enable state to create a namespace.");
		rc = NVM_ERR_BADNAMESPACEENABLESTATE;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to check if a device meets a requested encryption requirement
 */
NVM_BOOL device_meets_encryption_criteria(const enum encryption_status criteria,
		const struct device_discovery *discovery)
{
	NVM_BOOL criteria_met = 1;
	NVM_BOOL encryption_enabled = device_is_encryption_enabled(discovery->lock_state);
	if (criteria == NVM_ENCRYPTION_ON && !encryption_enabled)
	{
		criteria_met = 0;
	}
	else if (criteria == NVM_ENCRYPTION_OFF && encryption_enabled)
	{
		criteria_met = 0;
	}
	// else NVM_ENCRYPTION_IGNORE
	return criteria_met;
}

/*
 * Helper function to check if a device meets a requested encryption requirement
 */
NVM_BOOL device_meets_erase_capable_criteria(const enum erase_capable_status criteria,
		const struct device_discovery *discovery)
{
	NVM_BOOL criteria_met = 1;
	NVM_BOOL erase_capable = device_is_erase_capable(discovery->security_capabilities);
	if (criteria == NVM_ERASE_CAPABLE_TRUE && !erase_capable)
	{
		criteria_met = 0;
	}
	else if (criteria == NVM_ERASE_CAPABLE_FALSE && erase_capable)
	{
		criteria_met = 0;
	}
	// else NVM_ERASE_CAPABLE_IGNORE
	return criteria_met;
}

/*
 * Helper function to check if dimm meets security requirements to create
 * a storage NS
 */
NVM_BOOL device_meets_security_criteria(const struct device_discovery *p_discovery,
		const struct namespace_security_features *security_features)
{
	COMMON_LOG_ENTRY();
	NVM_BOOL security_criteria_met = 1;
	if ((security_features->erase_capable != NVM_ERASE_CAPABLE_IGNORE) ||
		(security_features->encryption != NVM_ENCRYPTION_IGNORE))
	{
		security_criteria_met = device_meets_erase_capable_criteria(
			security_features->erase_capable, p_discovery);
		if (security_criteria_met)
		{
			security_criteria_met = device_meets_encryption_criteria(
				security_features->encryption, p_discovery);
		}
	}
	COMMON_LOG_EXIT_RETURN_I(security_criteria_met);
	return security_criteria_met;
}

NVM_BOOL dimm_meets_security_criteria(const NVM_UID dimm,
		const struct namespace_security_features *security_features)
{
	COMMON_LOG_ENTRY();

	NVM_BOOL security_criteria_met = 1;
	// ignore security features if these fields are set to ignore
	struct device_discovery discovery;
	if (nvm_get_device_discovery(dimm, &discovery) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Failed to get device discovery information.");
		security_criteria_met = 0;
	}
	else
	{
		security_criteria_met = device_meets_security_criteria(
			&discovery, security_features);
	}
	COMMON_LOG_EXIT_RETURN_I(security_criteria_met);
	return security_criteria_met;
}

/*
 * Helper function to check if interleave set meets security requirements to create
 * App Direct NS
 */
NVM_BOOL interleave_meets_security_criteria(const struct interleave_set *p_ilset,
		const struct namespace_security_features *p_security_features)
{
	COMMON_LOG_ENTRY();

	NVM_BOOL security_criteria_met = 1;
	// ignore security features if these fields are set to ignore
	if ((p_security_features->erase_capable != NVM_ERASE_CAPABLE_IGNORE) ||
		(p_security_features->encryption != NVM_ENCRYPTION_IGNORE))
	{
		for (int dimm_index = 0; dimm_index < p_ilset->dimm_count; dimm_index++)
		{
			// verify if the security features match with the pool
			if (!dimm_meets_security_criteria(p_ilset->dimms[dimm_index], p_security_features))
			{
				security_criteria_met = 0;
				break;
			}
		} // end of for loop
	}

	COMMON_LOG_EXIT_RETURN_I(security_criteria_met);
	return security_criteria_met;
}

/*
 * Helper function to validate namespace size when creating a NS
 */
int validate_ns_size_for_creation(struct pool *p_pool,
		const struct namespace_create_settings *p_settings,
		const struct nvm_capabilities *p_nvm_caps,
		const struct possible_namespace_ranges *p_range)

{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	NVM_UINT64 namespace_capacity = p_settings->block_count * p_settings->block_size;

	if ((p_settings->type == NAMESPACE_TYPE_APP_DIRECT) &&
			(p_settings->block_size != 1))
	{
		COMMON_LOG_ERROR("Invalid block size to create an App Direct namespace.");
		rc = NVM_ERR_BADBLOCKSIZE;
	}
	else if (namespace_capacity > p_pool->free_capacity)
	{
		COMMON_LOG_ERROR_F("Requested namespace capacity %llu bytes \
					is more than the maximum available size of %llu bytes",
				namespace_capacity, p_pool->free_capacity);
		rc = NVM_ERR_BADSIZE;
	}
	else if (p_settings->type == NAMESPACE_TYPE_APP_DIRECT &&
			namespace_capacity > p_range->largest_possible_app_direct_ns)
	{
		COMMON_LOG_ERROR_F("Requested namespace capacity %llu bytes \
					is more than the maximum available size of %llu bytes",
				namespace_capacity, p_range->largest_possible_app_direct_ns);
		rc = NVM_ERR_BADSIZE;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int find_id_for_ns_creation(const struct pool *p_pool,
		const struct nvm_capabilities *p_nvm_caps,
		struct namespace_create_settings *p_settings,
		NVM_UINT32 *p_namespace_creation_id,
		const struct interleave_format *p_format,
		NVM_BOOL allow_adjustment,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (p_settings->type == NAMESPACE_TYPE_APP_DIRECT)
	{
		rc = find_interleave_with_capacity(p_pool, p_nvm_caps, p_settings,
				p_namespace_creation_id, p_format, allow_adjustment,
				p_namespaces, ns_count);
	}
	else
	{
		COMMON_LOG_ERROR("Invalid namespace type.");
		rc = NVM_ERR_BADNAMESPACETYPE;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int get_devices_matching_security(struct device_discovery *p_discoveries, NVM_UINT16 *dev_count,
		struct namespace_security_features *p_security_features)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	struct device_discovery *p_matching_discoveries = NULL;
	NVM_UINT16 matching_count = 0;
	p_matching_discoveries = calloc(*dev_count, sizeof (struct device_discovery));
	if (!p_matching_discoveries)
	{
		rc = NVM_ERR_NOMEMORY;
	}
	else
	{
		for (int i = 0; i < *dev_count; i++)
		{
			if (device_meets_security_criteria(&p_discoveries[i], p_security_features))
			{
				memmove(&p_matching_discoveries[matching_count++],
					&p_discoveries[i], sizeof (struct device_discovery));
			}
		}
		memmove(p_discoveries, p_matching_discoveries,
			matching_count * sizeof (struct device_discovery));
		*dev_count = matching_count;
	}

	free(p_matching_discoveries);
	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

NVM_BOOL are_all_dimms_locked(const struct device_discovery *p_discoveries, NVM_UINT16 dev_count)
{
	COMMON_LOG_ENTRY();
	NVM_BOOL all_locked = 1;
	for (int i = 0; i < dev_count; i++)
	{
		if (p_discoveries[i].lock_state != LOCK_STATE_LOCKED)
		{
			all_locked = 0;
			break;
		}
	}
	COMMON_LOG_EXIT();
	return all_locked;
}

NVM_UINT64 get_minimum_ns_size(NVM_UINT8 ways, NVM_UINT64 driver_min_ns_size)
{
	COMMON_LOG_ENTRY();

	NVM_UINT64 nvm_min_ns_size = BYTES_PER_GIB * ways;
	nvm_min_ns_size = (nvm_min_ns_size > driver_min_ns_size) ?
			nvm_min_ns_size : driver_min_ns_size;

	COMMON_LOG_EXIT();
	return nvm_min_ns_size;
}

NVM_BOOL interleave_set_has_locked_dimms(const struct interleave_set *p_ilset)
{
	COMMON_LOG_ENTRY();
	NVM_BOOL result = 0;

	struct device_discovery discovery;
	for (int dimm_index = 0; dimm_index < p_ilset->dimm_count; dimm_index++)
	{
		if (nvm_get_device_discovery(p_ilset->dimms[dimm_index], &discovery) != NVM_SUCCESS)
		{
			COMMON_LOG_ERROR("Failed to get device discovery information.");
		}
		else
		{
			if (discovery.lock_state == LOCK_STATE_LOCKED)
			{
				result = 1;
				break;
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(result);
	return result;
}


int find_interleave_with_capacity(const struct pool *p_pool,
		const struct nvm_capabilities *p_nvm_caps,
		struct namespace_create_settings *p_settings,
		NVM_UINT32 *namespace_creation_id,
		const struct interleave_format *p_format,
		NVM_BOOL allow_adjustment,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	NVM_BOOL at_least_one_security_met = 0;
	NVM_BOOL at_least_one_interleave_format_met = 0;
	NVM_BOOL at_least_one_alignment_met = 0;
	NVM_BOOL found_ilset = 0;
	NVM_BOOL at_least_size_is_good = 0;
	NVM_UINT64 new_block_count = 0;
	NVM_BOOL atleast_one_ilset_have_unlocked_dimms = 0;
	for (int ilset_index = 0; ilset_index < p_pool->ilset_count &&
			found_ilset == 0; ilset_index++)
	{
		const struct interleave_set *p_interleave = &(p_pool->ilsets[ilset_index]);
		if (!interleave_set_has_locked_dimms(p_interleave))
		{
			atleast_one_ilset_have_unlocked_dimms = 1;
			NVM_UINT64 minimum_ns_size = get_minimum_ns_size(p_interleave->dimm_count,
					p_interleave->dimm_count *
					p_nvm_caps->platform_capabilities.app_direct_mode.
						interleave_alignment_size);
			new_block_count = p_settings->block_count;
			adjust_namespace_block_count_if_allowed(&new_block_count, p_settings->block_size,
				p_interleave->dimm_count, allow_adjustment);
			NVM_UINT64 new_namespace_capacity = new_block_count * p_settings->block_size;

			// verify if the requested namespace capacity is available
			if (p_interleave->available_size >= new_namespace_capacity &&
					new_namespace_capacity >= minimum_ns_size)
			{
				at_least_size_is_good = 1;
				const NVM_BOOL is_security_met = interleave_meets_security_criteria(
						p_interleave, &p_settings->security_features);
				const NVM_BOOL is_interleave_format_met =
						interleave_meets_app_direct_settings(p_interleave, p_format);
				const NVM_BOOL namespace_alignment_is_good =
						check_namespace_alignment(new_namespace_capacity,
						p_settings->block_size, p_interleave->dimm_count);
				at_least_one_security_met |= is_security_met;
				at_least_one_interleave_format_met |= is_interleave_format_met;
				at_least_one_alignment_met |= namespace_alignment_is_good;

				if (is_security_met && is_interleave_format_met && namespace_alignment_is_good)
				{
					*namespace_creation_id = p_pool->ilsets[ilset_index].driver_id;
					found_ilset = 1;
				}
			}
		}
	}
	// determine return code based on search results
	if (found_ilset)
	{
		p_settings->block_count = new_block_count;
		rc = NVM_SUCCESS;
	}
	else if (!atleast_one_ilset_have_unlocked_dimms)
	{
		// all interleave sets have one/more locked dimms
		rc = NVM_ERR_BADSECURITYSTATE;
	}
	else if (!at_least_size_is_good)
	{
		// namespace request was either too big or too small
		rc = NVM_ERR_BADSIZE;
	}
	else if (!at_least_one_security_met)
	{
		// never met security goals
		rc = NVM_ERR_BADSECURITYGOAL;
	}
	else if (!at_least_one_alignment_met)
	{
		// never met the alignment requirement
		rc = NVM_ERR_BADALIGNMENT;
	}
	else
	{
		rc = NVM_ERR_BADNAMESPACESETTINGS;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

NVM_BOOL interleave_ways_match(const struct interleave_set *p_interleave,
		const struct interleave_format *p_format)
{
	NVM_BOOL format_is_x1 = p_format->ways == INTERLEAVE_WAYS_1 ? 1 : 0;
	NVM_BOOL ilset_is_x1 = p_interleave->settings.ways == INTERLEAVE_WAYS_1 ? 1 : 0;
	return format_is_x1 == ilset_is_x1 ? 1 : 0;
}

NVM_BOOL interleave_sizes_match(const struct interleave_set *p_interleave,
		const struct interleave_format *p_format)
{
	NVM_BOOL ret = 0;
	if (p_format->imc == p_interleave->settings.imc &&
		p_format->channel == p_interleave->settings.channel)
	{
		ret = 1;
	}
	return ret;
}

NVM_BOOL interleave_meets_app_direct_settings(const struct interleave_set *p_interleave,
		const struct interleave_format *p_format)
{
	COMMON_LOG_ENTRY();
	NVM_BOOL result = 0;

	if (p_format == NULL)
	{
		result = 1;
	}
	else
	{
		if (interleave_ways_match(p_interleave, p_format))
		{
			if (p_format->ways == INTERLEAVE_WAYS_1)
			{
				result = 1;
			}
			else if (interleave_sizes_match(p_interleave, p_format))
			{
				result = 1;
			}
		}
	}


	COMMON_LOG_EXIT_RETURN_I(result);
	return result;
}

int validate_ns_type_for_pool(enum namespace_type type, const struct pool *p_pool)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (type != NAMESPACE_TYPE_APP_DIRECT)
	{
		COMMON_LOG_ERROR("Invalid namespace type.");
		rc = NVM_ERR_BADNAMESPACETYPE;
	}
	else if (type == NAMESPACE_TYPE_APP_DIRECT &&
			((p_pool->type != POOL_TYPE_PERSISTENT_MIRROR) &&
			(p_pool->type != POOL_TYPE_PERSISTENT)))
	{
		COMMON_LOG_ERROR("Invalid namespace type for the specified pool.");
		rc = NVM_ERR_BADNAMESPACETYPE;
	}
	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int allocatedPageStructsAreInDramOrPmem(struct namespace_create_settings *p_settings)
{
	return ((p_settings->memory_page_allocation == NAMESPACE_MEMORY_PAGE_ALLOCATION_DRAM) ||
	(p_settings->memory_page_allocation == NAMESPACE_MEMORY_PAGE_ALLOCATION_APP_DIRECT));
}

int translate_pool_health_to_nvm_error(struct pool *p_pool)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	switch (p_pool->health)
	{
	case POOL_HEALTH_NORMAL:
		break;
	case POOL_HEALTH_PENDING:
		rc = NVM_ERR_GOALPENDING;
		break;
	case POOL_HEALTH_LOCKED:
		rc = NVM_ERR_BADSECURITYSTATE;
		break;
	case POOL_HEALTH_UNKNOWN:
	case POOL_HEALTH_ERROR:
	default:
		rc = NVM_ERR_BADPOOLHEALTH;
		break;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to validate namespace settings
 * Also determines namespace_creation_id to be used by the driver
 */
int validate_namespace_create_settings(struct pool *p_pool,
		struct namespace_create_settings *p_settings,
		const struct interleave_format *p_format,
		NVM_UINT32 *p_ns_creation_id,
		NVM_BOOL allow_adjustment)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	struct nvm_capabilities nvm_caps;
	memset(&nvm_caps, 0, sizeof (nvm_caps));

	if ((rc = nvm_get_nvm_capabilities(&nvm_caps)) == NVM_SUCCESS)
	{
		struct nvm_namespace_details *p_namespaces = NULL;
		int ns_count = get_nvm_namespaces_details_alloc(&p_namespaces);
		if (ns_count >= 0)
		{
			// get the pool supported size ranges
			struct possible_namespace_ranges range;
			memset(&range, 0, sizeof (range));
			NVM_UINT8 ways = INTERLEAVE_WAYS_0;
			if (NULL != p_format)
			{
				ways = p_format->ways;
			}
			if ((rc = get_pool_supported_size_ranges(p_pool, &nvm_caps,
					&range, p_namespaces, ns_count, ways)) == NVM_SUCCESS)
			{
				// can we even create a ns on this pool?
				if (p_settings->type == NAMESPACE_TYPE_APP_DIRECT)
				{
					if (!nvm_caps.nvm_features.app_direct_mode)
					{
						COMMON_LOG_ERROR(
							"App Direct namespaces not supported");
						rc = NVM_ERR_NOTSUPPORTED;
					}
					else if (range.largest_possible_app_direct_ns == 0)
					{
						COMMON_LOG_ERROR(
							"No more App Direct namespaces can be created on the pool");
						rc = NVM_ERR_TOOMANYNAMESPACES;
					}
					else if ((p_settings->btt) &&
						(allocatedPageStructsAreInDramOrPmem(p_settings)))
					{
						COMMON_LOG_ERROR(
								"Namespace can either be claimed by pfn or btt configuration");
						rc = NVM_ERR_NOTSUPPORTED;
					}
					else if ((allocatedPageStructsAreInDramOrPmem(p_settings)) &&
						(!nvm_caps.sw_capabilities.namespace_memory_page_allocation_capable))
					{
						COMMON_LOG_ERROR("Memory page allocation is not supported.");
						rc = NVM_ERR_NOTSUPPORTED;
					}
				}
				else
				{
					COMMON_LOG_ERROR("Invalid namespace type.");
					rc = NVM_ERR_BADNAMESPACETYPE;
				}

				if (rc == NVM_SUCCESS)
				{
					if ((rc = validate_ns_type_for_pool(p_settings->type,
							p_pool)) == NVM_SUCCESS &&
						(rc = validate_ns_enabled_state(p_settings->enabled)) == NVM_SUCCESS &&
						(rc = validate_ns_size_for_creation(p_pool, p_settings,
								&nvm_caps, &range)) == NVM_SUCCESS)
					{
						rc = find_id_for_ns_creation(p_pool, &nvm_caps, p_settings,
								p_ns_creation_id, p_format, allow_adjustment,
								p_namespaces, ns_count);
					}
				}
			}
			free(p_namespaces);
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int nvm_adjust_create_namespace_block_count(const NVM_UID pool_uid,
		struct namespace_create_settings *p_settings,
		const struct interleave_format *p_format)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(create_namespace)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Creating a namespace is not supported.");
	}
	else if (pool_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, pool uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_settings == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_settings is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else
	{
		struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
		if (p_pool)
		{
			if ((rc = nvm_get_pool(pool_uid, p_pool)) == NVM_SUCCESS)
			{
				if ((rc = translate_pool_health_to_nvm_error(p_pool)) == NVM_SUCCESS)
				{
					NVM_UINT32 namespace_creation_id;
					rc = validate_namespace_create_settings(p_pool, p_settings,
							p_format, &namespace_creation_id, 1);
				}
			}
			else
			{
				COMMON_LOG_ERROR_F("nvm_get_pool failed with rc - %d", rc);
			}
			free(p_pool);
		}
		else
		{
			rc = NVM_ERR_NOMEMORY;
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int nvm_adjust_modify_namespace_block_count(
		const NVM_UID namespace_uid, NVM_UINT64 *p_block_count)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	struct namespace_details details;
	memset(&details, 0, sizeof (details));

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_block_count == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, block_count pointer is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (*p_block_count <= 0)
	{
		COMMON_LOG_ERROR("Invalid parameter, block_count must be greater than zero");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = nvm_get_namespace_details(namespace_uid, &details)) == NVM_SUCCESS)
	{
		struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
		if ((p_pool) && ((rc = nvm_get_pool(details.pool_uid, p_pool)) == NVM_SUCCESS) &&
			((rc = translate_pool_health_to_nvm_error(p_pool)) == NVM_SUCCESS))
		{
			if (details.block_count < *p_block_count &&
					(rc = IS_NVM_FEATURE_LICENSED(grow_namespace)) != NVM_SUCCESS)
			{
				COMMON_LOG_ERROR("Increasing namespace size not supported.");
			}
			else if (details.block_count > *p_block_count &&
					(rc = IS_NVM_FEATURE_LICENSED(shrink_namespace)) != NVM_SUCCESS)
			{
				COMMON_LOG_ERROR("Decreasing namespace size not supported.");
			}
			else
			{
				rc = validate_namespace_block_count(namespace_uid, p_block_count, 1);
			}
		}
		free(p_pool);
	}
	else
	{
		COMMON_LOG_ERROR_F("nvm_get_namespace_details failed with rc - %d", rc);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Create a new namespace from the specified pool.
 */
int nvm_create_namespace(NVM_UID *p_namespace_uid, const NVM_UID pool_uid,
		struct namespace_create_settings *p_settings,
		const struct interleave_format *p_format, const NVM_BOOL allow_adjustment)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;
	NVM_UINT32 namespace_creation_id; // the identifier used by the driver to create a namespace

	struct nvm_capabilities nvm_caps;
	memset(&nvm_caps, 0, sizeof (nvm_caps));

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(create_namespace)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Creating a namespace is not supported.");
	}
	else if (pool_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, pool uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_settings == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_settings is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_namespace is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else
	{
		// check if pool exists
		struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
		if (p_pool)
		{
			if ((rc = nvm_get_pool(pool_uid, p_pool)) == NVM_SUCCESS)
			{
				if (((rc = translate_pool_health_to_nvm_error(p_pool)) == NVM_SUCCESS) &&
						(rc = validate_namespace_create_settings(p_pool, p_settings, p_format,
							&namespace_creation_id, allow_adjustment)) == NVM_SUCCESS)
				{
					struct nvm_namespace_create_settings nvm_settings;
					memset(&nvm_settings, 0, sizeof (nvm_settings));

					nvm_settings.type = p_settings->type;
					if (nvm_settings.type == NAMESPACE_TYPE_APP_DIRECT)
					{
						nvm_settings.namespace_creation_id.interleave_setid
							= namespace_creation_id;
						COMMON_LOG_DEBUG_F("Creating App Direct namespace on interleave set %u",
								namespace_creation_id);
					}
					else
					{
						nvm_settings.namespace_creation_id.device_handle.handle
							= namespace_creation_id;
						COMMON_LOG_DEBUG_F("Creating storage namespace on DIMM %u",
								namespace_creation_id);
					}
					if (s_strnlen(p_settings->friendly_name, NAMESPACE_FRIENDLY_NAME_LEN) == 0)
					{
						char friendly_name[NVM_NAMESPACE_NAME_LEN];
						int max_unique_id = 0;
						int namespace_count = nvm_get_namespace_count();
						if (namespace_count < 0)
						{
							namespace_count = 0;
						}

						if (namespace_count > 0)
						{
							// Figure out a unique ID for the default namespace name
							// Find the max value of the ID currently present,
							// and generate max_unique_id+1 as the ID
							struct nvm_namespace_discovery *nvm_namespaces =  malloc(namespace_count * sizeof(struct nvm_namespace_discovery));
							get_namespaces(namespace_count, nvm_namespaces);
							for (int ns = 0; ns < namespace_count; ns++)
							{
								// Determine ID only if it is a default namespace name
								if (!s_strncmp(NVM_DEFAULT_NAMESPACE_NAME,
										nvm_namespaces[ns].friendly_name,
										s_strnlen(NVM_DEFAULT_NAMESPACE_NAME,
												NVM_NAMESPACE_NAME_LEN)))
								{
									unsigned int id = 0;
									s_strtoui(nvm_namespaces[ns].friendly_name,
										s_strnlen(nvm_namespaces[ns].friendly_name,
											NVM_NAMESPACE_NAME_LEN),
										NULL,
										&id);
									if (id > max_unique_id)
									{
										max_unique_id = id;
									}
								}
							}

                            free(nvm_namespaces);
						}
						// create a unique friendly name - NvDimmVolN.
						char namespace_name[NVM_NAMESPACE_NAME_LEN];
						s_strcpy(namespace_name, NVM_DEFAULT_NAMESPACE_NAME,
								NVM_NAMESPACE_NAME_LEN);
						s_snprintf(friendly_name, NVM_NAMESPACE_NAME_LEN,
								s_strcat(namespace_name, NVM_NAMESPACE_NAME_LEN, "%d"),
										max_unique_id + 1);
						s_strcpy(nvm_settings.friendly_name,
								friendly_name, NVM_NAMESPACE_NAME_LEN);
					}
					else
					{
						s_strncpy(nvm_settings.friendly_name, NVM_NAMESPACE_NAME_LEN,
								p_settings->friendly_name, NVM_NAMESPACE_NAME_LEN);
					}
					nvm_settings.enabled = p_settings->enabled;
					nvm_settings.block_size = p_settings->block_size;
					nvm_settings.block_count = p_settings->block_count;
					nvm_settings.btt = p_settings->btt;
					nvm_settings.memory_page_allocation = p_settings->memory_page_allocation;

					rc = create_namespace(p_namespace_uid, &nvm_settings);
					if (rc == NVM_SUCCESS)
					{
						// the context is no longer valid
						invalidate_namespaces();

						// Log an event indicating we successfully created a namespace
						NVM_EVENT_ARG ns_uid_arg;
						uid_to_event_arg(*p_namespace_uid, ns_uid_arg);
						NVM_EVENT_ARG ns_name_arg;
						s_strncpy(ns_name_arg, NVM_EVENT_ARG_LEN,
								nvm_settings.friendly_name, NVM_NAMESPACE_NAME_LEN);
						log_mgmt_event(EVENT_SEVERITY_INFO,
								EVENT_CODE_MGMT_NAMESPACE_CREATED,
								*p_namespace_uid,
								0, // no action required
								ns_name_arg, ns_uid_arg, NULL);
					}
				}
			}
			else
			{
				COMMON_LOG_ERROR_F("couldn't get pool, rc = %d", rc);
			}
			free(p_pool);
		}
		else
		{
			rc = NVM_ERR_NOMEMORY;
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int validate_app_direct_namespace_size_for_modification(
		const struct pool *p_pool,
		NVM_UINT64 *p_block_count,
		struct nvm_namespace_details *p_nvm_details,
		struct nvm_capabilities *p_nvm_caps,
		const NVM_BOOL allow_adjustment)
{
	int rc = NVM_SUCCESS;

	for (int i = 0; i < p_pool->ilset_count; i++)
	{
		if (p_pool->ilsets[i].driver_id == p_nvm_details->namespace_creation_id.interleave_setid)
		{
			NVM_UINT64 minimum_ns_size = get_minimum_ns_size(p_pool->ilsets[i].dimm_count,
					p_pool->ilsets[i].dimm_count *
					p_nvm_caps->platform_capabilities.app_direct_mode.
						interleave_alignment_size);
			NVM_UINT64 old_namespace_capacity =
					p_nvm_details->block_count * (NVM_UINT64)p_nvm_details->block_size;
			NVM_UINT64 new_block_count = *p_block_count;
			adjust_namespace_block_count_if_allowed(&new_block_count, p_nvm_details->block_size,
					p_pool->ilsets[i].dimm_count, allow_adjustment);
			NVM_UINT64 namespace_capacity = new_block_count * p_nvm_details->block_size;

			if ((namespace_capacity	> old_namespace_capacity) &&
					(p_pool->ilsets[i].available_size <
					(namespace_capacity - old_namespace_capacity)))
			{
				COMMON_LOG_ERROR_F("Caller requested namespace capacity \
					%llu bytes is more than the available size on the \
					interleave set %llu bytes",
					namespace_capacity, p_pool->ilsets[i].available_size);
				rc = NVM_ERR_BADSIZE;
			}
			else if (namespace_capacity < minimum_ns_size)
			{
					COMMON_LOG_ERROR_F("Caller requested namespace capacity \
						%llu bytes is less than the smallest \
						supported namespace size %llu bytes",
						namespace_capacity, minimum_ns_size);
					rc = NVM_ERR_BADSIZE;
			}
			else if (!check_namespace_alignment(namespace_capacity,
					p_nvm_details->block_size, p_pool->ilsets[i].dimm_count))
			{
				COMMON_LOG_ERROR_F("Caller requested namespace capacity %llu bytes is \
						not in alignment with the namespace alignment size \
						supported by the driver %llu bytes",
						namespace_capacity, p_pool->free_capacity);
				rc = NVM_ERR_BADALIGNMENT;
			}
			else
			{
				*p_block_count = new_block_count;
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Helper function to validate namespace size when modifying a NS
 */
int validate_namespace_size_for_modification(const struct pool *p_pool, NVM_UINT64 *p_block_count,
	struct nvm_namespace_details *p_nvm_details, const NVM_BOOL allow_adjustment)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	NVM_UINT64 namespace_capacity = *p_block_count * (NVM_UINT64)p_nvm_details->block_size;
	struct nvm_capabilities nvm_caps;
	memset(&nvm_caps, 0, sizeof (nvm_caps));
	if ((rc = nvm_get_nvm_capabilities(&nvm_caps)) == NVM_SUCCESS)
	{
		NVM_UINT32 real_block_size = get_real_block_size(p_nvm_details->block_size);
		NVM_UINT64 old_namespace_capacity =
				(NVM_UINT64)real_block_size * p_nvm_details->block_count;

		if ((namespace_capacity > old_namespace_capacity) &&
				(p_pool->free_capacity < (namespace_capacity - old_namespace_capacity)))
		{
			COMMON_LOG_ERROR_F("Caller requested namespace capacity %llu bytes \
					is more than the available size of pool %llu bytes",
					namespace_capacity, p_pool->free_capacity);
			rc = NVM_ERR_BADSIZE;
		}
		else if (p_nvm_details->type == NAMESPACE_TYPE_APP_DIRECT)
		{
			rc = validate_app_direct_namespace_size_for_modification(p_pool, p_block_count,
				p_nvm_details, &nvm_caps, allow_adjustment);
		}
		else
		{
			COMMON_LOG_ERROR("The namespace type is not valid");
			rc = NVM_ERR_BADNAMESPACETYPE;
		}
	}
	else
	{
		COMMON_LOG_ERROR_F("nvm_get_nvm_capabilities failed with error code %d", rc);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}
/*
 * Helper function to validate modify namespace settings
 */
int validate_namespace_block_count(const NVM_UID namespace_uid,
		NVM_UINT64 *p_block_count, const NVM_BOOL allow_adjustment)
{
	// check if the namespace exists
	struct nvm_namespace_details nvm_details;
	memset(&nvm_details, 0, sizeof (nvm_details));
	int rc = get_namespace_details(namespace_uid, &nvm_details);
	if (rc == NVM_SUCCESS)
	{
		struct pool *p_pool = calloc(1, sizeof (struct pool));
		if (!p_pool)
		{
			rc = NVM_ERR_NOMEMORY;
		}
		else
		{
			rc = get_pool_from_namespace_details(&nvm_details, p_pool);
			if (rc == NVM_SUCCESS)
			{
				rc = validate_namespace_size_for_modification(p_pool, p_block_count,
					&nvm_details, allow_adjustment);
			}
			free(p_pool);

		}
	}
	else
	{
		COMMON_LOG_ERROR("Failed to retrieve namespace details.");
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

int dimms_are_locked(const struct namespace_details *details, NVM_BOOL *p_locked)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (details->type == NAMESPACE_TYPE_APP_DIRECT)
	{
		NVM_UINT32 interleaveset_id = details->creation_id.interleave_setid;
		NVM_UID pool_uid;
		s_strcpy(pool_uid, details->pool_uid, NVM_MAX_UID_LEN);
		struct interleave_set interleave;
		memset(&interleave, 0, sizeof (struct interleave_set));

		struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
		if (p_pool)
		{
			if ((rc = nvm_get_pool(pool_uid, p_pool)) == NVM_SUCCESS)
			{
				for (int i = 0; i < p_pool->ilset_count; i++)
				{
					if (p_pool->ilsets[i].driver_id == interleaveset_id)
					{
						memmove(&interleave, &(p_pool->ilsets[i]),
								sizeof (struct interleave_set));
						if (!interleave_set_has_locked_dimms(&interleave))
						{
							*p_locked = 0;
						}
						break;
					}
				}
			}
			free(p_pool);
		}
		else
		{
			rc = NVM_ERR_NOMEMORY;
		}
	}
	else
	{
		rc = NVM_ERR_BADNAMESPACETYPE;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Modify the namespace specified.
 */
int nvm_modify_namespace_name(const NVM_UID namespace_uid,
		const NVM_NAMESPACE_NAME name)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(rename_namespace)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Renaming a namespace is not supported.");
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (name == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, name is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else
	{
		struct namespace_details details;
		memset(&details, 0, sizeof (details));
		rc = nvm_get_namespace_details(namespace_uid, &details);

		if (rc == NVM_SUCCESS)
		{
			NVM_BOOL locked = 1;
			rc = dimms_are_locked(&details, &locked);

			if (rc == NVM_SUCCESS)
			{
				if (locked)
				{
					rc = NVM_ERR_BADSECURITYSTATE;
				}
				else
				{
					rc = modify_namespace_name(namespace_uid, name);
					if (rc == NVM_SUCCESS)
					{
						// the namespace context is no longer valid
						invalidate_namespaces();

						// Log an event indicating we successfully modified a namespace
						NVM_EVENT_ARG ns_uid_arg;
						uid_to_event_arg(namespace_uid, ns_uid_arg);
						NVM_EVENT_ARG ns_name_arg;
						s_strncpy(ns_name_arg, NVM_EVENT_ARG_LEN,
								name, NVM_NAMESPACE_NAME_LEN);
						log_mgmt_event(EVENT_SEVERITY_INFO,
								EVENT_CODE_MGMT_NAMESPACE_MODIFIED,
								namespace_uid,
								0, // no action required
								ns_name_arg, ns_uid_arg, NULL);
					}
					else
					{
						COMMON_LOG_ERROR("Could not modify namespace name");
					}
				}
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Modify the namespace specified.
 */
int nvm_modify_namespace_block_count(const NVM_UID namespace_uid,
		NVM_UINT64 block_count, NVM_BOOL allow_adjustment)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (block_count == 0)
	{
		COMMON_LOG_ERROR("Invalid block count to create a namespace.");
		rc = NVM_ERR_BADSIZE;
	}
	else
	{
		struct namespace_details details;
		memset(&details, 0, sizeof (details));
		rc = nvm_get_namespace_details(namespace_uid, &details);
		if (rc == NVM_SUCCESS)
		{
			if (details.block_count < block_count &&
					(rc = IS_NVM_FEATURE_LICENSED(grow_namespace)) != NVM_SUCCESS)
			{
				COMMON_LOG_ERROR("Increasing namespace size not supported.");
			}
			else if (details.block_count > block_count &&
					(rc = IS_NVM_FEATURE_LICENSED(shrink_namespace)) != NVM_SUCCESS)
			{
				COMMON_LOG_ERROR("Decreasing namespace size not supported.");
			}
			else
			{
				struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
				if ((p_pool) &&
						((rc = nvm_get_pool(details.pool_uid, p_pool)) == NVM_SUCCESS) &&
						((rc = translate_pool_health_to_nvm_error(p_pool)) == NVM_SUCCESS))
				{
					rc = validate_namespace_block_count(namespace_uid,
							&block_count, allow_adjustment);
					if (rc == NVM_SUCCESS)
					{
						NVM_BOOL locked = 1;
						rc = dimms_are_locked(&details, &locked);
						if (rc == NVM_SUCCESS && !locked)
						{
							rc = modify_namespace_block_count(namespace_uid, block_count);
							if (rc == NVM_SUCCESS)
							{
								// the namespace context is no longer valid
								invalidate_namespaces();

								// Log an event indicating we successfully modified a namespace
								NVM_EVENT_ARG ns_uid_arg;
								uid_to_event_arg(namespace_uid, ns_uid_arg);
								NVM_EVENT_ARG ns_name_arg;
								s_strncpy(ns_name_arg, NVM_EVENT_ARG_LEN,
										details.discovery.friendly_name,
										NVM_NAMESPACE_NAME_LEN);
								log_mgmt_event(EVENT_SEVERITY_INFO,
										EVENT_CODE_MGMT_NAMESPACE_MODIFIED,
										namespace_uid,
										0, // no action required
										ns_name_arg, ns_uid_arg, NULL);
							}
							else
							{
								COMMON_LOG_ERROR("Could not modify namespace block count");
							}
						}
						else if (rc == NVM_SUCCESS && locked)
						{
							rc = NVM_ERR_BADSECURITYSTATE;
						}
						else
						{
							COMMON_LOG_ERROR("Could not verify lock state of dimms.");
						}
					}
					else
					{
						COMMON_LOG_ERROR("Bad block count for nvm_modify_namespace");
					}
				}
				free(p_pool);
			}
		}
		else
		{
			COMMON_LOG_ERROR("Could not retrieve namespace details.");
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Modify the namespace specified.
 */
int nvm_modify_namespace_enabled(const NVM_UID namespace_uid,
		const enum namespace_enable_state enabled)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (enabled == NAMESPACE_ENABLE_STATE_DISABLED ||
			enabled == NAMESPACE_ENABLE_STATE_ENABLED)
	{
		if (enabled  == NAMESPACE_ENABLE_STATE_DISABLED &&
			(rc = IS_NVM_FEATURE_LICENSED(disable_namespace)) != NVM_SUCCESS)
		{
			COMMON_LOG_ERROR("Disabling a namespace is not supported.");
		}
		else if (enabled  == NAMESPACE_ENABLE_STATE_ENABLED &&
				(rc = IS_NVM_FEATURE_LICENSED(enable_namespace)) != NVM_SUCCESS)
		{
			COMMON_LOG_ERROR("Enabling a namespace is not supported.");
		}
		else
		{
			struct namespace_details details;
			memset(&details, 0, sizeof (details));
			rc = nvm_get_namespace_details(namespace_uid, &details);
			if (rc == NVM_SUCCESS && details.enabled != enabled)
			{
				NVM_BOOL locked = 1;
				rc = dimms_are_locked(&details, &locked);

				if (rc == NVM_SUCCESS)
				{
					if (locked)
					{
						rc = NVM_ERR_BADSECURITYSTATE;
					}
					else
					{
						rc = modify_namespace_enabled(namespace_uid, enabled);
						if (rc == NVM_SUCCESS)
						{
							// the namespace context is no longer valid
							invalidate_namespaces();

							// Log an event indicating we successfully modified a namespace
							NVM_EVENT_ARG ns_uid_arg;
							uid_to_event_arg(namespace_uid, ns_uid_arg);
							NVM_EVENT_ARG ns_name_arg;
							s_strncpy(ns_name_arg, NVM_EVENT_ARG_LEN,
									details.discovery.friendly_name, NVM_NAMESPACE_NAME_LEN);
							log_mgmt_event(EVENT_SEVERITY_INFO,
									EVENT_CODE_MGMT_NAMESPACE_MODIFIED,
									namespace_uid,
									0, // no action required
									ns_name_arg, ns_uid_arg, NULL);
						}
						else
						{
							COMMON_LOG_ERROR("Could not modify namespace block count");
						}
					}
				}
			}
		}
	}
	else
	{
		COMMON_LOG_ERROR("Unrecognized enable state.");
		rc = NVM_ERR_BADNAMESPACEENABLESTATE;
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}
/*
 * Delete an existing namespace.
 */
int nvm_delete_namespace(const NVM_UID namespace_uid)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = IS_NVM_FEATURE_LICENSED(delete_namespace)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Deleting a namespace is not supported.");
	}
	else if (namespace_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, namespace_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else
	{
		struct namespace_details details;
		memset(&details, 0, sizeof (details));
		rc = nvm_get_namespace_details(namespace_uid, &details);
		if (rc == NVM_SUCCESS)
		{
			rc = delete_namespace(namespace_uid);
			if (rc == NVM_SUCCESS)
			{
				// the namespace context is no longer valid
				invalidate_namespaces();

				// Log an event indicating we successfully deleted a namespace
				NVM_EVENT_ARG ns_uid_arg;
				uid_to_event_arg(namespace_uid, ns_uid_arg);
				NVM_EVENT_ARG ns_name_arg;
				s_strncpy(ns_name_arg, NVM_EVENT_ARG_LEN,
						details.discovery.friendly_name, NVM_NAMESPACE_NAME_LEN);
				log_mgmt_event(EVENT_SEVERITY_INFO,
						EVENT_CODE_MGMT_NAMESPACE_DELETED,
						namespace_uid,
						0, // no action required
						ns_name_arg, ns_uid_arg, NULL);

				// clear any action required events for the deleted namespace
				struct event_filter filter;
				memset(&filter, 0, sizeof (filter));
				filter.filter_mask = NVM_FILTER_ON_UID | NVM_FILTER_ON_AR;
				memmove(filter.uid, namespace_uid, NVM_MAX_UID_LEN);
				filter.action_required = 1;
				acknowledge_events(&filter);
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Return the size of the largest App Direct namespace that can be created.
 * Expect pool_uid and p_size to not be NULL
 */
void get_largest_app_direct_namespace(const struct pool *p_pool, NVM_UINT64 *p_size,
		const struct nvm_namespace_details *p_namespaces, int ns_count, const NVM_UINT8 ways)
{
	COMMON_LOG_ENTRY();

	*p_size = 0;

	// find the largest available space on any interleave sets in the pool
	if (ways)
	{
		for (int i = 0; i < p_pool->ilset_count; i++)
		{
			if (p_pool->ilsets[i].settings.ways == ways && p_pool->ilsets[i].available_size > *p_size)
			{
				*p_size = p_pool->ilsets[i].available_size;
			}
		}
	}
	else // No PM type preference mentioned for available interleave sets
	{
		for (int i = 0; i < p_pool->ilset_count; i++)
		{
			if (p_pool->ilsets[i].available_size > *p_size)
			{
				*p_size = p_pool->ilsets[i].available_size;
			}
		}
	}

	COMMON_LOG_EXIT();
}

/*
 * Return the size of the smallest App Direct namespace that can be created.
 */
void get_smallest_app_direct_namespace(const struct pool *p_pool,
		const NVM_UINT64 interleave_alignment_size, NVM_UINT64 *p_size)
{
	COMMON_LOG_ENTRY();

	// find the smallest interleave way
	int min_way = 0;
	if (p_pool->ilset_count)
	{
		min_way = p_pool->dimm_count;
		for (int i = 0; i < p_pool->ilset_count; i++)
		{
			if (p_pool->ilsets[i].dimm_count < min_way)
			{
				min_way = p_pool->ilsets[i].dimm_count;
			}
		}
	}

	// smallest = interleave_alignment_size * way
	*p_size = get_minimum_ns_size(min_way, min_way * interleave_alignment_size);
	COMMON_LOG_EXIT();
}

int get_pool_supported_size_ranges(const struct pool *p_pool,
		const struct nvm_capabilities *p_caps,
		struct possible_namespace_ranges *p_range,
		const struct nvm_namespace_details *p_namespaces,
		int ns_count, const NVM_UINT8 ways)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (p_pool->free_capacity == 0)
	{
		p_range->largest_possible_app_direct_ns = 0;
		p_range->smallest_possible_app_direct_ns = 0;
	}
	else if (p_caps->nvm_features.app_direct_mode)
	{
		get_largest_app_direct_namespace(
				p_pool, &(p_range->largest_possible_app_direct_ns),
				p_namespaces, ns_count, ways);
		if (p_range->largest_possible_app_direct_ns > 0)
		{
			NVM_UINT64 interleave_alignment_size =
					(NVM_UINT64)pow(2,
					p_caps->platform_capabilities.app_direct_mode.interleave_alignment_size);
			p_range->app_direct_increment = interleave_alignment_size;
			get_smallest_app_direct_namespace(p_pool, interleave_alignment_size,
					&(p_range->smallest_possible_app_direct_ns));
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}


/*
 * Given a namespace setting instance, return the largest and smallest
 * namespaces that can be created with that setting, along with the increment
 * that must be used to determine the valid capacities between the smallest and
 * largest namespaces.
 */
int nvm_get_available_persistent_size_range(const NVM_UID pool_uid,
		struct possible_namespace_ranges *p_range, const NVM_UINT8 ways)
{
	COMMON_LOG_ENTRY();
	int rc = NVM_SUCCESS;

	if (pool_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, pool_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (p_range == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, p_range is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else
	{
		memset(p_range, 0, sizeof (struct possible_namespace_ranges));

		// retrieve the capabilities
		struct nvm_capabilities nvm_caps;
		memset(&nvm_caps, 0, sizeof (nvm_caps));
		if ((rc = nvm_get_nvm_capabilities(&nvm_caps)) == NVM_SUCCESS)
		{
			// retrieve the pool
			struct pool *p_pool = (struct pool *)calloc(1, sizeof (struct pool));
			if (!p_pool)
			{
				COMMON_LOG_ERROR("Failed to allocate memory for the pool\n");
				rc = NVM_ERR_NOMEMORY;
			}
			else
			{
				if ((rc = nvm_get_pool(pool_uid, p_pool)) == NVM_SUCCESS)
				{
					struct nvm_namespace_details *p_namespaces = NULL;
					int ns_count = get_nvm_namespaces_details_alloc(&p_namespaces);
					if (ns_count >= 0)
					{
						rc = get_pool_supported_size_ranges(p_pool,
							&nvm_caps, p_range, p_namespaces, ns_count, ways);
						free(p_namespaces);
					}
				}
				free(p_pool);
			}
		}
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}

/*
 * Determine if any existing namespaces of the specified type
 * utilize capacity from the specified device.
 */
int nvm_get_device_namespace_count(const NVM_UID device_uid,
		const enum namespace_type type)
{
	COMMON_LOG_ENTRY();
	int rc = 0; // return namespace count
	struct nvm_capabilities capabilities;
	struct device_discovery discovery;

	if (check_caller_permissions() != NVM_SUCCESS)
	{
		rc = NVM_ERR_INVALIDPERMISSIONS;
	}
	else if (!is_supported_driver_available())
	{
		rc = NVM_ERR_BADDRIVER;
	}
	else if ((rc = nvm_get_nvm_capabilities(&capabilities)) != NVM_SUCCESS)
	{
		COMMON_LOG_ERROR("Failed to retrieve the system capabilities.");
	}
	else if (!capabilities.nvm_features.get_namespaces) // also confirms pass through
	{
		COMMON_LOG_ERROR("Retrieving namespaces is not supported.");
		rc = NVM_ERR_NOTSUPPORTED;
	}
	else if (device_uid == NULL)
	{
		COMMON_LOG_ERROR("Invalid parameter, device_uid is NULL");
		rc = NVM_ERR_INVALIDPARAMETER;
	}
	else if ((rc = exists_and_manageable(device_uid, &discovery, 1)) == NVM_SUCCESS)
	{
		rc = dimm_has_namespaces_of_type(discovery.device_handle, type);
	}

	COMMON_LOG_EXIT_RETURN_I(rc);
	return rc;
}
