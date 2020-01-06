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

#include "pipeline.h"

#include "device.h"
#include "pipeline_layout.h"
#include "shader_module.h"

namespace vkb
{
Pipeline::Pipeline(Device &device) :
    device{device}
{}

Pipeline::Pipeline(Pipeline &&other) :
    vk::Pipeline{other},
    device{other.device},
    state{other.state}
{
	static_cast<vk::Pipeline &>(other) = nullptr;
}

Pipeline::~Pipeline()
{
	// Destroy pipeline
	if (operator bool())
	{
		device.get_handle().destroy(*this);
	}
}

vk::Pipeline Pipeline::get_handle() const
{
	return static_cast<const vk::Pipeline &>(*this);
}

const PipelineState &Pipeline::get_state() const
{
	return state;
}

ComputePipeline::ComputePipeline(Device &          device,
                                 vk::PipelineCache pipeline_cache,
                                 PipelineState &   pipeline_state) :
    Pipeline{device}
{
	const ShaderModule *shader_module = pipeline_state.get_pipeline_layout().get_shader_program().get_shader_modules().front();

	if (shader_module->get_stage() != vk::ShaderStageFlagBits::eCompute)
	{
		vk::throwResultException(vk::Result::eErrorInitializationFailed, "Shader module stage is not compute");
	}

	vk::PipelineShaderStageCreateInfo stage;

	stage.stage = shader_module->get_stage();
	stage.pName = shader_module->get_entry_point().c_str();

	// Create the Vulkan handle
	vk::ShaderModuleCreateInfo vk_create_info;

	vk_create_info.codeSize = shader_module->get_binary().size() * sizeof(uint32_t);
	vk_create_info.pCode    = shader_module->get_binary().data();

	stage.module = device.get_handle().createShaderModule(vk_create_info);

	// Create specialization info from tracked state.
	std::vector<uint8_t>                    data{};
	std::vector<vk::SpecializationMapEntry> map_entries{};

	const auto specialization_constant_state = pipeline_state.get_specialization_constant_state().get_specialization_constant_state();

	for (const auto specialization_constant : specialization_constant_state)
	{
		map_entries.push_back({specialization_constant.first, to_u32(data.size()), specialization_constant.second.size()});
		data.insert(data.end(), specialization_constant.second.begin(), specialization_constant.second.end());
	}

	vk::SpecializationInfo specialization_info;
	specialization_info.mapEntryCount = to_u32(map_entries.size());
	specialization_info.pMapEntries   = map_entries.data();
	specialization_info.dataSize      = data.size();
	specialization_info.pData         = data.data();

	stage.pSpecializationInfo = &specialization_info;

	vk::ComputePipelineCreateInfo create_info;

	create_info.layout = pipeline_state.get_pipeline_layout().get_handle();
	create_info.stage  = stage;

	static_cast<vk::Pipeline &>(*this) = device.get_handle().createComputePipeline(pipeline_cache, create_info);

	device.get_handle().destroy(stage.module);
}

GraphicsPipeline::GraphicsPipeline(Device &          device,
                                   vk::PipelineCache pipeline_cache,
                                   PipelineState &   pipeline_state) :
    Pipeline{device}
{
	std::vector<vk::ShaderModule> shader_modules;

	std::vector<vk::PipelineShaderStageCreateInfo> stage_create_infos;

	// Create specialization info from tracked state. This is shared by all shaders.
	std::vector<uint8_t>                    data{};
	std::vector<vk::SpecializationMapEntry> map_entries{};

	const auto specialization_constant_state = pipeline_state.get_specialization_constant_state().get_specialization_constant_state();

	for (const auto specialization_constant : specialization_constant_state)
	{
		map_entries.push_back({specialization_constant.first, to_u32(data.size()), specialization_constant.second.size()});
		data.insert(data.end(), specialization_constant.second.begin(), specialization_constant.second.end());
	}

	vk::SpecializationInfo specialization_info;
	specialization_info.mapEntryCount = to_u32(map_entries.size());
	specialization_info.pMapEntries   = map_entries.data();
	specialization_info.dataSize      = data.size();
	specialization_info.pData         = data.data();

	for (const ShaderModule *shader_module : pipeline_state.get_pipeline_layout().get_shader_program().get_shader_modules())
	{
		vk::PipelineShaderStageCreateInfo stage_create_info;

		stage_create_info.stage = shader_module->get_stage();
		stage_create_info.pName = shader_module->get_entry_point().c_str();

		// Create the Vulkan handle
		vk::ShaderModuleCreateInfo vk_create_info;

		vk_create_info.codeSize = shader_module->get_binary().size() * sizeof(uint32_t);
		vk_create_info.pCode    = shader_module->get_binary().data();

		stage_create_info.module = device.get_handle().createShaderModule(vk_create_info);

		stage_create_info.pSpecializationInfo = &specialization_info;

		stage_create_infos.push_back(stage_create_info);
		shader_modules.push_back(stage_create_info.module);
	}

	vk::GraphicsPipelineCreateInfo create_info;

	create_info.stageCount = to_u32(stage_create_infos.size());
	create_info.pStages    = stage_create_infos.data();

	vk::PipelineVertexInputStateCreateInfo vertex_input_state;

	vertex_input_state.pVertexAttributeDescriptions    = pipeline_state.get_vertex_input_state().attributes.data();
	vertex_input_state.vertexAttributeDescriptionCount = to_u32(pipeline_state.get_vertex_input_state().attributes.size());

	vertex_input_state.pVertexBindingDescriptions    = pipeline_state.get_vertex_input_state().bindings.data();
	vertex_input_state.vertexBindingDescriptionCount = to_u32(pipeline_state.get_vertex_input_state().bindings.size());

	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state;

	input_assembly_state.topology               = pipeline_state.get_input_assembly_state().topology;
	input_assembly_state.primitiveRestartEnable = pipeline_state.get_input_assembly_state().primitive_restart_enable;

	vk::PipelineViewportStateCreateInfo viewport_state;

	viewport_state.viewportCount = pipeline_state.get_viewport_state().viewport_count;
	viewport_state.scissorCount  = pipeline_state.get_viewport_state().scissor_count;

	vk::PipelineRasterizationStateCreateInfo rasterization_state;

	rasterization_state.depthClampEnable        = pipeline_state.get_rasterization_state().depth_clamp_enable;
	rasterization_state.rasterizerDiscardEnable = pipeline_state.get_rasterization_state().rasterizer_discard_enable;
	rasterization_state.polygonMode             = pipeline_state.get_rasterization_state().polygon_mode;
	rasterization_state.cullMode                = pipeline_state.get_rasterization_state().cull_mode;
	rasterization_state.frontFace               = pipeline_state.get_rasterization_state().front_face;
	rasterization_state.depthBiasEnable         = pipeline_state.get_rasterization_state().depth_bias_enable;
	rasterization_state.depthBiasClamp          = 1.0f;
	rasterization_state.depthBiasSlopeFactor    = 1.0f;
	rasterization_state.lineWidth               = 1.0f;

	vk::PipelineMultisampleStateCreateInfo multisample_state;

	multisample_state.sampleShadingEnable   = pipeline_state.get_multisample_state().sample_shading_enable;
	multisample_state.rasterizationSamples  = pipeline_state.get_multisample_state().rasterization_samples;
	multisample_state.minSampleShading      = pipeline_state.get_multisample_state().min_sample_shading;
	multisample_state.alphaToCoverageEnable = pipeline_state.get_multisample_state().alpha_to_coverage_enable;
	multisample_state.alphaToOneEnable      = pipeline_state.get_multisample_state().alpha_to_one_enable;

	if (pipeline_state.get_multisample_state().sample_mask)
	{
		multisample_state.pSampleMask = &pipeline_state.get_multisample_state().sample_mask;
	}

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state;

	depth_stencil_state.depthTestEnable       = pipeline_state.get_depth_stencil_state().depth_test_enable;
	depth_stencil_state.depthWriteEnable      = pipeline_state.get_depth_stencil_state().depth_write_enable;
	depth_stencil_state.depthCompareOp        = pipeline_state.get_depth_stencil_state().depth_compare_op;
	depth_stencil_state.depthBoundsTestEnable = pipeline_state.get_depth_stencil_state().depth_bounds_test_enable;
	depth_stencil_state.stencilTestEnable     = pipeline_state.get_depth_stencil_state().stencil_test_enable;
	depth_stencil_state.front.failOp          = pipeline_state.get_depth_stencil_state().front.fail_op;
	depth_stencil_state.front.passOp          = pipeline_state.get_depth_stencil_state().front.pass_op;
	depth_stencil_state.front.depthFailOp     = pipeline_state.get_depth_stencil_state().front.depth_fail_op;
	depth_stencil_state.front.compareOp       = pipeline_state.get_depth_stencil_state().front.compare_op;
	depth_stencil_state.front.compareMask     = ~0U;
	depth_stencil_state.front.writeMask       = ~0U;
	depth_stencil_state.front.reference       = ~0U;
	depth_stencil_state.back.failOp           = pipeline_state.get_depth_stencil_state().back.fail_op;
	depth_stencil_state.back.passOp           = pipeline_state.get_depth_stencil_state().back.pass_op;
	depth_stencil_state.back.depthFailOp      = pipeline_state.get_depth_stencil_state().back.depth_fail_op;
	depth_stencil_state.back.compareOp        = pipeline_state.get_depth_stencil_state().back.compare_op;
	depth_stencil_state.back.compareMask      = ~0U;
	depth_stencil_state.back.writeMask        = ~0U;
	depth_stencil_state.back.reference        = ~0U;

	vk::PipelineColorBlendStateCreateInfo color_blend_state;

	color_blend_state.logicOpEnable     = pipeline_state.get_color_blend_state().logic_op_enable;
	color_blend_state.logicOp           = pipeline_state.get_color_blend_state().logic_op;
	color_blend_state.attachmentCount   = to_u32(pipeline_state.get_color_blend_state().attachments.size());
	color_blend_state.pAttachments      = reinterpret_cast<const vk::PipelineColorBlendAttachmentState *>(pipeline_state.get_color_blend_state().attachments.data());
	color_blend_state.blendConstants[0] = 1.0f;
	color_blend_state.blendConstants[1] = 1.0f;
	color_blend_state.blendConstants[2] = 1.0f;
	color_blend_state.blendConstants[3] = 1.0f;

	std::array<vk::DynamicState, 9> dynamic_states{
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor,
	    vk::DynamicState::eLineWidth,
	    vk::DynamicState::eDepthBias,
	    vk::DynamicState::eBlendConstants,
	    vk::DynamicState::eDepthBounds,
	    vk::DynamicState::eStencilCompareMask,
	    vk::DynamicState::eStencilWriteMask,
	    vk::DynamicState::eStencilReference,
	};

	vk::PipelineDynamicStateCreateInfo dynamic_state;

	dynamic_state.pDynamicStates    = dynamic_states.data();
	dynamic_state.dynamicStateCount = to_u32(dynamic_states.size());

	create_info.pVertexInputState   = &vertex_input_state;
	create_info.pInputAssemblyState = &input_assembly_state;
	create_info.pViewportState      = &viewport_state;
	create_info.pRasterizationState = &rasterization_state;
	create_info.pMultisampleState   = &multisample_state;
	create_info.pDepthStencilState  = &depth_stencil_state;
	create_info.pColorBlendState    = &color_blend_state;
	create_info.pDynamicState       = &dynamic_state;

	create_info.layout     = pipeline_state.get_pipeline_layout().get_handle();
	create_info.renderPass = pipeline_state.get_render_pass()->get_handle();
	create_info.subpass    = pipeline_state.get_subpass_index();

	static_cast<vk::Pipeline &>(*this) = device.get_handle().createGraphicsPipeline(pipeline_cache, create_info);

	for (auto shader_module : shader_modules)
	{
		device.get_handle().destroy(shader_module);
	}

	state = pipeline_state;
}
}        // namespace vkb
