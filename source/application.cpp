#include "application.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <math.h>

#include <fstream>
#include <iostream>
#include <vector>

#include <imgui.h>

#include "ico_math.hpp"

namespace application {

namespace {

auto& context = graphics::internal::context;

struct Vertex {
	float position[3];
	float color[3];
};

struct GlobalUniforms {
	float matrix[4][4];
	float tint[4];
};

const float positions[][3] = {
	{ -0.525731f, +0.850651f, +0.000000f },
	{ +0.525731f, +0.850651f, +0.000000f },
	{ -0.525731f, -0.850651f, +0.000000f },
	{ +0.525731f, -0.850651f, +0.000000f },
	{ +0.000000f, -0.525731f, +0.850651f },
	{ +0.000000f, +0.525731f, +0.850651f },
	{ +0.000000f, -0.525731f, -0.850651f },
	{ +0.000000f, +0.525731f, -0.850651f },
	{ +0.850651f, +0.000000f, -0.525731f },
	{ +0.850651f, +0.000000f, +0.525731f },
	{ -0.850651f, +0.000000f, -0.525731f },
	{ -0.850651f, +0.000000f, +0.525731f },
};
constexpr uint32_t vertex_count = sizeof(positions) / sizeof(positions[0]);

constexpr float max_coordinate = 0.850651f;

Vertex vertices[vertex_count];

const uint32_t indices[] = {
	0, 11, 5,   0, 5, 1,    0, 1, 7,    0, 7, 10,   0, 10, 11,
	1, 5, 9,    5, 11, 4,   11, 10, 2,  10, 7, 6,   7, 1, 8,
	3, 9, 4,    3, 4, 2,    3, 2, 6,    3, 6, 8,    3, 8, 9,
	4, 9, 5,    2, 4, 11,   6, 2, 10,   8, 6, 7,    9, 8, 1,
};
constexpr uint32_t index_count = sizeof(indices) / sizeof(indices[0]);

enum {
	PROJECTION_PERSPECTIVE,
	PROJECTION_ORTHOGRAPHIC,
};

constexpr float pi = 3.14159265f;
constexpr float camera_distance = 3.0f;
constexpr float near_plane = 0.1f;
constexpr float far_plane = 100.0f;

int projection_type = PROJECTION_PERSPECTIVE;
float fov_degrees = 60.0f;
float ortho_size = 3.0f; 

float position[3] = { 0.0f, 0.0f, 0.0f };
float rotation_degrees[3] = { 0.0f, 0.0f, 0.0f };
float scale_factor[3] = { 1.0f, 1.0f, 1.0f };

bool animation_playing = true;
float animation_speed = 1.0f;     
float orbit_radius = 1.0f;        
float wave_amplitude = 0.5f;      
int wave_count = 3;               
float spin_speed = 1.0f;          
float animation_time = 0.0f;      

float tint[3] = { 1.0f, 1.0f, 1.0f };

double current_time;
double previous_time;

VkShaderModule vk_vertex_shader;
VkShaderModule vk_fragment_shader;

VkDescriptorSetLayout vk_descriptor_set_layout;
VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;

VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_descriptor_set;

VkBuffer vk_vertex_buffer;
VmaAllocation vk_vertex_buffer_allocation;

VkBuffer vk_index_buffer;
VmaAllocation vk_index_buffer_allocation;

VkBuffer vk_uniform_buffer;
VmaAllocation vk_uniform_buffer_allocation;
GlobalUniforms* vk_uniform_buffer_memory;

VkShaderModule loadShaderModule(const char path[]) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader file " << path << '\n';
		return VK_NULL_HANDLE;
	}

	const size_t size = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> buffer(size / sizeof(uint32_t));

	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), size);
	file.close();

	const VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = buffer.data(),
	};

	VkShaderModule result;
	if (vkCreateShaderModule(context.device, &info, nullptr, &result) != VK_SUCCESS) {
		return VK_NULL_HANDLE;
	}

	return result;
}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer& buffer, VmaAllocation& allocation, void** mapped) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo result_info;
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &buffer, &allocation, &result_info) != VK_SUCCESS) {
		return false;
	}

	*mapped = result_info.pMappedData;
	return true;
}

void assignVertexColors() {
	for (uint32_t i = 0; i < vertex_count; i++) {
		for (int k = 0; k < 3; k++) {
			vertices[i].position[k] = positions[i][k];
			vertices[i].color[k] = 0.5f + 0.5f * positions[i][k] / max_coordinate;
		}
	}
}

