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

#include "utils.h"

#include <queue>
#include <stdexcept>

#include "glm/gtx/vec_swizzle.hpp"

#include "core/pipeline_layout.h"
#include "core/shader_module.h"
#include "scene_graph/components/image.h"
#include "scene_graph/components/material.h"
#include "scene_graph/components/mesh.h"
#include "scene_graph/components/pbr_material.h"
#include "scene_graph/components/perspective_camera.h"
#include "scene_graph/components/sampler.h"
#include "scene_graph/components/sub_mesh.h"
#include "scene_graph/components/texture.h"
#include "scene_graph/components/transform.h"
#include "scene_graph/node.h"
#include "scene_graph/script.h"
#include "scene_graph/scripts/free_camera.h"
#include "vk_common.h"

namespace vkb
{
std::string get_extension(const std::string &uri)
{
	auto dot_pos = uri.find_last_of('.');
	if (dot_pos == std::string::npos)
	{
		throw std::runtime_error{"Uri has no extension"};
	}

	return uri.substr(dot_pos + 1);
}

void screenshot(RenderContext &render_context, const std::string &filename)
{
	vk::Format format = vk::Format::eR8G8B8A8Unorm;

	// We want the last completed frame since we don't want to be reading from an incomplete framebuffer
	auto &frame          = render_context.get_last_rendered_frame();
	auto &src_image_view = frame.get_render_target().get_views().at(0);
	auto &src_image      = src_image_view.get_image();

	// Check if framebuffer images are in a BGR format
	auto bgr_formats = {vk::Format::eB8G8R8A8Srgb, vk::Format::eB8G8R8A8Unorm, vk::Format::eB8G8R8A8Snorm};
	bool swizzle     = std::find(bgr_formats.begin(), bgr_formats.end(), src_image_view.get_format()) != bgr_formats.end();

	auto width  = render_context.get_surface_extent().width;
	auto height = render_context.get_surface_extent().height;

	core::Image dst_image{render_context.get_device(),
	                      vk::Extent3D{width, height, 1},
	                      format,
	                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                      vma::MemoryUsage::eGpuToCpu,
	                      vk::SampleCountFlagBits::e1,
	                      1, 1,
	                      vk::ImageTiling::eLinear};

	core::ImageView dst_image_view{dst_image, vk::ImageViewType::e2D};

	render_context.get_device().with_command_buffer([&](const vk::CommandBuffer &cmd_buf) {
		// Enable destination image to be written to
        vkb::set_image_layout(
            cmd_buf,
            dst_image.get_handle(),
		    vk::ImageLayout::eUndefined,
		    vk::ImageLayout::eTransferDstOptimal,
		    dst_image_view.get_subresource_range(),
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eTransfer);

		// Enable framebuffer image view to be read from
        vkb::set_image_layout(
            cmd_buf,
            src_image.get_handle(),
            vk::ImageLayout::ePresentSrcKHR,
            vk::ImageLayout::eTransferSrcOptimal,
            src_image_view.get_subresource_range(),
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer);

		// Copy framebuffer image memory
		vk::ImageCopy image_copy_region;
		image_copy_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		image_copy_region.srcSubresource.layerCount = 1;
		image_copy_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		image_copy_region.dstSubresource.layerCount = 1;
		image_copy_region.extent.width              = width;
		image_copy_region.extent.height             = height;
		image_copy_region.extent.depth              = 1;

		cmd_buf.copyImage(src_image.get_handle(), vk::ImageLayout::eTransferSrcOptimal, dst_image.get_handle(), vk::ImageLayout::eTransferDstOptimal, image_copy_region);

		// Enable destination image to map image memory
        vkb::set_image_layout(
            cmd_buf,
            dst_image.get_handle(),
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::ImageLayout::eGeneral,
		    dst_image_view.get_subresource_range(),
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eTransfer);

		// Revert back the framebuffer image view from transfer to present
        vkb::set_image_layout(
            cmd_buf,
            src_image.get_handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            src_image_view.get_subresource_range(),
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer);
	});

	auto raw_data = dst_image.map();

	// Android requires the sub resource to be queried while the memory is mapped
	vk::ImageSubresource  subresource{vk::ImageAspectFlagBits::eColor};
	vk::SubresourceLayout subresource_layout = render_context.get_device().get_handle().getImageSubresourceLayout(dst_image.get_handle(), subresource);

	raw_data += subresource_layout.offset;

	// Creates a pointer to the address of the first byte past the offset of the image data
	// Replace the A component with 255 (remove transparency)
	// If swapchain format is BGR, swapping the R and B components
	uint8_t *data = raw_data;
	if (swizzle)
	{
		for (size_t i = 0; i < height; ++i)
		{
			// Gets the first pixel of the first row of the image (a pixel is 4 bytes)
			glm::u8vec4 *pixel = (glm::u8vec4 *) data;

			// Iterate over each pixel, swapping R and B components and writing the max value for alpha
			for (size_t j = 0; j < width; ++j)
			{
				*pixel = {glm::zyx(*pixel), 255};
				// Get next pixel
				pixel++;
			}
			// Pointer arithmetic to get the first byte in the next row of pixels
			data += subresource_layout.rowPitch;
		}
	}
	else
	{
		for (size_t i = 0; i < height; ++i)
		{
			// Gets the first pixel of the first row of the image (a pixel is 4 bytes)
			glm::u8vec4 *pixel = (glm::u8vec4 *) data;

			// Iterate over each pixel, writing the max value for alpha
			for (size_t j = 0; j < width; ++j)
			{
				pixel->a = 255;
				// Get next pixel
				pixel++;
			}
			// Pointer arithmetic to get the first byte in the next row of pixels
			data += subresource_layout.rowPitch;
		}
	}

	vkb::fs::write_image(raw_data,
	                     filename,
	                     width,
	                     height,
	                     4,
	                     static_cast<uint32_t>(subresource_layout.rowPitch));

	dst_image.unmap();
}        // namespace vkb

std::string to_snake_case(const std::string &text)
{
	std::stringstream result;

	for (const auto ch : text)
	{
		if (std::isalpha(ch))
		{
			if (std::isspace(ch))
			{
				result << "_";
			}
			else
			{
				if (std::isupper(ch))
				{
					result << "_";
				}

				result << static_cast<char>(std::tolower(ch));
			}
		}
		else
		{
			result << ch;
		}
	}

	return result.str();
}

sg::Light &add_light(sg::Scene &scene, sg::LightType type, const glm::vec3 &position, const glm::quat &rotation, const sg::LightProperties &props, sg::Node *parent_node)
{
	auto light_ptr = std::make_unique<sg::Light>("light");
	auto node      = std::make_unique<sg::Node>(-1, "light node");

	if (parent_node)
	{
		node->set_parent(*parent_node);
	}

	light_ptr->set_node(*node);
	light_ptr->set_light_type(type);
	light_ptr->set_properties(props);

	auto &t = node->get_transform();
	t.set_translation(position);
	t.set_rotation(rotation);

	// Storing the light component because the unique_ptr will be moved to the scene
	auto &light = *light_ptr;

	node->set_component(light);
	scene.add_child(*node);
	scene.add_component(std::move(light_ptr));
	scene.add_node(std::move(node));

	return light;
}

sg::Light &add_point_light(sg::Scene &scene, const glm::vec3 &position, const sg::LightProperties &props, sg::Node *parent_node)
{
	return add_light(scene, sg::LightType::Point, position, {}, props, parent_node);
}

sg::Light &add_directional_light(sg::Scene &scene, const glm::quat &rotation, const sg::LightProperties &props, sg::Node *parent_node)
{
	return add_light(scene, sg::LightType::Directional, {}, rotation, props, parent_node);
}

sg::Node &add_free_camera(sg::Scene &scene, const std::string &node_name, vk::Extent2D extent)
{
	auto camera_node = scene.find_node(node_name);

	if (!camera_node)
	{
		LOGW("Camera node `{}` not found. Looking for `default_camera` node.", node_name.c_str());

		camera_node = scene.find_node("default_camera");
	}

	if (!camera_node)
	{
		throw std::runtime_error("Camera node with name `" + node_name + "` not found.");
	}

	if (!camera_node->has_component<sg::Camera>())
	{
		throw std::runtime_error("No camera component found for `" + node_name + "` node.");
	}

	auto free_camera_script = std::make_unique<sg::FreeCamera>(*camera_node);

	free_camera_script->resize(extent.width, extent.height);

	scene.add_component(std::move(free_camera_script), *camera_node);

	return *camera_node;
}

}        // namespace vkb
