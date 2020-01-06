/* Copyright (c) 2019, Sascha Willems
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

#include "api_vulkan_sample.h"

#include "core/device.h"
#include "core/swapchain.h"
#include "gltf_loader.h"
#include "scene_graph/components/image.h"
#include "scene_graph/components/sampler.h"
#include "scene_graph/components/sub_mesh.h"
#include "scene_graph/components/texture.h"

bool ApiVulkanSample::prepare(vkb::Platform &platform)
{
	if (!VulkanSample::prepare(platform))
	{
		return false;
	}

	depth_format = vkb::get_supported_depth_format(device->get_physical_device());
	assert(depth_format != vk::Format::eUndefined);

	// Create synchronization objects
	// Create a semaphore used to synchronize image presentation
	// Ensures that the current swapchain render target has completed presentation and has been released by the presentation engine, ready for rendering
	semaphores.acquired_image_ready = device->get_handle().createSemaphore({});
	// Create a semaphore used to synchronize command submission
	// Ensures that the image is not presented until all commands have been sumbitted and executed
	semaphores.render_complete = device->get_handle().createSemaphore({});

	// Set up submit info structure
	// Semaphores will stay the same during application lifetime
	// Command buffer submission info is set by each example
	submit_info                   = vkb::initializers::submit_info();
	submit_info.pWaitDstStageMask = &submit_pipeline_stages;
	if (!is_headless())
	{
		submit_info.waitSemaphoreCount   = 1;
		submit_info.pWaitSemaphores      = &semaphores.acquired_image_ready;
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores    = &semaphores.render_complete;
	}

	queue = device->get_suitable_graphics_queue().get_handle();

	create_swapchain_buffers();
	create_command_pool();
	create_command_buffers();
	create_synchronization_primitives();
	setup_depth_stencil();
	setup_render_pass();
	create_pipeline_cache();
	setup_framebuffer();

	width  = get_render_context().get_surface_extent().width;
	height = get_render_context().get_surface_extent().height;

	gui = std::make_unique<vkb::Gui>(*this, platform.get_window().get_dpi_factor(), 15.0f, true);
	gui->prepare(pipeline_cache, render_pass,
	             {load_shader("uioverlay/uioverlay.vert", vk::ShaderStageFlagBits::eVertex),
	              load_shader("uioverlay/uioverlay.frag", vk::ShaderStageFlagBits::eFragment)});

	return true;
}

void ApiVulkanSample::prepare_render_context()
{
	get_render_context().set_present_mode_priority({
	    vk::PresentModeKHR::eMailbox,
	    vk::PresentModeKHR::eImmediate,
	    vk::PresentModeKHR::eFifo,
	});

	get_render_context().set_surface_format_priority({
	    {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
	});

	get_render_context().request_present_mode(vk::PresentModeKHR::eMailbox);

	get_render_context().request_image_format(vk::Format::eB8G8R8A8Unorm);

	get_render_context().prepare();
}

void ApiVulkanSample::update(float delta_time)
{
	if (view_updated)
	{
		view_updated = false;
		view_changed();
	}

	update_overlay(delta_time);

	render(delta_time);
	camera.update(delta_time);
	if (camera.moving())
	{
		view_updated = true;
	}
}

void ApiVulkanSample::resize(const uint32_t, const uint32_t)
{
	if (!prepared)
	{
		return;
	}

	get_render_context().handle_surface_changes();

	// Don't recreate the swapchain if the dimensions haven't changed
	if (width == get_render_context().get_surface_extent().width && height == get_render_context().get_surface_extent().height)
	{
		return;
	}

	width  = get_render_context().get_surface_extent().width;
	height = get_render_context().get_surface_extent().height;

	prepared = false;

	// Ensure all operations on the device have been finished before destroying resources
	device->wait_idle();

	create_swapchain_buffers();

	// Recreate the frame buffers
	device->get_handle().destroy(depth_stencil.view);
	device->get_handle().destroy(depth_stencil.image);
	device->get_handle().freeMemory(depth_stencil.mem);
	setup_depth_stencil();
	for (uint32_t i = 0; i < framebuffers.size(); i++)
	{
		device->get_handle().destroy(framebuffers[i]);
	}
	setup_framebuffer();

	if ((width > 0.0f) && (height > 0.0f))
	{
		if (gui)
		{
			gui->resize(width, height);
		}
	}

	// Command buffers need to be recreated as they may store
	// references to the recreated frame buffer
	destroy_command_buffers();
	create_command_buffers();
	build_command_buffers();

	device->wait_idle();

	if ((width > 0.0f) && (height > 0.0f))
	{
		camera.update_aspect_ratio((float) width / (float) height);
	}

	// Notify derived class
	view_changed();

	prepared = true;
}

vkb::Device &ApiVulkanSample::get_device()
{
	return *device;
}

void ApiVulkanSample::input_event(const vkb::InputEvent &input_event)
{
	Application::input_event(input_event);

	bool gui_captures_event = false;

	if (gui)
	{
		gui_captures_event = gui->input_event(input_event);
	}

	if (!gui_captures_event)
	{
		if (input_event.get_source() == vkb::EventSource::Mouse)
		{
			const auto &mouse_button = static_cast<const vkb::MouseButtonInputEvent &>(input_event);

			handle_mouse_move(static_cast<int32_t>(mouse_button.get_pos_x()), static_cast<int32_t>(mouse_button.get_pos_y()));

			if (mouse_button.get_action() == vkb::MouseAction::Down)
			{
				switch (mouse_button.get_button())
				{
					case vkb::MouseButton::Left:
						mouse_buttons.left = true;
						break;
					case vkb::MouseButton::Right:
						mouse_buttons.right = true;
						break;
					case vkb::MouseButton::Middle:
						mouse_buttons.middle = true;
						break;
					default:
						break;
				}
			}
			else if (mouse_button.get_action() == vkb::MouseAction::Up)
			{
				switch (mouse_button.get_button())
				{
					case vkb::MouseButton::Left:
						mouse_buttons.left = false;
						break;
					case vkb::MouseButton::Right:
						mouse_buttons.right = false;
						break;
					case vkb::MouseButton::Middle:
						mouse_buttons.middle = false;
						break;
					default:
						break;
				}
			}
		}
		else if (input_event.get_source() == vkb::EventSource::Touchscreen)
		{
			const auto &touch_event = static_cast<const vkb::TouchInputEvent &>(input_event);

			if (touch_event.get_action() == vkb::TouchAction::Down)
			{
				touch_down         = true;
				touch_pos.x        = static_cast<int32_t>(touch_event.get_pos_x());
				touch_pos.y        = static_cast<int32_t>(touch_event.get_pos_y());
				mouse_pos.x        = touch_event.get_pos_x();
				mouse_pos.y        = touch_event.get_pos_y();
				mouse_buttons.left = true;
			}
			else if (touch_event.get_action() == vkb::TouchAction::Up)
			{
				touch_pos.x        = static_cast<int32_t>(touch_event.get_pos_x());
				touch_pos.y        = static_cast<int32_t>(touch_event.get_pos_y());
				touch_timer        = 0.0;
				touch_down         = false;
				camera.keys.up     = false;
				mouse_buttons.left = false;
			}
			else if (touch_event.get_action() == vkb::TouchAction::Move)
			{
				bool handled = false;
				if (gui)
				{
					ImGuiIO &io = ImGui::GetIO();
					handled     = io.WantCaptureMouse;
				}
				if (!handled)
				{
					int32_t eventX = static_cast<int32_t>(touch_event.get_pos_x());
					int32_t eventY = static_cast<int32_t>(touch_event.get_pos_y());

					float deltaX = (float) (touch_pos.y - eventY) * rotation_speed * 0.5f;
					float deltaY = (float) (touch_pos.x - eventX) * rotation_speed * 0.5f;

					camera.rotate(glm::vec3(deltaX, 0.0f, 0.0f));
					camera.rotate(glm::vec3(0.0f, -deltaY, 0.0f));

					rotation.x += deltaX;
					rotation.y -= deltaY;

					view_changed();

					touch_pos.x = eventX;
					touch_pos.y = eventY;
				}
			}
		}
		else if (input_event.get_source() == vkb::EventSource::Keyboard)
		{
			const auto &key_button = static_cast<const vkb::KeyInputEvent &>(input_event);

			if (key_button.get_action() == vkb::KeyAction::Down)
			{
				switch (key_button.get_code())
				{
					case vkb::KeyCode::W:
						camera.keys.up = true;
						break;
					case vkb::KeyCode::S:
						camera.keys.down = true;
						break;
					case vkb::KeyCode::A:
						camera.keys.left = true;
						break;
					case vkb::KeyCode::D:
						camera.keys.right = true;
						break;
					case vkb::KeyCode::P:
						paused = !paused;
						break;
					default:
						break;
				}
			}
			else if (key_button.get_action() == vkb::KeyAction::Up)
			{
				switch (key_button.get_code())
				{
					case vkb::KeyCode::W:
						camera.keys.up = false;
						break;
					case vkb::KeyCode::S:
						camera.keys.down = false;
						break;
					case vkb::KeyCode::A:
						camera.keys.left = false;
						break;
					case vkb::KeyCode::D:
						camera.keys.right = false;
						break;
					default:
						break;
				}
			}
		}
	}
}

void ApiVulkanSample::handle_mouse_move(int32_t x, int32_t y)
{
	int32_t dx = (int32_t) mouse_pos.x - x;
	int32_t dy = (int32_t) mouse_pos.y - y;

	bool handled = false;

	if (gui)
	{
		ImGuiIO &io = ImGui::GetIO();
		handled     = io.WantCaptureMouse;
	}
	mouse_moved((float) x, (float) y, handled);

	if (handled)
	{
		mouse_pos = glm::vec2((float) x, (float) y);
		return;
	}

	if (mouse_buttons.left)
	{
		rotation.x += dy * 1.25f * rotation_speed;
		rotation.y -= dx * 1.25f * rotation_speed;
		camera.rotate(glm::vec3(dy * camera.rotation_speed, -dx * camera.rotation_speed, 0.0f));
		view_updated = true;
	}
	if (mouse_buttons.right)
	{
		zoom += dy * .005f * zoom_speed;
		camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f * zoom_speed));
		view_updated = true;
	}
	if (mouse_buttons.middle)
	{
		camera_pos.x -= dx * 0.01f;
		camera_pos.y -= dy * 0.01f;
		camera.translate(glm::vec3(-dx * 0.01f, -dy * 0.01f, 0.0f));
		view_updated = true;
	}
	mouse_pos = glm::vec2((float) x, (float) y);
}

void ApiVulkanSample::mouse_moved(double x, double y, bool &handled)
{}

bool ApiVulkanSample::check_command_buffers()
{
	for (auto &command_buffer : draw_cmd_buffers)
	{
		if (!command_buffer)
		{
			return false;
		}
	}
	return true;
}

void ApiVulkanSample::create_command_buffers()
{
	// Create one command buffer for each swap chain image and reuse for rendering
	vk::CommandBufferAllocateInfo allocate_info =
	    vkb::initializers::command_buffer_allocate_info(
	        cmd_pool,
	        vk::CommandBufferLevel::ePrimary,
	        static_cast<uint32_t>(render_context->get_render_frames().size()));

	draw_cmd_buffers = device->get_handle().allocateCommandBuffers(allocate_info);
}

void ApiVulkanSample::destroy_command_buffers()
{
	device->get_handle().freeCommandBuffers(cmd_pool, draw_cmd_buffers);
}

void ApiVulkanSample::create_pipeline_cache()
{
	pipeline_cache = device->get_handle().createPipelineCache({});
}

vk::PipelineShaderStageCreateInfo ApiVulkanSample::load_shader(const std::string &file, vk::ShaderStageFlagBits stage)
{
	vk::PipelineShaderStageCreateInfo shader_stage;
	shader_stage.stage  = stage;
	shader_stage.module = vkb::load_shader(file.c_str(), device->get_handle(), stage);
	shader_stage.pName  = "main";
	assert(shader_stage.module.operator bool());
	shader_modules.push_back(shader_stage.module);
	return shader_stage;
}

void ApiVulkanSample::update_overlay(float delta_time)
{
	if (gui)
	{
		gui->show_simple_window(get_name(), vkb::to_u32(fps), [this]() {
			on_update_ui_overlay(gui->get_drawer());
		});

		gui->update(delta_time);

		if (gui->update_buffers() || gui->get_drawer().is_dirty())
		{
			build_command_buffers();
			gui->get_drawer().clear();
		}
	}
}

void ApiVulkanSample::draw_ui(const vk::CommandBuffer command_buffer)
{
	if (gui)
	{
		const vk::Viewport viewport = vkb::initializers::viewport(static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
		const vk::Rect2D   scissor  = vkb::initializers::rect2D(width, height, 0, 0);
		command_buffer.setViewport(0, viewport);
		command_buffer.setScissor(0, scissor);
		gui->draw(command_buffer);
	}
}

void ApiVulkanSample::prepare_frame()
{
	if (render_context->has_swapchain())
	{
		handle_surface_changes();
		// Acquire the next image from the swap chain
		vk::Result result = render_context->get_swapchain().acquire_next_image(current_buffer, semaphores.acquired_image_ready);
		// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
		if ((result == vk::Result::eErrorOutOfDateKHR) || (result == vk::Result::eSuboptimalKHR))
		{
			resize(width, height);
		}
		else
		{
			static_cast<VkResult>(result);
		}
	}
}

void ApiVulkanSample::submit_frame()
{
	if (render_context->has_swapchain())
	{
		const auto &queue = device->get_queue_by_present(0);

		vk::SwapchainKHR sc = render_context->get_swapchain().get_handle();

		vk::PresentInfoKHR present_info;
		present_info.pNext          = NULL;
		present_info.swapchainCount = 1;
		present_info.pSwapchains    = &sc;
		present_info.pImageIndices  = &current_buffer;
		// Check if a wait semaphore has been specified to wait for before presenting the image
		if (semaphores.render_complete.operator bool())
		{
			present_info.pWaitSemaphores    = &semaphores.render_complete;
			present_info.waitSemaphoreCount = 1;
		}

		vk::Result present_result = queue.present(present_info);

		if (!((present_result == vk::Result::eSuccess) || (present_result == vk::Result::eSuboptimalKHR)))
		{
			if (present_result == vk::Result::eErrorOutOfDateKHR)
			{
				// Swap chain is no longer compatible with the surface and needs to be recreated
				resize(width, height);
				return;
			}
			else
			{
				static_cast<VkResult>(present_result);
			}
		}
	}

	// DO NOT USE
	// vkDeviceWaitIdle and vkQueueWaitIdle are extremely expensive functions, and are used here purely for demonstrating the vulkan API
	// without having to concern ourselves with proper syncronization. These functions should NEVER be used inside the render loop like this (every frame).
	device->get_handle().waitIdle();
}

ApiVulkanSample::~ApiVulkanSample()
{
	if (device)
	{
		device->wait_idle();

		// Clean up Vulkan resources
		if (descriptor_pool.operator bool())
		{
			device->get_handle().destroy(descriptor_pool);
		}
		destroy_command_buffers();
		device->get_handle().destroy(render_pass);
		for (uint32_t i = 0; i < framebuffers.size(); i++)
		{
			device->get_handle().destroy(framebuffers[i]);
		}

		for (auto &swapchain_buffer : swapchain_buffers)
		{
			device->get_handle().destroy(swapchain_buffer.view);
		}

		for (auto &shader_module : shader_modules)
		{
			device->get_handle().destroy(shader_module);
		}
		device->get_handle().destroy(depth_stencil.view);
		device->get_handle().destroy(depth_stencil.image);
		device->get_handle().freeMemory(depth_stencil.mem);

		device->get_handle().destroy(pipeline_cache);

		device->get_handle().destroy(cmd_pool);

		device->get_handle().destroy(semaphores.acquired_image_ready);
		device->get_handle().destroy(semaphores.render_complete);
		for (auto &fence : wait_fences)
		{
			device->get_handle().destroy(fence);
		}
	}

	gui.reset();
}

void ApiVulkanSample::view_changed()
{}

void ApiVulkanSample::update_draw_command_buffer(const vk::CommandBuffer &draw_cmd_buffer)
{
	vk::Viewport viewport = vkb::initializers::viewport(static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
	draw_cmd_buffer.setViewport(0, 1, &viewport);

	vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
	draw_cmd_buffer.setScissor(0, 1, &scissor);
}

void ApiVulkanSample::build_command_buffers()
{
	// Destroy command buffers if already present
	if (!check_command_buffers())
	{
		destroy_command_buffers();
		create_command_buffers();
	}

	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ClearValue clear_values[2];
	clear_values[0].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
	clear_values[1].depthStencil = {0.0f, 0};

	vk::RenderPassBeginInfo render_pass_begin_info;
	render_pass_begin_info.renderPass               = render_pass;
	render_pass_begin_info.renderArea.extent.width  = width;
	render_pass_begin_info.renderArea.extent.height = height;
	render_pass_begin_info.clearValueCount          = 2;
	render_pass_begin_info.pClearValues             = clear_values;

	for (int32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		// Set target frame buffer
		render_pass_begin_info.framebuffer = framebuffers[i];
		auto &draw_cmd_buffer              = draw_cmd_buffers[i];

		draw_cmd_buffer.begin(command_buffer_begin_info);

		draw_cmd_buffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		update_draw_command_buffer(draw_cmd_buffer);

		draw_ui(draw_cmd_buffer);

		draw_cmd_buffer.endRenderPass();

		draw_cmd_buffer.end();
	}
}

void ApiVulkanSample::create_synchronization_primitives()
{
	// Wait fences to sync command buffer access
	vk::FenceCreateInfo fence_create_info = vkb::initializers::fence_create_info(vk::FenceCreateFlagBits::eSignaled);
	wait_fences.resize(draw_cmd_buffers.size());
	for (auto &fence : wait_fences)
	{
		fence = device->get_handle().createFence(fence_create_info);
	}
}

void ApiVulkanSample::create_command_pool()
{
	vk::CommandPoolCreateInfo command_pool_info = {};
	command_pool_info.queueFamilyIndex          = device->get_queue_by_flags(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, 0).get_family_index();
	command_pool_info.flags                     = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	cmd_pool                                    = device->get_handle().createCommandPool(command_pool_info);
}

void ApiVulkanSample::setup_depth_stencil()
{
	vk::ImageCreateInfo image_create_info;
	image_create_info.imageType   = vk::ImageType::e2D;
	image_create_info.format      = depth_format;
	image_create_info.extent      = {get_render_context().get_surface_extent().width, get_render_context().get_surface_extent().height, 1};
	image_create_info.mipLevels   = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples     = vk::SampleCountFlagBits::e1;
	image_create_info.tiling      = vk::ImageTiling::eOptimal;
	image_create_info.usage       = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc;

	depth_stencil.image = device->get_handle().createImage(image_create_info);

	vk::MemoryRequirements memReqs = device->get_handle().getImageMemoryRequirements(depth_stencil.image);

	vk::MemoryAllocateInfo memory_allocation;
	memory_allocation.allocationSize  = memReqs.size;
	memory_allocation.memoryTypeIndex = device->get_memory_type(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

	depth_stencil.mem = device->get_handle().allocateMemory(memory_allocation);
	device->get_handle().bindImageMemory(depth_stencil.image, depth_stencil.mem, 0);

	vk::ImageViewCreateInfo image_view_create_info;
	image_view_create_info.viewType                        = vk::ImageViewType::e2D;
	image_view_create_info.image                           = depth_stencil.image;
	image_view_create_info.format                          = depth_format;
	image_view_create_info.subresourceRange.baseMipLevel   = 0;
	image_view_create_info.subresourceRange.levelCount     = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount     = 1;
	image_view_create_info.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eDepth;
	// Stencil aspect should only be set on depth + stencil formats (vk::Format::eD16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8Uint
	if (depth_format >= vk::Format::eD16UnormS8Uint)
	{
		image_view_create_info.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
	}
	depth_stencil.view = device->get_handle().createImageView(image_view_create_info);
}

void ApiVulkanSample::setup_framebuffer()
{
	vk::ImageView attachments[2];

	// Depth/Stencil attachment is the same for all frame buffers
	attachments[1] = depth_stencil.view;

	vk::FramebufferCreateInfo framebuffer_create_info = {};
	framebuffer_create_info.renderPass                = render_pass;
	framebuffer_create_info.attachmentCount           = 2;
	framebuffer_create_info.pAttachments              = attachments;
	framebuffer_create_info.width                     = get_render_context().get_surface_extent().width;
	framebuffer_create_info.height                    = get_render_context().get_surface_extent().height;
	framebuffer_create_info.layers                    = 1;

	// Create frame buffers for every swap chain image
	framebuffers.resize(render_context->get_render_frames().size());
	for (uint32_t i = 0; i < framebuffers.size(); i++)
	{
		attachments[0]  = swapchain_buffers[i].view;
		framebuffers[i] = device->get_handle().createFramebuffer(framebuffer_create_info);
	}
}

void ApiVulkanSample::setup_render_pass()
{
	std::array<vk::AttachmentDescription, 2> attachments = {};
	// Color attachment
	attachments[0].format         = render_context->get_format();
	attachments[0].samples        = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp         = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp        = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout  = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout    = vk::ImageLayout::ePresentSrcKHR;
	// Depth attachment
	attachments[1].format         = depth_format;
	attachments[1].samples        = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp         = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp        = vk::AttachmentStoreOp::eDontCare;
	attachments[1].stencilLoadOp  = vk::AttachmentLoadOp::eClear;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout  = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::AttachmentReference color_reference = {};
	color_reference.attachment              = 0;
	color_reference.layout                  = vk::ImageLayout::eColorAttachmentOptimal;

	vk::AttachmentReference depth_reference = {};
	depth_reference.attachment              = 1;
	depth_reference.layout                  = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::SubpassDescription subpass_description  = {};
	subpass_description.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
	subpass_description.colorAttachmentCount    = 1;
	subpass_description.pColorAttachments       = &color_reference;
	subpass_description.pDepthStencilAttachment = &depth_reference;
	subpass_description.inputAttachmentCount    = 0;
	subpass_description.pInputAttachments       = nullptr;
	subpass_description.preserveAttachmentCount = 0;
	subpass_description.pPreserveAttachments    = nullptr;
	subpass_description.pResolveAttachments     = nullptr;

	// Subpass dependencies for layout transitions
	std::array<vk::SubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass      = 0;
	dependencies[0].srcStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[0].dstStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask   = vk::AccessFlagBits::eMemoryRead;
	dependencies[0].dstAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	dependencies[1].srcSubpass      = 0;
	dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
	dependencies[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask   = vk::AccessFlagBits::eMemoryRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	vk::RenderPassCreateInfo render_pass_create_info = {};
	render_pass_create_info.attachmentCount          = static_cast<uint32_t>(attachments.size());
	render_pass_create_info.pAttachments             = attachments.data();
	render_pass_create_info.subpassCount             = 1;
	render_pass_create_info.pSubpasses               = &subpass_description;
	render_pass_create_info.dependencyCount          = static_cast<uint32_t>(dependencies.size());
	render_pass_create_info.pDependencies            = dependencies.data();

	render_pass = device->get_handle().createRenderPass(render_pass_create_info);
}

void ApiVulkanSample::on_update_ui_overlay(vkb::Drawer &drawer)
{}

void ApiVulkanSample::create_swapchain_buffers()
{
	if (render_context->has_swapchain())
	{
		auto &images = render_context->get_swapchain().get_images();

		// Get the swap chain buffers containing the image and imageview
		for (auto &swapchain_buffer : swapchain_buffers)
		{
			device->get_handle().destroy(swapchain_buffer.view);
		}
		swapchain_buffers.clear();
		swapchain_buffers.resize(images.size());
		for (uint32_t i = 0; i < images.size(); i++)
		{
			vk::ImageViewCreateInfo color_attachment_view;
			color_attachment_view.format                      = render_context->get_swapchain().get_format();
			color_attachment_view.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			color_attachment_view.subresourceRange.levelCount = 1;
			color_attachment_view.subresourceRange.layerCount = 1;
			color_attachment_view.viewType                    = vk::ImageViewType::e2D;

			swapchain_buffers[i].image = images[i];

			color_attachment_view.image = swapchain_buffers[i].image;

			swapchain_buffers[i].view = device->get_handle().createImageView(color_attachment_view);
		}
	}
	else
	{
		auto &frames = render_context->get_render_frames();

		// Get the swap chain buffers containing the image and imageview
		swapchain_buffers.clear();
		swapchain_buffers.resize(frames.size());
		for (uint32_t i = 0; i < frames.size(); i++)
		{
			auto &image_view = *frames[i].get_render_target().get_views().begin();

			swapchain_buffers[i].image = image_view.get_image().get_handle();
			swapchain_buffers[i].view  = image_view.get_handle();
		}
	}
}

void ApiVulkanSample::handle_surface_changes()
{
	vk::SurfaceCapabilitiesKHR surface_properties = device->get_physical_device().getSurfaceCapabilitiesKHR(get_render_context().get_swapchain().get_surface());

	if (surface_properties.currentExtent != get_render_context().get_surface_extent())
	{
		resize(surface_properties.currentExtent.width, surface_properties.currentExtent.height);
	}
}

vk::DescriptorBufferInfo ApiVulkanSample::create_descriptor(vkb::core::Buffer &buffer, vk::DeviceSize size, vk::DeviceSize offset)
{
	vk::DescriptorBufferInfo descriptor;
	descriptor.buffer = buffer.get_handle();
	descriptor.range  = size;
	descriptor.offset = offset;
	return descriptor;
}

vk::DescriptorImageInfo ApiVulkanSample::create_descriptor(Texture &texture, vk::DescriptorType descriptor_type)
{
	vk::DescriptorImageInfo descriptor;
	descriptor.sampler   = texture.sampler;
	descriptor.imageView = texture.image->get_vk_image_view().get_handle();

	// Add image layout info based on descriptor type
	switch (descriptor_type)
	{
		case vk::DescriptorType::eCombinedImageSampler:
		case vk::DescriptorType::eInputAttachment:
			if (vkb::is_depth_stencil_format(texture.image->get_vk_image_view().get_format()))
			{
				descriptor.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
			}
			else
			{
				descriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			}
			break;
		case vk::DescriptorType::eStorageImage:
			descriptor.imageLayout = vk::ImageLayout::eGeneral;
			break;
		default:
			descriptor.imageLayout = vk::ImageLayout::eUndefined;
			break;
	}

	return descriptor;
}

Texture ApiVulkanSample::load_texture(const std::string &file, vk::ImageViewType image_view_type, const vk::ImageCreateFlags &flags)
{
	Texture texture{};

	texture.image = vkb::sg::Image::load(file, file);
	texture.image->create_vk_image(*device, image_view_type, flags);

	std::vector<vk::BufferImageCopy> buffer_copy_regions = texture.image->get_buffer_copy_regions();

	get_device().stage_to_image(
	    texture.image->get_data(),
	    buffer_copy_regions,
	    texture.image->get_vk_image());

	// Create a defaultsampler
	vk::SamplerCreateInfo sampler_create_info = {};
	sampler_create_info.magFilter             = vk::Filter::eLinear;
	sampler_create_info.minFilter             = vk::Filter::eLinear;
	sampler_create_info.mipmapMode            = vk::SamplerMipmapMode::eLinear;
	sampler_create_info.addressModeU          = vk::SamplerAddressMode::eRepeat;
	sampler_create_info.addressModeV          = vk::SamplerAddressMode::eRepeat;
	sampler_create_info.addressModeW          = vk::SamplerAddressMode::eRepeat;
	// Max level-of-detail should match mip level count
	sampler_create_info.maxLod = static_cast<float>(texture.image->get_mipmaps().size());
	// Only enable anisotropic filtering if enabled on the device
	// Note that for simplicity, we will always be using max. available anisotropy level for the current device
	// This may have an impact on performance, esp. on lower-specced devices
	// In a real-world scenario the level of anisotropy should be a user setting or e.g. lowered for mobile devices by default
	sampler_create_info.maxAnisotropy    = device->get_features().samplerAnisotropy ? (device->get_properties().limits.maxSamplerAnisotropy) : 1.0f;
	sampler_create_info.anisotropyEnable = device->get_features().samplerAnisotropy;
	sampler_create_info.borderColor      = vk::BorderColor::eFloatOpaqueWhite;
	texture.sampler                      = device->get_handle().createSampler(sampler_create_info);

	return texture;
}

Texture ApiVulkanSample::load_texture(const std::string &file)
{
	return load_texture(file, vk::ImageViewType::e2D);
}

Texture ApiVulkanSample::load_texture_array(const std::string &file)
{
	return load_texture(file, vk::ImageViewType::e2DArray);
}

Texture ApiVulkanSample::load_texture_cubemap(const std::string &file)
{
	return load_texture(file, vk::ImageViewType::eCube, vk::ImageCreateFlagBits::eCubeCompatible);
}

std::unique_ptr<vkb::sg::SubMesh> ApiVulkanSample::load_model(const std::string &file, uint32_t index)
{
	vkb::GLTFLoader loader{*device};

	std::unique_ptr<vkb::sg::SubMesh> model = loader.read_model_from_file(file, index);

	if (!model)
	{
		LOGE("Cannot load model from file: {}", file.c_str());
		throw std::runtime_error("Cannot load model from: " + file);
	}

	return model;
}

void ApiVulkanSample::draw_model(const std::unique_ptr<vkb::sg::SubMesh> &model, vk::CommandBuffer command_buffer)
{
	const auto &vertex_buffer = model->vertex_buffers.at("vertex_buffer");
	const auto &index_buffer  = model->index_buffer;

	command_buffer.bindVertexBuffers(0, vertex_buffer.get_handle(), {0});
	command_buffer.bindIndexBuffer(index_buffer->get_handle(), 0, model->index_type);
	command_buffer.drawIndexed(model->vertex_indices, 1, 0, 0, 0);
}

const std::vector<const char *> ApiVulkanSample::get_instance_extensions()
{
	return instance_extensions;
}

const std::vector<const char *> ApiVulkanSample::get_device_extensions()
{
	return device_extensions;
}