void uploadUniforms() {
	if (context.swapchain_extent.height == 0) {
		return;
	}

	const float to_radians = pi / 180.0f;

	const float aspect = (float)context.swapchain_extent.width /
	                     (float)context.swapchain_extent.height;

	const float phase = animation_time;
	const float offset_x = orbit_radius * cosf(phase);
	const float offset_y = wave_amplitude * sinf((float)wave_count * phase);
	const float offset_z = orbit_radius * sinf(phase);

	const float spin_angle = phase * spin_speed;
	const ico::Mat4 spin = ico::mul(ico::rotate_y(spin_angle),
	                                ico::rotate_x(spin_angle * 0.5f));

	const ico::Mat4 orientation = ico::mul(ico::rotate_z(rotation_degrees[2] * to_radians),
	                                       ico::mul(ico::rotate_y(rotation_degrees[1] * to_radians),
	                                                ico::rotate_x(rotation_degrees[0] * to_radians)));

	const ico::Mat4 model = ico::mul(
		ico::translate(position[0] + offset_x, position[1] + offset_y, position[2] + offset_z),
		ico::mul(orientation,
		         ico::mul(spin, ico::scale(scale_factor[0], scale_factor[1], scale_factor[2]))));

	const ico::Mat4 view = ico::translate(0.0f, 0.0f, camera_distance);

	ico::Mat4 projection;
	if (projection_type == PROJECTION_ORTHOGRAPHIC) {
		const float half_height = ortho_size;
		const float half_width = half_height * aspect;
		projection = ico::orthographic(-half_width, half_width, -half_height, half_height,
		                               near_plane, far_plane);
	} else {
		projection = ico::perspective(fov_degrees * to_radians, aspect, near_plane, far_plane);
	}

	const ico::Mat4 mvp = ico::mul(projection, ico::mul(view, model));

	std::memcpy(vk_uniform_buffer_memory->matrix, mvp.m, sizeof(mvp.m));
	vk_uniform_buffer_memory->tint[0] = tint[0];
	vk_uniform_buffer_memory->tint[1] = tint[1];
	vk_uniform_buffer_memory->tint[2] = tint[2];
	vk_uniform_buffer_memory->tint[3] = 1.0f;
	vmaFlushAllocation(context.allocator, vk_uniform_buffer_allocation, 0, VK_WHOLE_SIZE);
}

}

bool initialize() {
	assignVertexColors();

	vk_vertex_shader = loadShaderModule("./shaders/shader.vert.spv");
	vk_fragment_shader = loadShaderModule("./shaders/shader.frag.spv");
	if (!vk_vertex_shader || !vk_fragment_shader) {
		std::cerr << "Failed to load shaders\n";
		return false;
	}

	const VkDescriptorSetLayoutBinding descriptor_set_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo descriptor_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(descriptor_set_bindings) / sizeof(descriptor_set_bindings[0]),
		.pBindings = descriptor_set_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout, nullptr,
	                                &vk_descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		return false;
	}

	const VkPipelineShaderStageCreateInfo stage_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vk_vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = vk_fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription vertex_bindings[] = {
		{
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		},
	};

	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = sizeof(vertex_bindings) / sizeof(vertex_bindings[0]),
		.pVertexBindingDescriptions = vertex_bindings,
		.vertexAttributeDescriptionCount = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo assembly_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo sample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = true,
		.depthWriteEnable = true,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	const VkPipelineColorBlendAttachmentState attachment_info = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		                  VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT |
		                  VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment_info,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stage_infos) / sizeof(stage_infos[0]),
		.pStages = stage_infos,
		.pVertexInputState = &vertex_input_state,
		.pInputAssemblyState = &assembly_state_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &sample_info,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state,
		.layout = vk_pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
	                              nullptr, &vk_pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}

	void* mapped = nullptr;

	if (!createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	                  vk_vertex_buffer, vk_vertex_buffer_allocation, &mapped)) {
		std::cerr << "Failed to create vertex buffer\n";
		return false;
	}
	std::memcpy(mapped, vertices, sizeof(vertices));
	vmaFlushAllocation(context.allocator, vk_vertex_buffer_allocation, 0, VK_WHOLE_SIZE);

	if (!createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	                  vk_index_buffer, vk_index_buffer_allocation, &mapped)) {
		std::cerr << "Failed to create index buffer\n";
		return false;
	}
	std::memcpy(mapped, indices, sizeof(indices));
	vmaFlushAllocation(context.allocator, vk_index_buffer_allocation, 0, VK_WHOLE_SIZE);

	if (!createBuffer((sizeof(GlobalUniforms) + 0xf) & ~0xf, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                  vk_uniform_buffer, vk_uniform_buffer_allocation, &mapped)) {
		std::cerr << "Failed to create uniform buffer\n";
		return false;
	}
	vk_uniform_buffer_memory = static_cast<GlobalUniforms*>(mapped);

	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
		},
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo descriptor_set = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &descriptor_set,
	                             &vk_descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor set\n";
		return false;
	}

	const VkDescriptorBufferInfo uniform_buffer_descriptor = {
		.buffer = vk_uniform_buffer,
		.offset = 0,
		.range = sizeof(GlobalUniforms),
	};

	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = vk_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &uniform_buffer_descriptor,
		},
	};

	vkUpdateDescriptorSets(context.device,
	                       sizeof(descriptor_writes) / sizeof(descriptor_writes[0]),
	                       descriptor_writes, 0, nullptr);

	return true;
}

