/* Copyright (c) 2018-2020, Arm Limited and Contributors
 * Copyright (c) 2019, Sascha Willems
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

#include "gui.h"

#include <map>
#include <numeric>

#include "common/error.h"

VKBP_DISABLE_WARNINGS()
#include "common/glm_common.h"
#include <glm/gtc/matrix_transform.hpp>
VKBP_ENABLE_WARNINGS()

#include "buffer_pool.h"
#include "common/logging.h"
#include "common/utils.h"
#include "common/vk_common.h"
#include "common/vk_initializers.h"
#include "core/descriptor_set.h"
#include "core/descriptor_set_layout.h"
#include "core/pipeline.h"
#include "core/pipeline_layout.h"
#include "core/shader_module.h"
#include "imgui_internal.h"
#include "platform/filesystem.h"
#include "rendering/render_context.h"
#include "timer.h"
#include "utils/graphs.h"
#include "vulkan_sample.h"

namespace vkb
{
namespace
{
void upload_draw_data(ImDrawData *draw_data, const uint8_t *vertex_data, const uint8_t *index_data)
{
	ImDrawVert *vtx_dst = (ImDrawVert *) vertex_data;
	ImDrawIdx * idx_dst = (ImDrawIdx *) index_data;

	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList *cmd_list = draw_data->CmdLists[n];
		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
		vtx_dst += cmd_list->VtxBuffer.Size;
		idx_dst += cmd_list->IdxBuffer.Size;
	}
}

inline void reset_graph_max_value(Gui::StatsView::GraphData &graph_data)
{
	// If it does not have a fixed max
	if (!graph_data.has_fixed_max)
	{
		// Reset it
		graph_data.max_value = 0.0f;
	}
}
}        // namespace

const double Gui::press_time_ms = 200.0f;

const float Gui::overlay_alpha = 0.3f;

const std::string Gui::default_font = "Roboto-Regular";

const ImGuiWindowFlags Gui::common_flags = ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoScrollbar |
                                           ImGuiWindowFlags_NoTitleBar |
                                           ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing;

const ImGuiWindowFlags Gui::options_flags = Gui::common_flags;

const ImGuiWindowFlags Gui::info_flags = Gui::common_flags | ImGuiWindowFlags_NoInputs;

Gui::Gui(VulkanSample &sample_, const float dpi_factor, const float font_size, bool explicit_update) :
    sample{sample_},
    dpi_factor{dpi_factor},
    explicit_update{explicit_update}
{
	ImGui::CreateContext();

	ImGuiStyle &style = ImGui::GetStyle();

	// Color scheme
	style.Colors[ImGuiCol_TitleBg]          = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
	style.Colors[ImGuiCol_TitleBgActive]    = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_MenuBarBg]        = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_Header]           = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderActive]     = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_HeaderHovered]    = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_FrameBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_CheckMark]        = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_SliderGrab]       = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
	style.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(1.0f, 1.0f, 1.0f, 0.1f);
	style.Colors[ImGuiCol_FrameBgActive]    = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
	style.Colors[ImGuiCol_Button]           = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
	style.Colors[ImGuiCol_ButtonHovered]    = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
	style.Colors[ImGuiCol_ButtonActive]     = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);

	// Borderless window
	style.WindowBorderSize = 0.0f;

	// Global scale
	style.ScaleAllSizes(dpi_factor);

	// Dimensions
	ImGuiIO &io                = ImGui::GetIO();
	auto     extent            = sample.get_render_context().get_surface_extent();
	io.DisplaySize.x           = static_cast<float>(extent.width);
	io.DisplaySize.y           = static_cast<float>(extent.height);
	io.FontGlobalScale         = 1.0f;
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	// Enable keyboard navigation
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.KeyMap[ImGuiKey_Space]      = static_cast<int>(KeyCode::Space);
	io.KeyMap[ImGuiKey_Enter]      = static_cast<int>(KeyCode::Enter);
	io.KeyMap[ImGuiKey_LeftArrow]  = static_cast<int>(KeyCode::Left);
	io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(KeyCode::Right);
	io.KeyMap[ImGuiKey_UpArrow]    = static_cast<int>(KeyCode::Up);
	io.KeyMap[ImGuiKey_DownArrow]  = static_cast<int>(KeyCode::Down);
	io.KeyMap[ImGuiKey_Tab]        = static_cast<int>(KeyCode::Tab);

	// Default font
	fonts.emplace_back(default_font, font_size * dpi_factor);

	// Debug window font
	fonts.emplace_back("RobotoMono-Regular", (font_size / 2) * dpi_factor);

	// Create font texture
	unsigned char *font_data;
	int            tex_width, tex_height;
	io.Fonts->GetTexDataAsRGBA32(&font_data, &tex_width, &tex_height);
	size_t upload_size = tex_width * tex_height * 4 * sizeof(char);

	auto &device = sample.get_render_context().get_device();
	vk::Extent3D font_extent{to_u32(tex_width), to_u32(tex_height), 1u};

	// Upload font data into the vulkan image memory
	font_image = std::make_unique<core::Image>(
	    device.stage_to_device_image(
	        font_data, upload_size,
	        font_extent, vk::Format::eR8G8B8A8Unorm,
	        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	        vma::MemoryUsage::eGpuOnly));

	font_image_view = std::make_unique<core::ImageView>(*font_image, vk::ImageViewType::e2D);

	// Create texture sampler
	vk::SamplerCreateInfo sampler_info;
	sampler_info.maxAnisotropy = 1.0f;
	sampler_info.magFilter     = vk::Filter::eLinear;
	sampler_info.minFilter     = vk::Filter::eLinear;
	sampler_info.mipmapMode    = vk::SamplerMipmapMode::eNearest;
	sampler_info.addressModeU  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.addressModeV  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.addressModeW  = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.borderColor   = vk::BorderColor::eFloatOpaqueWhite;

	vkb::ShaderSource vert_shader("imgui.vert");
	vkb::ShaderSource frag_shader("imgui.frag");

	std::vector<vkb::ShaderModule *> shader_modules;
	shader_modules.push_back(&device.get_resource_cache().request_shader_module(vk::ShaderStageFlagBits::eVertex, vert_shader, {}));
	shader_modules.push_back(&device.get_resource_cache().request_shader_module(vk::ShaderStageFlagBits::eFragment, frag_shader, {}));

	pipeline_layout = &device.get_resource_cache().request_pipeline_layout(shader_modules, false);

	sampler = std::make_unique<core::Sampler>(device, sampler_info);

	if (explicit_update)
	{
		vertex_buffer = std::make_unique<core::Buffer>(sample.get_render_context().get_device(), 1, vk::BufferUsageFlagBits::eVertexBuffer, vma::MemoryUsage::eGpuToCpu);
		index_buffer  = std::make_unique<core::Buffer>(sample.get_render_context().get_device(), 1, vk::BufferUsageFlagBits::eIndexBuffer, vma::MemoryUsage::eGpuToCpu);
	}
}

void Gui::prepare(const vk::PipelineCache pipeline_cache, const vk::RenderPass render_pass, const std::vector<vk::PipelineShaderStageCreateInfo> &shader_stages)
{
	const auto &device = sample.get_render_context().get_device().get_handle();
	// Descriptor pool
	std::vector<vk::DescriptorPoolSize> pool_sizes = {
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 1)};
	vk::DescriptorPoolCreateInfo descriptorPoolInfo = vkb::initializers::descriptor_pool_create_info(pool_sizes, 2);
	descriptor_pool                                 = device.createDescriptorPool(descriptorPoolInfo);

	// Descriptor set layout
	std::vector<vk::DescriptorSetLayoutBinding> layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0),
	};
	vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = vkb::initializers::descriptor_set_layout_create_info(layout_bindings);
	descriptor_set_layout                                               = device.createDescriptorSetLayout(descriptor_set_layout_create_info);

	// Descriptor set
	vk::DescriptorSetAllocateInfo descriptor_allocation = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);
	descriptor_set                                      = device.allocateDescriptorSets(descriptor_allocation)[0];

	vk::DescriptorImageInfo font_descriptor = vkb::initializers::descriptor_image_info(
	    sampler->get_handle(),
	    font_image_view->get_handle(),
	    vk::ImageLayout::eShaderReadOnlyOptimal);
	std::vector<vk::WriteDescriptorSet> write_descriptor_sets = {
	    vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eCombinedImageSampler, 0, &font_descriptor),
	};
	device.updateDescriptorSets(write_descriptor_sets, nullptr);

	// Setup graphics pipeline for UI rendering
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise);

	// Enable blending
	vk::PipelineColorBlendAttachmentState blend_attachment_state;
	blend_attachment_state.blendEnable         = VK_TRUE;
	blend_attachment_state.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	blend_attachment_state.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
	blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	blend_attachment_state.colorBlendOp        = vk::BlendOp::eAdd;
	blend_attachment_state.srcAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
	blend_attachment_state.dstAlphaBlendFactor = vk::BlendFactor::eZero;
	blend_attachment_state.alphaBlendOp        = vk::BlendOp::eAdd;

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(1, &blend_attachment_state);

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info();

	vk::PipelineViewportStateCreateInfo viewport_state =
	    vkb::initializers::pipeline_viewport_state_create_info();

	vk::PipelineMultisampleStateCreateInfo multisample_state =
	    vkb::initializers::pipeline_multisample_state_create_info();

	std::vector<vk::DynamicState> dynamic_state_enables = {
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	vk::GraphicsPipelineCreateInfo pipeline_create_info = vkb::initializers::pipeline_create_info(pipeline_layout->get_handle(), render_pass);

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;
	pipeline_create_info.stageCount          = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();
	pipeline_create_info.subpass             = 0;

	// Vertex bindings an attributes based on ImGui vertex definition
	std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(ImDrawVert)),
	};
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, pos)),         // Location 0: Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32Sfloat, offsetof(ImDrawVert, uv)),          // Location 1: UV
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR8G8B8A8Unorm, offsetof(ImDrawVert, col)),        // Location 0: Color
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state_create_info = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state_create_info.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state_create_info.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state_create_info.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state_create_info.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	pipeline_create_info.pVertexInputState = &vertex_input_state_create_info;

	pipeline = device.createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}        // namespace vkb

void Gui::update(const float delta_time)
{
	if (visible != prev_visible)
	{
		drawer.set_dirty(true);
		prev_visible = visible;
	}

	if (!visible)
	{
		ImGui::EndFrame();
		return;
	}

	// Update imGui
	ImGuiIO &io  = ImGui::GetIO();
	io.DeltaTime = delta_time;

	// Render to generate draw buffers
	ImGui::Render();
}

bool Gui::update_buffers()
{
	ImDrawData *draw_data = ImGui::GetDrawData();
	bool        updated   = false;

	if (!draw_data)
	{
		return false;
	}

	size_t vertex_buffer_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	size_t index_buffer_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertex_buffer_size == 0) || (index_buffer_size == 0))
	{
		return false;
	}

	if ((vertex_buffer->get_handle().operator VkBuffer() == VK_NULL_HANDLE) || (vertex_buffer_size != last_vertex_buffer_size))
	{
		last_vertex_buffer_size = vertex_buffer_size;
		updated                 = true;

		vertex_buffer.reset();
		vertex_buffer = std::make_unique<core::Buffer>(sample.get_render_context().get_device(), vertex_buffer_size,
		                                               vk::BufferUsageFlagBits::eVertexBuffer,
		                                               vma::MemoryUsage::eGpuToCpu);
	}

	if ((index_buffer->get_handle().operator VkBuffer() == VK_NULL_HANDLE) || (index_buffer_size != last_index_buffer_size))
	{
		last_index_buffer_size = index_buffer_size;
		updated                = true;

		index_buffer.reset();
		index_buffer = std::make_unique<core::Buffer>(sample.get_render_context().get_device(), index_buffer_size,
		                                              vk::BufferUsageFlagBits::eIndexBuffer,
		                                              vma::MemoryUsage::eGpuToCpu);
	}

	// Upload data
	upload_draw_data(draw_data, vertex_buffer->map(), index_buffer->map());

	vertex_buffer->flush();
	index_buffer->flush();

	vertex_buffer->unmap();
	index_buffer->unmap();

	return updated;
}

void Gui::update_buffers(CommandBuffer &command_buffer, RenderFrame &render_frame)
{
	ImDrawData *draw_data = ImGui::GetDrawData();

	if (!draw_data)
	{
		return;
	}

	size_t vertex_buffer_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	size_t index_buffer_size  = draw_data->TotalIdxCount * sizeof(ImDrawIdx);

	if ((vertex_buffer_size == 0) || (index_buffer_size == 0))
	{
		return;
	}

	std::vector<uint8_t> vertex_data(vertex_buffer_size);
	std::vector<uint8_t> index_data(index_buffer_size);

	upload_draw_data(draw_data, vertex_data.data(), index_data.data());

	auto vertex_allocation = sample.get_render_context().get_active_frame().allocate_buffer(vk::BufferUsageFlagBits::eVertexBuffer, vertex_buffer_size);

	vertex_allocation.update(vertex_data);

	std::vector<vk::Buffer> buffers;
	buffers.emplace_back(std::ref(vertex_allocation.get_buffer().get_handle()));

	std::vector<vk::DeviceSize> offsets{vertex_allocation.get_offset()};

	command_buffer.bind_vertex_buffers(0, buffers, offsets);

	auto index_allocation = sample.get_render_context().get_active_frame().allocate_buffer(vk::BufferUsageFlagBits::eIndexBuffer, index_buffer_size);

	index_allocation.update(index_data);

	command_buffer.bind_index_buffer(index_allocation.get_buffer(), index_allocation.get_offset(), vk::IndexType::eUint16);
}

void Gui::resize(const uint32_t width, const uint32_t height) const
{
	auto &io         = ImGui::GetIO();
	io.DisplaySize.x = static_cast<float>(width);
	io.DisplaySize.y = static_cast<float>(height);
}

void Gui::draw(CommandBuffer &command_buffer)
{
	if (!visible)
	{
		return;
	}

	// Vertex input state
	vk::VertexInputBindingDescription vertex_input_binding;
	vertex_input_binding.stride = to_u32(sizeof(ImDrawVert));

	// Location 0: Position
	vk::VertexInputAttributeDescription pos_attr;
	pos_attr.format = vk::Format::eR32G32Sfloat;
	pos_attr.offset = to_u32(offsetof(ImDrawVert, pos));

	// Location 1: UV
	vk::VertexInputAttributeDescription uv_attr;
	uv_attr.location = 1;
	uv_attr.format   = vk::Format::eR32G32Sfloat;
	uv_attr.offset   = to_u32(offsetof(ImDrawVert, uv));

	// Location 2: Color
	vk::VertexInputAttributeDescription col_attr;
	col_attr.location = 2;
	col_attr.format   = vk::Format::eR8G8B8A8Unorm;
	col_attr.offset   = to_u32(offsetof(ImDrawVert, col));

	VertexInputState vertex_input_state{};
	vertex_input_state.bindings   = {vertex_input_binding};
	vertex_input_state.attributes = {pos_attr, uv_attr, col_attr};

	command_buffer.set_vertex_input_state(vertex_input_state);

	// Blend state
	vkb::ColorBlendAttachmentState color_attachment{};
	color_attachment.blend_enable           = VK_TRUE;
	color_attachment.color_write_mask       = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB;
	color_attachment.src_color_blend_factor = vk::BlendFactor::eSrcAlpha;
	color_attachment.dst_color_blend_factor = vk::BlendFactor::eOneMinusSrcAlpha;
	color_attachment.src_alpha_blend_factor = vk::BlendFactor::eOneMinusSrcAlpha;

	vkb::ColorBlendState blend_state{};
	blend_state.attachments = {color_attachment};

	command_buffer.set_color_blend_state(blend_state);

	vkb::RasterizationState rasterization_state{};
	rasterization_state.cull_mode = vk::CullModeFlagBits::eNone;
	command_buffer.set_rasterization_state(rasterization_state);

	vkb::DepthStencilState depth_state{};
	depth_state.depth_test_enable  = VK_FALSE;
	depth_state.depth_write_enable = VK_FALSE;
	command_buffer.set_depth_stencil_state(depth_state);

	// Bind pipeline layout
	command_buffer.bind_pipeline_layout(*pipeline_layout);

	command_buffer.bind_image(*font_image_view, *sampler, 0, 0, 0);

	// Pre-rotation
	auto &io             = ImGui::GetIO();
	auto  push_transform = glm::mat4(1.0f);

	if (sample.get_render_context().has_swapchain())
	{
		auto transform = sample.get_render_context().get_swapchain().get_transform();

		glm::vec3 rotation_axis = glm::vec3(0.0f, 0.0f, 1.0f);
		if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate90)
		{
			push_transform = glm::rotate(push_transform, glm::radians(90.0f), rotation_axis);
		}
		else if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate270)
		{
			push_transform = glm::rotate(push_transform, glm::radians(270.0f), rotation_axis);
		}
		else if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate180)
		{
			push_transform = glm::rotate(push_transform, glm::radians(180.0f), rotation_axis);
		}
	}

	// GUI coordinate space to screen space
	push_transform = glm::translate(push_transform, glm::vec3(-1.0f, -1.0f, 0.0f));
	push_transform = glm::scale(push_transform, glm::vec3(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y, 0.0f));

	// Push constants
	command_buffer.push_constants(0, push_transform);

	// If a render context is used, then use the frames buffer pools to allocate GUI vertex/index data from
	if (!explicit_update)
	{
		update_buffers(command_buffer, sample.get_render_context().get_active_frame());
	}
	else
	{
		std::vector<vk::Buffer> buffers;
		buffers.push_back(vertex_buffer->get_handle());
		command_buffer.bind_vertex_buffers(0, buffers, {0});

		command_buffer.bind_index_buffer(*index_buffer, 0, vk::IndexType::eUint16);
	}

	// Render commands
	ImDrawData *draw_data     = ImGui::GetDrawData();
	int32_t     vertex_offset = 0;
	uint32_t    index_offset  = 0;

	if (!draw_data || draw_data->CmdListsCount == 0)
	{
		return;
	}

	for (int32_t i = 0; i < draw_data->CmdListsCount; i++)
	{
		const ImDrawList *cmd_list = draw_data->CmdLists[i];
		for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
		{
			const ImDrawCmd *cmd = &cmd_list->CmdBuffer[j];
			vk::Rect2D       scissor_rect;
			scissor_rect.offset.x      = std::max(static_cast<int32_t>(cmd->ClipRect.x), 0);
			scissor_rect.offset.y      = std::max(static_cast<int32_t>(cmd->ClipRect.y), 0);
			scissor_rect.extent.width  = static_cast<uint32_t>(cmd->ClipRect.z - cmd->ClipRect.x);
			scissor_rect.extent.height = static_cast<uint32_t>(cmd->ClipRect.w - cmd->ClipRect.y);

			// Adjust for pre-rotation if necessary
			if (sample.get_render_context().has_swapchain())
			{
				auto transform = sample.get_render_context().get_swapchain().get_transform();
				if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate90)
				{
					scissor_rect.offset.x      = static_cast<uint32_t>(io.DisplaySize.y - cmd->ClipRect.w);
					scissor_rect.offset.y      = static_cast<uint32_t>(cmd->ClipRect.x);
					scissor_rect.extent.width  = static_cast<uint32_t>(cmd->ClipRect.w - cmd->ClipRect.y);
					scissor_rect.extent.height = static_cast<uint32_t>(cmd->ClipRect.z - cmd->ClipRect.x);
				}
				else if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate180)
				{
					scissor_rect.offset.x      = static_cast<uint32_t>(io.DisplaySize.x - cmd->ClipRect.z);
					scissor_rect.offset.y      = static_cast<uint32_t>(io.DisplaySize.y - cmd->ClipRect.w);
					scissor_rect.extent.width  = static_cast<uint32_t>(cmd->ClipRect.z - cmd->ClipRect.x);
					scissor_rect.extent.height = static_cast<uint32_t>(cmd->ClipRect.w - cmd->ClipRect.y);
				}
				else if (transform & vk::SurfaceTransformFlagBitsKHR::eRotate270)
				{
					scissor_rect.offset.x      = static_cast<uint32_t>(cmd->ClipRect.y);
					scissor_rect.offset.y      = static_cast<uint32_t>(io.DisplaySize.x - cmd->ClipRect.z);
					scissor_rect.extent.width  = static_cast<uint32_t>(cmd->ClipRect.w - cmd->ClipRect.y);
					scissor_rect.extent.height = static_cast<uint32_t>(cmd->ClipRect.z - cmd->ClipRect.x);
				}
			}

			command_buffer.set_scissor(0, {scissor_rect});
			command_buffer.draw_indexed(cmd->ElemCount, 1, index_offset, vertex_offset, 0);
			index_offset += cmd->ElemCount;
		}
		vertex_offset += cmd_list->VtxBuffer.Size;
	}
}

void Gui::draw(vk::CommandBuffer command_buffer)
{
	if (!visible)
	{
		return;
	}

	auto &      io            = ImGui::GetIO();
	ImDrawData *draw_data     = ImGui::GetDrawData();
	int32_t     vertex_offset = 0;
	int32_t     index_offset  = 0;

	if ((!draw_data) || (draw_data->CmdListsCount == 0))
	{
		return;
	}

	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
	command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout->get_handle(), 0, 1, &descriptor_set, 0, NULL);

	// Push constants
	auto push_transform = glm::mat4(1.0f);
	push_transform      = glm::translate(push_transform, glm::vec3(-1.0f, -1.0f, 0.0f));
	push_transform      = glm::scale(push_transform, glm::vec3(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y, 0.0f));
	command_buffer.pushConstants<glm::mat4>(pipeline_layout->get_handle(), vk::ShaderStageFlagBits::eVertex, 0, push_transform);

	vk::Buffer vertex_buffer_handle = vertex_buffer->get_handle();
	command_buffer.bindVertexBuffers(0, vertex_buffer_handle, {0});

	vk::Buffer index_buffer_handle = index_buffer->get_handle();
	command_buffer.bindIndexBuffer(index_buffer_handle, 0, vk::IndexType::eUint16);

	for (int32_t i = 0; i < draw_data->CmdListsCount; i++)
	{
		const ImDrawList *cmd_list = draw_data->CmdLists[i];
		for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
		{
			const ImDrawCmd *cmd = &cmd_list->CmdBuffer[j];
			vk::Rect2D       scissor_rect;
			scissor_rect.offset.x      = std::max(static_cast<int32_t>(cmd->ClipRect.x), 0);
			scissor_rect.offset.y      = std::max(static_cast<int32_t>(cmd->ClipRect.y), 0);
			scissor_rect.extent.width  = static_cast<uint32_t>(cmd->ClipRect.z - cmd->ClipRect.x);
			scissor_rect.extent.height = static_cast<uint32_t>(cmd->ClipRect.w - cmd->ClipRect.y);

			command_buffer.setScissor(0, scissor_rect);
			command_buffer.drawIndexed(cmd->ElemCount, 1, index_offset, vertex_offset, 0);
			index_offset += cmd->ElemCount;
		}
		vertex_offset += cmd_list->VtxBuffer.Size;
	}
}

Gui::~Gui()
{
	auto &device = sample.get_render_context().get_device().get_handle();
	device.destroy(descriptor_pool);
	device.destroy(descriptor_set_layout);
	device.destroy(pipeline);

	ImGui::DestroyContext();
}

void Gui::show_demo_window()
{
	ImGui::ShowDemoWindow();
}

Gui::StatsView &Gui::get_stats_view()
{
	return stats_view;
}

Drawer &Gui::get_drawer()
{
	return drawer;
}

Font &Gui::get_font(const std::string &font_name)
{
	assert(!fonts.empty() && "No fonts exist");

	auto it = std::find_if(fonts.begin(), fonts.end(), [&font_name](Font &font) { return font.name == font_name; });

	if (it != fonts.end())
	{
		return *it;
	}
	else
	{
		LOGW("Couldn't find font with name {}", font_name);
		return *fonts.begin();
	}
}

bool Gui::is_debug_view_active() const
{
	return debug_view.active;
}

void Gui::StatsView::reset_max_value(const StatIndex index)
{
	auto pr = graph_map.find(index);
	if (pr != graph_map.end())
	{
		reset_graph_max_value(pr->second);
	}
}

void Gui::StatsView::reset_max_values()
{
	// For every entry in the map
	std::for_each(graph_map.begin(),
	              graph_map.end(),
	              [](auto &pr) { reset_graph_max_value(pr.second); });
}

void Gui::show_top_window(const std::string &app_name, const Stats *stats, DebugInfo *debug_info)
{
	// Transparent background
	ImGui::SetNextWindowBgAlpha(overlay_alpha);
	ImVec2 size{ImGui::GetIO().DisplaySize.x, 0.0f};
	ImGui::SetNextWindowSize(size, ImGuiSetCond_Always);

	// Top left
	ImVec2 pos{0.0f, 0.0f};
	ImGui::SetNextWindowPos(pos, ImGuiSetCond_Always);

	bool is_open = true;
	ImGui::Begin("Top", &is_open, common_flags);

	show_app_info(app_name);

	if (stats)
	{
		show_stats(*stats);

		// Reset max values if user taps on this window
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0 /* left */))
		{
			stats_view.reset_max_values();
		}
	}

	if (debug_info)
	{
		if (debug_view.active)
		{
			show_debug_window(*debug_info, ImVec2{0, ImGui::GetWindowSize().y});
		}
	}

	ImGui::End();
}

