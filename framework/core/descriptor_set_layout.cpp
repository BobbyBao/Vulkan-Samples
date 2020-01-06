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

#include "descriptor_set_layout.h"

#include "device.h"
#include "shader_module.h"

namespace vkb
{
namespace
{
inline vk::DescriptorType find_descriptor_type(ShaderResourceType resource_type, bool dynamic)
{
	switch (resource_type)
	{
		case ShaderResourceType::InputAttachment:
			return vk::DescriptorType::eInputAttachment;
			break;
		case ShaderResourceType::Image:
			return vk::DescriptorType::eSampledImage;
			break;
		case ShaderResourceType::ImageSampler:
			return vk::DescriptorType::eCombinedImageSampler;
			break;
		case ShaderResourceType::ImageStorage:
			return vk::DescriptorType::eStorageImage;
			break;
		case ShaderResourceType::Sampler:
			return vk::DescriptorType::eSampler;
			break;
		case ShaderResourceType::BufferUniform:
			if (dynamic)
			{
				return vk::DescriptorType::eUniformBufferDynamic;
			}
			else
			{
				return vk::DescriptorType::eUniformBuffer;
			}
			break;
		case ShaderResourceType::BufferStorage:
			if (dynamic)
			{
				return vk::DescriptorType::eStorageBufferDynamic;
			}
			else
			{
				return vk::DescriptorType::eStorageBuffer;
			}
			break;
		default:
			throw std::runtime_error("No conversion possible for the shader resource type.");
			break;
	}
}
}        // namespace

DescriptorSetLayout::DescriptorSetLayout(Device &device, const std::vector<ShaderResource> &resource_set, bool use_dynamic_resources) :
    device{device}
{
	for (auto &resource : resource_set)
	{
		// Skip shader resources whitout a binding point
		if (resource.type == ShaderResourceType::Input ||
		    resource.type == ShaderResourceType::Output ||
		    resource.type == ShaderResourceType::PushConstant ||
		    resource.type == ShaderResourceType::SpecializationConstant)
		{
			continue;
		}

		// Convert from ShaderResourceType to VkDescriptorType.
		auto descriptor_type = find_descriptor_type(resource.type, use_dynamic_resources);

		// Convert ShaderResource to vk::DescriptorSetLayoutBinding
		vk::DescriptorSetLayoutBinding layout_binding;

		layout_binding.binding         = resource.binding;
		layout_binding.descriptorCount = resource.array_size;
		layout_binding.descriptorType  = descriptor_type;
		layout_binding.stageFlags      = static_cast<vk::ShaderStageFlags>(resource.stages);

		bindings.push_back(layout_binding);

		// Store mapping between binding and the binding point
		bindings_lookup.emplace(resource.binding, layout_binding);

		resources_lookup.emplace(resource.name, resource.binding);
	}

	vk::DescriptorSetLayoutCreateInfo create_info;

	create_info.bindingCount = to_u32(bindings.size());
	create_info.pBindings    = bindings.data();

	// Create the Vulkan descriptor set layout handle
	static_cast<vk::DescriptorSetLayout &>(*this) = device.get_handle().createDescriptorSetLayout(create_info);
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout &&other) :
    vk::DescriptorSetLayout{other},
    device{other.device},
    bindings{std::move(other.bindings)},
    bindings_lookup{std::move(other.bindings_lookup)},
    resources_lookup{std::move(other.resources_lookup)}
{
	static_cast<vk::DescriptorSetLayout &&>(other) = nullptr;
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	// Destroy descriptor set layout
	if (operator bool())
	{
		device.get_handle().destroy(*this);
	}
}

const vk::DescriptorSetLayout &DescriptorSetLayout::get_handle() const
{
	return static_cast<const vk::DescriptorSetLayout &>(*this);
}

const std::vector<vk::DescriptorSetLayoutBinding> &DescriptorSetLayout::get_bindings() const
{
	return bindings;
}

std::unique_ptr<vk::DescriptorSetLayoutBinding> DescriptorSetLayout::get_layout_binding(uint32_t binding_index) const
{
	auto it = bindings_lookup.find(binding_index);

	if (it == bindings_lookup.end())
	{
		return nullptr;
	}

	return std::make_unique<vk::DescriptorSetLayoutBinding>(it->second);
}

std::unique_ptr<vk::DescriptorSetLayoutBinding> DescriptorSetLayout::get_layout_binding(const std::string &name) const
{
	auto it = resources_lookup.find(name);

	if (it == resources_lookup.end())
	{
		return nullptr;
	}

	return get_layout_binding(it->second);
}
}        // namespace vkb
