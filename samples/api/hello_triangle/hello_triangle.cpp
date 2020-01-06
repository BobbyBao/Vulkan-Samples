/* Copyright (c) 2018-2020, Arm Limited and Contributors
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

#include "hello_triangle.h"

#include "common/logging.h"
#include "common/vk_common.h"
#include "glsl_compiler.h"
#include "platform/filesystem.h"
#include "platform/platform.h"

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
/// @brief A debug callback called from Vulkan validation layers.
static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(VkDebugReportFlagsEXT rawFlags, VkDebugReportObjectTypeEXT type,
                                                       uint64_t object, size_t location, int32_t message_code,
                                                       const char *layer_prefix, const char *message, void *user_data)
{
	if (rawFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		LOGE("Validation Layer: Error: {}: {}", layer_prefix, message);
	}
	else if (rawFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		LOGE("Validation Layer: Warning: {}: {}", layer_prefix, message);
	}
	else if (rawFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		LOGI("Validation Layer: Performance warning: {}: {}", layer_prefix, message);
	}
	else
	{
		LOGI("Validation Layer: Information: {}: {}", layer_prefix, message);
	}
	return VK_FALSE;
}
#endif

/**
 * @brief Validates a list of required extensions, comparing it with the available ones.
 *
 * @param required A vector containing required extension names.
 * @param available A vk::ExtensionProperties object containing available extensions.
 * @return true if all required extensions are available
 * @return false otherwise
 */
bool HelloTriangle::validate_extensions(const std::vector<const char *> &           required,
                                        const std::vector<vk::ExtensionProperties> &available)
{
	for (auto extension : required)
	{
		bool found = false;
		for (auto &available_extension : available)
		{
			if (strcmp(available_extension.extensionName, extension) == 0)
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			return false;
		}
	}

	return true;
}

/**
 * @brief Validates a list of required layers, comparing it with the available ones.
 *
 * @param required A vector containing required layer names.
 * @param available A vk::LayerProperties object containing available layers.
 * @return true if all required extensions are available
 * @return false otherwise
 */
bool HelloTriangle::validate_layers(const std::vector<const char *> &       required,
                                    const std::vector<vk::LayerProperties> &available)
{
	for (auto extension : required)
	{
		bool found = false;
		for (auto &available_extension : available)
		{
			if (strcmp(available_extension.layerName, extension) == 0)
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			return false;
		}
	}

	return true;
}

/**
 * @brief Find the vulkan shader stage for a given a string.
 *
 * @param ext A string containing the shader stage name.
 * @return vk::ShaderStageFlagBits The shader stage mapping from the given string, vk::ShaderStageFlagBits::eVertex otherwise.
 */
vk::ShaderStageFlagBits HelloTriangle::find_shader_stage(const std::string &ext)
{
	if (ext == "vert")
	{
		return vk::ShaderStageFlagBits::eVertex;
	}
	else if (ext == "frag")
	{
		return vk::ShaderStageFlagBits::eFragment;
	}
	else if (ext == "comp")
	{
		return vk::ShaderStageFlagBits::eCompute;
	}
	else if (ext == "geom")
	{
		return vk::ShaderStageFlagBits::eGeometry;
	}
	else if (ext == "tesc")
	{
		return vk::ShaderStageFlagBits::eTessellationControl;
	}
	else if (ext == "tese")
	{
		return vk::ShaderStageFlagBits::eTessellationEvaluation;
	}

	throw std::runtime_error("No Vulkan shader stage found for the file extension name.");
};

/**
 * @brief Initializes the Vulkan instance.
 *
 * @param context A newly created Vulkan context.
 * @param required_instance_extensions The required Vulkan instance extensions.
 * @param required_instance_layers
 */
