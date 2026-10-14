// Fixed fixture renderer. Calls the downstream driver dispatch, never our hooks.
static std::vector<uint8_t> vertexBytes,atlasBytes,overlayBytes;
static std::vector<uint32_t> meshVS,meshFS,overlayVS,overlayFS,gbufferFS,hdrFS,shadowFS; // the last three: integrate.h
static std::atomic<bool> fixtureReady{false},renderEnabled{false},overlayEnabled{false};
static float fixtureAnchor[3]={-556.6f,127.5f,1426.8f};
static std::atomic_flag anchorBusy=ATOMIC_FLAG_INIT;

// The native graphics state of a command buffer, replayed after our draw.
// It is updated on every native bind (about ten thousand a frame), so it
// allocates nothing. Descriptor sets are kept per set number with that set's
// dynamic offsets: the pinned Cemu binds the vertex and the pixel shader's sets
// in one call when both changed and singly otherwise
// (VulkanRendererCore.cpp), and remembers what it bound. A call that does not
// fit sets `overflow` and our draw is skipped for that command buffer rather
// than replaying an incomplete state.
// How many dynamic uniform buffers a descriptor set has (all of Cemu's uniform
// buffers are dynamic): written when a set is updated, read when it is bound,
// both on the render thread.
static std::unordered_map<VkDescriptorSet,uint32_t> dynamicCounts;
struct NativeState {
    VkPipeline pipeline=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
    struct Slot{VkDescriptorSet set=VK_NULL_HANDLE;uint32_t offsetCount=0;uint32_t offsets[20]{};};
    struct Constants{VkPipelineLayout layout;VkShaderStageFlags stages;uint32_t offset;std::vector<uint8_t> bytes;};
    std::array<Slot,4> slots;bool overflow=false;std::vector<Constants> constants;
    VkBuffer vertex=VK_NULL_HANDLE;VkDeviceSize vertexOffset=0;
    VkViewport viewport{};VkRect2D scissor{};bool hasViewport=false,hasScissor=false;
    // Dynamic state Cemu sets once and remembers; a pipeline of ours without it makes it undefined.
    float bias[3]{},blend[4]{};bool hasBias=false,hasBlend=false;
    void clear(){pipeline=VK_NULL_HANDLE;layout=VK_NULL_HANDLE;for(auto& slot:slots)slot.set=VK_NULL_HANDLE;overflow=false;constants.clear();vertex=VK_NULL_HANDLE;vertexOffset=0;hasViewport=hasScissor=hasBias=hasBlend=false;}
    void bind(VkPipelineLayout l,uint32_t first,uint32_t count,const VkDescriptorSet* sets,uint32_t n,const uint32_t* offsets){
        if(layout!=l)for(auto& slot:slots)slot.set=VK_NULL_HANDLE;
        layout=l;if(first+count>slots.size()){overflow=true;return;}
        uint32_t used=0;
        for(uint32_t i=0;i<count;i++){
            uint32_t own=n-used; // the last set of a call has what is left
            if(i+1<count){auto known=dynamicCounts.find(sets[i]);own=known==dynamicCounts.end()?0:known->second;if(own>n-used){overflow=true;return;}}
            if(own>20){overflow=true;return;}
            auto& slot=slots[first+i];slot.set=sets[i];slot.offsetCount=own;if(own)std::copy_n(offsets+used,own,slot.offsets);used+=own;
        }
    }
};
// Node addresses are stable (states are cleared, never erased); recording is
// single-threaded in the pinned Cemu, like every other map in this layer.
static std::unordered_map<VkCommandBuffer,NativeState> nativeStates;
static VkCommandBuffer nativeStateOwner=VK_NULL_HANDLE;static NativeState* nativeStateLast=nullptr;
static NativeState& nativeState(VkCommandBuffer cb){if(cb!=nativeStateOwner||!nativeStateLast){nativeStateLast=&nativeStates[cb];nativeStateOwner=cb;}return *nativeStateLast;}
struct Texture{VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkBuffer staging=VK_NULL_HANDLE;VkDeviceMemory stagingMemory=VK_NULL_HANDLE;uint32_t w=0,h=0;uint8_t* mapped=nullptr;};
struct RenderTarget{VkImage color,depth;uint64_t colorGeneration,depthGeneration;VkFramebuffer fb;VkImageView cv,dv;VkImage scratchDepth;VkDeviceMemory scratchMemory;bool scratchInitialized=false;};
static std::vector<RenderTarget> targets;
static VkBuffer vertexBuffer=VK_NULL_HANDLE;
static VkDeviceMemory vertexMemory=VK_NULL_HANDLE;
static Texture atlasTexture,overlayTexture;
static VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;
static VkDescriptorPool descriptorPool=VK_NULL_HANDLE;
static VkDescriptorSet descriptorSet=VK_NULL_HANDLE;
static VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
static VkRenderPass renderPass=VK_NULL_HANDLE;
// Mesh: opaque and cutout, writes the private depth. Blend: water, glass, ice,
// particles, cracks and the outline over what is there, depth tested only;
// the section meshes cull their back faces (WorldRender.cpp), entity quads
// and the outline do not.
static VkPipeline meshPipeline=VK_NULL_HANDLE,overlayPipeline=VK_NULL_HANDLE,blendCullPipeline=VK_NULL_HANDLE,blendPipeline=VK_NULL_HANDLE;
enum {kPipeMesh,kPipeOverlay,kPipeBlendCull,kPipeBlend};
static VkSampler sampler=VK_NULL_HANDLE;
static bool resourcesInitialized=false;
static std::atomic<bool> resourcesFailed{false};
static uint32_t timestampBits=0;
static VkQueryPool timingPool=VK_NULL_HANDLE;
static float timestampPeriod=0;
static uint32_t timingCursor=0,timingFrames[128]{};static int timingKinds[128]{};
static bool timingPending[128]{};

