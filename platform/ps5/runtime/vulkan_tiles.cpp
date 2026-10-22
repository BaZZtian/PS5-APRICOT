// SPDX-License-Identifier: GPL-3.0-or-later
#include "vulkan_tiles.h"
#include <cstring>
bool VulkanTiles::check(VkResult result,const char *operation) {
    if(result==VK_SUCCESS)return true;error=std::string(operation)+": "+std::to_string(int(result));return false;
}
bool VulkanTiles::buffer(Buffer &to,VkDeviceSize bytes) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=bytes;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(!check(CreateBuffer(device,&info,nullptr,&to.buffer),"create buffer"))return false;
    VkMemoryRequirements requirements{};GetBufferMemoryRequirements(device,to.buffer,&requirements);
    int selected=-1;
    for(unsigned pass=0;pass<2&&selected<0;++pass)for(unsigned n=0;n<memory_properties.memoryTypeCount;++n) {
        auto flags=memory_properties.memoryTypes[n].propertyFlags;
        if((requirements.memoryTypeBits&(1u<<n))&&(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)&&(pass||(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {selected=int(n);break;}
    }
    if(selected<0) {error="no host-visible storage memory";return false;}
    to.coherent=memory_properties.memoryTypes[selected].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=unsigned(selected);
    return check(AllocateMemory(device,&allocate,nullptr,&to.memory),"allocate buffer memory")&&
        check(BindBufferMemory(device,to.buffer,to.memory,0),"bind buffer memory")&&
        check(MapMemory(device,to.memory,0,VK_WHOLE_SIZE,0,&to.mapped),"map buffer memory");
}
bool VulkanTiles::sync(Buffer &b,bool invalidate) {
    if(b.coherent)return true;
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=b.memory;range.size=VK_WHOLE_SIZE;
    return check(invalidate?InvalidateMappedMemoryRanges(device,1,&range):FlushMappedMemoryRanges(device,1,&range),invalidate?"invalidate memory":"flush memory");
}
bool VulkanTiles::initialize(PFN_vkGetInstanceProcAddr loader,const uint32_t *spirv,size_t bytes,const uint32_t *resolve,size_t resolve_bytes) {
    if(instance||!loader||!spirv||bytes<20||bytes%4||spirv[0]!=0x07230203) {error="invalid compositor initialization";return false;}
    if(resolve&&(!resolve_bytes||resolve_bytes%4||resolve[0]!=0x07230203)||!resolve&&resolve_bytes) {error="invalid resolve shader";return false;}
    auto create=reinterpret_cast<PFN_vkCreateInstance>(loader(VK_NULL_HANDLE,"vkCreateInstance"));if(!create) {error="Vulkan instance entry unavailable";return false;}
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};application.pApplicationName="Orqenva";application.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&application;
    if(!check(create(&info,nullptr,&instance),"create instance"))return false;
#define OQ_LOAD_INSTANCE(name) name=reinterpret_cast<PFN_vk##name>(loader(instance,"vk" #name));if(!name){error="missing vk" #name;return false;}
    OQ_VK_INSTANCE(OQ_LOAD_INSTANCE)
#undef OQ_LOAD_INSTANCE
    uint32_t count=0;if(!check(EnumeratePhysicalDevices(instance,&count,nullptr),"enumerate devices")||!count) {error="no Vulkan device";return false;}
    std::vector<VkPhysicalDevice> devices(count);if(!check(EnumeratePhysicalDevices(instance,&count,devices.data()),"read devices"))return false;
    physical=devices[0];GetPhysicalDeviceProperties(physical,&properties);GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
    uint32_t family_count=0;GetPhysicalDeviceQueueFamilyProperties(physical,&family_count,nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);GetPhysicalDeviceQueueFamilyProperties(physical,&family_count,families.data());
    unsigned family=family_count;
    for(unsigned pass=0;pass<2&&family==family_count;++pass)for(unsigned n=0;n<family_count;++n)
        if(families[n].queueCount&&(families[n].queueFlags&VK_QUEUE_COMPUTE_BIT)&&(pass||(families[n].queueFlags&VK_QUEUE_GRAPHICS_BIT))) {family=n;break;}
    if(family==family_count) {error="no compute queue";return false;}
    float priority=1;VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue_info.queueFamilyIndex=family;queue_info.queueCount=1;queue_info.pQueuePriorities=&priority;
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue_info;
    if(!check(CreateDevice(physical,&device_info,nullptr,&device),"create device"))return false;
#define OQ_LOAD_DEVICE(name) name=reinterpret_cast<PFN_vk##name>(GetDeviceProcAddr(device,"vk" #name));if(!name){error="missing vk" #name;return false;}
    OQ_VK_DEVICE(OQ_LOAD_DEVICE)
#undef OQ_LOAD_DEVICE
    GetDeviceQueue(device,family,0,&queue);
    if(!buffer(input,input_capacity)||!buffer(output,256*384*4)||!buffer(status,4)||!buffer(work,256*384*4))return false;
    VkDescriptorSetLayoutBinding bindings[4]{};
    for(unsigned n=0;n<4;++n) {bindings[n].binding=n;bindings[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;bindings[n].descriptorCount=1;bindings[n].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
    VkDescriptorSetLayoutCreateInfo descriptor_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor_info.bindingCount=4;descriptor_info.pBindings=bindings;
    if(!check(CreateDescriptorSetLayout(device,&descriptor_info,nullptr,&descriptors),"create descriptor layout"))return false;
    VkPipelineLayoutCreateInfo pipeline_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pipeline_layout.setLayoutCount=1;pipeline_layout.pSetLayouts=&descriptors;
    if(!check(CreatePipelineLayout(device,&pipeline_layout,nullptr,&layout),"create pipeline layout"))return false;
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader_info.codeSize=bytes;shader_info.pCode=spirv;
    if(!check(CreateShaderModule(device,&shader_info,nullptr,&shader),"create compositor shader"))return false;
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};compute.layout=layout;compute.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;compute.stage.module=shader;compute.stage.pName="main";
    if(!check(CreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&pipeline),"create compute pipeline"))return false;
    DestroyShaderModule(device,shader,nullptr);shader=VK_NULL_HANDLE;
    if(resolve) {
        shader_info.codeSize=resolve_bytes;shader_info.pCode=resolve;
        if(!check(CreateShaderModule(device,&shader_info,nullptr,&shader),"create resolve shader"))return false;
        compute.stage.module=shader;
        if(!check(CreateComputePipelines(device,VK_NULL_HANDLE,1,&compute,nullptr,&resolve_pipeline),"create resolve pipeline"))return false;
        DestroyShaderModule(device,shader,nullptr);shader=VK_NULL_HANDLE;
    }
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
    if(!check(CreateDescriptorPool(device,&pool,nullptr,&descriptor_pool),"create descriptor pool"))return false;
    VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};set_info.descriptorPool=descriptor_pool;set_info.descriptorSetCount=1;set_info.pSetLayouts=&descriptors;
    if(!check(AllocateDescriptorSets(device,&set_info,&set),"allocate descriptors"))return false;
    VkDescriptorBufferInfo buffer_info[4]={{input.buffer,0,input_capacity},{output.buffer,0,256*384*4},{status.buffer,0,4},{work.buffer,0,256*384*4}};
    VkWriteDescriptorSet writes[4]{};
    for(unsigned n=0;n<4;++n) {writes[n].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[n].dstSet=set;writes[n].dstBinding=n;writes[n].descriptorCount=1;writes[n].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[n].pBufferInfo=&buffer_info[n];}
    UpdateDescriptorSets(device,4,writes,0,nullptr);
    VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};commands.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;commands.queueFamilyIndex=family;
    if(!check(CreateCommandPool(device,&commands,nullptr,&command_pool),"create command pool"))return false;
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command_info.commandPool=command_pool;command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_info.commandBufferCount=1;
    if(!check(AllocateCommandBuffers(device,&command_info,&command),"allocate command buffer"))return false;
    VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};return check(CreateFence(device,&fence_info,nullptr,&fence),"create fence");
}
bool VulkanTiles::render(const std::vector<uint8_t> &state_bytes,std::vector<uint32_t> &pixels,uint32_t &unsupported) {
    if(!pipeline||submitted||state_bytes.size()<64||state_bytes.size()>input_capacity||state_bytes.size()%4) {error="invalid compositor state";return false;}
    std::memcpy(input.mapped,state_bytes.data(),state_bytes.size());std::memset(status.mapped,0,4);
    if(!sync(input,false)||!sync(status,false)||!check(ResetCommandBuffer(command,0),"reset commands"))return false;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(!check(BeginCommandBuffer(command,&begin),"begin commands"))return false;
    VkMemoryBarrier upload{VK_STRUCTURE_TYPE_MEMORY_BARRIER};upload.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;upload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&upload,0,nullptr,0,nullptr);
    CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);CmdDispatch(command,32,48,1);
    if(resolve_pipeline) {
        VkMemoryBarrier between{VK_STRUCTURE_TYPE_MEMORY_BARRIER};between.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;between.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&between,0,nullptr,0,nullptr);
        CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,resolve_pipeline);CmdDispatch(command,32,48,1);
    }
    VkMemoryBarrier download{VK_STRUCTURE_TYPE_MEMORY_BARRIER};download.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;download.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&download,0,nullptr,0,nullptr);
    if(!check(EndCommandBuffer(command),"end commands")||!check(ResetFences(device,1,&fence),"reset fence"))return false;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    if(!check(QueueSubmit(queue,1,&submit,fence),"submit compute"))return false;submitted=true;
    if(!check(WaitForFences(device,1,&fence,VK_TRUE,10000000000ull),"wait compute"))return false;submitted=false;
    if(!sync(output,true)||!sync(status,true))return false;
    pixels.resize(256*384);std::memcpy(pixels.data(),output.mapped,pixels.size()*4);std::memcpy(&unsupported,status.mapped,4);return true;
}
void VulkanTiles::close() {
    // Never free memory still used by an unresponsive GPU or wait forever here.
    // A timed-out submission intentionally retains its resources until process exit.
    if(device&&submitted&&WaitForFences) {
        if(WaitForFences(device,1,&fence,VK_TRUE,1000000000ull)!=VK_SUCCESS)return;
        submitted=false;
    }
    if(device) {
        if(fence&&DestroyFence)DestroyFence(device,fence,nullptr);
        if(command_pool&&DestroyCommandPool)DestroyCommandPool(device,command_pool,nullptr);
        if(descriptor_pool&&DestroyDescriptorPool)DestroyDescriptorPool(device,descriptor_pool,nullptr);
        if(pipeline&&DestroyPipeline)DestroyPipeline(device,pipeline,nullptr);
        if(resolve_pipeline&&DestroyPipeline)DestroyPipeline(device,resolve_pipeline,nullptr);
        if(shader&&DestroyShaderModule)DestroyShaderModule(device,shader,nullptr);
        if(layout&&DestroyPipelineLayout)DestroyPipelineLayout(device,layout,nullptr);
        if(descriptors&&DestroyDescriptorSetLayout)DestroyDescriptorSetLayout(device,descriptors,nullptr);
        for(auto b:{&input,&output,&status,&work}) {
            if(b->mapped&&UnmapMemory)UnmapMemory(device,b->memory);
            if(b->buffer&&DestroyBuffer)DestroyBuffer(device,b->buffer,nullptr);
            if(b->memory&&FreeMemory)FreeMemory(device,b->memory,nullptr);*b=Buffer{};
        }
        if(DestroyDevice)DestroyDevice(device,nullptr);device=VK_NULL_HANDLE;
    }
    if(instance&&DestroyInstance)DestroyInstance(instance,nullptr);instance=VK_NULL_HANDLE;
    fence=VK_NULL_HANDLE;command_pool=VK_NULL_HANDLE;descriptor_pool=VK_NULL_HANDLE;pipeline=VK_NULL_HANDLE;
    resolve_pipeline=VK_NULL_HANDLE;
    shader=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;descriptors=VK_NULL_HANDLE;command=VK_NULL_HANDLE;set=VK_NULL_HANDLE;
}
