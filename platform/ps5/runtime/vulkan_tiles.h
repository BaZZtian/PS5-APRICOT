// SPDX-License-Identifier: GPL-3.0-or-later
// Original Vulkan compute compositor plumbing. No window/swapchain yet.
#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <vector>
#include <string>
#define OQ_VK_INSTANCE(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define OQ_VK_DEVICE(X) X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) X(InvalidateMappedMemoryRanges) X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch) X(CreateFence) X(DestroyFence) X(ResetFences) X(QueueSubmit) X(WaitForFences)
struct VulkanTiles {
    static constexpr unsigned input_capacity=8*1024*1024;
    struct Buffer {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void *mapped=nullptr;bool coherent=false;};
    VkInstance instance=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE,resolve_pipeline=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;VkCommandPool command_pool=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    Buffer input,output,status,work;
    VkPhysicalDeviceMemoryProperties memory_properties{};VkPhysicalDeviceProperties properties{};
    std::string error;
    bool submitted=false;
#define OQ_DECLARE(name) PFN_vk##name name=nullptr;
    OQ_VK_INSTANCE(OQ_DECLARE)
    OQ_VK_DEVICE(OQ_DECLARE)
#undef OQ_DECLARE
    bool check(VkResult result,const char *operation);
    bool buffer(Buffer &to,VkDeviceSize bytes);
    bool sync(Buffer &buffer,bool invalidate);
    bool initialize(PFN_vkGetInstanceProcAddr loader,const uint32_t *spirv,size_t bytes,const uint32_t *resolve=nullptr,size_t resolve_bytes=0);
    bool render(const std::vector<uint8_t> &state,std::vector<uint32_t> &pixels,uint32_t &unsupported);
    void close();
    ~VulkanTiles(){close();}
    VulkanTiles()=default;
    VulkanTiles(const VulkanTiles&)=delete;VulkanTiles &operator=(const VulkanTiles&)=delete;
};