void Gui::show_app_info(const std::string &app_name)
{
	// Sample name
	ImGui::Text("%s", app_name.c_str());

	// GPU name
	auto &device            = sample.get_render_context().get_device();
	auto  device_name_label = "GPU: " + std::string(device.get_properties().deviceName);
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(device_name_label.c_str()).x);
	ImGui::Text("%s", device_name_label.c_str());
}

void Gui::show_debug_window(DebugInfo &debug_info, const ImVec2 &position)
{
	auto &io    = ImGui::GetIO();
	auto &style = ImGui::GetStyle();
	auto &font  = get_font("RobotoMono-Regular");

	// Calculate only once
	if (debug_view.label_column_width == 0)
	{
		debug_view.label_column_width = style.ItemInnerSpacing.x + debug_info.get_longest_label() * font.size / debug_view.scale;
	}

	ImGui::SetNextWindowBgAlpha(overlay_alpha);
	ImGui::SetNextWindowPos(position, ImGuiSetCond_FirstUseEver);
	ImGui::SetNextWindowContentSize(ImVec2{io.DisplaySize.x, 0.0f});

	bool                   is_open = true;
	const ImGuiWindowFlags flags   = ImGuiWindowFlags_AlwaysAutoResize |
	                               ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoTitleBar |
	                               ImGuiWindowFlags_NoResize |
	                               ImGuiWindowFlags_NoFocusOnAppearing |
	                               ImGuiWindowFlags_NoNav;

	ImGui::Begin("Debug Window", &is_open, flags);
	ImGui::PushFont(font.handle);

	auto field_count = debug_info.get_fields().size() > debug_view.max_fields ? debug_view.max_fields : debug_info.get_fields().size();

	ImGui::BeginChild("Table", ImVec2(0, field_count * (font.size + style.ItemSpacing.y)), false);
	ImGui::Columns(2);
	ImGui::SetColumnWidth(0, debug_view.label_column_width);
	ImGui::SetColumnWidth(1, io.DisplaySize.x - debug_view.label_column_width);
	for (auto &field : debug_info.get_fields())
	{
		const std::string &label = field->label;
		const std::string &value = field->to_string();
		ImGui::Text("%s", label.c_str());
		ImGui::NextColumn();
		ImGui::Text(" %s", value.c_str());
		ImGui::NextColumn();
	}
	ImGui::Columns(1);
	ImGui::EndChild();

	static Timer       timer;
	static const char *message;

	if (ImGui::Button("Save Debug Graphs"))
	{
		if (utils::debug_graphs(sample.get_render_context(), sample.get_scene()))
		{
			message = "Graphs Saved!";
		}
		else
		{
			message = "Error outputting graphs!";
		}

		if (timer.is_running())
		{
			timer.lap();
		}
		else
		{
			timer.start();
		}
	}

	if (timer.is_running())
	{
		if (timer.elapsed() > 2.0)
		{
			timer.stop();
		}
		else
		{
			ImGui::SameLine();
			ImGui::Text("%s", message);
		}
	}

	ImGui::PopFont();
	ImGui::End();
}