void shutdown() {
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_descriptor_set_layout, nullptr);

	vmaDestroyBuffer(context.allocator, vk_uniform_buffer, vk_uniform_buffer_allocation);
	vmaDestroyBuffer(context.allocator, vk_index_buffer, vk_index_buffer_allocation);
	vmaDestroyBuffer(context.allocator, vk_vertex_buffer, vk_vertex_buffer_allocation);

	vkDestroyShaderModule(context.device, vk_fragment_shader, nullptr);
	vkDestroyShaderModule(context.device, vk_vertex_shader, nullptr);
}

void update(double time) {

	current_time = time;

	const float delta = (float)(time - previous_time);
	previous_time = time;
	if (animation_playing) {
		animation_time += delta * animation_speed;
	}

	ImGui::Begin("Icosahedron");

	ImGui::SeparatorText("Projection");
	ImGui::RadioButton("Perspective", &projection_type, PROJECTION_PERSPECTIVE);
	ImGui::SameLine();
	ImGui::RadioButton("Orthographic", &projection_type, PROJECTION_ORTHOGRAPHIC);
	if (projection_type == PROJECTION_PERSPECTIVE) {
		ImGui::SliderFloat("FOV (deg)", &fov_degrees, 20.0f, 120.0f);
	} else {
		ImGui::SliderFloat("Box size", &ortho_size, 0.5f, 10.0f);
	}

	ImGui::SeparatorText("Transform");
	ImGui::SliderFloat3("Position", position, -2.0f, 2.0f);
	ImGui::SliderFloat3("Rotation (deg)", rotation_degrees, -180.0f, 180.0f);
	ImGui::SliderFloat3("Scale", scale_factor, 0.1f, 3.0f);
	if (ImGui::Button("Reset transform")) {
		for (int k = 0; k < 3; k++) {
			position[k] = 0.0f;
			rotation_degrees[k] = 0.0f;
			scale_factor[k] = 1.0f;
		}
	}

	ImGui::SeparatorText("Animation");
	if (ImGui::Button(animation_playing ? "Pause" : "Play")) {
		animation_playing = !animation_playing;
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart")) {
		animation_time = 0.0f;
	}
	ImGui::SliderFloat("Speed", &animation_speed, 0.0f, 5.0f);
	ImGui::SliderFloat("Orbit radius", &orbit_radius, 0.0f, 2.0f);
	ImGui::SliderFloat("Wave amplitude", &wave_amplitude, 0.0f, 1.5f);
	ImGui::SliderInt("Waves per orbit", &wave_count, 1, 8);
	ImGui::SliderFloat("Spin speed", &spin_speed, 0.0f, 5.0f);

	ImGui::SeparatorText("Color");
	ImGui::ColorEdit3("Tint", tint);

	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	uploadUniforms();

	vkResetCommandBuffer(fd.command_buffer, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

	const VkClearValue clear_values[] = {
		{ .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(context.swapchain_extent.width),
		.height = static_cast<float>(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	const VkRect2D scissor = { .extent = context.swapchain_extent };

	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	const VkDeviceSize vertex_buffer_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vk_vertex_buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, vk_index_buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        vk_pipeline_layout, 0, 1, &vk_descriptor_set, 0, nullptr);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

	vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

}