// Poll one old pair without WAIT; an unavailable slot simply loses its sample.
// kind: 0 the draw at the marker, 1 G-buffer, 2 HDR blend, 3 the first shadow cascade (integrate.h).
static int beginTiming(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb,uint32_t frame,int kind=0){
    if(!timingPool)return -1;
    auto slot=timingCursor++%128;const auto& d=*dispatch.pDeviceDispatch;
    if(timingPending[slot]){
        uint64_t times[2]{};auto r=d.GetQueryPoolResults(d.Device,timingPool,slot*2,2,sizeof(times),times,8,VK_QUERY_RESULT_64_BIT);
        if(r!=VK_SUCCESS)return -1;
        auto mask=timestampBits==64?~0ULL:(1ULL<<timestampBits)-1;
        event("{\"event\":\"gpu_timing\",\"frame\":%u,\"start\":%llu,\"end\":%llu,\"ns\":%.9g,\"kind\":%d}",timingFrames[slot],times[0],times[1],double((times[1]-times[0])&mask)*timestampPeriod,timingKinds[slot]);
    }
    dispatch.CmdResetQueryPool(cb,timingPool,slot*2,2);dispatch.CmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timingPool,slot*2);timingFrames[slot]=frame;timingKinds[slot]=kind;timingPending[slot]=true;return int(slot);
}