void HelloTriangle::init_instance(Context &                        context,
                                  const std::vector<const char *> &required_instance_extensions,
                                  const std::vector<const char *> &required_instance_layers)
{
	LOGI("Initializing vulkan instance.");

	if (volkInitialize())
	{
		throw std::runtime_error("Failed to initialize volk.");
	}
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	auto instance_extensions = vk::enumerateInstanceExtensionProperties();

	std::vector<const char *> active_instance_extensions(required_instance_extensions);

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	active_instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	active_instance_extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
	active_instance_extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	active_instance_extensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	active_instance_extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
	active_instance_extensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#else
#	pragma error Platform not supported
#endif

	if (!validate_extensions(active_instance_extensions, instance_extensions))
	{
		throw std::runtime_error("Required instance extensions are missing.");
	}

	auto instance_layers = vk::enumerateInstanceLayerProperties();

	std::vector<const char *> active_instance_layers(required_instance_layers);

#ifdef VKB_VALIDATION_LAYERS
	// Optimal validation layers
	std::vector<const char *> default_debug_layers{{
	    "VK_LAYER_KHRONOS_validation",
	}};
	// Alternative layers
	if (!validate_layers(default_debug_layers, instance_layers))
	{
		default_debug_layers = {
		    "VK_LAYER_LUNARG_standard_validation",
		};
	}
	// Fallback layers
	if (!validate_layers(default_debug_layers, instance_layers))
	{
		default_debug_layers = {
		    "VK_LAYER_GOOGLE_threading",
		    "VK_LAYER_LUNARG_parameter_validation",
		    "VK_LAYER_LUNARG_object_tracker",
		    "VK_LAYER_LUNARG_core_validation",
		    "VK_LAYER_GOOGLE_unique_objects",
		};
	}
	if (!validate_layers(default_debug_layers, instance_layers))
	{
		default_debug_layers = {};
	}
	active_instance_layers.insert(active_instance_layers.end(), default_debug_layers.begin(), default_debug_layers.end());
#endif

	if (!validate_layers(active_instance_layers, instance_layers))
	{
		throw std::runtime_error("Required instance layers are missing.");
	}

	vk::ApplicationInfo app;
	app.pApplicationName = "Hello Triangle";
	app.pEngineName      = "Vulkan Samples";
	app.apiVersion       = VK_MAKE_VERSION(1, 0, 0);

	vk::InstanceCreateInfo instance_info;

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	vk::DebugReportCallbackCreateInfoEXT debug_report_info;
	debug_report_info.flags       = vk::DebugReportFlagBitsEXT::eError | vk::DebugReportFlagBitsEXT::eWarning;
	debug_report_info.pfnCallback = debug_callback;
	instance_info.pNext           = &debug_report_info;
#endif

	instance_info.pApplicationInfo        = &app;
	instance_info.enabledExtensionCount   = vkb::to_u32(active_instance_extensions.size());
	instance_info.ppEnabledExtensionNames = active_instance_extensions.data();
	instance_info.enabledLayerCount       = vkb::to_u32(active_instance_layers.size());
	instance_info.ppEnabledLayerNames     = active_instance_layers.data();
	// Create the Vulkan instance
	context.instance = vk::createInstance(instance_info);

	volkLoadInstance(context.instance);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(context.instance);

#if defined(VKB_DEBUG) || defined(VKB_VALIDATION_LAYERS)
	context.debug_callback = context.instance.createDebugReportCallbackEXT(debug_report_info);
#endif
}

/**
 * @brief Initializes the Vulkan physical device and logical device.
 *
 * @param context A Vulkan context with an instance already set up.
 * @param required_device_extensions The required Vulkan device extensions.
 */
void HelloTriangle::init_device(Context &                        context,
                                const std::vector<const char *> &required_device_extensions)
{
	LOGI("Initializing vulkan device.");

	auto gpus = context.instance.enumeratePhysicalDevices();
	if (gpus.empty())
	{
		throw std::runtime_error("No physical device found.");
	}

	// Select the first GPU we find in the system.
	context.gpu = gpus.front();

	auto queue_family_properties = context.gpu.getQueueFamilyProperties();
	if (queue_family_properties.empty())
	{
		throw std::runtime_error("No queue family found.");
	}

	auto device_extensions = context.gpu.enumerateDeviceExtensionProperties();

	if (!validate_extensions(required_device_extensions, device_extensions))
	{
		throw std::runtime_error("Required device extensions are missing, will try without.");
	}

	auto queue_family_count = vkb::to_u32(queue_family_properties.size());
	for (uint32_t i = 0; i < queue_family_count; i++)
	{
		vk::Bool32 supports_present = context.gpu.getSurfaceSupportKHR(i, context.surface);

		// Find a queue family which supports graphics and presentation.
		if ((queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics) && supports_present)
		{
			context.graphics_queue_index = i;
			break;
		}
	}

	if (context.graphics_queue_index < 0)
	{
		LOGE("Did not find suitable queue which supports graphics, compute and presentation.");
	}

	float queue_priority = 1.0f;

	// Create one queue
	vk::DeviceQueueCreateInfo queue_info;
	queue_info.queueFamilyIndex = context.graphics_queue_index;
	queue_info.queueCount       = 1;
	queue_info.pQueuePriorities = &queue_priority;

	vk::DeviceCreateInfo device_info;
	device_info.queueCreateInfoCount    = 1;
	device_info.pQueueCreateInfos       = &queue_info;
	device_info.enabledExtensionCount   = vkb::to_u32(required_device_extensions.size());
	device_info.ppEnabledExtensionNames = required_device_extensions.data();

	context.device = context.gpu.createDevice(device_info);
	volkLoadDevice(context.device);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(context.device);

	context.queue = context.device.getQueue(context.graphics_queue_index, 0);
}

