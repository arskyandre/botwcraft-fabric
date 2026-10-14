// One frame of BOTW's rendering written down: every render pass with its
// targets, every draw with its pipeline, sampled images and uniform buffers,
// the copies between targets, and a reduced picture of each target when it
// stops being a target (BOTW keeps its G-buffer bound over hundreds of
// passes; a picture after each would not fit anywhere). A target that comes
// and goes many times is pictured the 1st, 2nd, 3rd, 4th, 6th, 8th, 12th...
// time. A measuring tool for the rendering integration; it changes nothing in
// the frame it records.
//
// lab/frame-survey.txt holds a token. A token that differs from the last one
// seen arms one capture: it starts after the next present and ends at the
// present that closes the frame. Everything is kept in memory on the render
// thread; target pictures are copied on the GPU into one staging buffer. The
// worker thread writes lab/frame-survey/<token>/ a few seconds later, when
// the GPU has long finished. Nothing waits. While no capture runs the hooks
// cost one atomic load; the creation hooks keep small tables all the time
// and SPIR-V only when the file held a token at start (`keepCode`).
#pragma once
namespace survey {
struct Blend {uint32_t enable,srcColor,dstColor,colorOp,srcAlpha,dstAlpha,alphaOp,mask;};
struct Pipe {uint64_t vs=0,fs=0,gs=0,serial=0;VkRenderPass pass=VK_NULL_HANDLE;uint32_t topology=0,cull=0,front=0,depthTest=0,depthWrite=0,compare=0,stencil=0,bias=0,stride=0,attributes=0;std::vector<Blend> blends;};
struct PassAttachment {uint32_t format,samples,load,store;};
struct PassLayout {std::vector<PassAttachment> attachments;std::vector<uint32_t> colours;uint32_t depth=~0u;}; // colours: attachment index per colour output, ~0 unused
struct Memory {VkDeviceSize size=0;uint8_t* host=nullptr;uint8_t* mapped=nullptr;VkDeviceSize mapOffset=0;};
struct Bound {VkDeviceMemory memory;VkDeviceSize offset;};
struct Range {VkBuffer buffer;VkDeviceSize offset,range;};
struct ViewRange {uint32_t mip=0,layer=0,layers=1,format=0;};
// Creation hooks may run on Cemu's pipeline compile threads.
static std::mutex tables;
static std::unordered_map<VkShaderModule,uint64_t> shaders;
static std::unordered_map<uint64_t,std::vector<uint32_t>> shaderCode;
static std::unordered_map<VkPipeline,Pipe> pipes;
static std::unordered_map<VkRenderPass,PassLayout> renderPasses;
static std::unordered_map<VkDeviceMemory,Memory> memories;
static std::unordered_map<VkBuffer,Bound> buffers;
// Guarded by imageMutex, like the image-descriptor table next to them.
static std::unordered_map<VkDescriptorSet,std::map<uint32_t,Range>> uniforms;
static std::unordered_map<VkImageView,ViewRange> viewRanges;
static std::unordered_map<VkFramebuffer,std::vector<ViewRange>> framebufferRanges;
static bool keepCode=false;static uint64_t pipeSerial=0;

enum {kIdle,kArmed,kRecording,kFlushing,kSettling,kReady,kWriting};
static std::atomic<int> state{kIdle};
static std::string token,text;      // token: written by the worker before kArmed, read by the render thread after
static std::vector<uint8_t> blobs;  // uniform buffer contents, as they were when each draw was recorded
struct BlobRecord {uint64_t buffer,at,guest;uint32_t offset,bytes,range;};
static std::vector<BlobRecord> blobRecords;
static std::map<std::tuple<uint64_t,uint64_t,uint64_t>,uint32_t> blobIndex;
static std::unordered_set<VkPipeline> usedPipes;
struct Attachment {VkImage image;ViewRange range;uint32_t pass=0,slot=0;bool drew=false;};
struct Open {uint32_t index=0,draws=0;uint64_t vertices=0;std::vector<Attachment> attachments;};
static std::unordered_map<VkCommandBuffer,Open> open;
// Targets written since their last picture, and how often each has stopped being a target.
using TargetKey=std::tuple<VkImage,uint32_t,uint32_t>;
static std::map<TargetKey,Attachment> pending;static std::map<TargetKey,uint32_t> departures;
static uint32_t passCount=0,drawCount=0,snapCount=0;static uint64_t startPresent=0,settleUntil=0;static int64_t startQpc=0;
// GPU side: one scratch image the colour targets are reduced into and one staging buffer.
constexpr size_t kStaging=448u<<20;constexpr uint32_t kScratch=512,kSide=480;
static VkImage scratch=VK_NULL_HANDLE;static VkDeviceMemory scratchMemory=VK_NULL_HANDLE,stagingMemory=VK_NULL_HANDLE;static VkBuffer staging=VK_NULL_HANDLE;
static uint8_t* stagingMapped=nullptr;static size_t stagingUsed=0;static bool scratchUsed=false,gpuFailed=false;
static std::unordered_map<uint32_t,bool> blitSource;
// Depth targets are reduced like colours where the driver can blit that depth format; otherwise copied whole.
struct DepthScratch {VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;bool used=false,failed=false;};
static std::unordered_map<uint32_t,DepthScratch> depthScratch;

static inline bool on(){return state.load(std::memory_order_relaxed)==kRecording;}
static void line(const char* fmt,...){
    char buffer[4096];va_list args;va_start(args,fmt);int n=vsnprintf(buffer,sizeof(buffer),fmt,args);va_end(args);
    if(n>0){text.append(buffer,std::min<size_t>(size_t(n),sizeof(buffer)-1));text.push_back('\n');}
}
static uint64_t hash(const void* data,size_t bytes){uint64_t h=14695981039346656037ULL;auto p=static_cast<const uint8_t*>(data);for(size_t i=0;i<bytes;i++)h=(h^p[i])*1099511628211ULL;return h;}

// ---- tables (always) ------------------------------------------------------------------------
static void shaderCreated(VkShaderModule module,const VkShaderModuleCreateInfo* ci){
    uint64_t h=hash(ci->pCode,ci->codeSize);std::lock_guard lock(tables);shaders[module]=h;
    if(keepCode&&!shaderCode.contains(h))shaderCode[h].assign(ci->pCode,ci->pCode+ci->codeSize/4);
}
static void pipelineCreated(VkPipeline pipeline,const VkGraphicsPipelineCreateInfo& ci){
    Pipe p;p.pass=ci.renderPass;std::lock_guard lock(tables);p.serial=++pipeSerial;
    for(uint32_t i=0;i<ci.stageCount;i++){auto found=shaders.find(ci.pStages[i].module);uint64_t h=found==shaders.end()?0:found->second;
        if(ci.pStages[i].stage==VK_SHADER_STAGE_VERTEX_BIT)p.vs=h;else if(ci.pStages[i].stage==VK_SHADER_STAGE_FRAGMENT_BIT)p.fs=h;else if(ci.pStages[i].stage==VK_SHADER_STAGE_GEOMETRY_BIT)p.gs=h;}
    if(ci.pInputAssemblyState)p.topology=ci.pInputAssemblyState->topology;
    if(ci.pRasterizationState){p.cull=ci.pRasterizationState->cullMode;p.front=ci.pRasterizationState->frontFace;p.bias=ci.pRasterizationState->depthBiasEnable;}
    if(ci.pDepthStencilState){p.depthTest=ci.pDepthStencilState->depthTestEnable;p.depthWrite=ci.pDepthStencilState->depthWriteEnable;p.compare=ci.pDepthStencilState->depthCompareOp;p.stencil=ci.pDepthStencilState->stencilTestEnable;}
    if(ci.pColorBlendState)for(uint32_t i=0;i<ci.pColorBlendState->attachmentCount;i++){auto& b=ci.pColorBlendState->pAttachments[i];
        p.blends.push_back({b.blendEnable,uint32_t(b.srcColorBlendFactor),uint32_t(b.dstColorBlendFactor),uint32_t(b.colorBlendOp),uint32_t(b.srcAlphaBlendFactor),uint32_t(b.dstAlphaBlendFactor),uint32_t(b.alphaBlendOp),b.colorWriteMask});}
    if(ci.pVertexInputState){p.attributes=ci.pVertexInputState->vertexAttributeDescriptionCount;if(ci.pVertexInputState->vertexBindingDescriptionCount)p.stride=ci.pVertexInputState->pVertexBindingDescriptions[0].stride;}
    pipes[pipeline]=std::move(p);
}
static void renderPassCreated(VkRenderPass pass,const VkRenderPassCreateInfo* ci){
    PassLayout layout;for(uint32_t i=0;i<ci->attachmentCount;i++)layout.attachments.push_back({uint32_t(ci->pAttachments[i].format),uint32_t(ci->pAttachments[i].samples),uint32_t(ci->pAttachments[i].loadOp),uint32_t(ci->pAttachments[i].storeOp)});
    if(ci->subpassCount){auto& sub=ci->pSubpasses[0];for(uint32_t i=0;i<sub.colorAttachmentCount;i++)layout.colours.push_back(sub.pColorAttachments[i].attachment);if(sub.pDepthStencilAttachment)layout.depth=sub.pDepthStencilAttachment->attachment;}
    std::lock_guard lock(tables);renderPasses[pass]=std::move(layout);
}
static void memoryAllocated(VkDeviceMemory memory,const VkMemoryAllocateInfo* ai){
    Memory m;m.size=ai->allocationSize;
    for(auto next=static_cast<const VkBaseInStructure*>(ai->pNext);next;next=next->pNext)if(next->sType==VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT)m.host=static_cast<uint8_t*>(reinterpret_cast<const VkImportMemoryHostPointerInfoEXT*>(next)->pHostPointer);
    std::lock_guard lock(tables);memories[memory]=m;
}
static void memoryMapped(VkDeviceMemory memory,VkDeviceSize offset,void* pointer){std::lock_guard lock(tables);auto found=memories.find(memory);if(found!=memories.end()){found->second.mapped=static_cast<uint8_t*>(pointer);found->second.mapOffset=offset;}}
static void memoryUnmapped(VkDeviceMemory memory){std::lock_guard lock(tables);auto found=memories.find(memory);if(found!=memories.end())found->second.mapped=nullptr;}

// ---- recording (render thread, only while a capture runs) -------------------------------------
static bool isDepth(uint32_t format){return format>=VK_FORMAT_D16_UNORM&&format<=VK_FORMAT_D32_SFLOAT_S8_UINT;}
static bool isInteger(uint32_t f){
    for(uint32_t v:{13,14,20,21,27,28,34,35,41,42,48,49,55,56,62,63,68,69,74,75,81,82,88,89,95,96,98,99,101,102,104,105,107,108})if(f==v)return true;
    return false;
}
static bool ensureGpu(const vkroots::VkCommandBufferDispatch& c){
    if(gpuFailed)return false;if(staging)return true;
    const auto& d=*c.pDeviceDispatch;
    try{
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=kStaging;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateBuffer(d.Device,&bi,nullptr,&staging),"survey staging");
        VkMemoryRequirements req;d.GetBufferMemoryRequirements(d.Device,staging,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
        try{ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT);}
        catch(...){ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);}
        check(d.AllocateMemory(d.Device,&ai,nullptr,&stagingMemory),"survey staging memory");check(d.BindBufferMemory(d.Device,staging,stagingMemory,0),"survey staging bind");
        void* pointer=nullptr;check(d.MapMemory(d.Device,stagingMemory,0,kStaging,0,&pointer),"survey staging map");stagingMapped=static_cast<uint8_t*>(pointer);
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R16G16B16A16_SFLOAT;ci.extent={kScratch,kScratch,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateImage(d.Device,&ci,nullptr,&scratch),"survey scratch");
        d.GetImageMemoryRequirements(d.Device,scratch,&req);ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(d.AllocateMemory(d.Device,&ai,nullptr,&scratchMemory),"survey scratch memory");check(d.BindImageMemory(d.Device,scratch,scratchMemory,0),"survey scratch bind");
    }catch(const std::exception& e){gpuFailed=true;event("{\"event\":\"survey_gpu_error\",\"message\":\"%s\"}",e.what());return false;}
    return true;
}
static DepthScratch& depthTarget(const vkroots::VkCommandBufferDispatch& c,uint32_t format){
    auto& s=depthScratch[format];if(s.image||s.failed)return s;
    const auto& d=*c.pDeviceDispatch;VkFormatProperties properties;d.pPhysicalDeviceDispatch->GetPhysicalDeviceFormatProperties(d.PhysicalDevice,VkFormat(format),&properties);
    if((properties.optimalTilingFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT))!=(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT)||format>VK_FORMAT_D32_SFLOAT){s.failed=true;return s;}
    try{
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VkFormat(format);ci.extent={kScratch,kScratch,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateImage(d.Device,&ci,nullptr,&s.image),"survey depth scratch");
        VkMemoryRequirements req;d.GetImageMemoryRequirements(d.Device,s.image,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(d.AllocateMemory(d.Device,&ai,nullptr,&s.memory),"survey depth scratch memory");check(d.BindImageMemory(d.Device,s.image,s.memory,0),"survey depth scratch bind");
    }catch(const std::exception& e){s.failed=true;event("{\"event\":\"survey_gpu_error\",\"message\":\"%s\"}",e.what());}
    return s;
}
// A picture of one attachment as it is at this point of the command buffer (outside a render pass).
static void snap(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,uint32_t pass,uint32_t slot,const Attachment& a,bool drew){
    ImageInfo info;{std::lock_guard lock(imageMutex);auto found=images.find(a.image);if(found==images.end())return;info=found->second;}
    uint32_t format=info.info.format,w=std::max(1u,info.info.extent.width>>a.range.mip),h=std::max(1u,info.info.extent.height>>a.range.mip);
    const char* skipped=nullptr;
    if(info.layout!=VK_IMAGE_LAYOUT_GENERAL)skipped="layout";else if(info.info.samples!=VK_SAMPLE_COUNT_1_BIT)skipped="multisampled";
    else if(!(info.info.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))skipped="usage";else if(!ensureGpu(c))skipped="resources";
    size_t bytes=0;uint32_t dw=w,dh=h,stored=format;DepthScratch* reduced=nullptr;
    if(!skipped&&std::max(w,h)>kSide){if(w>=h){dw=kSide;dh=std::max(1u,uint32_t(uint64_t(h)*kSide/w));}else{dh=kSide;dw=std::max(1u,uint32_t(uint64_t(w)*kSide/h));}}
    if(!skipped&&isDepth(format)){
        if(!drew)skipped="no draws";
        else{auto& scratchDepth=depthTarget(c,format);if(scratchDepth.image)reduced=&scratchDepth;else{dw=w;dh=h;}bytes=size_t(dw)*dh*(format==VK_FORMAT_D16_UNORM?2:4);}
    }else if(!skipped){
        if(isInteger(format))skipped="integer format";
        else{auto known=blitSource.find(format);if(known==blitSource.end()){VkFormatProperties properties;c.pDeviceDispatch->pPhysicalDeviceDispatch->GetPhysicalDeviceFormatProperties(c.pDeviceDispatch->PhysicalDevice,VkFormat(format),&properties);
                known=blitSource.emplace(format,bool(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)).first;}
            if(!known->second)skipped="no blit";}
        bytes=size_t(dw)*dh*8;stored=VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if(!skipped&&stagingUsed+bytes>kStaging)skipped="staging full";
    if(skipped){line("{\"t\":\"snap_skipped\",\"pass\":%u,\"slot\":%u,\"image\":%llu,\"why\":\"%s\"}",pass,slot,(uint64_t)a.image,skipped);return;}
    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    c.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,0,nullptr);
    VkBufferImageCopy copy{};copy.bufferOffset=stagingUsed;
    if(reduced){
        barrier(c,cb,reduced->image,VK_IMAGE_ASPECT_DEPTH_BIT,reduced->used?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,reduced->used?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);reduced->used=true;
        VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,a.range.mip,a.range.layer,1};blit.srcOffsets[1]={int32_t(w),int32_t(h),1};blit.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};blit.dstOffsets[1]={int32_t(dw),int32_t(dh),1};
        c.CmdBlitImage(cb,a.image,VK_IMAGE_LAYOUT_GENERAL,reduced->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        barrier(c,cb,reduced->image,VK_IMAGE_ASPECT_DEPTH_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        copy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.imageExtent={dw,dh,1};
        c.CmdCopyImageToBuffer(cb,reduced->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging,1,&copy);
    }else if(isDepth(format)){
        copy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,a.range.mip,a.range.layer,1};copy.imageExtent={w,h,1};
        c.CmdCopyImageToBuffer(cb,a.image,VK_IMAGE_LAYOUT_GENERAL,staging,1,&copy);
    }else{
        barrier(c,cb,scratch,VK_IMAGE_ASPECT_COLOR_BIT,scratchUsed?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,scratchUsed?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);scratchUsed=true;
        VkImageBlit blit{};blit.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,a.range.mip,a.range.layer,1};blit.srcOffsets[1]={int32_t(w),int32_t(h),1};blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.dstOffsets[1]={int32_t(dw),int32_t(dh),1};
        c.CmdBlitImage(cb,a.image,VK_IMAGE_LAYOUT_GENERAL,scratch,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        barrier(c,cb,scratch,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={dw,dh,1};
        c.CmdCopyImageToBuffer(cb,scratch,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,staging,1,&copy);
    }
    c.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,0,nullptr);
    line("{\"t\":\"snap\",\"id\":%u,\"pass\":%u,\"slot\":%u,\"image\":%llu,\"offset\":%zu,\"bytes\":%zu,\"w\":%u,\"h\":%u,\"format\":%u,\"source\":[%u,%u,%u],\"mip\":%u,\"layer\":%u}",snapCount++,pass,slot,(uint64_t)a.image,stagingUsed,bytes,dw,dh,stored,w,h,format,a.range.mip,a.range.layer);
    stagingUsed+=bytes;
}
static void passBegin(VkCommandBuffer cb,const VkRenderPassBeginInfo* begin){
    auto& o=open[cb];o={};o.index=passCount++;std::string list;char item[512];
    std::vector<PassAttachment> described;std::string slots;
    {std::lock_guard lock(tables);auto found=renderPasses.find(begin->renderPass);if(found!=renderPasses.end()){described=found->second.attachments;
        for(auto colour:found->second.colours){snprintf(item,sizeof(item),"%s%d",slots.empty()?"":",",colour==~0u?-1:int(colour));slots+=item;}
        snprintf(item,sizeof(item),"],\"depth_slot\":%d,\"x\":[",found->second.depth==~0u?-1:int(found->second.depth));slots+=item;}}
    if(slots.empty())slots="],\"depth_slot\":-2,\"x\":[";
    {std::lock_guard lock(imageMutex);
        auto ims=framebuffers.find(begin->framebuffer);auto ranges=framebufferRanges.find(begin->framebuffer);
        if(ims!=framebuffers.end())for(size_t i=0;i<ims->second.size();i++){
            Attachment a{ims->second[i],ranges!=framebufferRanges.end()&&i<ranges->second.size()?ranges->second[i]:ViewRange{},o.index,uint32_t(i)};o.attachments.push_back(a);
            auto info=images.find(a.image);PassAttachment pa=i<described.size()?described[i]:PassAttachment{};
            if(info==images.end())snprintf(item,sizeof(item),"%s{\"image\":%llu,\"unknown\":1}",i?",":"",(uint64_t)a.image);
            else snprintf(item,sizeof(item),"%s{\"image\":%llu,\"generation\":%llu,\"format\":%u,\"view_format\":%u,\"w\":%u,\"h\":%u,\"depth\":%u,\"mips\":%u,\"layers\":%u,\"mip\":%u,\"layer\":%u,\"view_layers\":%u,\"samples\":%u,\"usage\":%u,\"load\":%u,\"store\":%u}",i?",":"",(uint64_t)a.image,info->second.generation,uint32_t(info->second.info.format),a.range.format,
                info->second.info.extent.width,info->second.info.extent.height,info->second.info.extent.depth,info->second.info.mipLevels,info->second.info.arrayLayers,a.range.mip,a.range.layer,a.range.layers,uint32_t(info->second.info.samples),uint32_t(info->second.info.usage),pa.load,pa.store);
            list+=item;
        }
    }
    line("{\"t\":\"pass\",\"i\":%u,\"cb\":%llu,\"render_pass\":%llu,\"framebuffer\":%llu,\"area\":[%d,%d,%u,%u],\"colour_slots\":[%s],\"attachments\":[%s]}",o.index,(uint64_t)cb,(uint64_t)begin->renderPass,(uint64_t)begin->framebuffer,begin->renderArea.offset.x,begin->renderArea.offset.y,begin->renderArea.extent.width,begin->renderArea.extent.height,slots.c_str(),list.c_str());
}
// Before a pass begins: picture every written target the new pass no longer has. `all`: every one (end of the frame).
static void leave(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,VkFramebuffer next,bool all){
    if(pending.empty())return;
    std::vector<TargetKey> staying;
    if(!all){std::lock_guard lock(imageMutex);auto ims=framebuffers.find(next);auto ranges=framebufferRanges.find(next);
        if(ims!=framebuffers.end())for(size_t i=0;i<ims->second.size();i++){ViewRange r=ranges!=framebufferRanges.end()&&i<ranges->second.size()?ranges->second[i]:ViewRange{};staying.push_back({ims->second[i],r.mip,r.layer});}}
    for(auto it=pending.begin();it!=pending.end();){
        if(std::find(staying.begin(),staying.end(),it->first)!=staying.end()){++it;continue;}
        uint32_t n=++departures[it->first];
        // 1,2,3,4,6,8,12,16,24,32...: powers of two and three times powers of two.
        bool take=all||n<=4||!(n&(n-1))||(n%3==0&&!((n/3)&(n/3-1)));
        if(take)snap(c,cb,it->second.pass,it->second.slot,it->second,it->second.drew);
        else line("{\"t\":\"snap_skipped\",\"pass\":%u,\"slot\":%u,\"image\":%llu,\"why\":\"departure %u\"}",it->second.pass,it->second.slot,(uint64_t)it->second.image,n);
        it=pending.erase(it);
    }
}
static void finish(uint64_t presents);
static void beforePass(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,const VkRenderPassBeginInfo* begin){
    int s=state.load(std::memory_order_relaxed);
    if(s==kRecording)leave(c,cb,begin->framebuffer,false);
    else if(s==kFlushing){leave(c,cb,VK_NULL_HANDLE,true);finish(0);}
}
static void passEnd(VkCommandBuffer cb){
    auto found=open.find(cb);if(found==open.end())return;auto o=std::move(found->second);open.erase(found);
    line("{\"t\":\"pass_end\",\"i\":%u,\"draws\":%u,\"vertices\":%llu}",o.index,o.draws,o.vertices);
    for(auto& a:o.attachments){TargetKey key{a.image,a.range.mip,a.range.layer};auto& p=pending[key];bool drew=p.drew||o.draws>0;p=a;p.drew=drew;}
}
// Where the bytes of a buffer range are now, when its memory is mapped or the host's (integrate.h reads light matrices there).
static const uint8_t* peek(VkBuffer buffer,VkDeviceSize at,VkDeviceSize range){
    std::lock_guard lock(tables);auto b=buffers.find(buffer);if(b==buffers.end())return nullptr;auto m=memories.find(b->second.memory);if(m==memories.end())return nullptr;
    VkDeviceSize offset=b->second.offset+at;if(offset+range>m->second.size)return nullptr;
    if(m->second.host)return m->second.host+offset;
    return m->second.mapped&&offset>=m->second.mapOffset?m->second.mapped+(offset-m->second.mapOffset):nullptr;
}
// The bytes of one uniform buffer binding; -1 when the buffer's memory is not readable from here.
static int blob(VkBuffer buffer,VkDeviceSize at,VkDeviceSize range,uint64_t& guestAddress){
    guestAddress=0;uint8_t* pointer=nullptr;
    {std::lock_guard lock(tables);auto b=buffers.find(buffer);if(b==buffers.end())return -1;auto m=memories.find(b->second.memory);if(m==memories.end())return -1;
        VkDeviceSize offset=b->second.offset+at;if(offset+range>m->second.size)return -1;
        if(m->second.host)pointer=m->second.host+offset;else if(m->second.mapped&&offset>=m->second.mapOffset)pointer=m->second.mapped+(offset-m->second.mapOffset);}
    if(!pointer)return -1;
    auto base=static_cast<uint8_t*>(getGuest());if(base&&pointer>=base&&pointer<base+0x100000000ull)guestAddress=uint64_t(pointer-base);
    auto bytes=uint32_t(std::min<VkDeviceSize>(range,4096));auto key=std::make_tuple(uint64_t(buffer),uint64_t(at),uint64_t(bytes));
    // Guest blocks are read once a frame per address; the ring of uniform variables has a new offset per draw anyway.
    auto known=blobIndex.find(key);if(known!=blobIndex.end())return int(known->second);
    if(blobs.size()+bytes>(96u<<20))return -1;
    uint32_t id=uint32_t(blobRecords.size());blobRecords.push_back({uint64_t(buffer),uint64_t(at),guestAddress,uint32_t(blobs.size()),bytes,uint32_t(range)});
    blobs.insert(blobs.end(),pointer,pointer+bytes);blobIndex.emplace(key,id);return int(id);
}
static void draw(VkCommandBuffer cb,uint32_t count,uint32_t instances,bool indexed){
    auto found=open.find(cb);if(found==open.end())return;auto& o=found->second;++o.draws;o.vertices+=uint64_t(count)*std::max(1u,instances);++drawCount;
    auto& native=nativeState(cb);usedPipes.insert(native.pipeline);
    std::string sets;char item[256];
    {std::lock_guard lock(imageMutex);
        for(uint32_t s=0;s<native.slots.size();s++){auto& bound=native.slots[s];if(!bound.set)continue;uint32_t dynamic=0;
            {
                snprintf(item,sizeof(item),"%s{\"set\":%u,\"handle\":%llu,\"ubo\":[",sets.empty()?"":",",s,(uint64_t)bound.set);sets+=item;
                auto ubo=uniforms.find(bound.set);bool first=true;
                if(ubo!=uniforms.end())for(auto& [slot,range]:ubo->second){
                    uint32_t offset=dynamic<bound.offsetCount?bound.offsets[dynamic]:0;++dynamic;uint64_t guestAddress=0;
                    int id=blob(range.buffer,range.offset+offset,range.range,guestAddress);
                    snprintf(item,sizeof(item),"%s[%u,%d,%llu,%llu,%llu]",first?"":",",slot,id,guestAddress,(uint64_t)range.range,(uint64_t)range.buffer);sets+=item;first=false;
                }
                sets+="],\"tex\":[";first=true;
                auto textures=descriptorImages.find(bound.set);
                if(textures!=descriptorImages.end())for(auto& [key,view]:textures->second){
                    auto image=views.find(view);auto range=viewRanges.find(view);if(image==views.end())continue;
                    snprintf(item,sizeof(item),"%s[%u,%llu,%u,%u]",first?"":",",uint32_t(key>>32),(uint64_t)image->second,range==viewRanges.end()?0:range->second.mip,range==viewRanges.end()?0:range->second.layer);sets+=item;first=false;
                }
                sets+="]}";
            }
        }
    }
    line("{\"t\":\"draw\",\"p\":%u,\"pipe\":%llu,\"n\":%u,\"inst\":%u,\"indexed\":%d,\"vp\":[%.6g,%.6g,%.6g,%.6g,%.6g,%.6g],\"sets\":[%s]}",o.index,(uint64_t)native.pipeline,count,instances,int(indexed),
        native.viewport.x,native.viewport.y,native.viewport.width,native.viewport.height,native.viewport.minDepth,native.viewport.maxDepth,sets.c_str());
}
static void transfer(const char* kind,VkImage from,VkImage to,uint32_t regions,const VkImageSubresourceLayers* source,const VkImageSubresourceLayers* target){
    line("{\"t\":\"%s\",\"from\":%llu,\"to\":%llu,\"regions\":%u,\"from_mip\":%u,\"from_layer\":%u,\"to_mip\":%u,\"to_layer\":%u,\"after_pass\":%d}",kind,(uint64_t)from,(uint64_t)to,regions,source?source->mipLevel:0,source?source->baseArrayLayer:0,target?target->mipLevel:0,target?target->baseArrayLayer:0,int(passCount)-1);
}
static void cleared(const char* kind,VkImage image,VkCommandBuffer cb,const float* color,float depth){
    line("{\"t\":\"clear\",\"kind\":\"%s\",\"image\":%llu,\"cb\":%llu,\"color\":[%.6g,%.6g,%.6g,%.6g],\"depth\":%.9g,\"after_pass\":%d}",kind,(uint64_t)image,(uint64_t)cb,color?color[0]:0,color?color[1]:0,color?color[2]:0,color?color[3]:0,depth,int(passCount)-1);
}
// The bridge's own scene marker and the camera it drew with: where Minecraft is put into the frame today.
static void marker(const Camera& camera,VkImage color,VkImage depth,const char* result){
    std::string numbers;char item[32];auto add=[&](const float* v,int n){for(int i=0;i<n;i++){snprintf(item,sizeof(item),"%s%.9g",i?",":"",v[i]);numbers+=item;}};
    numbers="\"view\":[";add(camera.view,12);numbers+="],\"projection\":[";add(camera.projection,16);numbers+="],\"device\":[";add(camera.device,16);numbers+="],\"eye\":[";add(camera.eye,3);numbers+="]";
    line("{\"t\":\"bridge_marker\",\"after_pass\":%d,\"frame\":%u,\"color\":%llu,\"depth\":%llu,\"result\":\"%s\",\"near\":%.9g,\"far\":%.9g,\"fov\":%.9g,\"aspect\":%.9g,\"scene\":[%u,%u],\"camera\":%u,\"camera_projection\":%u,%s}",int(passCount)-1,camera.id,(uint64_t)color,(uint64_t)depth,result,camera.nearZ,camera.farZ,camera.fov,camera.aspect,camera.sceneW,camera.sceneH,camera.cam,camera.proj,numbers.c_str());
}
static void submitted(uint64_t serial,uint32_t count,const VkSubmitInfo* info){
    for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<info[i].commandBufferCount;j++)line("{\"t\":\"submit\",\"serial\":%llu,\"cb\":%llu,\"after_pass\":%d}",serial,(uint64_t)info[i].pCommandBuffers[j],int(passCount)-1);
}
// Called after every present, on the render thread.
static void presented(uint64_t presents,int64_t qpc,int64_t frequency){
    int s=state.load(std::memory_order_acquire);
    if(s==kArmed){
        text.clear();blobs.clear();blobRecords.clear();blobIndex.clear();usedPipes.clear();open.clear();pending.clear();departures.clear();passCount=drawCount=snapCount=0;stagingUsed=0;
        startPresent=presents;startQpc=qpc;line("{\"t\":\"start\",\"present\":%llu,\"qpc\":%lld,\"guest_base\":%llu}",presents,(long long)qpc,(uint64_t)(uintptr_t)getGuest());
        state.store(kRecording,std::memory_order_release);
    }else if(s==kRecording){
        // A second present within a few milliseconds belongs to the same frame (the gamepad's picture).
        if((qpc-startQpc)*50<frequency){line("{\"t\":\"present\",\"present\":%llu,\"qpc\":%lld,\"after_pass\":%d}",presents,(long long)qpc,int(passCount)-1);return;}
        // The frame is over; the targets still waiting are pictured before the next frame's first pass (`beforePass`).
        line("{\"t\":\"frame_end\",\"present\":%llu,\"qpc\":%lld}",presents,(long long)qpc);settleUntil=presents;state.store(kFlushing,std::memory_order_release);
    }else if(s==kFlushing&&presents>settleUntil+2){finish(presents); // no pass began any more: close without the last pictures
    }else if(s==kSettling&&presents>=settleUntil)state.store(kReady,std::memory_order_release);
}
static void finish(uint64_t presents){
    if(!presents)presents=settleUntil;
    {
        line("{\"t\":\"end\",\"passes\":%u,\"draws\":%u,\"snaps\":%u,\"staging_bytes\":%zu,\"uniform_bytes\":%zu}",passCount,drawCount,snapCount,stagingUsed,blobs.size());
        {std::lock_guard lock(tables);char item[256];
            for(auto pipeline:usedPipes){auto found=pipes.find(pipeline);if(found==pipes.end()){line("{\"t\":\"pipeline\",\"handle\":%llu,\"unknown\":1}",(uint64_t)pipeline);continue;}
                auto& p=found->second;std::string blends;for(auto& b:p.blends){snprintf(item,sizeof(item),"%s[%u,%u,%u,%u,%u,%u,%u,%u]",blends.empty()?"":",",b.enable,b.srcColor,b.dstColor,b.colorOp,b.srcAlpha,b.dstAlpha,b.alphaOp,b.mask);blends+=item;}
                line("{\"t\":\"pipeline\",\"handle\":%llu,\"serial\":%llu,\"vs\":\"%016llx\",\"fs\":\"%016llx\",\"gs\":\"%016llx\",\"render_pass\":%llu,\"topology\":%u,\"cull\":%u,\"front\":%u,\"depth_test\":%u,\"depth_write\":%u,\"compare\":%u,\"stencil\":%u,\"bias\":%u,\"stride\":%u,\"attributes\":%u,\"blend\":[%s]}",
                    (uint64_t)pipeline,p.serial,p.vs,p.fs,p.gs,(uint64_t)p.pass,p.topology,p.cull,p.front,p.depthTest,p.depthWrite,p.compare,p.stencil,p.bias,p.stride,p.attributes,blends.c_str());
            }
        }
        for(size_t i=0;i<blobRecords.size();i++){auto& b=blobRecords[i];line("{\"t\":\"uniform\",\"id\":%zu,\"buffer\":%llu,\"at\":%llu,\"guest\":%llu,\"offset\":%u,\"bytes\":%u,\"range\":%u}",i,b.buffer,b.at,b.guest,b.offset,b.bytes,b.range);}
        settleUntil=presents+90;state.store(kSettling,std::memory_order_release);
    }
}
// Worker thread: arm on a new token, write a finished capture.
static void poll(){
    static uint64_t checked=0;static std::string seen;static bool first=true;
    if(GetTickCount64()-checked<250)return;checked=GetTickCount64();
    std::string wanted;{std::ifstream file(directory+"/frame-survey.txt");file>>wanted;}
    if(first){first=false;seen=wanted;keepCode=!wanted.empty();return;}
    int s=state.load(std::memory_order_acquire);
    if(s==kIdle&&!wanted.empty()&&wanted!=seen&&wanted.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos){
        seen=wanted;token=wanted;state.store(kArmed,std::memory_order_release);event("{\"event\":\"survey_armed\",\"token\":\"%s\"}",wanted.c_str());
    }else if(s==kReady){
        state.store(kWriting,std::memory_order_release);
        std::string folder=directory+"/frame-survey";CreateDirectoryA(folder.c_str(),nullptr);folder+="/"+token;CreateDirectoryA(folder.c_str(),nullptr);
        auto write=[&](const std::string& name,const void* data,size_t bytes){FILE* f=fopen((folder+"/"+name).c_str(),"wb");if(!f)return false;bool ok=fwrite(data,1,bytes,f)==bytes;fclose(f);return ok;};
        bool ok=write("frame.jsonl",text.data(),text.size())&&write("uniforms.bin",blobs.data(),blobs.size())&&(!stagingUsed||write("snaps.bin",stagingMapped,stagingUsed));
        size_t modules=0;
        {std::lock_guard lock(tables);std::unordered_set<uint64_t> wanted;for(auto pipeline:usedPipes){auto p=pipes.find(pipeline);if(p!=pipes.end())for(uint64_t h:{p->second.vs,p->second.fs,p->second.gs})if(h)wanted.insert(h);}
            for(uint64_t h:wanted){auto code=shaderCode.find(h);if(code==shaderCode.end())continue;char name[40];snprintf(name,sizeof(name),"shader-%016llx.spv",(unsigned long long)h);if(write(name,code->second.data(),code->second.size()*4))++modules;}}
        event("{\"event\":\"survey_written\",\"token\":\"%s\",\"ok\":%d,\"passes\":%u,\"draws\":%u,\"snaps\":%u,\"snap_bytes\":%zu,\"uniform_bytes\":%zu,\"shaders\":%zu}",token.c_str(),int(ok),passCount,drawCount,snapCount,stagingUsed,blobs.size(),modules);
        write("done.txt",ok?"1":"0",1);state.store(kIdle,std::memory_order_release);
    }
}
static void destroy(const vkroots::VkDeviceDispatch& d){
    if(scratch)d.DestroyImage(d.Device,scratch,nullptr);if(scratchMemory)d.FreeMemory(d.Device,scratchMemory,nullptr);
    for(auto& [format,s]:depthScratch){if(s.image)d.DestroyImage(d.Device,s.image,nullptr);if(s.memory)d.FreeMemory(d.Device,s.memory,nullptr);s={};s.failed=true;}
    if(staging)d.DestroyBuffer(d.Device,staging,nullptr);if(stagingMemory)d.FreeMemory(d.Device,stagingMemory,nullptr);
    scratch=VK_NULL_HANDLE;staging=VK_NULL_HANDLE;scratchMemory=stagingMemory=VK_NULL_HANDLE;stagingMapped=nullptr;gpuFailed=true;
}
}
