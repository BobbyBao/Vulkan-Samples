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

#include "command_buffer.h"

#include "command_pool.h"
#include "common/error.h"
#include "device.h"
#include "rendering/render_frame.h"
#include "rendering/shader_program.h"
#include "rendering/subpass.h"

namespace vkb
{
CommandBuffer::CommandBuffer(CommandPool &command_pool, vk::CommandBufferLevel level) :
    command_pool{command_pool},
    level{level}
{
	auto &device = command_pool.get_device();
	auto &self   = static_cast<vk::CommandBuffer &>(*this);
	self         = device.get_handle().allocateCommandBuffers(vk::CommandBufferAllocateInfo{command_pool.get_handle(), level, 1})[0];
}

CommandBuffer::~CommandBuffer()
{
	auto &device = command_pool.get_device();
	auto &self   = static_cast<vk::CommandBuffer &>(*this);
	// Destroy command buffer
	if (self)
	{
		device.get_handle().freeCommandBuffers(command_pool, self);
	}
}

CommandBuffer::CommandBuffer(CommandBuffer &&other) :
    command_pool{other.command_pool},
    level{other.level},
    state{other.state}
{
	static_cast<vk::CommandBuffer &>(other) = nullptr;
	other.state                             = State::Invalid;
}

Device &CommandBuffer::get_device()
{
	return command_pool.get_device();
}

const vk::CommandBuffer &CommandBuffer::get_handle() const
{
	return static_cast<const vk::CommandBuffer &>(*this);
}

bool CommandBuffer::is_recording() const
{
	return state == State::Recording;
}

void CommandBuffer::clear(vk::ClearAttachment attachment, vk::ClearRect rect)
{
	clearAttachments(attachment, rect);
}

vk::Result CommandBuffer::begin(vk::CommandBufferUsageFlags flags, CommandBuffer *primary_cmd_buf)
{
	assert(!is_recording() && "Command buffer is already recording, please call end before beginning again");

	if (is_recording())
	{
		return vk::Result::eNotReady;
	}

	state = State::Recording;

	// Reset state
	pipeline_state.reset();
	resource_binding_state.reset();
	descriptor_set_layout_binding_state.clear();
	stored_push_constants.clear();

	vk::CommandBufferBeginInfo       begin_info;
	vk::CommandBufferInheritanceInfo inheritance;
	begin_info.flags = flags;

	if (level == vk::CommandBufferLevel::eSecondary)
	{
		assert(primary_cmd_buf && "A primary command buffer pointer must be provided when calling begin from a secondary one");

		auto render_pass_binding        = primary_cmd_buf->get_current_render_pass();
		current_render_pass.render_pass = render_pass_binding.render_pass;
		current_render_pass.framebuffer = render_pass_binding.framebuffer;

		inheritance.renderPass  = current_render_pass.render_pass->get_handle();
		inheritance.framebuffer = current_render_pass.framebuffer->get_handle();
		inheritance.subpass     = primary_cmd_buf->get_current_subpass_index();

		begin_info.pInheritanceInfo = &inheritance;
	}

	auto &self = static_cast<vk::CommandBuffer &>(*this);
	self.begin(begin_info);
	return vk::Result::eSuccess;
}

vk::Result CommandBuffer::end()
{
	assert(is_recording() && "Command buffer is not recording, please call begin before end");

	if (!is_recording())
	{
		return vk::Result::eNotReady;
	}
	auto &self = static_cast<vk::CommandBuffer &>(*this);
	self.end();

	state = State::Executable;

	return vk::Result::eSuccess;
}

void CommandBuffer::begin_render_pass(const RenderTarget &render_target, const std::vector<LoadStoreInfo> &load_store_infos, const std::vector<vk::ClearValue> &clear_values, const std::vector<std::unique_ptr<Subpass>> &subpasses, vk::SubpassContents contents)
{
	// Reset state
	pipeline_state.reset();
	resource_binding_state.reset();
	descriptor_set_layout_binding_state.clear();

	// Create render pass
	assert(subpasses.size() > 0 && "Cannot create a render pass without any subpass");
	std::vector<SubpassInfo> subpass_infos(subpasses.size());
	auto                     subpass_info_it = subpass_infos.begin();
	for (auto &subpass : subpasses)
	{
		subpass_info_it->input_attachments  = subpass->get_input_attachments();
		subpass_info_it->output_attachments = subpass->get_output_attachments();

		++subpass_info_it;
	}
	current_render_pass.render_pass = &get_device().get_resource_cache().request_render_pass(render_target.get_attachments(), load_store_infos, subpass_infos);
	current_render_pass.framebuffer = &get_device().get_resource_cache().request_framebuffer(render_target, *current_render_pass.render_pass);

	// Begin render pass
	vk::RenderPassBeginInfo begin_info;
	begin_info.renderPass        = current_render_pass.render_pass->get_handle();
	begin_info.framebuffer       = current_render_pass.framebuffer->get_handle();
	begin_info.renderArea.extent = render_target.get_extent();
	begin_info.clearValueCount   = to_u32(clear_values.size());
	begin_info.pClearValues      = clear_values.data();

	const auto &framebuffer_extent = current_render_pass.framebuffer->get_extent();

	// Test the requested render area to confirm that it is optimal and could not cause a performance reduction
	if (!is_render_size_optimal(framebuffer_extent, begin_info.renderArea))
	{
		// Only prints the warning if the framebuffer or render area are different since the last time the render size was not optimal
		if (framebuffer_extent.width != last_framebuffer_extent.width || framebuffer_extent.height != last_framebuffer_extent.height ||
		    begin_info.renderArea.extent.width != last_render_area_extent.width || begin_info.renderArea.extent.height != last_render_area_extent.height)
		{
			LOGW("Render target extent is not an optimal size, this may result in reduced performance.");
		}

		last_framebuffer_extent = current_render_pass.framebuffer->get_extent();
		last_render_area_extent = begin_info.renderArea.extent;
	}

	get_handle().beginRenderPass(begin_info, contents);

	// Update blend state attachments for first subpass
	auto blend_state = pipeline_state.get_color_blend_state();
	blend_state.attachments.resize(current_render_pass.render_pass->get_color_output_count(pipeline_state.get_subpass_index()));
	pipeline_state.set_color_blend_state(blend_state);
}

void CommandBuffer::next_subpass()
{
	// Increment subpass index
	pipeline_state.set_subpass_index(pipeline_state.get_subpass_index() + 1);

	// Update blend state attachments
	auto blend_state = pipeline_state.get_color_blend_state();
	blend_state.attachments.resize(current_render_pass.render_pass->get_color_output_count(pipeline_state.get_subpass_index()));
	pipeline_state.set_color_blend_state(blend_state);

	// Reset descriptor sets
	resource_binding_state.reset();
	descriptor_set_layout_binding_state.clear();

	// Clear stored push constants
	stored_push_constants.clear();

	auto &self = static_cast<vk::CommandBuffer &>(*this);
	self.nextSubpass(vk::SubpassContents::eInline);
}

void CommandBuffer::execute_commands(const vk::ArrayProxy<const vk::CommandBuffer> &secondary_command_buffer)
{
	executeCommands(secondary_command_buffer);
}

void CommandBuffer::execute_commands(std::vector<CommandBuffer *> &secondary_command_buffers)
{
	std::vector<vk::CommandBuffer> sec_cmd_buf_handles(secondary_command_buffers.size());
	std::transform(secondary_command_buffers.begin(), secondary_command_buffers.end(), sec_cmd_buf_handles.begin(),
	               [](const vkb::CommandBuffer *sec_cmd_buf) { return sec_cmd_buf->get_handle(); });
	executeCommands(sec_cmd_buf_handles);
}

void CommandBuffer::end_render_pass()
{
	endRenderPass();
}

void CommandBuffer::bind_pipeline_layout(PipelineLayout &pipeline_layout)
{
	pipeline_state.set_pipeline_layout(pipeline_layout);
}

void CommandBuffer::set_specialization_constant(uint32_t constant_id, const std::vector<uint8_t> &data)
{
	pipeline_state.set_specialization_constant(constant_id, data);
}

void CommandBuffer::set_push_constants(const std::vector<uint8_t> &values)
{
	stored_push_constants.insert(stored_push_constants.end(), values.begin(), values.end());
}

void CommandBuffer::push_constants_accumulated(const std::vector<uint8_t> &values, uint32_t offset)
{
	auto accumulated_values = stored_push_constants;
	accumulated_values.insert(accumulated_values.end(), values.begin(), values.end());

	push_constants(offset, accumulated_values);
}

void CommandBuffer::push_constants(uint32_t offset, const std::vector<uint8_t> &values)
{
	auto accumulated_values = stored_push_constants;
	accumulated_values.insert(accumulated_values.end(), values.begin(), values.end());

	const PipelineLayout &pipeline_layout = pipeline_state.get_pipeline_layout();

	vk::ShaderStageFlags shader_stage = pipeline_layout.get_push_constant_range_stage(offset, to_u32(values.size()));

	if (shader_stage)
	{
		pushConstants<uint8_t>(pipeline_layout.get_handle(), shader_stage, offset, values);
	}
	else
	{
		LOGW("Push constant range [{}, {}] not found", offset, values.size());
	}
}

void CommandBuffer::bind_buffer(const core::Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize range, uint32_t set, uint32_t binding, uint32_t array_element)
{
	resource_binding_state.bind_buffer(buffer, offset, range, set, binding, array_element);
}

void CommandBuffer::bind_image(const core::ImageView &image_view, const core::Sampler &sampler, uint32_t set, uint32_t binding, uint32_t array_element)
{
	resource_binding_state.bind_image(image_view, sampler, set, binding, array_element);
}

void CommandBuffer::bind_input(const core::ImageView &image_view, uint32_t set, uint32_t binding, uint32_t array_element)
{
	resource_binding_state.bind_input(image_view, set, binding, array_element);
}

void CommandBuffer::bind_vertex_buffers(uint32_t first_binding, const vk::ArrayProxy<const vk::Buffer> &buffers, const vk::ArrayProxy<const vk::DeviceSize> &offsets)
{
	bindVertexBuffers(first_binding, buffers, offsets);
}

void CommandBuffer::bind_index_buffer(const core::Buffer &buffer, vk::DeviceSize offset, vk::IndexType index_type)
{
	bindIndexBuffer(buffer.get_handle(), offset, index_type);
}

void CommandBuffer::set_viewport_state(const ViewportState &state_info)
{
	pipeline_state.set_viewport_state(state_info);
}

void CommandBuffer::set_vertex_input_state(const VertexInputState &state_info)
{
	pipeline_state.set_vertex_input_state(state_info);
}

void CommandBuffer::set_input_assembly_state(const InputAssemblyState &state_info)
{
	pipeline_state.set_input_assembly_state(state_info);
}

void CommandBuffer::set_rasterization_state(const RasterizationState &state_info)
{
	pipeline_state.set_rasterization_state(state_info);
}

void CommandBuffer::set_multisample_state(const MultisampleState &state_info)
{
	pipeline_state.set_multisample_state(state_info);
}

void CommandBuffer::set_depth_stencil_state(const DepthStencilState &state_info)
{
	pipeline_state.set_depth_stencil_state(state_info);
}

void CommandBuffer::set_color_blend_state(const ColorBlendState &state_info)
{
	pipeline_state.set_color_blend_state(state_info);
}

void CommandBuffer::set_viewport(uint32_t first_viewport, const vk::ArrayProxy<const vk::Viewport> &viewports)
{
	setViewport(first_viewport, viewports);
}

void CommandBuffer::set_scissor(uint32_t first_scissor, const vk::ArrayProxy<const vk::Rect2D> &scissors)
{
	setScissor(first_scissor, scissors);
}

void CommandBuffer::set_line_width(float line_width)
{
	setLineWidth(line_width);
}

void CommandBuffer::set_depth_bias(float depth_bias_constant_factor, float depth_bias_clamp, float depth_bias_slope_factor)
{
	setDepthBias(depth_bias_constant_factor, depth_bias_clamp, depth_bias_slope_factor);
}

void CommandBuffer::set_blend_constants(const std::array<float, 4> &blend_constants)
{
	setBlendConstants(blend_constants.data());
}

void CommandBuffer::set_depth_bounds(float min_depth_bounds, float max_depth_bounds)
{
	setDepthBounds(min_depth_bounds, max_depth_bounds);
}

void CommandBuffer::draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	flush_pipeline_state(vk::PipelineBindPoint::eGraphics);