/**
 * @brief Initializes per frame data.
 * @param context A newly created Vulkan context.
 * @param per_frame The data of a frame.
 */
void HelloTriangle::init_per_frame(Context &context, PerFrame &per_frame)
{
	vk::FenceCreateInfo info;
	per_frame.queue_submit_fence = context.device.createFence({vk::FenceCreateFlagBits::eSignaled});

	vk::CommandPoolCreateInfo cmd_pool_info;
	cmd_pool_info.queueFamilyIndex = context.graphics_queue_index;
	per_frame.primary_command_pool = context.device.createCommandPool(cmd_pool_info);

	vk::CommandBufferAllocateInfo cmd_buf_info;
	cmd_buf_info.commandPool         = per_frame.primary_command_pool;
	cmd_buf_info.level               = vk::CommandBufferLevel::ePrimary;
	cmd_buf_info.commandBufferCount  = 1;
	per_frame.primary_command_buffer = context.device.allocateCommandBuffers(cmd_buf_info)[0];

	per_frame.device      = context.device;
	per_frame.queue_index = context.graphics_queue_index;
}

/**
 * @brief Tears down the frame data.
 * @param context The Vulkan context.
 * @param per_frame The data of a frame.
 */
void HelloTriangle::teardown_per_frame(Context &context, PerFrame &per_frame)
{
	if (per_frame.queue_submit_fence)
	{
		context.device.destroy(per_frame.queue_submit_fence);

		per_frame.queue_submit_fence = nullptr;
	}

	if (per_frame.primary_command_buffer)
	{
		context.device.freeCommandBuffers(per_frame.primary_command_pool, per_frame.primary_command_buffer);

		per_frame.primary_command_buffer = nullptr;
	}

	if (per_frame.primary_command_pool)
	{
		context.device.destroy(per_frame.primary_command_pool);

		per_frame.primary_command_pool = nullptr;
	}

	if (per_frame.swapchain_acquire_semaphore)
	{
		context.device.destroy(per_frame.swapchain_acquire_semaphore);

		per_frame.swapchain_acquire_semaphore = nullptr;
	}

	if (per_frame.swapchain_release_semaphore)
	{
		context.device.destroy(per_frame.swapchain_release_semaphore);

		per_frame.swapchain_release_semaphore = nullptr;
	}

	per_frame.device      = nullptr;
	per_frame.queue_index = -1;
}

/**
 * @brief Initializes the Vulkan swapchain.
 * @param context A Vulkan context with a physical device already set up.
 */
