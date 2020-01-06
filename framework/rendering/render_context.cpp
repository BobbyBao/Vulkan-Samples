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

#include "render_context.h"

namespace vkb
{
vk::Format RenderContext::DEFAULT_VK_FORMAT = vk::Format::eR8G8B8A8Srgb;

RenderContext::RenderContext(Device &device, vk::SurfaceKHR surface, uint32_t window_width, uint32_t window_height) :
    device{device},
    queue{device.get_suitable_graphics_queue()},
    surface_extent{window_width, window_height}
{
	if (surface)
	{
		swapchain = std::make_unique<Swapchain>(device, surface);
	}
}

void RenderContext::request_present_mode(const vk::PresentModeKHR present_mode)
{
	if (swapchain)
	{
		auto &properties        = swapchain->get_properties();
		properties.present_mode = present_mode;
	}
}

void RenderContext::request_image_format(const vk::Format format)
{
	if (swapchain)
	{
		auto &properties                 = swapchain->get_properties();
		properties.surface_format.format = format;
	}
}

void RenderContext::prepare(size_t thread_count, RenderTarget::CreateFunc create_render_target_func)
{
	device.wait_idle();

	if (swapchain)
	{
		swapchain->set_present_mode_priority(present_mode_priority_list);
		swapchain->set_surface_format_priority(surface_format_priority_list);
		swapchain->create();

		surface_extent = swapchain->get_extent();

		vk::Extent3D extent{surface_extent.width, surface_extent.height, 1};

		for (auto &image_handle : swapchain->get_images())
		{
			auto swapchain_image = core::Image{
			    device, image_handle,
			    extent,
			    swapchain->get_format(),
			    swapchain->get_usage()};
			auto render_target = create_render_target_func(std::move(swapchain_image));
			frames.emplace_back(RenderFrame{device, std::move(render_target), thread_count});
		}
	}
	else
	{
		// Otherwise, create a single RenderFrame
		swapchain = nullptr;

		auto color_image = core::Image{device,
		                               vk::Extent3D{surface_extent.width, surface_extent.height, 1},
		                               DEFAULT_VK_FORMAT,        // We can use any format here that we like
		                               vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		                               vma::MemoryUsage::eGpuOnly};

		auto render_target = create_render_target_func(std::move(color_image));
		frames.emplace_back(RenderFrame{device, std::move(render_target), thread_count});
	}

	this->create_render_target_func = create_render_target_func;
	this->thread_count              = thread_count;
	this->prepared                  = true;
}

void RenderContext::set_present_mode_priority(const std::vector<vk::PresentModeKHR> &new_present_mode_priority_list)
{
	this->present_mode_priority_list = new_present_mode_priority_list;
}

void RenderContext::set_surface_format_priority(const std::vector<vk::SurfaceFormatKHR> &new_surface_format_priority_list)
{
	this->surface_format_priority_list = new_surface_format_priority_list;
}

vk::Format RenderContext::get_format()
{
	vk::Format format = DEFAULT_VK_FORMAT;

	if (swapchain)
	{
		format = swapchain->get_format();
	}

	return format;
}

void RenderContext::update_swapchain(const vk::Extent2D &extent)
{
	if (!swapchain)
	{
		LOGW("Can't update the swapchains extent in headless mode, skipping.");
		return;
	}

	device.get_resource_cache().clear_framebuffers();

	swapchain = std::make_unique<Swapchain>(*swapchain, extent);

	recreate();
}

void RenderContext::update_swapchain(const uint32_t image_count)
{
	if (!swapchain)
	{
		LOGW("Can't update the swapchains image count in headless mode, skipping.");
		return;
	}

	device.get_resource_cache().clear_framebuffers();

	device.wait_idle();

	swapchain = std::make_unique<Swapchain>(*swapchain, image_count);

	recreate();
}

void RenderContext::update_swapchain(const std::set<vk::ImageUsageFlagBits> &image_usage_flags)
{
	if (!swapchain)
	{
		LOGW("Can't update the swapchains image usage in headless mode, skipping.");
		return;
	}

	device.get_resource_cache().clear_framebuffers();

	swapchain = std::make_unique<Swapchain>(*swapchain, image_usage_flags);

	recreate();
}

void RenderContext::update_swapchain(const vk::Extent2D &extent, const vk::SurfaceTransformFlagBitsKHR transform)
{
	if (!swapchain)
	{
		LOGW("Can't update the swapchains extent and surface transform in headless mode, skipping.");
		return;
	}

	device.get_resource_cache().clear_framebuffers();

	auto width  = extent.width;
	auto height = extent.height;
	if (transform == vk::SurfaceTransformFlagBitsKHR::eRotate90 || transform == vk::SurfaceTransformFlagBitsKHR::eRotate270)
	{
		// Pre-rotation: always use native orientation i.e. if rotated, use width and height of identity transform
		std::swap(width, height);
	}

	swapchain = std::make_unique<Swapchain>(*swapchain, vk::Extent2D{width, height}, transform);

	// Save the preTransform attribute for future rotations
	pre_transform = transform;

	recreate();
}

void RenderContext::recreate()
{
	LOGI("Recreated swapchain");

	vk::Extent2D swapchain_extent = swapchain->get_extent();
	vk::Extent3D extent{swapchain_extent.width, swapchain_extent.height, 1};

	auto frame_it = frames.begin();

	for (auto &image_handle : swapchain->get_images())
	{
		core::Image swapchain_image{device, image_handle,
		                            extent,
		                            swapchain->get_format(),
		                            swapchain->get_usage()};

		auto render_target = create_render_target_func(std::move(swapchain_image));

		if (frame_it != frames.end())
		{
			frame_it->update_render_target(std::move(render_target));
		}
		else
		{
			// Create a new frame if the new swapchain has more images than current frames
			frames.emplace_back(RenderFrame{device, std::move(render_target), thread_count});
		}

		++frame_it;
	}
}

void RenderContext::handle_surface_changes()
{
	if (!swapchain)
	{
		LOGW("Can't handle surface changes in headless mode, skipping.");
		return;
	}

	vk::SurfaceCapabilitiesKHR surface_properties = device.get_physical_device().getSurfaceCapabilitiesKHR(swapchain->get_surface());

	// Only recreate the swapchain if the dimensions have changed;
	// handle_surface_changes() is called on VK_SUBOPTIMAL_KHR,
	// which might not be due to a surface resize
	if (surface_properties.currentExtent != surface_extent)
	{
		// Recreate swapchain
		device.wait_idle();

		update_swapchain(surface_properties.currentExtent, pre_transform);

		surface_extent = surface_properties.currentExtent;
	}
}

CommandBuffer &RenderContext::begin(CommandBuffer::ResetMode reset_mode)
{
	assert(prepared && "RenderContext not prepared for rendering, call prepare()");

	acquired_semaphore = begin_frame();

	if (!acquired_semaphore)
	{
		throw std::runtime_error("Couldn't begin frame");
	}

	const auto &queue = device.get_queue_by_flags(vk::QueueFlagBits::eGraphics, 0);
	return get_active_frame().request_command_buffer(queue, reset_mode);
}

void RenderContext::submit(CommandBuffer &command_buffer)
{
	assert(frame_active && "RenderContext is inactive, cannot submit command buffer. Please call begin()");

	vk::Semaphore render_semaphore;

	if (swapchain)
	{
		render_semaphore = submit(queue, command_buffer, acquired_semaphore, vk::PipelineStageFlagBits::eColorAttachmentOutput);
	}
	else
	{
		submit(queue, command_buffer);
	}

	end_frame(render_semaphore);

	acquired_semaphore = nullptr;
}

vk::Semaphore RenderContext::begin_frame()
{
	// Only handle surface changes if a swapchain exists
	if (swapchain)
	{
		handle_surface_changes();
	}

	assert(!frame_active && "Frame is still active, please call end_frame");

	auto &prev_frame = frames.at(active_frame_index);

	auto aquired_semaphore = prev_frame.request_semaphore();

	if (swapchain)
	{
		auto fence = prev_frame.request_fence();

		auto result = swapchain->acquire_next_image(active_frame_index, aquired_semaphore, fence);

		if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
		{
			handle_surface_changes();

			result = swapchain->acquire_next_image(active_frame_index, aquired_semaphore, fence);
		}

		if (result != vk::Result::eSuccess)
		{
			prev_frame.reset();

			return nullptr;
		}
	}

	// Now the frame is active again
	frame_active = true;

	wait_frame();

	return aquired_semaphore;
}

vk::Semaphore RenderContext::submit(const Queue &queue, const CommandBuffer &command_buffer, vk::Semaphore wait_semaphore, vk::PipelineStageFlags wait_pipeline_stage)
{
	RenderFrame &frame = get_active_frame();

	vk::Semaphore signal_semaphore = frame.request_semaphore();

	vk::CommandBuffer cmd_buf = command_buffer.get_handle();

	vk::SubmitInfo submit_info;

	submit_info.commandBufferCount   = 1;
	submit_info.pCommandBuffers      = &cmd_buf;
	submit_info.waitSemaphoreCount   = 1;
	submit_info.pWaitSemaphores      = &wait_semaphore;
	submit_info.pWaitDstStageMask    = &wait_pipeline_stage;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = &signal_semaphore;

	vk::Fence fence = frame.request_fence();

	queue.get_handle().submit(submit_info, fence);

	return signal_semaphore;
}

void RenderContext::submit(const Queue &queue, const CommandBuffer &command_buffer)
{
	RenderFrame &frame = get_active_frame();

	vk::CommandBuffer cmd_buf = command_buffer.get_handle();

	vk::SubmitInfo submit_info;

	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &cmd_buf;

	vk::Fence fence = frame.request_fence();

	queue.get_handle().submit(submit_info, fence);
}

void RenderContext::wait_frame()
{
	RenderFrame &frame = get_active_frame();
	frame.reset();
}

void RenderContext::end_frame(vk::Semaphore semaphore)
{
	assert(frame_active && "Frame is not active, please call begin_frame");

	if (swapchain)
	{
		vk::SwapchainKHR vk_swapchain = swapchain->get_handle();

		vk::PresentInfoKHR present_info;

		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores    = &semaphore;
		present_info.swapchainCount     = 1;
		present_info.pSwapchains        = &vk_swapchain;
		present_info.pImageIndices      = &active_frame_index;

		vk::Result result = queue.present(present_info);

		if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
		{
			handle_surface_changes();
		}
	}

	// Frame is not active anymore
	frame_active = false;
}

RenderFrame &RenderContext::get_active_frame()
{
	assert(frame_active && "Frame is not active, please call begin_frame");
	return frames.at(active_frame_index);
}

uint32_t RenderContext::get_active_frame_index()
{
	assert(frame_active && "Frame is not active, please call begin_frame");
	return active_frame_index;
}

RenderFrame &RenderContext::get_last_rendered_frame()
{
	assert(!frame_active && "Frame is still active, please call end_frame");
	return frames.at(active_frame_index);
}

vk::Semaphore RenderContext::request_semaphore()
{
	RenderFrame &frame = get_active_frame();
	return frame.request_semaphore();
}

Device &RenderContext::get_device()
{
	return device;
}

void RenderContext::recreate_swapchain()
{
	device.wait_idle();
	device.get_resource_cache().clear_framebuffers();

	vk::Extent2D swapchain_extent = swapchain->get_extent();
	vk::Extent3D extent{swapchain_extent.width, swapchain_extent.height, 1};

	auto frame_it = frames.begin();

	for (auto &image_handle : swapchain->get_images())
	{
		core::Image swapchain_image{device, image_handle,
		                            extent,
		                            swapchain->get_format(),
		                            swapchain->get_usage()};

		auto render_target = create_render_target_func(std::move(swapchain_image));
		frame_it->update_render_target(std::move(render_target));

		++frame_it;
	}
}

bool RenderContext::has_swapchain()
{
	return swapchain != nullptr;
}

Swapchain &RenderContext::get_swapchain()
{
	assert(swapchain && "Swapchain is not valid");
	return *swapchain;
}

vk::Extent2D RenderContext::get_surface_extent() const
{
	return surface_extent;
}

uint32_t RenderContext::get_active_frame_index() const
{
	return active_frame_index;
}

std::vector<RenderFrame> &RenderContext::get_render_frames()
{
	return frames;
}

}        // namespace vkb