	flush_descriptor_state(vk::PipelineBindPoint::eGraphics);

	get_handle().draw(vertex_count, instance_count, first_vertex, first_instance);
}

void CommandBuffer::draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	flush_pipeline_state(vk::PipelineBindPoint::eGraphics);

	flush_descriptor_state(vk::PipelineBindPoint::eGraphics);

	get_handle().drawIndexed(index_count, instance_count, first_index, vertex_offset, first_instance);
}

void CommandBuffer::draw_indexed_indirect(const core::Buffer &buffer, vk::DeviceSize offset, uint32_t draw_count, uint32_t stride)
{
	flush_pipeline_state(vk::PipelineBindPoint::eGraphics);

	flush_descriptor_state(vk::PipelineBindPoint::eGraphics);

	get_handle().drawIndexedIndirect(buffer.get_handle(), offset, draw_count, stride);
}

void CommandBuffer::dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
	flush_pipeline_state(vk::PipelineBindPoint::eCompute);

	flush_descriptor_state(vk::PipelineBindPoint::eCompute);

	get_handle().dispatch(group_count_x, group_count_y, group_count_z);
}

void CommandBuffer::dispatch_indirect(const core::Buffer &buffer, vk::DeviceSize offset)
{
	flush_pipeline_state(vk::PipelineBindPoint::eCompute);

	flush_descriptor_state(vk::PipelineBindPoint::eCompute);

	get_handle().dispatchIndirect(buffer.get_handle(), offset);
}