void HelloTriangle::init_swapchain(Context &context)
{
	vk::SurfaceCapabilitiesKHR surface_properties = context.gpu.getSurfaceCapabilitiesKHR(context.surface);

	auto     formats      = context.gpu.getSurfaceFormatsKHR(context.surface);
	uint32_t format_count = vkb::to_u32(formats.size());

	vk::SurfaceFormatKHR format;
	if (format_count == 1 && formats[0].format == vk::Format::eUndefined)
	{
		// There is no preferred format, so pick a default one
		format        = formats[0];
		format.format = vk::Format::eB8G8R8A8Unorm;
	}
	else
	{
		if (format_count == 0)
		{
			throw std::runtime_error("Surface has no formats.");
		}

		format.format = vk::Format::eUndefined;
		for (auto &candidate : formats)
		{
			switch (candidate.format)
			{
				case vk::Format::eR8G8B8A8Unorm:
				case vk::Format::eB8G8R8A8Unorm:
				case vk::Format::eA8B8G8R8UnormPack32:
					format = candidate;
					break;

				default:
					break;
			}

			if (format.format != vk::Format::eUndefined)
			{
				break;
			}
		}

		if (format.format == vk::Format::eUndefined)
		{
			format = formats[0];
		}
	}

	vk::Extent2D swapchain_size = surface_properties.currentExtent;

	// FIFO must be supported by all implementations.
	vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;

	// Determine the number of vk::Image's to use in the swapchain.
	// Ideally, we desire to own 1 image at a time, the rest of the images can
	// either be rendered to and/or being queued up for display.
	uint32_t desired_swapchain_images = surface_properties.minImageCount + 1;
	if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount))
	{
		// Application must settle for fewer images than desired.
		desired_swapchain_images = surface_properties.maxImageCount;
	}

	// Figure out a suitable surface transform.
	vk::SurfaceTransformFlagBitsKHR pre_transform;
	if (surface_properties.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
	{
		pre_transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
	}
	else
	{
		pre_transform = surface_properties.currentTransform;
	}

	vk::SwapchainKHR old_swapchain = context.swapchain;

	// Find a supported composite type.
	vk::CompositeAlphaFlagBitsKHR composite = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)
	{
		composite = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	}
	else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
	{
		composite = vk::CompositeAlphaFlagBitsKHR::eInherit;
	}
	else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
	{
		composite = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
	}
	else if (surface_properties.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
	{
		composite = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
	}

	vk::SwapchainCreateInfoKHR info;
	info.surface            = context.surface;
	info.minImageCount      = desired_swapchain_images;
	info.imageFormat        = format.format;
	info.imageColorSpace    = format.colorSpace;
	info.imageExtent.width  = swapchain_size.width;
	info.imageExtent.height = swapchain_size.height;
	info.imageArrayLayers   = 1;
	info.imageUsage         = vk::ImageUsageFlagBits::eColorAttachment;
	info.imageSharingMode   = vk::SharingMode::eExclusive;
	info.preTransform       = pre_transform;
	info.compositeAlpha     = composite;
	info.presentMode        = swapchain_present_mode;
	info.clipped            = true;
	info.oldSwapchain       = old_swapchain;

	context.swapchain = context.device.createSwapchainKHR(info);

	if (old_swapchain)
	{
		for (vk::ImageView image_view : context.swapchain_image_views)
		{
			context.device.destroy(image_view);
		}

		uint32_t image_count;
		vkGetSwapchainImagesKHR(context.device, old_swapchain, &image_count, nullptr);

		for (size_t i = 0; i < image_count; i++)
		{
			teardown_per_frame(context, context.per_frame[i]);
		}

		context.swapchain_image_views.clear();

		context.device.destroy(old_swapchain);
	}

	context.swapchain_dimensions = {swapchain_size.width, swapchain_size.height, format.format};

	/// The swapchain images.
	auto swapchain_images = context.device.getSwapchainImagesKHR(context.swapchain);
	auto image_count      = vkb::to_u32(swapchain_images.size());

	// Initialize per-frame resources.
	// Every swapchain image has its own command pool and fence manager.
	// This makes it very easy to keep track of when we can reset command buffers and such.
	context.per_frame.clear();
	context.per_frame.resize(image_count);

	for (size_t i = 0; i < image_count; i++)
	{
		init_per_frame(context, context.per_frame[i]);
	}

	for (size_t i = 0; i < image_count; i++)
	{
		// Create an image view which we can render into.
		vk::ImageViewCreateInfo view_info;
		view_info.viewType                    = vk::ImageViewType::e2D;
		view_info.format                      = context.swapchain_dimensions.format;
		view_info.image                       = swapchain_images[i];
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;

		vk::ImageView image_view;
		image_view = context.device.createImageView(view_info);

		context.swapchain_image_views.push_back(image_view);
	}
}

/**
 * @brief Initializes the Vulkan render pass.
 * @param context A Vulkan context with a device already set up.
 */
