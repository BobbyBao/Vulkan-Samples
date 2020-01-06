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

#pragma once

#include <string>
#include <unordered_map>

#include <vulkan/vulkan.hpp>

namespace vkb
{
enum class ShaderResourceType;

namespace sg
{
enum class AlphaMode;
}

namespace utils
{
extern std::string vk_result_to_string(vk::Result result);
extern std::string vk_result_to_string(VkResult result);

extern std::string to_string(vk::Format format);

extern std::string to_string(vk::SampleCountFlags flags);

extern std::string to_string_shader_stage_flags(vk::ShaderStageFlags flags);

extern std::string to_string(vk::PhysicalDeviceType type);

extern std::string to_string(vk::SurfaceTransformFlagsKHR flags);

extern std::string to_string(vk::PresentModeKHR mode);

extern std::string to_string_vk_image_usage_flags(vk::ImageUsageFlags flags);

extern std::string to_string_vk_image_aspect_flags(vk::ImageAspectFlags flags);

extern std::string to_string(vk::ImageTiling tiling);

extern std::string to_string(vk::ImageType type);

extern std::string to_string(vk::Extent2D format);

extern std::string to_string(vk::BlendFactor blend);

extern std::string to_string(vk::VertexInputRate rate);

extern std::string to_string_vk_bool(vk::Bool32 state);

extern std::string to_string(vk::PrimitiveTopology topology);

extern std::string to_string(vk::FrontFace face);

extern std::string to_string(vk::PolygonMode mode);

extern std::string to_string_vk_cull_mode_flags(vk::CullModeFlags flags);

extern std::string to_string(vk::CompareOp operation);

extern std::string to_string(vk::StencilOp operation);

extern std::string to_string(vk::LogicOp operation);

extern std::string to_string(vk::BlendOp operation);

extern std::string to_string_vk_color_component_flags(vk::ColorComponentFlags operation);

extern std::string to_string(sg::AlphaMode mode);

extern std::string to_string(bool flag);

extern std::string to_string(ShaderResourceType type);

extern std::unordered_map<vk::Format, std::string> vk_format_strings;

}        // namespace utils
}        // namespace vkb