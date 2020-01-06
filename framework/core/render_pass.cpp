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

#include "render_pass.h"

#include <numeric>

#include "device.h"
#include "rendering/render_target.h"

namespace vkb
{
vk::RenderPass RenderPass::get_handle() const
{
	return static_cast<const vk::RenderPass &>(*this);
}

RenderPass::RenderPass(Device &device, const std::vector<Attachment> &attachments, const std::vector<LoadStoreInfo> &load_store_infos, const std::vector<SubpassInfo> &subpasses) :
    device{device},
    subpass_count{std::max<size_t>(1, subpasses.size())},        // At least 1 subpass
    input_attachments{subpass_count},
    color_attachments{subpass_count},
    depth_stencil_attachments{subpass_count}
{
	uint32_t depth_stencil_attachment{VK_ATTACHMENT_UNUSED};

	std::vector<vk::AttachmentDescription> attachment_descriptions;

	for (uint32_t i = 0U; i < attachments.size(); ++i)
	{
		vk::AttachmentDescription attachment;

		attachment.format      = attachments[i].format;
		attachment.samples     = attachments[i].samples;
		attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

		if (i < load_store_infos.size())
		{
			attachment.loadOp         = load_store_infos[i].load_op;
			attachment.storeOp        = load_store_infos[i].store_op;
			attachment.stencilLoadOp  = load_store_infos[i].load_op;
			attachment.stencilStoreOp = load_store_infos[i].store_op;
		}

		if (is_depth_stencil_format(attachment.format))
		{
			depth_stencil_attachment = i;
			attachment.finalLayout   = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		attachment_descriptions.push_back(std::move(attachment));
	}

	std::vector<vk::SubpassDescription> subpass_descriptions;
	subpass_descriptions.reserve(subpass_count);

	for (size_t i = 0; i < subpasses.size(); ++i)
	{
		auto &subpass = subpasses[i];

		// Fill color/depth attachments references
		for (auto o_attachment : subpass.output_attachments)
		{
			if (o_attachment != depth_stencil_attachment)
			{
				color_attachments[i].push_back({o_attachment, vk::ImageLayout::eColorAttachmentOptimal});
			}
		}

		// Fill input attachments references
		for (auto i_attachment : subpass.input_attachments)
		{
			if (is_depth_stencil_format(attachment_descriptions[i_attachment].format))
			{
				input_attachments[i].push_back({i_attachment, vk::ImageLayout::eDepthStencilReadOnlyOptimal});
			}
			else
			{
				input_attachments[i].push_back({i_attachment, vk::ImageLayout::eShaderReadOnlyOptimal});
			}
		}

		if (depth_stencil_attachment != VK_ATTACHMENT_UNUSED)
		{
			depth_stencil_attachments[i].push_back({depth_stencil_attachment, vk::ImageLayout::eDepthStencilAttachmentOptimal});
		}
	}

	for (size_t i = 0; i < subpasses.size(); ++i)
	{
		auto &subpass = subpasses[i];

		vk::SubpassDescription subpass_description;
		subpass_description.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

		subpass_description.pInputAttachments    = input_attachments[i].empty() ? nullptr : input_attachments[i].data();
		subpass_description.inputAttachmentCount = to_u32(input_attachments[i].size());

		subpass_description.pColorAttachments    = color_attachments[i].empty() ? nullptr : color_attachments[i].data();
		subpass_description.colorAttachmentCount = to_u32(color_attachments[i].size());

		subpass_description.pDepthStencilAttachment = depth_stencil_attachments[i].empty() ? nullptr : depth_stencil_attachments[i].data();

		subpass_descriptions.push_back(subpass_description);
	}

	// Default subpass
	if (subpasses.empty())
	{
		vk::SubpassDescription subpass_description;
		subpass_description.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;

		for (uint32_t k = 0U; k < attachment_descriptions.size(); ++k)
		{
			if (k == depth_stencil_attachment)
			{
				continue;
			}

			color_attachments[0].push_back({k, vk::ImageLayout::eGeneral});
		}

		subpass_description.pColorAttachments = color_attachments[0].data();

		if (depth_stencil_attachment != VK_ATTACHMENT_UNUSED)
		{
			depth_stencil_attachments[0].push_back({depth_stencil_attachment, vk::ImageLayout::eDepthStencilAttachmentOptimal});

			subpass_description.pDepthStencilAttachment = depth_stencil_attachments[0].data();
		}

		subpass_descriptions.push_back(subpass_description);
	}

	// Make the initial layout same as in the first subpass using that attachment
	for (auto &subpass : subpass_descriptions)
	{
		for (uint32_t k = 0U; k < subpass.colorAttachmentCount; ++k)
		{
			auto reference = subpass.pColorAttachments[k];
			// Set it only if not defined yet
			if (attachment_descriptions[reference.attachment].initialLayout == vk::ImageLayout::eUndefined)
			{
				attachment_descriptions[reference.attachment].initialLayout = reference.layout;
			}
		}

		for (uint32_t k = 0U; k < subpass.inputAttachmentCount; ++k)
		{
			auto reference = subpass.pInputAttachments[k];
			// Set it only if not defined yet
			if (attachment_descriptions[reference.attachment].initialLayout == vk::ImageLayout::eUndefined)
			{
				attachment_descriptions[reference.attachment].initialLayout = reference.layout;
			}
		}

		if (subpass.pDepthStencilAttachment)
		{
			auto reference = *subpass.pDepthStencilAttachment;
			// Set it only if not defined yet
			if (attachment_descriptions[reference.attachment].initialLayout == vk::ImageLayout::eUndefined)
			{
				attachment_descriptions[reference.attachment].initialLayout = reference.layout;
			}
		}
	}

	// Make the final layout same as the last subpass layout
	{
		auto &subpass = subpass_descriptions.back();

		for (uint32_t k = 0U; k < subpass.colorAttachmentCount; ++k)
		{
			const auto &reference = subpass.pColorAttachments[k];

			attachment_descriptions[reference.attachment].finalLayout = reference.layout;
		}

		for (uint32_t k = 0U; k < subpass.inputAttachmentCount; ++k)
		{
			const auto &reference = subpass.pInputAttachments[k];

			attachment_descriptions[reference.attachment].finalLayout = reference.layout;

			// Do not use depth attachment if used as input
			if (reference.attachment == depth_stencil_attachment)
			{
				subpass.pDepthStencilAttachment = nullptr;
			}
		}

		if (subpass.pDepthStencilAttachment)
		{
			const auto &reference = *subpass.pDepthStencilAttachment;

			attachment_descriptions[reference.attachment].finalLayout = reference.layout;
		}
	}

	// Set subpass dependencies
	std::vector<vk::SubpassDependency> dependencies(subpass_count - 1);

	if (subpass_count > 1)
	{
		for (uint32_t i = 0; i < dependencies.size(); ++i)
		{
			// Transition input attachments from color attachment to shader read
			dependencies[i].srcSubpass      = i;
			dependencies[i].dstSubpass      = i + 1;
			dependencies[i].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
			dependencies[i].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
			dependencies[i].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
			dependencies[i].dstAccessMask   = vk::AccessFlagBits::eInputAttachmentRead;
			dependencies[i].dependencyFlags = vk::DependencyFlagBits::eByRegion;
		}
	}

	// Create render pass
	vk::RenderPassCreateInfo create_info;

	create_info.attachmentCount = to_u32(attachment_descriptions.size());
	create_info.pAttachments    = attachment_descriptions.data();
	create_info.subpassCount    = to_u32(subpass_count);
	create_info.pSubpasses      = subpass_descriptions.data();
	create_info.dependencyCount = to_u32(dependencies.size());
	create_info.pDependencies   = dependencies.data();

	static_cast<vk::RenderPass &>(*this) = device.get_handle().createRenderPass(create_info);
}        // namespace vkb

RenderPass::RenderPass(RenderPass &&other) :
    vk::RenderPass{other},
    device{other.device},
    subpass_count{other.subpass_count},
    input_attachments{other.input_attachments},
    color_attachments{other.color_attachments},
    depth_stencil_attachments{other.depth_stencil_attachments}
{
	static_cast<vk::RenderPass &&>(other) = nullptr;
}

RenderPass::~RenderPass()
{
	// Destroy render pass
	if (operator bool())
	{
		device.get_handle().destroy(*this);
	}
}

const uint32_t RenderPass::get_color_output_count(uint32_t subpass_index) const
{
	return to_u32(color_attachments[subpass_index].size());
}

const vk::Extent2D RenderPass::get_render_area_granularity() const
{
    return device.get_handle().getRenderAreaGranularity(*this);
}
}        // namespace vkb