void HelloTriangle::init_render_pass(Context &context)
{
	vk::AttachmentDescription attachment;
	// Backbuffer format.
	attachment.format = context.swapchain_dimensions.format;
	// Not multisampled.
	attachment.samples = vk::SampleCountFlagBits::e1;
	// When starting the frame, we want tiles to be cleared.
	attachment.loadOp = vk::AttachmentLoadOp::eClear;
	// When ending the frame, we want tiles to be written out.
	attachment.storeOp = vk::AttachmentStoreOp::eStore;
	// Don't care about stencil since we're not using it.
	attachment.stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
	attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;

	// The image layout will be undefined when the render pass begins.
	attachment.initialLayout = vk::ImageLayout::eUndefined;
	// After the render pass is complete, we will transition to PRESENT_SRC_KHR layout.
	attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

	// We have one subpass. This subpass has one color attachment.
	// While executing this subpass, the attachment will be in attachment optimal layout.
	vk::AttachmentReference color_ref = {0, vk::ImageLayout::eColorAttachmentOptimal};

	// We will end up with two transitions.
	// The first one happens right before we start subpass #0, where
	// UNDEFINED is transitioned into COLOR_ATTACHMENT_OPTIMAL.
	// The final layout in the render pass attachment states PRESENT_SRC_KHR, so we
	// will get a final transition from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR.
	vk::SubpassDescription subpass;
	subpass.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments    = &color_ref;

	// Create a dependency to external events.
	// We need to wait for the WSI semaphore to signal.
	// Only pipeline stages which depend on COLOR_ATTACHMENT_OUTPUT_BIT will
	// actually wait for the semaphore, so we must also wait for that pipeline stage.
	vk::SubpassDependency dependency;
	dependency.srcSubpass   = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass   = 0;
	dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	// Since we changed the image layout, we need to make the memory visible to
	// color attachment to modify.
	dependency.srcAccessMask = {};
	dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

	// Finally, create the renderpass.
	vk::RenderPassCreateInfo rp_info;
	rp_info.attachmentCount = 1;
	rp_info.pAttachments    = &attachment;
	rp_info.subpassCount    = 1;
	rp_info.pSubpasses      = &subpass;
	rp_info.dependencyCount = 1;
	rp_info.pDependencies   = &dependency;

	context.render_pass = context.device.createRenderPass(rp_info);
}

/**
 * @brief Helper function to load a shader module.
 * @param context A Vulkan context with a device.
 * @param path The path for the shader (relative to the assets directory).
 * @returns A vk::ShaderModule handle. Aborts execution if shader creation fails.
 */
vk::ShaderModule HelloTriangle::load_shader_module(Context &context, const char *path)
{
	vkb::GLSLCompiler glsl_compiler;

	auto buffer = vkb::fs::read_shader(path);

	std::string file_ext = path;

	// Extract extension name from the glsl shader file
	file_ext = file_ext.substr(file_ext.find_last_of(".") + 1);

	std::vector<uint32_t> spirv;
	std::string           info_log;

	// Compile the GLSL source
	if (!glsl_compiler.compile_to_spirv(find_shader_stage(file_ext), buffer, "main", {}, spirv, info_log))
	{
		LOGE("Failed to compile shader, Error: {}", info_log.c_str());
		return nullptr;
	}

	vk::ShaderModuleCreateInfo module_info;
	module_info.codeSize = spirv.size() * sizeof(uint32_t);
	module_info.pCode    = spirv.data();

	vk::ShaderModule shader_module;
	shader_module = context.device.createShaderModule(module_info);

	return shader_module;
}

/**
 * @brief Initializes the Vulkan pipeline.
 * @param context A Vulkan context with a device and a render pass already set up.
 */