Gui::StatsView::GraphData::GraphData(const std::string &name_,
                                     const std::string &graph_label_format_,
                                     float              scale_factor_,
                                     bool               has_fixed_max_,
                                     float              max_value_) :
    name(name_),
    format{graph_label_format_},
    scale_factor{scale_factor_},
    has_fixed_max{has_fixed_max_},
    max_value{max_value_}
{}

void Gui::show_stats(const Stats &stats)
{
	for (const auto &stat_index : stats.get_enabled_stats())
	{
		// Find the graph data of this stat index
		auto pr = stats_view.graph_map.find(stat_index);

		assert(pr != stats_view.graph_map.end() && "StatIndex not implemented in gui graph_map");

		// Draw graph
		auto &      graph_data     = pr->second;
		const auto &graph_elements = stats.get_data(stat_index);
		float       graph_min      = 0.0f;
		float &     graph_max      = graph_data.max_value;

		if (!graph_data.has_fixed_max)
		{
			auto new_max = *std::max_element(graph_elements.begin(), graph_elements.end()) * stats_view.top_padding;
			if (new_max > graph_max)
			{
				graph_max = new_max;
			}
		}

		const ImVec2 graph_size = ImVec2{
		    ImGui::GetIO().DisplaySize.x,
		    stats_view.graph_height /* dpi */ * dpi_factor};

		std::stringstream graph_label;
		float             avg = std::accumulate(graph_elements.begin(), graph_elements.end(), 0.0f) / graph_elements.size();

		// Check if the stat is available in the current platform
		if (!stats.is_available(stat_index))
		{
			graph_label << graph_data.name << ": not available";
		}
		else
		{
			graph_label << fmt::format(graph_data.name + ": " + graph_data.format, avg * graph_data.scale_factor);
		}

		ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
		ImGui::PlotLines("", &graph_elements[0], static_cast<int>(graph_elements.size()), 0, graph_label.str().c_str(), graph_min, graph_max, graph_size);
		ImGui::PopItemFlag();
	}
}

