/* Copyright (c) 2019-2020, Arm Limited and Contributors
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

#include "pipeline_layout.h"

#include "descriptor_set_layout.h"
#include "device.h"
#include "pipeline.h"
#include "shader_module.h"

namespace vkb
{
PipelineLayout::PipelineLayout(Device &device, const std::vector<ShaderModule *> &shader_modules, bool use_dynamic_resources) :
    device{device},
    shader_program{shader_modules}
{
	// Create a descriptor set layout for each shader set in the shader program
	for (auto &shader_set_it : shader_program.get_shader_sets())
	{
		descriptor_set_layouts.emplace(shader_set_it.first, &device.get_resource_cache().request_descriptor_set_layout(shader_set_it.second, use_dynamic_resources));
	}

	// Collect all the descriptor set layout handles
	std::vector<VkDescriptorSetLayout> descriptor_set_layout_handles(descriptor_set_layouts.size());
	std::transform(descriptor_set_layouts.begin(), descriptor_set_layouts.end(), descriptor_set_layout_handles.begin(),
	               [](auto &descriptor_set_layout_it) { return descriptor_set_layout_it.second->get_handle(); });

	// Collect all the push constant shader resources
	std::vector<VkPushConstantRange> push_constant_ranges;
	for (auto &push_constant_resource : shader_program.get_resources(ShaderResourceType::PushConstant))
	{
		push_constant_ranges.push_back({push_constant_resource.stages, push_constant_resource.offset, push_constant_resource.size});
	}

	VkPipelineLayoutCreateInfo create_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};

	create_info.setLayoutCount         = to_u32(descriptor_set_layout_handles.size());
	create_info.pSetLayouts            = descriptor_set_layout_handles.data();
	create_info.pushConstantRangeCount = to_u32(push_constant_ranges.size());
	create_info.pPushConstantRanges    = push_constant_ranges.data();

	// Create the Vulkan pipeline layout handle
	auto result = vkCreatePipelineLayout(device.get_handle(), &create_info, nullptr, &handle);

	if (result != VK_SUCCESS)
	{
		throw VulkanException{result, "Cannot create PipelineLayout"};
	}
}

PipelineLayout::PipelineLayout(PipelineLayout &&other) :
    device{other.device},
    handle{other.handle},
    shader_program{std::move(other.shader_program)},
    descriptor_set_layouts{std::move(other.descriptor_set_layouts)}
{
	other.handle = VK_NULL_HANDLE;
}

PipelineLayout::~PipelineLayout()
{
	// Destroy pipeline layout
	if (handle != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device.get_handle(), handle, nullptr);
	}
}

VkPipelineLayout PipelineLayout::get_handle() const
{
	return handle;
}

const ShaderProgram &PipelineLayout::get_shader_program() const
{
	return shader_program;
}

bool PipelineLayout::has_descriptor_set_layout(uint32_t set_index) const
{
	return set_index < descriptor_set_layouts.size();
}

DescriptorSetLayout &PipelineLayout::get_descriptor_set_layout(uint32_t set_index) const
{
	return *descriptor_set_layouts.at(set_index);
}

VkShaderStageFlags PipelineLayout::get_push_constant_range_stage(uint32_t offset, uint32_t size) const
{
	VkShaderStageFlags stages = 0;

	for (auto &push_constant_resource : shader_program.get_resources(ShaderResourceType::PushConstant))
	{
		if (offset >= push_constant_resource.offset && offset + size <= push_constant_resource.offset + push_constant_resource.size)
		{
			stages |= push_constant_resource.stages;
		}
	}
	return stages;
}
}        // namespace vkb