void HelloTriangle::init_pipeline(Context &context)
{
	// Create a blank pipeline layout.
	// We are not binding any resources to the pipeline in this first sample.
	vk::PipelineLayoutCreateInfo layout_info;
	context.pipeline_layout = context.device.createPipelineLayout(layout_info);

	vk::PipelineVertexInputStateCreateInfo vertex_input;

	// Specify we will use triangle lists to draw geometry.
	vk::PipelineInputAssemblyStateCreateInfo input_assembly;
	input_assembly.topology = vk::PrimitiveTopology::eTriangleList;

	// Specify rasterization state.
	vk::PipelineRasterizationStateCreateInfo raster;
	raster.cullMode  = vk::CullModeFlagBits::eBack;
	raster.frontFace = vk::FrontFace::eClockwise;
	raster.lineWidth = 1.0f;

	// Our attachment will write to all color channels, but no blending is enabled.
	vk::PipelineColorBlendAttachmentState blend_attachment;
	blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	vk::PipelineColorBlendStateCreateInfo blend;
	blend.attachmentCount = 1;
	blend.pAttachments    = &blend_attachment;

	// We will have one viewport and scissor box.
	vk::PipelineViewportStateCreateInfo viewport;
	viewport.viewportCount = 1;
	viewport.scissorCount  = 1;

	// Disable all depth testing.
	vk::PipelineDepthStencilStateCreateInfo depth_stencil;

	// No multisampling.
	vk::PipelineMultisampleStateCreateInfo multisample;
	multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

	// Specify that these states will be dynamic, i.e. not part of pipeline state object.
	std::array<vk::DynamicState, 2> dynamics{vk::DynamicState::eViewport, vk::DynamicState::eScissor};

	vk::PipelineDynamicStateCreateInfo dynamic;
	dynamic.pDynamicStates    = dynamics.data();
	dynamic.dynamicStateCount = vkb::to_u32(dynamics.size());

	// Load our SPIR-V shaders.
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages{};

	// Vertex stage of the pipeline
	shader_stages[0].stage  = vk::ShaderStageFlagBits::eVertex;
	shader_stages[0].module = load_shader_module(context, "triangle.vert");
	shader_stages[0].pName  = "main";

	// Fragment stage of the pipeline
	shader_stages[1].stage  = vk::ShaderStageFlagBits::eFragment;
	shader_stages[1].module = load_shader_module(context, "triangle.frag");
	shader_stages[1].pName  = "main";

	vk::GraphicsPipelineCreateInfo pipe;
	pipe.stageCount          = vkb::to_u32(shader_stages.size());
	pipe.pStages             = shader_stages.data();
	pipe.pVertexInputState   = &vertex_input;
	pipe.pInputAssemblyState = &input_assembly;
	pipe.pRasterizationState = &raster;
	pipe.pColorBlendState    = &blend;
	pipe.pMultisampleState   = &multisample;
	pipe.pViewportState      = &viewport;
	pipe.pDepthStencilState  = &depth_stencil;
	pipe.pDynamicState       = &dynamic;

	// We need to specify the pipeline layout and the render pass description up front as well.
	pipe.renderPass = context.render_pass;
	pipe.layout     = context.pipeline_layout;

	context.pipeline = context.device.createGraphicsPipeline(nullptr, pipe);

	// Pipeline is baked, we can delete the shader modules now.
	context.device.destroy(shader_stages[0].module);
	context.device.destroy(shader_stages[1].module);
}

/**
 * @brief Acquires an image from the swapchain.
 * @param context A Vulkan context with a swapchain already set up.
 * @param[out] image The swapchain index for the acquired image.
 * @returns Vulkan result code
 */
vk::Result HelloTriangle::acquire_next_image(Context &context, uint32_t *image)
{
	vk::Semaphore acquire_semaphore;
	if (context.recycled_semaphores.empty())
	{
		acquire_semaphore = context.device.createSemaphore({});
	}
	else
	{
		acquire_semaphore = context.recycled_semaphores.back();
		context.recycled_semaphores.pop_back();
	}

	auto resultValue = context.device.acquireNextImageKHR(context.swapchain, UINT64_MAX, acquire_semaphore, nullptr);
	auto res         = resultValue.result;
	if (res != vk::Result::eSuccess)
	{
		context.recycled_semaphores.push_back(acquire_semaphore);
		return res;
	}
	*image = resultValue.value;

	// If we have outstanding fences for this swapchain image, wait for them to complete first.
	// After begin frame returns, it is safe to reuse or delete resources which
	// were used previously.
	//
	// We wait for fences which completes N frames earlier, so we do not stall,
	// waiting for all GPU work to complete before this returns.
	// Normally, this doesn't really block at all,
	// since we're waiting for old frames to have been completed, but just in case.
	if (context.per_frame[*image].queue_submit_fence)
	{
		context.device.waitForFences(context.per_frame[*image].queue_submit_fence, VK_TRUE, UINT64_MAX);
		context.device.resetFences(context.per_frame[*image].queue_submit_fence);
	}

	if (context.per_frame[*image].primary_command_pool)
	{
		context.device.resetCommandPool(context.per_frame[*image].primary_command_pool, {});
	}

	// Recycle the old semaphore back into the semaphore manager.
	vk::Semaphore old_semaphore = context.per_frame[*image].swapchain_acquire_semaphore;

	if (old_semaphore)
	{
		context.recycled_semaphores.push_back(old_semaphore);
	}

	context.per_frame[*image].swapchain_acquire_semaphore = acquire_semaphore;

	return vk::Result::eSuccess;
}

/**
 * @brief Renders a triangle to the specified swapchain image.
 * @param context A Vulkan context set up for rendering.
 * @param swapchain_index The swapchain index for the image being rendered.
 */
