/* Copyright (c) 2019, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "descriptor_pool.h"

#include "common/error.h"
#include "descriptor_set_layout.h"
#include "device.h"

namespace vkb
{
DescriptorPool::DescriptorPool(Device &                   device,
                               const DescriptorSetLayout &descriptor_set_layout,
                               uint32_t                   pool_size) :
    device{device},
    descriptor_set_layout{&descriptor_set_layout}
{
	const auto &bindings = descriptor_set_layout.get_bindings();

	std::map<vk::DescriptorType, std::uint32_t> descriptor_type_counts;

	// Count each type of descriptor set
	for (auto &binding : bindings)
	{
		descriptor_type_counts[binding.descriptorType] += binding.descriptorCount;
	}

	// Allocate pool sizes array
	pool_sizes.resize(descriptor_type_counts.size());

	auto pool_size_it = pool_sizes.begin();

	// Fill pool size for each descriptor type count multiplied by the pool size
	for (auto &it : descriptor_type_counts)
	{
		pool_size_it->type = it.first;

		pool_size_it->descriptorCount = it.second * pool_size;

		++pool_size_it;
	}

	pool_max_sets = pool_size;
}

DescriptorPool::~DescriptorPool()
{
	// Destroy all descriptor pools
	for (auto pool : pools)
	{
		device.get_handle().destroy(pool);
	}
}

void DescriptorPool::reset()
{
	// Reset all descriptor pools
	for (auto pool : pools)
	{
		device.get_handle().resetDescriptorPool(pool, {});
	}

	// Clear internal tracking of descriptor set allocations
	std::fill(pool_sets_count.begin(), pool_sets_count.end(), 0);
	set_pool_mapping.clear();

	// Reset the pool index from which descriptor sets are allocated
	pool_index = 0;
}

const DescriptorSetLayout &DescriptorPool::get_descriptor_set_layout() const
{
	assert(descriptor_set_layout && "Descriptor set layout is invalid");
	return *descriptor_set_layout;
}

void DescriptorPool::set_descriptor_set_layout(const DescriptorSetLayout &set_layout)
{
	descriptor_set_layout = &set_layout;
}

vk::DescriptorSet DescriptorPool::allocate()
{
	pool_index = find_available_pool(pool_index);

	// Increment allocated set count for the current pool
	++pool_sets_count[pool_index];

	vk::DescriptorSetLayout set_layout = get_descriptor_set_layout().get_handle();

	vk::DescriptorSetAllocateInfo allocInfo;

	allocInfo.descriptorPool     = pools[pool_index];
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts        = &set_layout;

	// Allocate a new descriptor set from the current pool
	vk::DescriptorSet handle = device.get_handle().allocateDescriptorSets(allocInfo)[0];

	// Store mapping between the descriptor set and the pool
	set_pool_mapping.emplace(handle, pool_index);

	return handle;
}

vk::Result DescriptorPool::free(vk::DescriptorSet descriptor_set)
{
	// Get the pool index of the descriptor set
	auto it = set_pool_mapping.find(descriptor_set);

	if (it == set_pool_mapping.end())
	{
		return vk::Result::eIncomplete;
	}

	auto desc_pool_index = it->second;

	// Free descriptor set from the pool
	device.get_handle().freeDescriptorSets(pools[desc_pool_index], descriptor_set);

	// Remove descriptor set mapping to the pool
	set_pool_mapping.erase(it);

	// Decrement allocated set count for the pool
	--pool_sets_count[desc_pool_index];

	// Change the current pool index to use the available pool
	pool_index = desc_pool_index;

	return vk::Result::eSuccess;
}

std::uint32_t DescriptorPool::find_available_pool(std::uint32_t search_index)
{
	// Create a new pool
	if (pools.size() <= search_index)
	{
		vk::DescriptorPoolCreateInfo create_info;

		// We do not set FREE_DESCRIPTOR_SET_BIT as we do not need to free individual descriptor sets
		create_info.poolSizeCount = to_u32(pool_sizes.size());
		create_info.pPoolSizes    = pool_sizes.data();
		create_info.maxSets       = pool_max_sets;

		vk::DescriptorPool handle;

		// Create the Vulkan descriptor pool
		// Store internally the Vulkan handle
		pools.push_back(device.get_handle().createDescriptorPool(create_info));

		// Add set count for the descriptor pool
		pool_sets_count.push_back(0);

		return search_index;
	}
	else if (pool_sets_count[search_index] < pool_max_sets)
	{
		return search_index;
	}

	// Increment pool index
	return find_available_pool(++search_index);
}
}        // namespace vkb