static void check(VkResult r,const char* name){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(name)+": "+std::to_string(r));}
static uint32_t memoryType(const vkroots::VkDeviceDispatch& d,uint32_t allowed,VkMemoryPropertyFlags required){
    VkPhysicalDeviceMemoryProperties p;d.pPhysicalDeviceDispatch->GetPhysicalDeviceMemoryProperties(d.PhysicalDevice,&p);
    for(uint32_t i=0;i<p.memoryTypeCount;i++)if((allowed&(1u<<i))&&(p.memoryTypes[i].propertyFlags&required)==required)return i;
    throw std::runtime_error("no compatible memory type");
}
static void buffer(const vkroots::VkDeviceDispatch& d,const void* bytes,size_t size,VkBufferUsageFlags usage,VkBuffer& b,VkDeviceMemory& m){
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateBuffer(d.Device,&ci,nullptr,&b),"CreateBuffer");
    VkMemoryRequirements req;d.GetBufferMemoryRequirements(d.Device,b,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(d.AllocateMemory(d.Device,&ai,nullptr,&m),"AllocateMemory");check(d.BindBufferMemory(d.Device,b,m,0),"BindBufferMemory");
    void* ptr;check(d.MapMemory(d.Device,m,0,size,0,&ptr),"MapMemory");memcpy(ptr,bytes,size);d.UnmapMemory(d.Device,m);
}
static VkImageView imageView(const vkroots::VkDeviceDispatch& d,VkImage im,VkFormat format,VkImageAspectFlags aspect){
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=im;ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=format;ci.subresourceRange={aspect,0,1,0,1};VkImageView result;check(d.CreateImageView(d.Device,&ci,nullptr,&result),"CreateImageView");return result;
}
static void barrier(const vkroots::VkCommandBufferDispatch& d,VkCommandBuffer cb,VkImage im,VkImageAspectFlags aspect,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=src;b.dstAccessMask=dst;b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=im;b.subresourceRange={aspect,0,1,0,1};
    d.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
static Texture texture(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb,const std::vector<uint8_t>& bytes){
    const auto& d=*dispatch.pDeviceDispatch;Texture t;memcpy(&t.w,bytes.data(),4);memcpy(&t.h,bytes.data()+4,4);
    if(bytes.size()!=8+size_t(t.w)*t.h*4)throw std::runtime_error("texture size");
    buffer(d,bytes.data()+8,bytes.size()-8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,t.staging,t.stagingMemory);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={t.w,t.h,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateImage(d.Device,&ci,nullptr,&t.image),"texture image");
    VkMemoryRequirements req;d.GetImageMemoryRequirements(d.Device,t.image,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(d.AllocateMemory(d.Device,&ai,nullptr,&t.memory),"texture memory");check(d.BindImageMemory(d.Device,t.image,t.memory,0),"texture bind");
    barrier(dispatch,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent=ci.extent;dispatch.CmdCopyBufferToImage(cb,t.staging,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier(dispatch,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    t.view=imageView(d,t.image,ci.format,VK_IMAGE_ASPECT_COLOR_BIT);return t;
}
static VkShaderModule shader(const vkroots::VkDeviceDispatch& d,const std::vector<uint32_t>& bytes){VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=bytes.size()*4;ci.pCode=bytes.data();VkShaderModule module;check(d.CreateShaderModule(d.Device,&ci,nullptr,&module),"shader");return module;}
static VkPipeline pipeline(const vkroots::VkDeviceDispatch& d,int kind){
    bool overlay=kind==kPipeOverlay,blended=kind==kPipeBlendCull||kind==kPipeBlend;
    VkShaderModule vs=shader(d,overlay?overlayVS:meshVS),fs=shader(d,overlay?overlayFS:meshFS);
    VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[0].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;stages[1].pName="main";
    VkVertexInputBindingDescription binding{0,32,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[5]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,12},{2,0,VK_FORMAT_R8G8B8A8_UNORM,20},{3,0,VK_FORMAT_R32_UINT,24},{4,0,VK_FORMAT_R32_UINT,28}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};if(!overlay){vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.vertexAttributeDescriptionCount=5;vertex.pVertexAttributeDescriptions=attrs;}
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=1;viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=kind==kPipeBlendCull?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable=!overlay;depth.depthWriteEnable=!overlay&&!blended;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    // A's PNG writer flips rows only; its HUD RGB remains premultiplied (Overlay.cpp).
    VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=15;blend.blendEnable=overlay;blend.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.colorBlendOp=VK_BLEND_OP_ADD;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;blend.alphaBlendOp=VK_BLEND_OP_ADD;
    // Straight alpha for the world; the target's own alpha is left as BOTW wrote it.
    if(blended){blend.blendEnable=VK_TRUE;blend.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;blend.srcAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;blend.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;}
    VkPipelineColorBlendStateCreateInfo blends{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blends.attachmentCount=1;blends.pAttachments=&blend;
    VkDynamicState dynamics[2]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamics;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vertex;ci.pInputAssemblyState=&assembly;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;ci.pMultisampleState=&samples;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blends;ci.pDynamicState=&dynamic;ci.layout=pipelineLayout;ci.renderPass=renderPass;
    VkPipeline result;auto r=d.CreateGraphicsPipelines(d.Device,VK_NULL_HANDLE,1,&ci,nullptr,&result);d.DestroyShaderModule(d.Device,vs,nullptr);d.DestroyShaderModule(d.Device,fs,nullptr);check(r,"pipeline");return result;
}
static void initResources(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb){
    const auto& d=*dispatch.pDeviceDispatch;
    if(!presentation::enabled.load()&&vertexBytes.size()!=36*32)throw std::runtime_error("fixture vertex count");
    if(!presentation::enabled.load())buffer(d,vertexBytes.data(),vertexBytes.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertexBuffer,vertexMemory);
    std::vector<uint8_t> empty{1,0,0,0,1,0,0,0,0,0,0,0};
    atlasTexture=texture(dispatch,cb,presentation::enabled.load()?empty:atlasBytes);overlayTexture=texture(dispatch,cb,presentation::enabled.load()?empty:overlayBytes);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_NEAREST;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;si.maxLod=0;check(d.CreateSampler(d.Device,&si,nullptr,&sampler),"sampler");
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=2;li.pBindings=bindings;check(d.CreateDescriptorSetLayout(d.Device,&li,nullptr,&descriptorLayout),"descriptor layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&size;check(d.CreateDescriptorPool(d.Device,&pi,nullptr,&descriptorPool),"descriptor pool");VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=descriptorPool;ai.descriptorSetCount=1;ai.pSetLayouts=&descriptorLayout;check(d.AllocateDescriptorSets(d.Device,&ai,&descriptorSet),"descriptor set");
    VkDescriptorImageInfo imageInfos[2]={{sampler,atlasTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,overlayTexture.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};VkWriteDescriptorSet writes[2]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};for(int i=0;i<2;i++){writes[i].dstSet=descriptorSet;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;writes[i].pImageInfo=&imageInfos[i];}d.UpdateDescriptorSets(d.Device,2,writes,0,nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128};VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&descriptorLayout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;check(d.CreatePipelineLayout(d.Device,&layout,nullptr,&pipelineLayout),"pipeline layout");
    VkAttachmentDescription attachments[2]{};for(auto& a:attachments){a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_GENERAL;}attachments[0].format=VK_FORMAT_A2B10G10R10_UNORM_PACK32;attachments[1].format=VK_FORMAT_D32_SFLOAT;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_GENERAL},depth{1,VK_IMAGE_LAYOUT_GENERAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;sub.pDepthStencilAttachment=&depth;
    VkSubpassDependency dependencies[2]{};dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,0};dependencies[1]={0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=2;ri.pAttachments=attachments;ri.subpassCount=1;ri.pSubpasses=&sub;ri.dependencyCount=2;ri.pDependencies=dependencies;check(d.CreateRenderPass(d.Device,&ri,nullptr,&renderPass),"render pass");meshPipeline=pipeline(d,kPipeMesh);overlayPipeline=pipeline(d,kPipeOverlay);blendCullPipeline=pipeline(d,kPipeBlendCull);blendPipeline=pipeline(d,kPipeBlend);
    VkPhysicalDeviceProperties properties;d.pPhysicalDeviceDispatch->GetPhysicalDeviceProperties(d.PhysicalDevice,&properties);timestampPeriod=properties.limits.timestampPeriod;
    if(timestampBits){VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qi.queryType=VK_QUERY_TYPE_TIMESTAMP;qi.queryCount=256;auto r=d.CreateQueryPool(d.Device,&qi,nullptr,&timingPool);if(r!=VK_SUCCESS)timingPool=VK_NULL_HANDLE;}
    resourcesInitialized=true;
    event("{\"event\":\"timestamp_support\",\"bits\":%u,\"period_ns\":%.9g,\"pool\":%llu}",timestampBits,timestampPeriod,(uint64_t)timingPool);
    event("{\"event\":\"resources_ready\",\"vertices\":36,\"upload_bytes\":%zu}",vertexBytes.size()+atlasBytes.size()+overlayBytes.size());
}
#include "live_gpu.h"
static void retireTargets(const vkroots::VkDeviceDispatch& d,VkImage image){
    for(auto it=targets.begin();it!=targets.end();)if(it->color==image||it->depth==image){event("{\"event\":\"target_retire\",\"color\":%llu,\"depth\":%llu,\"scratch_depth\":%llu,\"epoch\":%llu}",(uint64_t)it->color,(uint64_t)it->depth,(uint64_t)it->scratchDepth,targetEpoch.load());d.DestroyFramebuffer(d.Device,it->fb,nullptr);d.DestroyImageView(d.Device,it->cv,nullptr);d.DestroyImageView(d.Device,it->dv,nullptr);d.DestroyImage(d.Device,it->scratchDepth,nullptr);d.FreeMemory(d.Device,it->scratchMemory,nullptr);it=targets.erase(it);}else ++it;
}
// One push block for every pipeline of this layer (128 bytes, the size every Vulkan device has).
// params: target width and height, half the outline's width in pixels, one value for the fragment
// shader; extra: two more vec4 for it (gbuffer.frag, hdr.frag say what they hold).
struct Push{float matrix[16],anchor[4],params[4],extra[8];};
static constexpr VkShaderStageFlags kPushStages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
enum {kPartOpaque=1,kPartTranslucent=2};
// Minecraft's world in the order it has to be drawn; a null pipeline leaves its part out.
// opaque: sections, entities, block entities, dropped items. blendCull, blend: water, glass and
// ice far to near, then blended entities and particles, cracks and the outline. overlay: hand and HUD.
struct WorldPipes{VkPipeline opaque=VK_NULL_HANDLE,blendCull=VK_NULL_HANDLE,blend=VK_NULL_HANDLE,overlay=VK_NULL_HANDLE;};
static void liveWorld(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb,GpuScene& scene,const Camera& camera,Push& push,const WorldPipes& pipes,uint32_t drawn[4]){
    auto* generation=&scene;const float scale=camera.mcScale;VkDeviceSize offset=0;VkDescriptorSet set=scene.set,bound=VK_NULL_HANDLE;
    auto place=[&](double x,double y,double z){
        double o[3]={x,y,z};for(int i=0;i<3;i++)push.anchor[i]=camera.mcAnchor[i]+float(o[i]-camera.mcOrigin[i])*scale;push.anchor[3]=scale;
        dispatch.CmdPushConstants(cb,pipelineLayout,kPushStages,0,sizeof(push),&push);
    };
    auto rows=[&](const GpuPart& part,uint32_t first,uint32_t count,int group){if(!count)return;dispatch.CmdBindVertexBuffers(cb,0,1,&part.a.b,&offset);dispatch.CmdDraw(cb,count,1,first,0);drawn[group]+=count;};
    // Texture 0 is the block and item atlas; an entity texture not (yet) received leaves its batch out.
    auto texture=[&](uint32_t id){
        VkDescriptorSet want=set;if(id){auto found=generation->textures.find(id);if(found==generation->textures.end())return false;want=found->second->own.set;}
        if(want!=bound){bound=want;dispatch.CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&bound,0,nullptr);}return true;
    };
    auto batches=[&](const std::shared_ptr<GpuPart>& part,bool blended){
        for(const auto& b:part->mesh->batches)if(bool(b.flags&1)==blended&&uint64_t(b.first)+b.count<=part->count&&texture(b.texture))rows(*part,b.first,b.count,blended?2:0);
    };
    // The player's own model stands on the feet the camera follows (third person only); every
    // other entity, block entity and particle is relative to the block Minecraft named.
    auto entities=[&](bool blended){
        if(camera.mcView&&camera.hasFeet&&generation->avatar){
            for(int i=0;i<3;i++)push.anchor[i]=camera.feet[i];push.anchor[3]=scale;dispatch.CmdPushConstants(cb,pipelineLayout,kPushStages,0,sizeof(push),&push);
            batches(generation->avatar,blended);
        }
        if(generation->entities){auto& o=generation->entities->mesh->origin;place(o[0],o[1],o[2]);batches(generation->entities,blended);}
    };
    texture(0);
    if(generation->atlas){
        auto& dynamic=generation->dynamic;auto& source=generation->dynamicSource;
        if(pipes.opaque){
            dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes.opaque);
            for(auto& [key,part]:generation->sections){auto [x,y,z]=key;if(part->split){place(x*16.,y*16.,z*16.);rows(*part,0,part->split,0);}}
            entities(false);
            if(dynamic&&source->solid){texture(0);place(source->origin[0],source->origin[1],source->origin[2]);rows(*dynamic,0,source->solid,0);}
        }
        if(pipes.blendCull&&pipes.blend){
            // Water, stained glass, ice: blended over what is there, the farthest section first.
            static thread_local std::vector<std::pair<float,const std::pair<const presentation::Key,std::shared_ptr<GpuPart>>*>> sorted;sorted.clear();
            float eye[3];for(int i=0;i<3;i++)eye[i]=(camera.eye[i]-camera.mcAnchor[i])/scale+camera.mcOrigin[i];
            for(auto& entry:generation->sections)if(entry.second->count>entry.second->split){
                auto [x,y,z]=entry.first;float dx=x*16.f+8-eye[0],dy=y*16.f+8-eye[1],dz=z*16.f+8-eye[2];sorted.push_back({dx*dx+dy*dy+dz*dz,&entry});
            }
            std::sort(sorted.begin(),sorted.end(),[](auto& a,auto& b){return a.first>b.first;});
            if(!sorted.empty()){
                dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes.blendCull);texture(0);
                for(auto& [distance,entry]:sorted){auto [x,y,z]=entry->first;place(x*16.,y*16.,z*16.);rows(*entry->second,entry->second->split,entry->second->count-entry->second->split,1);}
            }
            dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes.blend);
            entities(true);
            if(dynamic&&source->cracks+source->outline){texture(0);place(source->origin[0],source->origin[1],source->origin[2]);rows(*dynamic,source->solid,source->cracks+source->outline,3);}
        }
    }
    // Independent HUD remains eligible even with empty/bootstrap geometry.
    if(pipes.overlay&&generation->hud&&!camera.scene){texture(0);dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipes.overlay);dispatch.CmdDraw(cb,3,1,0,0);}
}
// Cemu's own state, bound again after one of our draws (it caches what it bound).
static void restoreNative(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb,const NativeState& saved){
    if(saved.pipeline)dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,saved.pipeline);
    for(uint32_t i=0;i<saved.slots.size();i++){auto& slot=saved.slots[i];if(slot.set)dispatch.CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,saved.layout,i,1,&slot.set,slot.offsetCount,slot.offsetCount?slot.offsets:nullptr);}
    for(auto& p:saved.constants)dispatch.CmdPushConstants(cb,p.layout,p.stages,p.offset,uint32_t(p.bytes.size()),p.bytes.data());
    if(saved.vertex)dispatch.CmdBindVertexBuffers(cb,0,1,&saved.vertex,&saved.vertexOffset);
    if(saved.hasViewport)dispatch.CmdSetViewport(cb,0,1,&saved.viewport);
    if(saved.hasScissor)dispatch.CmdSetScissor(cb,0,1,&saved.scissor);
    if(saved.hasBias)dispatch.CmdSetDepthBias(cb,saved.bias[0],saved.bias[1],saved.bias[2]);
    if(saved.hasBlend)dispatch.CmdSetBlendConstants(cb,saved.blend);
}
static Reject drawFixture(const vkroots::VkCommandBufferDispatch& dispatch,VkCommandBuffer cb,VkImage color,VkImage depth,uint64_t colorGeneration,uint64_t depthGeneration,VkExtent2D extent,const Camera& camera,int parts=3){
    if(!fixtureReady.load(std::memory_order_acquire))return Reject::fixture;
    if(resourcesFailed)return Reject::resources;
    bool isLive=presentation::enabled.load();
    if(isLive?(!presentation::eligible.load()||!camera.common):!renderEnabled.load())return Reject::disabled;
    event("{\"event\":\"body_frame\",\"frame\":%u,\"qpc\":%llu,\"camera_epoch\":%llu,\"body_seq\":%llu,\"body_epoch\":%llu,\"actor\":%u,\"authority\":%d}",camera.id,camera.qpc,camera.epoch,camera.bodySeq,camera.bodyEpoch,camera.actor,int(camera.authority));
    auto saved=nativeState(cb);if(saved.overflow)return Reject::resources;
    if(anchorBusy.test_and_set(std::memory_order_acquire))return Reject::anchorBusy;
    float anchor[3];for(int i=0;i<3;i++)anchor[i]=fixtureAnchor[i];anchorBusy.clear(std::memory_order_release);
    LARGE_INTEGER start,end;QueryPerformanceCounter(&start);
    try{
        if(!resourcesInitialized)initResources(dispatch,cb);
        const auto& d=*dispatch.pDeviceDispatch;
        auto generation=isLive?frameGpu(dispatch,cb):nullptr;if(isLive&&!generation)return Reject::resources;
        if(generation)gpuRecorded[cb].push_back(generation);
        auto found=std::find_if(targets.begin(),targets.end(),[&](auto& t){return t.color==color&&t.depth==depth&&t.colorGeneration==colorGeneration&&t.depthGeneration==depthGeneration;});
        if(found==targets.end()){
            RenderTarget target{color,depth,colorGeneration,depthGeneration};target.cv=imageView(d,color,VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_IMAGE_ASPECT_COLOR_BIT);
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_D32_SFLOAT;ci.extent={extent.width,extent.height,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateImage(d.Device,&ci,nullptr,&target.scratchDepth),"scratch depth");
            VkMemoryRequirements req;d.GetImageMemoryRequirements(d.Device,target.scratchDepth,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(d.AllocateMemory(d.Device,&ai,nullptr,&target.scratchMemory),"scratch memory");check(d.BindImageMemory(d.Device,target.scratchDepth,target.scratchMemory,0),"scratch bind");
            target.dv=imageView(d,target.scratchDepth,VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_DEPTH_BIT);VkImageView views[2]={target.cv,target.dv};VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=renderPass;fi.attachmentCount=2;fi.pAttachments=views;fi.width=extent.width;fi.height=extent.height;fi.layers=1;check(d.CreateFramebuffer(d.Device,&fi,nullptr,&target.fb),"framebuffer");targets.push_back(target);found=targets.end()-1;
            event("{\"event\":\"target\",\"color\":%llu,\"depth\":%llu,\"color_generation\":%llu,\"depth_generation\":%llu,\"scratch_depth\":%llu,\"width\":%u,\"height\":%u,\"color_format\":64,\"depth_format\":126,\"samples\":1,\"native_layout\":1}",(uint64_t)color,(uint64_t)depth,colorGeneration,depthGeneration,(uint64_t)target.scratchDepth,extent.width,extent.height);
            event("{\"event\":\"viewport\",\"frame\":%u,\"epoch\":%llu,\"native\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],\"scissor\":[%d,%d,%u,%u]}",camera.id,camera.epoch,saved.viewport.x,saved.viewport.y,saved.viewport.width,saved.viewport.height,saved.viewport.minDepth,saved.viewport.maxDepth,saved.scissor.offset.x,saved.scissor.offset.y,saved.scissor.extent.width,saved.scissor.extent.height);
        }
        auto timing=beginTiming(dispatch,cb,camera.id);
        barrier(dispatch,cb,color,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        // Copy entirely on GPU. Self-depth writes never modify BOTW's depth image.
        barrier(dispatch,cb,depth,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        barrier(dispatch,cb,found->scratchDepth,VK_IMAGE_ASPECT_DEPTH_BIT,found->scratchInitialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,found->scratchInitialized?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);found->scratchInitialized=true;
        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.extent={extent.width,extent.height,1};dispatch.CmdCopyImage(cb,depth,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,found->scratchDepth,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier(dispatch,cb,depth,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
        barrier(dispatch,cb,found->scratchDepth,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=renderPass;begin.framebuffer=found->fb;begin.renderArea={{0,0},extent};dispatch.CmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0,float(extent.height),float(extent.width),-float(extent.height),0,1};VkRect2D scissor{{0,0},extent};dispatch.CmdSetViewport(cb,0,1,&viewport);dispatch.CmdSetScissor(cb,0,1,&scissor);
        Push push{};
        push.params[0]=float(extent.width);push.params[1]=float(extent.height);push.params[2]=std::max(1.f,float(extent.height)/1080*1.25f);
        float view[16]{};for(int i=0;i<12;i++)view[i]=camera.view[i];view[15]=1;
        for(int i=0;i<4;i++)for(int j=0;j<4;j++)for(int k=0;k<4;k++)push.matrix[i*4+j]+=camera.device[i*4+k]*view[k*4+j];
        for(int i=0;i<3;i++)push.anchor[i]=anchor[i];
        VkDeviceSize offset=0;
        if(isLive){
            uint32_t drawn[4]{}; // vertices: opaque, translucent blocks, blended entities, outline and cracks
            WorldPipes pipes;if(parts&kPartOpaque)pipes.opaque=meshPipeline;if(parts&kPartTranslucent){pipes.blendCull=blendCullPipeline;pipes.blend=blendPipeline;}pipes.overlay=overlayPipeline;
            liveWorld(dispatch,cb,*generation,camera,push,pipes,drawn);
            frameMarks::mark(frameMarks::kDraw);
            event("{\"event\":\"live_draw\",\"frame\":%u,\"session\":%llu,\"clear\":%llu,\"version\":%llu,\"sections\":%zu,\"hud_frame\":%llu,\"view\":%d,\"textures\":%zu,\"opaque\":%u,\"translucent\":%u,\"blended\":%u,\"overlaid\":%u,\"parts\":%d}",camera.id,generation->session,generation->clear,generation->version,generation->sections.size(),generation->frame,camera.mcView,generation->textures.size(),drawn[0],drawn[1],drawn[2],drawn[3],parts);
        }else{
            dispatch.CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelineLayout,0,1,&descriptorSet,0,nullptr);dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,meshPipeline);
            push.anchor[3]=1;dispatch.CmdBindVertexBuffers(cb,0,1,&vertexBuffer,&offset);dispatch.CmdPushConstants(cb,pipelineLayout,kPushStages,0,sizeof(push),&push);dispatch.CmdDraw(cb,36,1,0,0);
            if(overlayEnabled.load()){dispatch.CmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,overlayPipeline);dispatch.CmdDraw(cb,3,1,0,0);}
        }
        dispatch.CmdEndRenderPass(cb);
        barrier(dispatch,cb,color,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
        restoreNative(dispatch,cb,saved);
        if(timing>=0)dispatch.CmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timingPool,uint32_t(timing)*2+1);
        QueryPerformanceCounter(&end);event("{\"event\":\"draw\",\"frame\":%u,\"cb\":%llu,\"color\":%llu,\"depth\":%llu,\"color_generation\":%llu,\"depth_generation\":%llu,\"qpc\":%llu,\"cpu_qpc\":%llu,\"anchor\":[%.9g,%.9g,%.9g],\"overlay\":%d}",camera.id,(uint64_t)cb,(uint64_t)color,(uint64_t)depth,colorGeneration,depthGeneration,(uint64_t)start.QuadPart,(uint64_t)(end.QuadPart-start.QuadPart),push.anchor[0],push.anchor[1],push.anchor[2],int(overlayEnabled.load()));
        return Reject::draw;
    }catch(const std::exception& e){resourcesFailed=true;event("{\"event\":\"render_error\",\"message\":\"%s\"}",e.what());return Reject::renderError;}
}