void HelloTriangle::render_triangle(Context &context, uint32_t swapchain_index)
{
	// Render to this framebuffer.
	vk::Framebuffer framebuffer = context.swapchain_framebuffers[swapchain_index];

	// Allocate or re-use a primary command buffer.
	vk::CommandBuffer cmd = context.per_frame[swapchain_index].primary_command_buffer;

	// We will only submit this once before it's recycled.
	vk::CommandBufferBeginInfo begin_info;
	begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	// Begin command recording
	cmd.begin(begin_info);

	// Set clear color values.
	vk::ClearValue clear_value;
	clear_value.color = std::array<float, 4>{0.1f, 0.1f, 0.2f, 1.0f};

	// Begin the render pass.
	vk::RenderPassBeginInfo rp_begin;
	rp_begin.renderPass               = context.render_pass;
	rp_begin.framebuffer              = framebuffer;
	rp_begin.renderArea.extent.width  = context.swapchain_dimensions.width;
	rp_begin.renderArea.extent.height = context.swapchain_dimensions.height;
	rp_begin.clearValueCount          = 1;
	rp_begin.pClearValues             = &clear_value;
	// We will add draw commands in the same command buffer.
	cmd.beginRenderPass(rp_begin, vk::SubpassContents::eInline);

	// Bind the graphics pipeline.
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.pipeline);

	vk::Viewport vp;
	vp.width    = static_cast<float>(context.swapchain_dimensions.width);
	vp.height   = static_cast<float>(context.swapchain_dimensions.height);
	vp.minDepth = 0.0f;
	vp.maxDepth = 1.0f;
	// Set viewport dynamically
	cmd.setViewport(0, vp);

	vk::Rect2D scissor;
	scissor.extent.width  = context.swapchain_dimensions.width;
	scissor.extent.height = context.swapchain_dimensions.height;
	// Set scissor dynamically
	cmd.setScissor(0, scissor);

	// Draw three vertices with one instance.
	cmd.draw(3, 1, 0, 0);

	// Complete render pass.
	cmd.endRenderPass();

	// Complete the command buffer.
	cmd.end();

	// Submit it to the queue with a release semaphore.
	if (!context.per_frame[swapchain_index].swapchain_release_semaphore)
	{
		vk::SemaphoreCreateInfo semaphore_info;
		context.per_frame[swapchain_index].swapchain_release_semaphore = context.device.createSemaphore(semaphore_info);
	}

	vk::PipelineStageFlags wait_stage{vk::PipelineStageFlagBits::eColorAttachmentOutput};

	vk::SubmitInfo info;
	info.commandBufferCount   = 1;
	info.pCommandBuffers      = &cmd;
	info.waitSemaphoreCount   = 1;
	info.pWaitSemaphores      = &context.per_frame[swapchain_index].swapchain_acquire_semaphore;
	info.pWaitDstStageMask    = &wait_stage;
	info.signalSemaphoreCount = 1;
	info.pSignalSemaphores    = &context.per_frame[swapchain_index].swapchain_release_semaphore;
	// Submit command buffer to graphics queue
	context.queue.submit(info, context.per_frame[swapchain_index].queue_submit_fence);
}

/**
 * @brief Presents an image to the swapchain.
 * @param context The Vulkan context, with a swapchain and per-frame resources already set up.
 * @param index The swapchain index previously obtained from @ref acquire_next_image.
 * @returns Vulkan result code
 */
vk::Result HelloTriangle::present_image(Context &context, uint32_t index)
{
	vk::PresentInfoKHR present;
	present.swapchainCount     = 1;
	present.pSwapchains        = &context.swapchain;
	present.pImageIndices      = &index;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores    = &context.per_frame[index].swapchain_release_semaphore;
	// Present swapchain image
	return context.queue.presentKHR(present);
}

/**
 * @brief Initializes the Vulkan frambuffers.
 * @param context A Vulkan context with the render pass already set up.
 */
void HelloTriangle::init_framebuffers(Context &context)
{
	vk::Device device = context.device;

	// Create framebuffer for each swapchain image view
	for (auto &image_view : context.swapchain_image_views)
	{
		// Build the framebuffer.
		vk::FramebufferCreateInfo fb_info;
		fb_info.renderPass      = context.render_pass;
		fb_info.attachmentCount = 1;
		fb_info.pAttachments    = &image_view;
		fb_info.width           = context.swapchain_dimensions.width;
		fb_info.height          = context.swapchain_dimensions.height;
		fb_info.layers          = 1;

		vk::Framebuffer framebuffer = device.createFramebuffer(fb_info);

		context.swapchain_framebuffers.push_back(framebuffer);
	}
}