void CommandBuffer::update_buffer(const core::Buffer &buffer, vk::DeviceSize offset, const std::vector<uint8_t> &data)
{
	updateBuffer<uint8_t>(buffer.get_handle(), offset, data);
}

void CommandBuffer::blit_image(const core::Image &src_img, const core::Image &dst_img, const vk::ArrayProxy<const vk::ImageBlit> &regions)
{
	blitImage(src_img, vk::ImageLayout::eTransferSrcOptimal, dst_img, vk::ImageLayout::eTransferDstOptimal, regions, vk::Filter::eNearest);
}

void CommandBuffer::copy_buffer(const core::Buffer &src_buffer, const core::Buffer &dst_buffer, vk::DeviceSize size)
{
	copyBuffer(src_buffer.get_handle(), dst_buffer.get_handle(), vk::BufferCopy{0, 0, size});
}

void CommandBuffer::copy_image(const core::Image &src_img, const core::Image &dst_img, const vk::ArrayProxy<const vk::ImageCopy> &regions)
{
	copyImage(src_img, vk::ImageLayout::eTransferSrcOptimal, dst_img, vk::ImageLayout::eTransferDstOptimal, regions);
}

void CommandBuffer::copy_buffer_to_image(const core::Buffer &buffer, const core::Image &image, const vk::ArrayProxy<const vk::BufferImageCopy> &regions)
{
	copyBufferToImage(buffer.get_handle(), image, vk::ImageLayout::eTransferDstOptimal, regions);
}

