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

#pragma once

#include <vector>

#include "common/vk_common.h"
#include "core/pipeline_layout.h"
#include "core/render_pass.h"

namespace vkb
{
struct VertexInputState
{
	std::vector<vk::VertexInputBindingDescription> bindings;

	std::vector<vk::VertexInputAttributeDescription> attributes;
};

struct InputAssemblyState
{
	vk::PrimitiveTopology topology{vk::PrimitiveTopology::eTriangleList};

	vk::Bool32 primitive_restart_enable{VK_FALSE};
};

struct RasterizationState
{
	vk::Bool32 depth_clamp_enable{VK_FALSE};

	vk::Bool32 rasterizer_discard_enable{VK_FALSE};

	vk::PolygonMode polygon_mode{vk::PolygonMode::eFill};

	vk::CullModeFlags cull_mode{vk::CullModeFlagBits::eBack};

	vk::FrontFace front_face{vk::FrontFace::eCounterClockwise};

	vk::Bool32 depth_bias_enable{VK_FALSE};
};

struct ViewportState
{
	uint32_t viewport_count{1};

	uint32_t scissor_count{1};
};

struct MultisampleState
{
	vk::SampleCountFlagBits rasterization_samples{vk::SampleCountFlagBits::e1};

	vk::Bool32 sample_shading_enable{VK_FALSE};

	float min_sample_shading{0.0f};

	vk::SampleMask sample_mask{0};

	vk::Bool32 alpha_to_coverage_enable{VK_FALSE};

	vk::Bool32 alpha_to_one_enable{VK_FALSE};
};

struct StencilOpState
{
	vk::StencilOp fail_op{vk::StencilOp::eReplace};

	vk::StencilOp pass_op{vk::StencilOp::eReplace};

	vk::StencilOp depth_fail_op{vk::StencilOp::eReplace};

	vk::CompareOp compare_op{vk::CompareOp::eNever};
};

struct DepthStencilState
{
	vk::Bool32 depth_test_enable{VK_TRUE};

	vk::Bool32 depth_write_enable{VK_TRUE};

	// Note: Using Reversed depth-buffer for increased precision, so Greater depth values are kept
	vk::CompareOp depth_compare_op{vk::CompareOp::eGreater};

	vk::Bool32 depth_bounds_test_enable{VK_FALSE};

	vk::Bool32 stencil_test_enable{VK_FALSE};

	StencilOpState front{};

	StencilOpState back{};
};

struct ColorBlendAttachmentState
{
	vk::Bool32 blend_enable{VK_FALSE};

	vk::BlendFactor src_color_blend_factor{vk::BlendFactor::eOne};

	vk::BlendFactor dst_color_blend_factor{vk::BlendFactor::eZero};

	vk::BlendOp color_blend_op{vk::BlendOp::eAdd};

	vk::BlendFactor src_alpha_blend_factor{vk::BlendFactor::eOne};

	vk::BlendFactor dst_alpha_blend_factor{vk::BlendFactor::eZero};

	vk::BlendOp alpha_blend_op{vk::BlendOp::eAdd};

	vk::ColorComponentFlags color_write_mask{vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
};

struct ColorBlendState
{
	vk::Bool32 logic_op_enable{VK_FALSE};

	vk::LogicOp logic_op{vk::LogicOp::eClear};

	std::vector<ColorBlendAttachmentState> attachments;
};

/// Helper class to create specialization constants for a Vulkan pipeline. The state tracks a pipeline globally, and not per shader. Two shaders using the same constant_id will have the same data.
class SpecializationConstantState
{
  public:
	void reset();

	bool is_dirty() const;

	void clear_dirty();

	template <class T>
	void set_constant(uint32_t constant_id, const T &data);

	void set_constant(uint32_t constant_id, const std::vector<uint8_t> &data);

	void set_specialization_constant_state(const std::map<uint32_t, std::vector<uint8_t>> &state);

	const std::map<uint32_t, std::vector<uint8_t>> &get_specialization_constant_state() const;

  private:
	bool dirty{false};
	// Map tracking state of the Specialization Constants
	std::map<uint32_t, std::vector<uint8_t>> specialization_constant_state;
};

template <class T>
inline void SpecializationConstantState::set_constant(std::uint32_t constant_id, const T &data)
{
	std::uint32_t value = static_cast<std::uint32_t>(data);

	set_constant(constant_id,
	             {reinterpret_cast<const uint8_t *>(&value),
	              reinterpret_cast<const uint8_t *>(&value) + sizeof(T)});
}

template <>
inline void SpecializationConstantState::set_constant<bool>(std::uint32_t constant_id, const bool &data_)
{
	std::uint32_t value = static_cast<std::uint32_t>(data_);

	set_constant(constant_id,
	             {reinterpret_cast<const uint8_t *>(&value),
	              reinterpret_cast<const uint8_t *>(&value) + sizeof(std::uint32_t)});
}

class PipelineState
{
  public:
	void reset();

	void set_pipeline_layout(PipelineLayout &pipeline_layout);

	void set_render_pass(const RenderPass &render_pass);

	void set_specialization_constant(uint32_t constant_id, const std::vector<uint8_t> &data);

	void set_vertex_input_state(const VertexInputState &vertex_input_sate);

	void set_input_assembly_state(const InputAssemblyState &input_assembly_state);

	void set_rasterization_state(const RasterizationState &rasterization_state);

	void set_viewport_state(const ViewportState &viewport_state);

	void set_multisample_state(const MultisampleState &multisample_state);

	void set_depth_stencil_state(const DepthStencilState &depth_stencil_state);

	void set_color_blend_state(const ColorBlendState &color_blend_state);

	void set_subpass_index(uint32_t subpass_index);

	const PipelineLayout &get_pipeline_layout() const;

	const RenderPass *get_render_pass() const;

	const SpecializationConstantState &get_specialization_constant_state() const;

	const VertexInputState &get_vertex_input_state() const;

	const InputAssemblyState &get_input_assembly_state() const;

	const RasterizationState &get_rasterization_state() const;

	const ViewportState &get_viewport_state() const;

	const MultisampleState &get_multisample_state() const;

	const DepthStencilState &get_depth_stencil_state() const;

	const ColorBlendState &get_color_blend_state() const;

	uint32_t get_subpass_index() const;

	bool is_dirty() const;

	void clear_dirty();

  private:
	bool dirty{false};

	PipelineLayout *pipeline_layout{nullptr};

	const RenderPass *render_pass{nullptr};

	SpecializationConstantState specialization_constant_state{};

	VertexInputState vertex_input_sate{};

	InputAssemblyState input_assembly_state{};

	RasterizationState rasterization_state{};

	ViewportState viewport_state{};

	MultisampleState multisample_state{};

	DepthStencilState depth_stencil_state{};

	ColorBlendState color_blend_state{};

	uint32_t subpass_index{0U};
};
}        // namespace vkb
