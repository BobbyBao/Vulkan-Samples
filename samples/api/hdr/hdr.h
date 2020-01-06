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

/*
 * High dynamic range rendering
 */

#pragma once

#include "api_vulkan_sample.h"

class HDR : public ApiVulkanSample
{
  public:
	bool bloom          = true;
	bool display_skybox = true;

	struct
	{
		Texture envmap;
	} textures;

	struct Models
	{
		std::unique_ptr<vkb::sg::SubMesh>              skybox;
		std::vector<std::unique_ptr<vkb::sg::SubMesh>> objects;
		std::vector<glm::mat4>                         transforms;
		int32_t                                        object_index = 0;
	} models;

	struct
	{
		std::unique_ptr<vkb::core::Buffer> matrices;
		std::unique_ptr<vkb::core::Buffer> params;
	} uniform_buffers;

	struct UBOVS
	{
		glm::mat4 projection;
		glm::mat4 modelview;
		glm::mat4 skybox_modelview;
		float     modelscale = 0.05f;
	} ubo_vs;

	struct UBOParams
	{
		float exposure = 1.0f;
	} ubo_params;

	struct
	{
		vk::Pipeline skybox;
		vk::Pipeline reflect;
		vk::Pipeline composition;
		vk::Pipeline bloom[2];
	} pipelines;

	struct
	{
		vk::PipelineLayout models;
		vk::PipelineLayout composition;
		vk::PipelineLayout bloom_filter;
	} pipeline_layouts;

	struct
	{
		vk::DescriptorSet object;
		vk::DescriptorSet skybox;
		vk::DescriptorSet composition;
		vk::DescriptorSet bloom_filter;
	} descriptor_sets;

	struct
	{
		vk::DescriptorSetLayout models;
		vk::DescriptorSetLayout composition;
		vk::DescriptorSetLayout bloom_filter;
	} descriptor_set_layouts;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment
	{
		vk::Image        image;
		vk::DeviceMemory mem;
		vk::ImageView    view;
		vk::Format       format;
		void             destroy(vk::Device device)
		{
			device.destroy(view);
			device.destroy(image);
			device.freeMemory(mem);
		}
	};
	struct FrameBuffer
	{
		int32_t               width, height;
		vk::Framebuffer       framebuffer;
		FrameBufferAttachment color[2];
		FrameBufferAttachment depth;
		vk::RenderPass        render_pass;
		vk::Sampler           sampler;
	} offscreen;

	struct
	{
		int32_t               width, height;
		vk::Framebuffer       framebuffer;
		FrameBufferAttachment color[1];
		vk::RenderPass        render_pass;
		vk::Sampler           sampler;
	} filter_pass;

	std::vector<std::string> object_names;

	HDR();
	~HDR();
	void         get_device_features() override;
	void         build_command_buffers() override;
	void         create_attachment(vk::Format format, vk::ImageUsageFlagBits usage, FrameBufferAttachment *attachment);
	void         prepare_offscreen_buffer();
	void         load_assets();
	void         setup_descriptor_pool();
	void         setup_descriptor_set_layout();
	void         setup_descriptor_sets();
	void         prepare_pipelines();
	void         prepare_uniform_buffers();
	void         update_uniform_buffers();
	void         update_params();
	void         draw();
	bool         prepare(vkb::Platform &platform) override;
	virtual void render(float delta_time) override;
	virtual void on_update_ui_overlay(vkb::Drawer &drawer) override;
	virtual void resize(const uint32_t width, const uint32_t height) override;
};

std::unique_ptr<vkb::Application> create_hdr();