void Gui::show_options_window(std::function<void()> body, const uint32_t lines)
{
	// Add padding around the text so that the options are not
	// too close to the edges and are easier to interact with.
	// Also add double vertical padding to avoid rounded corners.
	const float window_padding = ImGui::CalcTextSize("T").x;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{window_padding, window_padding * 2.0f});
	auto window_height = lines * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
	auto window_width  = ImGui::GetIO().DisplaySize.x;
	ImGui::SetNextWindowBgAlpha(overlay_alpha);
	const ImVec2 size = ImVec2(window_width, 0);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	const ImVec2 pos = ImVec2(0.0f, ImGui::GetIO().DisplaySize.y - window_height);
	ImGui::SetNextWindowPos(pos, ImGuiSetCond_Always);
	const ImGuiWindowFlags flags   = (ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoScrollbar |
                                    ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_AlwaysUseWindowPadding |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing);
	bool                   is_open = true;
	ImGui::Begin("Options", &is_open, flags);
	body();
	ImGui::End();
	ImGui::PopStyleVar();
}

void Gui::show_simple_window(const std::string &name, uint32_t last_fps, std::function<void()> body)
{
	ImGuiIO &io = ImGui::GetIO();

	ImGui::NewFrame();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(10, 10));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Vulkan Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::TextUnformatted(name.c_str());
	ImGui::TextUnformatted(std::string(sample.get_render_context().get_device().get_properties().deviceName).c_str());
	ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / last_fps), last_fps);
	ImGui::PushItemWidth(110.0f * dpi_factor);

	body();

	ImGui::PopItemWidth();
	ImGui::End();
	ImGui::PopStyleVar();
}

