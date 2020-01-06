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

#include "pipeline_barriers.h"

#include "core/device.h"
#include "core/pipeline_layout.h"
#include "core/shader_module.h"
#include "gltf_loader.h"
#include "gui.h"
#include "platform/filesystem.h"
#include "platform/platform.h"
#include "rendering/subpasses/forward_subpass.h"
#include "rendering/subpasses/lighting_subpass.h"
#include "scene_graph/components/material.h"
#include "scene_graph/components/pbr_material.h"
#include "scene_graph/components/perspective_camera.h"
#include "stats.h"

PipelineBarriers::PipelineBarriers()
{
	auto &config = get_configuration();

	config.insert<vkb::IntSetting>(0, reinterpret_cast<int &>(dependency_type), DependencyType::BOTTOM_TO_TOP);
	config.insert<vkb::IntSetting>(1, reinterpret_cast<int &>(dependency_type), DependencyType::FRAG_TO_VERT);
	config.insert<vkb::IntSetting>(2, reinterpret_cast<int &>(dependency_type), DependencyType::FRAG_TO_FRAG);
}

bool PipelineBarriers::prepare(vkb::Platform &platform)
{
	if (!VulkanSample::prepare(platform))
	{
		return false;
	}

	load_scene("scenes/sponza/Sponza01.gltf");

	scene->clear_components<vkb::sg::Light>();

	auto light_pos   = glm::vec3(0.0f, 128.0f, -225.0f);
	auto light_color = glm::vec3(1.0, 1.0, 1.0);

	// Magic numbers used to offset lights in the Sponza scene
	for (int i = -2; i < 2; ++i)
	{
		for (int j = 0; j < 2; ++j)
		{
			glm::vec3 pos = light_pos;
			pos.x += i * 400;
			pos.z += j * (225 + 140);
			pos.y = 8;

			for (int k = 0; k < 3; ++k)
			{
				pos.y = pos.y + (k * 100);

				light_color.x = static_cast<float>(rand()) / (RAND_MAX);
				light_color.y = static_cast<float>(rand()) / (RAND_MAX);
				light_color.z = static_cast<float>(rand()) / (RAND_MAX);

				vkb::sg::LightProperties props;
				props.color     = light_color;
				props.intensity = 0.2f;

				vkb::add_point_light(*scene, pos, props);
			}
		}
	}

	auto &camera_node = vkb::add_free_camera(*scene, "main_camera", get_render_context().get_surface_extent());
	camera            = &camera_node.get_component<vkb::sg::Camera>();

	auto geometry_vs = vkb::ShaderSource{vkb::fs::read_shader("deferred/geometry.vert")};
	auto geometry_fs = vkb::ShaderSource{vkb::fs::read_shader("deferred/geometry.frag")};

	auto gbuffer_pass = std::make_unique<vkb::GeometrySubpass>(get_render_context(), std::move(geometry_vs), std::move(geometry_fs), *scene, *camera);
	gbuffer_pass->set_output_attachments({1, 2, 3});
	gbuffer_pipeline.add_subpass(std::move(gbuffer_pass));
	gbuffer_pipeline.set_load_store(vkb::gbuffer::get_clear_store_all());

	auto lighting_vs = vkb::ShaderSource{vkb::fs::read_shader("deferred/lighting.vert")};
	auto lighting_fs = vkb::ShaderSource{vkb::fs::read_shader("deferred/lighting.frag")};

	auto lighting_subpass = std::make_unique<vkb::LightingSubpass>(get_render_context(), std::move(lighting_vs), std::move(lighting_fs), *camera, *scene);
	lighting_subpass->set_input_attachments({1, 2, 3});
	lighting_pipeline.add_subpass(std::move(lighting_subpass));
	lighting_pipeline.set_load_store(vkb::gbuffer::get_load_all_store_swapchain());

	stats = std::make_unique<vkb::Stats>(std::set<vkb::StatIndex>{vkb::StatIndex::frame_times,
	                                                              vkb::StatIndex::vertex_compute_cycles,
	                                                              vkb::StatIndex::fragment_cycles},
	                                     vkb::CounterSamplingConfig{vkb::CounterSamplingMode::Continuous});
	gui   = std::make_unique<vkb::Gui>(*this, platform.get_window().get_dpi_factor());

	return true;
}