void CommandBuffer::image_memory_barrier(const core::ImageView &image_view, const ImageMemoryBarrier &memory_barrier)
{
	vk::ImageMemoryBarrier image_memory_barrier;
	image_memory_barrier.oldLayout        = memory_barrier.old_layout;
	image_memory_barrier.newLayout        = memory_barrier.new_layout;
	image_memory_barrier.image            = image_view.get_image().get_handle();
	image_memory_barrier.subresourceRange = image_view.get_subresource_range();
	image_memory_barrier.srcAccessMask    = memory_barrier.src_access_mask;
	image_memory_barrier.dstAccessMask    = memory_barrier.dst_access_mask;

	vk::PipelineStageFlags src_stage_mask = memory_barrier.src_stage_mask;
	vk::PipelineStageFlags dst_stage_mask = memory_barrier.dst_stage_mask;

	pipelineBarrier(src_stage_mask, dst_stage_mask, {}, nullptr, nullptr, image_memory_barrier);
}

void CommandBuffer::buffer_memory_barrier(const core::Buffer &buffer, vk::DeviceSize offset, vk::DeviceSize size, const BufferMemoryBarrier &memory_barrier)
{
	vk::BufferMemoryBarrier buffer_memory_barrier;
	buffer_memory_barrier.srcAccessMask = memory_barrier.src_access_mask;
	buffer_memory_barrier.dstAccessMask = memory_barrier.dst_access_mask;
	buffer_memory_barrier.buffer        = buffer.get_handle();
	buffer_memory_barrier.offset        = offset;
	buffer_memory_barrier.size          = size;

	vk::PipelineStageFlags src_stage_mask = memory_barrier.src_stage_mask;
	vk::PipelineStageFlags dst_stage_mask = memory_barrier.dst_stage_mask;

	pipelineBarrier(src_stage_mask, dst_stage_mask, {}, nullptr, buffer_memory_barrier, nullptr);
}