bool Gui::input_event(const InputEvent &input_event)
{
	auto &io                 = ImGui::GetIO();
	auto  capture_move_event = false;

	if (input_event.get_source() == EventSource::Keyboard)
	{
		const auto &key_event = static_cast<const KeyInputEvent &>(input_event);

		if (key_event.get_action() == KeyAction::Down)
		{
			io.KeysDown[static_cast<int>(key_event.get_code())] = true;
		}
		else if (key_event.get_action() == KeyAction::Up)
		{
			io.KeysDown[static_cast<int>(key_event.get_code())] = false;
		}
	}
	else if (input_event.get_source() == EventSource::Mouse)
	{
		const auto &mouse_button = static_cast<const MouseButtonInputEvent &>(input_event);

		io.MousePos = ImVec2{mouse_button.get_pos_x(),
		                     mouse_button.get_pos_y()};

		auto button_id = static_cast<int>(mouse_button.get_button());

		if (mouse_button.get_action() == MouseAction::Down)
		{
			io.MouseDown[button_id] = true;
		}
		else if (mouse_button.get_action() == MouseAction::Up)
		{
			io.MouseDown[button_id] = false;
		}
		else if (mouse_button.get_action() == MouseAction::Move)
		{
			capture_move_event = io.WantCaptureMouse;
		}
	}
	else if (input_event.get_source() == EventSource::Touchscreen)
	{
		const auto &touch_event = static_cast<const TouchInputEvent &>(input_event);

		io.MousePos = ImVec2{touch_event.get_pos_x(), touch_event.get_pos_y()};

		if (touch_event.get_action() == TouchAction::Down)
		{
			io.MouseDown[touch_event.get_pointer_id()] = true;
		}
		else if (touch_event.get_action() == TouchAction::Up)
		{
			io.MouseDown[touch_event.get_pointer_id()] = false;
		}
		else if (touch_event.get_action() == TouchAction::Move)
		{
			capture_move_event = io.WantCaptureMouse;
		}
	}

	// Toggle GUI elements when tap or clicking outside the GUI windows
	if (!io.WantCaptureMouse)
	{
		bool press_down = (input_event.get_source() == EventSource::Mouse && static_cast<const MouseButtonInputEvent &>(input_event).get_action() == MouseAction::Down) || (input_event.get_source() == EventSource::Touchscreen && static_cast<const TouchInputEvent &>(input_event).get_action() == TouchAction::Down);
		bool press_up   = (input_event.get_source() == EventSource::Mouse && static_cast<const MouseButtonInputEvent &>(input_event).get_action() == MouseAction::Up) || (input_event.get_source() == EventSource::Touchscreen && static_cast<const TouchInputEvent &>(input_event).get_action() == TouchAction::Up);

		if (press_down)
		{
			timer.start();
			if (input_event.get_source() == EventSource::Touchscreen)
			{
				const auto &touch_event = static_cast<const TouchInputEvent &>(input_event);
				if (touch_event.get_touch_points() == 2)
				{
					two_finger_tap = true;
				}
			}
		}
		if (press_up)
		{
			auto press_delta = timer.stop<Timer::Milliseconds>();
			if (press_delta < press_time_ms)
			{
				if (input_event.get_source() == EventSource::Mouse)
				{
					const auto &mouse_button = static_cast<const MouseButtonInputEvent &>(input_event);
					if (mouse_button.get_button() == MouseButton::Left)
					{
						visible = !visible;
					}
					else if (mouse_button.get_button() == MouseButton::Right)
					{
						debug_view.active = !debug_view.active;
					}
				}
				else if (input_event.get_source() == EventSource::Touchscreen)
				{
					const auto &touch_event = static_cast<const TouchInputEvent &>(input_event);
					if (!two_finger_tap && touch_event.get_touch_points() == 1)
					{
						visible = !visible;
					}
					else if (two_finger_tap && touch_event.get_touch_points() == 2)
					{
						debug_view.active = !debug_view.active;
					}
					else
					{
						two_finger_tap = false;
					}
				}
			}
		}
	}

	return capture_move_event;
}