/**
 * @brief Tears down the framebuffers. If our swapchain changes, we will call this, and create a new swapchain.
 * @param context The Vulkan context.
 */
void HelloTriangle::teardown_framebuffers(Context &context)
{
	// Wait until device is idle before teardown.
	context.queue.waitIdle();

	for (auto &framebuffer : context.swapchain_framebuffers)
	{
		context.device.destroy(framebuffer);
	}

	context.swapchain_framebuffers.clear();
}

/**
 * @brief Tears down the Vulkan context.
 * @param context The Vulkan context.
 */
void HelloTriangle::teardown(Context &context)
{
	// Don't release anything until the GPU is completely idle.
	context.device.waitIdle();

	teardown_framebuffers(context);

	for (auto &per_frame : context.per_frame)
	{
		teardown_per_frame(context, per_frame);
	}

	context.per_frame.clear();

	for (auto semaphore : context.recycled_semaphores)
	{
		context.device.destroy(semaphore);
	}

	if (context.pipeline)
	{
		context.device.destroy(context.pipeline);
	}

	if (context.pipeline_layout)
	{
		context.device.destroy(context.pipeline_layout);
	}

	if (context.render_pass)
	{
		context.device.destroy(context.render_pass);
	}

	for (vk::ImageView image_view : context.swapchain_image_views)
	{
		context.device.destroy(image_view);
	}

	if (context.swapchain)
	{
		context.device.destroy(context.swapchain);
		context.swapchain = nullptr;
	}

	if (context.surface)
	{
		context.instance.destroySurfaceKHR(context.surface);
		context.surface = nullptr;
	}

	if (context.device)
	{
		context.device.destroy();
		context.device = nullptr;
	}

	if (context.debug_callback)
	{
		context.instance.destroyDebugReportCallbackEXT(context.debug_callback);
		context.debug_callback = nullptr;
	}

	vk_instance.reset();
}

HelloTriangle::HelloTriangle()
{
}

HelloTriangle::~HelloTriangle()
{
	teardown(context);
}

bool HelloTriangle::prepare(vkb::Platform &platform)
{
	init_instance(context, {VK_KHR_SURFACE_EXTENSION_NAME}, {});

	vk_instance = std::make_unique<vkb::Instance>(context.instance);

	context.surface = platform.get_window().create_surface(*vk_instance);

	init_device(context, {"VK_KHR_swapchain"});

	init_swapchain(context);

	// Create the necessary objects for rendering.
	init_render_pass(context);
	init_pipeline(context);
	init_framebuffers(context);

	return true;
}

void HelloTriangle::update(float delta_time)
{
	uint32_t index;

	auto res = acquire_next_image(context, &index);

	if (res == vk::Result::eSuboptimalKHR || res == vk::Result::eErrorOutOfDateKHR)
	{
		resize(context.swapchain_dimensions.width, context.swapchain_dimensions.height);
	}
	else if (res != vk::Result::eSuccess)
	{
		context.queue.waitIdle();
		return;
	}

	if (res != vk::Result::eSuccess)
	{
		LOGE("Outdated swapchain image.");
		return;
	}

	render_triangle(context, index);
	res = present_image(context, index);

	// Handle Outdated error in acquire.
	if (res != vk::Result::eSuccess)
	{
		LOGE("Failed to present swapchain image.");
	}
}

void HelloTriangle::resize(const uint32_t, const uint32_t)
{
	if (!context.device)
	{
		return;
	}

	vk::SurfaceCapabilitiesKHR surface_properties = context.gpu.getSurfaceCapabilitiesKHR(context.surface);

	// Only rebuild the swapchain if the dimensions have changed
	if (surface_properties.currentExtent.width == context.swapchain_dimensions.width &&
	    surface_properties.currentExtent.height == context.swapchain_dimensions.height)
	{
		return;
	}

	context.device.waitIdle();
	teardown_framebuffers(context);

	init_swapchain(context);
	init_framebuffers(context);
}

std::unique_ptr<vkb::Application> create_hello_triangle()
{
	return std::make_unique<HelloTriangle>();
}