void PipelineBarriers::prepare_render_context()
{
	get_render_context().prepare(1, [this](vkb::core::Image &&swapchain_image) { return create_render_target(std::move(swapchain_image)); });
}

vkb::RenderTarget PipelineBarriers::create_render_target(vkb::core::Image &&swapchain_image)
{
	auto &device = swapchain_image.get_device();
	auto &extent = swapchain_image.get_extent();

	vkb::core::Image depth_image{device,
	                             extent,
	                             vk::Format::eD32Sfloat,
	                             vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eInputAttachment,
	                             vma::MemoryUsage::eGpuOnly};

	vkb::core::Image albedo_image{device,
	                              extent,
	                              vk::Format::eR8G8B8A8Unorm,
	                              vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
	                              vma::MemoryUsage::eGpuOnly};

	vkb::core::Image normal_image{device,
	                              extent,
	                              vk::Format::eA2B10G10R10UnormPack32,
	                              vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
	                              vma::MemoryUsage::eGpuOnly};

	std::vector<vkb::core::Image> images;

	// Attachment 0
	images.push_back(std::move(swapchain_image));

	// Attachment 1
	images.push_back(std::move(depth_image));

	// Attachment 2
	images.push_back(std::move(albedo_image));

	// Attachment 3
	images.push_back(std::move(normal_image));

	return vkb::RenderTarget{std::move(images)};
}