void Drawer::clear()
{
	dirty = false;
}

bool Drawer::is_dirty()
{
	return dirty;
}

void Drawer::set_dirty(bool dirty)
{
	this->dirty = dirty;
}

bool Drawer::header(const char *caption)
{
	return ImGui::CollapsingHeader(caption, ImGuiTreeNodeFlags_DefaultOpen);
}

bool Drawer::checkbox(const char *caption, bool *value)
{
	bool res = ImGui::Checkbox(caption, value);
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::checkbox(const char *caption, int32_t *value)
{
	bool val = (*value == 1);
	bool res = ImGui::Checkbox(caption, &val);
	*value   = val;
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::input_float(const char *caption, float *value, float step, uint32_t precision)
{
	bool res = ImGui::InputFloat(caption, value, step, step * 10.0f, precision);
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::slider_float(const char *caption, float *value, float min, float max)
{
	bool res = ImGui::SliderFloat(caption, value, min, max);
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::slider_int(const char *caption, int32_t *value, int32_t min, int32_t max)
{
	bool res = ImGui::SliderInt(caption, value, min, max);
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::combo_box(const char *caption, int32_t *itemindex, std::vector<std::string> items)
{
	if (items.empty())
	{
		return false;
	}
	std::vector<const char *> charitems;
	charitems.reserve(items.size());
	for (size_t i = 0; i < items.size(); i++)
	{
		charitems.push_back(items[i].c_str());
	}
	uint32_t itemCount = static_cast<uint32_t>(charitems.size());
	bool     res       = ImGui::Combo(caption, itemindex, &charitems[0], itemCount, itemCount);
	if (res)
	{
		dirty = true;
	};
	return res;
}

bool Drawer::button(const char *caption)
{
	bool res = ImGui::Button(caption);
	if (res)
	{
		dirty = true;
	};
	return res;
}

void Drawer::text(const char *formatstr, ...)
{
	va_list args;
	va_start(args, formatstr);
	ImGui::TextV(formatstr, args);
	va_end(args);
}

}        // namespace vkb
