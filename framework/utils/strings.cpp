/* Copyright (c) 2018-2019, Arm Limited and Contributors
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

#include "strings.h"

#include <spdlog/fmt/fmt.h>

#include "core/shader_module.h"
#include "scene_graph/components/material.h"

namespace vkb
{
namespace utils
{
std::string vk_result_to_string(vk::Result result)
{
	return vk::to_string(result);
}

std::string vk_result_to_string(VkResult result)
{
	return vk::to_string(static_cast<vk::Result>(result));
}
std::string to_string(vk::Format format)
{
	return vk::to_string(format);
}

std::string to_string(vk::SampleCountFlags flags)
{
	return vk::to_string(flags);
}

std::string to_string_shader_stage_flags(vk::ShaderStageFlags flags)
{
	return vk::to_string(flags);
}

std::string to_string(vk::PhysicalDeviceType type)
{
	return vk::to_string(type);
}

std::string to_string(vk::SurfaceTransformFlagsKHR flags)
{
	return vk::to_string(flags);
}

std::string to_string(vk::PresentModeKHR mode)
{
	return vk::to_string(mode);
}

std::string to_string_vk_image_usage_flags(vk::ImageUsageFlags flags)
{
	return vk::to_string(flags);
	std::string result{""};
}

std::string to_string_vk_image_aspect_flags(vk::ImageAspectFlags flags)
{
	return vk::to_string(flags);
}

std::string to_string(vk::ImageTiling tiling)
{
	return vk::to_string(tiling);
}

std::string to_string(vk::ImageType type)
{
	return vk::to_string(type);
}

std::string to_string(vk::Extent2D extent)
{
	return fmt::format("{}x{}", extent.width, extent.height);
}

std::string to_string(vk::BlendFactor blend)
{
	return vk::to_string(blend);
}

std::string to_string(vk::VertexInputRate rate)
{
	return vk::to_string(rate);
}

std::string to_string_vk_bool(vk::Bool32 state)
{
	if (state == VK_TRUE)
	{
		return "true";
	}

	return "false";
}

std::string to_string(vk::PrimitiveTopology topology)
{
	return vk::to_string(topology);
}

std::string to_string(vk::FrontFace face)
{
	return vk::to_string(face);
}

std::string to_string(vk::PolygonMode mode)
{
	return vk::to_string(mode);
}

std::string to_string_vk_cull_mode_flags(vk::CullModeFlags flags)
{
	return vk::to_string(flags);
}

std::string to_string(vk::CompareOp operation)
{
	return vk::to_string(operation);
}

std::string to_string(vk::StencilOp operation)
{
	return vk::to_string(operation);
}

std::string to_string(vk::LogicOp operation)
{
	return vk::to_string(operation);
}

std::string to_string(vk::BlendOp operation)
{
	return vk::to_string(operation);
}

std::string to_string_vk_color_component_flags(vk::ColorComponentFlags flags)
{
	return vk::to_string(flags);
}

std::string to_string(sg::AlphaMode mode)
{
	if (mode == sg::AlphaMode::Blend)
	{
		return "Blend";
	}
	else if (mode == sg::AlphaMode::Mask)
	{
		return "Mask";
	}
	else if (mode == sg::AlphaMode::Opaque)
	{
		return "Opaque";
	}
	return "Unkown";
}

std::string to_string(bool flag)
{
	if (flag == true)
	{
		return "true";
	}
	return "false";
}

std::string to_string(ShaderResourceType type)
{
	switch (type)
	{
		case ShaderResourceType::Input:
			return "Input";
		case ShaderResourceType::InputAttachment:
			return "InputAttachment";
		case ShaderResourceType::Output:
			return "Output";
		case ShaderResourceType::Image:
			return "Image";
		case ShaderResourceType::ImageSampler:
			return "ImageSampler";
		case ShaderResourceType::ImageStorage:
			return "ImageStorage";
		case ShaderResourceType::Sampler:
			return "Sampler";
		case ShaderResourceType::BufferUniform:
			return "BufferUniform";
		case ShaderResourceType::BufferStorage:
			return "BufferStorage";
		case ShaderResourceType::PushConstant:
			return "PushConstant";
		case ShaderResourceType::SpecializationConstant:
			return "SpecializationConstant";
		default:
			return "Unkown Type";
	}
}

}        // namespace utils
}        // namespace vkb