void CommandBuffer::flush_pipeline_state(vk::PipelineBindPoint pipeline_bind_point)
{
	// Create a new pipeline only if the graphics state changed
	if (!pipeline_state.is_dirty())
	{
		return;
	}

	pipeline_state.clear_dirty();

	// Create and bind pipeline
	if (pipeline_bind_point == vk::PipelineBindPoint::eGraphics)
	{
		pipeline_state.set_render_pass(*current_render_pass.render_pass);
		auto &pipeline = get_device().get_resource_cache().request_graphics_pipeline(pipeline_state);
		bindPipeline(pipeline_bind_point, pipeline);
	}
	else if (pipeline_bind_point == vk::PipelineBindPoint::eCompute)
	{
		auto &pipeline = get_device().get_resource_cache().request_compute_pipeline(pipeline_state);
		bindPipeline(pipeline_bind_point, pipeline);
	}
	else
	{
		throw "Only graphics and compute pipeline bind points are supported now";
	}
}

void CommandBuffer::flush_descriptor_state(vk::PipelineBindPoint pipeline_bind_point)
{
	assert(command_pool.get_render_frame() && "The command pool must be associated to a render frame");

	const auto &pipeline_layout = pipeline_state.get_pipeline_layout();

	const auto &shader_program = pipeline_layout.get_shader_program();

	std::unordered_set<uint32_t> update_descriptor_sets;

	// Iterate over the shader sets to check if they have already been bound
	// If they have, add the set so that the command buffer later updates it
	for (auto &set_it : shader_program.get_shader_sets())
	{
		uint32_t descriptor_set_id = set_it.first;

		auto descriptor_set_layout_it = descriptor_set_layout_binding_state.find(descriptor_set_id);

		if (descriptor_set_layout_it != descriptor_set_layout_binding_state.end())
		{
			if (descriptor_set_layout_it->second->get_handle() != pipeline_layout.get_descriptor_set_layout(descriptor_set_id).get_handle())
			{
				update_descriptor_sets.emplace(descriptor_set_id);
			}
		}
	}

	// Validate that the bound descriptor set layouts exist in the pipeline layout
	for (auto set_it = descriptor_set_layout_binding_state.begin(); set_it != descriptor_set_layout_binding_state.end();)
	{
		if (!pipeline_layout.has_descriptor_set_layout(set_it->first))
		{
			set_it = descriptor_set_layout_binding_state.erase(set_it);
		}
		else
		{
			++set_it;
		}
	}

	// Check if a descriptor set needs to be created
	if (resource_binding_state.is_dirty() || !update_descriptor_sets.empty())
	{
		resource_binding_state.clear_dirty();

		// Iterate over all of the resource sets bound by the command buffer
		for (auto &resource_set_it : resource_binding_state.get_resource_sets())
		{
			uint32_t descriptor_set_id = resource_set_it.first;
			auto &   resource_set      = resource_set_it.second;

			// Don't update resource set if it's not in the update list OR its state hasn't changed
			if (!resource_set.is_dirty() && (update_descriptor_sets.find(descriptor_set_id) == update_descriptor_sets.end()))
			{
				continue;
			}

			// Clear dirty flag for resource set
			resource_binding_state.clear_dirty(descriptor_set_id);

			// Skip resource set if a descriptor set layout doesn't exist for it
			if (!pipeline_layout.has_descriptor_set_layout(descriptor_set_id))
			{
				continue;
			}

			auto &descriptor_set_layout = pipeline_layout.get_descriptor_set_layout(descriptor_set_id);

			// Make descriptor set layout bound for current set
			descriptor_set_layout_binding_state[descriptor_set_id] = &descriptor_set_layout;

			BindingMap<vk::DescriptorBufferInfo> buffer_infos;
			BindingMap<vk::DescriptorImageInfo>  image_infos;

			std::vector<uint32_t> dynamic_offsets;

			// Iterate over all resource bindings
			for (auto &binding_it : resource_set.get_resource_bindings())
			{
				auto  binding_index     = binding_it.first;
				auto &binding_resources = binding_it.second;

				// Check if binding exists in the pipeline layout
				if (auto binding_info = descriptor_set_layout.get_layout_binding(binding_index))
				{
					// Iterate over all binding resources
					for (auto &element_it : binding_resources)
					{
						auto  array_element = element_it.first;
						auto &resource_info = element_it.second;

						// Pointer references
						auto &buffer     = resource_info.buffer;
						auto &sampler    = resource_info.sampler;
						auto &image_view = resource_info.image_view;

						// Get buffer info
						if (buffer != nullptr && is_buffer_descriptor_type(binding_info->descriptorType))
						{
							VkDescriptorBufferInfo buffer_info{};

							buffer_info.buffer = resource_info.buffer->get_handle();
							buffer_info.offset = resource_info.offset;
							buffer_info.range  = resource_info.range;

							if (is_dynamic_buffer_descriptor_type(binding_info->descriptorType))
							{
								dynamic_offsets.push_back(to_u32(buffer_info.offset));

								buffer_info.offset = 0;
							}

							buffer_infos[binding_index][array_element] = buffer_info;
						}

						// Get image info
						else if (image_view != nullptr || sampler != VK_NULL_HANDLE)
						{
							// Can be null for input attachments
							vk::DescriptorImageInfo image_info;
							image_info.sampler   = sampler ? sampler->get_handle() : nullptr;
							image_info.imageView = image_view->get_handle();

							if (image_view != nullptr)
							{
								// Add image layout info based on descriptor type
								switch (binding_info->descriptorType)
								{
									case vk::DescriptorType::eCombinedImageSampler:
									case vk::DescriptorType::eInputAttachment:
										if (is_depth_stencil_format(image_view->get_format()))
										{
											image_info.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
										}
										else
										{
											image_info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
										}
										break;
									case vk::DescriptorType::eStorageImage:
										image_info.imageLayout = vk::ImageLayout::eGeneral;
										break;

									default:
										continue;
								}
							}

							image_infos[binding_index][array_element] = std::move(image_info);
						}
					}
				}
			}

			auto &descriptor_set = command_pool.get_render_frame()->request_descriptor_set(descriptor_set_layout, buffer_infos, image_infos, command_pool.get_thread_index());

			vk::DescriptorSet descriptor_set_handle = descriptor_set.get_handle();

			// Bind descriptor set
			bindDescriptorSets(
			    pipeline_bind_point,
			    pipeline_layout.get_handle(),
			    descriptor_set_id,
			    descriptor_set_handle,
			    dynamic_offsets);
		}
	}
}