void PipelineBarriers::draw(vkb::CommandBuffer &command_buffer, vkb::RenderTarget &render_target)
{
	// POI
	//
	// Pipeline stages and access masks for all barriers are picked based on the sample's setting.
	//
	// The first set of barriers transitions images for the first render pass. Color images only need to be ready
	// at COLOR_ATTACHMENT_OUTPUT time (while the depth image needs EARLY_FRAGMENT_TESTS | LATE_FRAGMENT_TESTS).
	// More conservative barriers are shown, waiting for acquisition at either VERTEX_SHADER or even TOP_OF_PIPE.
	//

	auto &views = render_target.get_views();

	{
		// Image 0 is the swapchain
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = vk::ImageLayout::eUndefined;
		memory_barrier.new_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		memory_barrier.src_access_mask = {};

		switch (dependency_type)
		{
			case DependencyType::BOTTOM_TO_TOP:
				memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eTopOfPipe;
				memory_barrier.dst_access_mask = {};
				break;
			case DependencyType::FRAG_TO_VERT:
				memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
				memory_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
				break;
			case DependencyType::FRAG_TO_FRAG:
			default:
				memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
				memory_barrier.dst_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
				break;
		}

		command_buffer.image_memory_barrier(views.at(0), memory_barrier);

		// Skip 1 as it is handled later as a depth-stencil attachment
		for (size_t i = 2; i < views.size(); ++i)
		{
			memory_barrier.old_layout = vk::ImageLayout::eUndefined;
			command_buffer.image_memory_barrier(views.at(i), memory_barrier);
		}
	}

	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = vk::ImageLayout::eUndefined;
		memory_barrier.new_layout      = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eTopOfPipe;
		memory_barrier.src_access_mask = {};

		switch (dependency_type)
		{
			case DependencyType::BOTTOM_TO_TOP:
				memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eTopOfPipe;
				memory_barrier.dst_access_mask = {};
				break;
			case DependencyType::FRAG_TO_VERT:
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
				memory_barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
				break;
			case DependencyType::FRAG_TO_FRAG:
			default:
				memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
				memory_barrier.dst_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
				break;
		}

		command_buffer.image_memory_barrier(views.at(1), memory_barrier);
	}

	auto &extent = render_target.get_extent();

	VkViewport viewport{};
	viewport.width    = static_cast<float>(extent.width);
	viewport.height   = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	command_buffer.set_viewport(0, {viewport});

	VkRect2D scissor{};
	scissor.extent = extent;
	command_buffer.set_scissor(0, {scissor});

	gbuffer_pipeline.draw(command_buffer, get_render_context().get_active_frame().get_render_target());

	command_buffer.end_render_pass();

	// POI
	//
	// The second set of barriers transitions the G-buffer images to SHADER_READ_ONLY_OPTIMAL for the second render pass.
	// It also ensures proper synchronization between render passes. The most optimal set of barriers is from COLOR_ATTACHMENT_OUTPUT
	// to FRAGMENT_SHADER, as the images only need to be ready at fragment shading time for the second render pass.
	//
	// With an optimal set of barriers, tiled GPUs would be able to run vertex shading for the second render pass in parallel with
	// fragment shading for the first render pass. Again, more conservative barriers are shown, waiting for VERTEX_SHADER or even TOP_OF_PIPE.
	// Those barriers will flush the GPU's pipeline, causing serialization between vertex and fragment work, potentially affecting performance.
	//
	for (size_t i = 1; i < render_target.get_views().size(); ++i)
	{
		auto &view = render_target.get_views().at(i);

		vkb::ImageMemoryBarrier barrier;

		if (i == 1)
		{
			barrier.old_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			barrier.new_layout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

			barrier.src_stage_mask  = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
			barrier.src_access_mask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		}
		else
		{
			barrier.old_layout = vk::ImageLayout::eColorAttachmentOptimal;
			barrier.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal;

			barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		}

		switch (dependency_type)
		{
			case DependencyType::BOTTOM_TO_TOP:
				barrier.src_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;
				barrier.src_access_mask = {};
				barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eTopOfPipe;
				barrier.dst_access_mask = {};
				break;
			case DependencyType::FRAG_TO_VERT:
				barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eVertexShader;
				barrier.dst_access_mask = vk::AccessFlagBits::eShaderRead;
				break;
			case DependencyType::FRAG_TO_FRAG:
			default:
				barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eFragmentShader;
				barrier.dst_access_mask = vk::AccessFlagBits::eInputAttachmentRead;
				break;
		}

		command_buffer.image_memory_barrier(view, barrier);
	}

	lighting_pipeline.draw(command_buffer, get_render_context().get_active_frame().get_render_target());

	if (gui)
	{
		gui->draw(command_buffer);
	}

	command_buffer.end_render_pass();

	{
		vkb::ImageMemoryBarrier memory_barrier{};
		memory_barrier.old_layout      = vk::ImageLayout::eColorAttachmentOptimal;
		memory_barrier.new_layout      = vk::ImageLayout::ePresentSrcKHR;
		memory_barrier.src_access_mask = vk::AccessFlagBits::eColorAttachmentWrite;
		memory_barrier.src_stage_mask  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		memory_barrier.dst_stage_mask  = vk::PipelineStageFlagBits::eBottomOfPipe;

		command_buffer.image_memory_barrier(views.at(0), memory_barrier);
	}
}

void PipelineBarriers::draw_gui()
{
	int  lines         = 2;
	bool portrait_mode = (reinterpret_cast<vkb::sg::PerspectiveCamera *>(camera)->get_aspect_ratio() < 1.0f);

	if (portrait_mode)
	{
		// In portrait, break the radio buttons into two separate lines
		lines++;
	}

	gui->show_options_window(
	    /* body = */ [this, portrait_mode]() {
		    ImGui::Text("Pipeline barrier stages:");
		    ImGui::RadioButton("Bottom to top", reinterpret_cast<int *>(&dependency_type), DependencyType::BOTTOM_TO_TOP);
		    ImGui::SameLine();
		    ImGui::RadioButton("Frag to vert", reinterpret_cast<int *>(&dependency_type), DependencyType::FRAG_TO_VERT);

		    if (!portrait_mode)
		    {
			    ImGui::SameLine();
		    }

		    ImGui::RadioButton("Frag to frag", reinterpret_cast<int *>(&dependency_type), DependencyType::FRAG_TO_FRAG);
	    },
	    /* lines = */ lines);
}

std::unique_ptr<vkb::VulkanSample> create_pipeline_barriers()
{
	return std::make_unique<PipelineBarriers>();
}