const CommandBuffer::State CommandBuffer::get_state() const
{
	return state;
}

const CommandBuffer::RenderPassBinding &CommandBuffer::get_current_render_pass() const
{
	return current_render_pass;
}

const uint32_t CommandBuffer::get_current_subpass_index() const
{
	return pipeline_state.get_subpass_index();
}

const bool CommandBuffer::is_render_size_optimal(const vk::Extent2D &framebuffer_extent, const vk::Rect2D &render_area)
{
	auto render_area_granularity = current_render_pass.render_pass->get_render_area_granularity();

	return ((render_area.offset.x % render_area_granularity.width == 0) && (render_area.offset.y % render_area_granularity.height == 0) &&
	        ((render_area.extent.width % render_area_granularity.width == 0) || (render_area.offset.x + render_area.extent.width == framebuffer_extent.width)) &&
	        ((render_area.extent.height % render_area_granularity.height == 0) || (render_area.offset.y + render_area.extent.height == framebuffer_extent.height)));
}

vk::Result CommandBuffer::reset(ResetMode reset_mode)
{
	vk::Result result = vk::Result::eSuccess;

	assert(reset_mode == command_pool.get_reset_mode() && "Command buffer reset mode must match the one used by the pool to allocate it");

	state = State::Initial;

	if (reset_mode == ResetMode::ResetIndividually)
	{
		get_handle().reset(vk::CommandBufferResetFlagBits::eReleaseResources);
	}

	return result;
}
}        // namespace vkb
