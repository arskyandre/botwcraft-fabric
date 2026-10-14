// Minecraft's geometry inside BOTW's own frame (report 44): instead of being drawn over
// the finished picture at the post-HDR marker, it is recorded into BOTW's targets at the points
// of the frame where BOTW's own geometry of the same kind is:
//   - opaque and cutout geometry into the G-buffer, when BOTW's own G-buffer fill has ended
//     (the full-size R32F linear-depth pass begins); BOTW then lights, shadows, fogs and
//     post-processes it;
//   - the same geometry into each shadow cascade when that cascade's passes have ended, with the
//     light matrices of BOTW's own caster draws;
//   - blended geometry into the HDR scene colour before the first bloom pass, lit with the sun
//     of BOTW's lighting pass.
// The frame is recognised by the formats and sizes of its framebuffers, never by pass numbers.
// A frame in which a step is not recognised keeps the marker path for that part (render.h).
// Everything here runs on Cemu's render thread while it records; nothing waits.
#pragma once
namespace integrate {
// lab/integrate.txt: `parts class flags normalAlpha skyFloor blockLight ambient biasScale albedo`.
// parts: 1 G-buffer, 2 HDR blend, 4 shadow casters (7 when the file is absent; 0 is stage 1).
enum {kOpaque=1,kBlend=2,kCast=4};
static std::atomic<int> parts{7};
static std::atomic<float> materialClass{4},materialFlags{0},normalAlpha{47},skyFloor{.25f},blockLight{3},ambient{.15f},biasScale{1},albedoScale{.45f};
static void poll(){
    static uint64_t checked=0;if(GetTickCount64()-checked<500)return;checked=GetTickCount64();
    int p=7;float v[8]={4,0,47,.25f,3,.15f,1,.45f};std::ifstream file(directory+"/integrate.txt");
    if(file>>p)for(float& f:v)if(!(file>>f))break;
    parts=p&7;materialClass=std::clamp(v[0],0.f,255.f);materialFlags=std::clamp(v[1],0.f,255.f);normalAlpha=std::clamp(v[2],0.f,255.f);skyFloor=std::clamp(v[3],0.f,1.f);
    blockLight=std::clamp(v[4],0.f,64.f);ambient=std::clamp(v[5],0.f,4.f);biasScale=std::clamp(v[6],0.f,16.f);albedoScale=std::clamp(v[7],0.f,2.f);
}

// What a framebuffer of Cemu's is, by its attachments.
enum {kOther,kGBuffer,kSceneColour,kLinearDepth,kShadow,kLighting,kComposite,kSmallHdr};
enum {iMaterial,iAlbedo,iNormal,iHdr,iDepth,iCount};
struct Kind{int type=kOther;bool empty=true;VkImage images[iCount]{};VkExtent2D extent{};uint32_t layer=0;};
static std::unordered_map<VkFramebuffer,Kind> kinds;
static const Kind& kindOf(VkFramebuffer fb){
    auto known=kinds.find(fb);if(known!=kinds.end())return known->second;
    Kind k;std::lock_guard lk(imageMutex);
    auto attached=framebuffers.find(fb);auto ranges=survey::framebufferRanges.find(fb);
    if(attached==framebuffers.end()||ranges==survey::framebufferRanges.end()||attached->second.size()!=ranges->second.size())return kinds[fb]=k;
    uint32_t n=uint32_t(attached->second.size()),count[iCount]{},r32=0,d16=0,r8=0,other=0,layers=1;VkImage shadow=VK_NULL_HANDLE;bool same=true,base=true;
    for(uint32_t i=0;i<n;i++){
        auto image=images.find(attached->second[i]);if(image==images.end())return kinds[fb]=k;
        auto& info=image->second.info;auto& range=ranges->second[i];k.empty=false;
        if(!i)k.extent={info.extent.width,info.extent.height};else same&=info.extent.width==k.extent.width&&info.extent.height==k.extent.height;
        base&=range.mip==0&&info.samples==VK_SAMPLE_COUNT_1_BIT;
        auto slot=[&](int which){if(!count[which]++)k.images[which]=attached->second[i];};
        switch(range.format?VkFormat(range.format):info.format){
            case VK_FORMAT_R8G8_UNORM:slot(iMaterial);break;
            case VK_FORMAT_R8G8B8A8_SRGB:slot(iAlbedo);break;
            case VK_FORMAT_R8G8B8A8_UNORM:slot(iNormal);break;
            case VK_FORMAT_B10G11R11_UFLOAT_PACK32:slot(iHdr);break;
            case VK_FORMAT_D32_SFLOAT:slot(iDepth);break;
            case VK_FORMAT_R32_SFLOAT:++r32;break;
            case VK_FORMAT_R8_UNORM:++r8;break;
            case VK_FORMAT_D16_UNORM:++d16;shadow=attached->second[i];layers=info.arrayLayers;k.layer=range.layer;break;
            default:++other;
        }
    }
    bool full=k.extent.width>=1024&&k.extent.height>=512;
    if(k.empty||!base){}
    else if(same&&full&&!other&&!r32&&!d16&&!r8&&count[iMaterial]==1&&count[iAlbedo]==1&&count[iNormal]==1&&count[iDepth]==1&&count[iHdr]<=1&&n==4+count[iHdr])k.type=kGBuffer;
    else if(same&&full&&n==2&&count[iHdr]==1&&count[iDepth]==1)k.type=kSceneColour;
    else if(full&&n==1&&r32==1)k.type=kLinearDepth;
    else if(n==1&&d16==1&&layers>1){k.type=kShadow;k.images[iDepth]=shadow;}
    else if(same&&n==6&&d16==1&&count[iHdr]>=2&&r8==1&&count[iNormal]==1)k.type=kLighting;
    else if(same&&full&&n==2&&count[iHdr]==1&&d16==1)k.type=kComposite;
    else if(n==1&&count[iHdr]==1)k.type=kSmallHdr;
    return kinds[fb]=k;
}

// The camera the selectors applied for a frame, kept when they run (probe.cpp renderCamera,
// renderProjection): the G-buffer of that frame is recorded long before its post-HDR marker.
struct EarlyCamera{uint32_t frame=0;bool hasView=false,hasProjection=false,allowed=false,scene=false;float view[12]{},eye[3]{},target[3]{},device[16]{},nearZ=0,farZ=0,fov=0,aspect=0;ViewProbe request;};
static EarlyCamera earlyCameras[64];
static std::atomic_flag earlyBusy=ATOMIC_FLAG_INIT;
// scene: the game's own camera in a scene of its own (probe.cpp sceneRequest); the picture is shown through its fades too.
static void earlyView(uint32_t frame,const uint8_t* data,const ViewProbe& request,bool scene){
    if(earlyBusy.test_and_set(std::memory_order_acquire))return;
    auto& e=earlyCameras[frame%64];if(e.frame!=frame){e={};e.frame=frame;}
    for(int i=0;i<12;i++)e.view[i]=real(data+i*4);for(int i=0;i<3;i++){e.eye[i]=real(data+0x34+i*4);e.target[i]=real(data+0x40+i*4);}
    e.request=request;e.allowed=scene||body::allowed();e.scene=scene;e.hasView=true;earlyBusy.clear(std::memory_order_release);
}
static void earlyProjection(uint32_t frame,const uint8_t* data,const ViewProbe& request){
    if(earlyBusy.test_and_set(std::memory_order_acquire))return;
    auto& e=earlyCameras[frame%64];if(e.frame!=frame){e={};e.frame=frame;}
    for(int i=0;i<16;i++)e.device[i]=real(data+0x44+i*4);e.nearZ=real(data+0x94);e.farZ=real(data+0x98);e.fov=real(data+0x9C);e.aspect=real(data+0xAC);
    e.request=request;e.hasProjection=true;earlyBusy.clear(std::memory_order_release);
}
static const char* earlyCamera(uint32_t frame,Camera& s){
    // The selectors hold this for one small copy; a miss here would cost the frame its lighting (route-75: 1 frame in 416), so look again for a few microseconds.
    bool held=false;for(int tries=0;tries<4000&&!held;tries++){held=!earlyBusy.test_and_set(std::memory_order_acquire);if(!held)YieldProcessor();}
    if(!held)return "camera_busy";
    EarlyCamera e=earlyCameras[frame%64];earlyBusy.clear(std::memory_order_release);
    if(e.frame!=frame||!e.hasView||!e.hasProjection)return "no_camera";
    s={};s.id=frame;s.scene=e.scene;s.epoch=e.request.target;s.bodyEpoch=e.request.epoch;s.actor=e.request.actor;s.gameplay=e.allowed;s.common=e.request.common;s.mcScale=e.request.scale;
    std::copy_n(e.request.origin,3,s.mcOrigin);std::copy_n(e.request.anchor,3,s.mcAnchor);s.mcView=e.request.view;s.hasFeet=e.request.hasFeet;std::copy_n(e.request.feet,3,s.feet);
    std::copy_n(e.view,12,s.view);std::copy_n(e.device,16,s.projection);std::copy_n(e.device,16,s.device);std::copy_n(e.eye,3,s.eye);std::copy_n(e.target,3,s.target);
    s.nearZ=e.nearZ;s.farZ=e.farZ;s.fov=e.fov;s.aspect=e.aspect;s.zScale=1;s.zOffset=0;
    if(!s.common)return "no_lease";
    if(!s.gameplay)return "gameplay";
    // The game's own camera is no lease of ours: a release inside the frame (the fade that ends a scene) does not make it stale.
    if(!s.scene&&s.epoch!=targetEpoch.load())return "camera_epoch";
    if(!s.scene&&s.bodyEpoch!=body::lifecycleEpoch.load())return "body_epoch";
    for(float v:s.view)if(!std::isfinite(v))return "view";
    if(!sceneProjection(s))return "projection";
    return nullptr;
}

// Our render passes on BOTW's targets and the pipelines for them (shaders: render.h).
static VkRenderPass gbufferPass=VK_NULL_HANDLE,hdrPass=VK_NULL_HANDLE,shadowPass=VK_NULL_HANDLE;
static VkPipeline gbufferPipeline=VK_NULL_HANDLE,hdrCullPipeline=VK_NULL_HANDLE,hdrPipeline=VK_NULL_HANDLE,shadowPipeline=VK_NULL_HANDLE;
// The blended geometry reads BOTW's screen-space sun shadow (the red of the lighting pass's RGBA8
// target, half size) as set 1; set 0 and the push block are those of every other pipeline here.
static VkDescriptorSetLayout maskLayout=VK_NULL_HANDLE;static VkPipelineLayout hdrLayout=VK_NULL_HANDLE;static VkDescriptorPool maskPool=VK_NULL_HANDLE;static VkSampler maskSampler=VK_NULL_HANDLE;
struct Mask{VkImage image;uint64_t generation;VkImageView view;VkDescriptorSet set;};
static std::vector<Mask> masks;
static bool built=false,failed=false;
static VkRenderPass makePass(const vkroots::VkDeviceDispatch& d,std::initializer_list<VkFormat> colours,VkFormat depth){
    VkAttachmentDescription attachments[6]{};VkAttachmentReference references[6]{};uint32_t n=0;
    auto add=[&](VkFormat format){auto& a=attachments[n];a.format=format;a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_GENERAL;references[n]={n,VK_IMAGE_LAYOUT_GENERAL};++n;};
    for(auto format:colours)add(format);uint32_t colourCount=n;add(depth);
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=colourCount;sub.pColorAttachments=references;sub.pDepthStencilAttachment=&references[colourCount];
    const VkPipelineStageFlags stages=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags access=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkSubpassDependency dependencies[2]{};dependencies[0]={VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,stages,VK_ACCESS_MEMORY_WRITE_BIT,access,0};dependencies[1]={0,VK_SUBPASS_EXTERNAL,stages,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,access,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};ri.attachmentCount=n;ri.pAttachments=attachments;ri.subpassCount=1;ri.pSubpasses=&sub;ri.dependencyCount=2;ri.pDependencies=dependencies;
    VkRenderPass pass;check(d.CreateRenderPass(d.Device,&ri,nullptr,&pass),"integrate render pass");return pass;
}
// blend: straight alpha over what is there. bias: depth bias set while recording.
static VkPipeline makePipeline(const vkroots::VkDeviceDispatch& d,const std::vector<uint32_t>& fragment,VkPipelineLayout layout,VkRenderPass pass,uint32_t colours,bool blend,VkCullModeFlags cull,bool depthWrite,bool bias){
    VkShaderModule vs=shader(d,meshVS),fs=shader(d,fragment);
    VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[0].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fs;stages[1].pName="main";
    VkVertexInputBindingDescription binding{0,32,VK_VERTEX_INPUT_RATE_VERTEX};VkVertexInputAttributeDescription attrs[5]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,12},{2,0,VK_FORMAT_R8G8B8A8_UNORM,20},{3,0,VK_FORMAT_R32_UINT,24},{4,0,VK_FORMAT_R32_UINT,28}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex.vertexBindingDescriptionCount=1;vertex.pVertexBindingDescriptions=&binding;vertex.vertexAttributeDescriptionCount=5;vertex.pVertexAttributeDescriptions=attrs;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=1;viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=cull;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;raster.depthBiasEnable=bias;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=depthWrite;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState blends[4]{};for(auto& b:blends){b.colorWriteMask=15;b.blendEnable=blend;b.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;b.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;b.colorBlendOp=VK_BLEND_OP_ADD;b.srcAlphaBlendFactor=VK_BLEND_FACTOR_ZERO;b.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;b.alphaBlendOp=VK_BLEND_OP_ADD;}
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=colours;blending.pAttachments=blends;
    VkDynamicState dynamics[3]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=bias?3:2;dynamic.pDynamicStates=dynamics;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};ci.stageCount=2;ci.pStages=stages;ci.pVertexInputState=&vertex;ci.pInputAssemblyState=&assembly;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;ci.pMultisampleState=&samples;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blending;ci.pDynamicState=&dynamic;ci.layout=layout;ci.renderPass=pass;
    VkPipeline result;auto r=d.CreateGraphicsPipelines(d.Device,VK_NULL_HANDLE,1,&ci,nullptr,&result);d.DestroyShaderModule(d.Device,vs,nullptr);d.DestroyShaderModule(d.Device,fs,nullptr);check(r,"integrate pipeline");return result;
}
static void build(const vkroots::VkDeviceDispatch& d){
    if(built)return;
    if(gbufferFS.empty()||hdrFS.empty()||shadowFS.empty())throw std::runtime_error("integrate shaders missing");
    gbufferPass=makePass(d,{VK_FORMAT_R8G8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B10G11R11_UFLOAT_PACK32},VK_FORMAT_D32_SFLOAT);
    hdrPass=makePass(d,{VK_FORMAT_B10G11R11_UFLOAT_PACK32},VK_FORMAT_D32_SFLOAT);
    shadowPass=makePass(d,{},VK_FORMAT_D16_UNORM);
    VkDescriptorSetLayoutBinding binding{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};li.bindingCount=1;li.pBindings=&binding;check(d.CreateDescriptorSetLayout(d.Device,&li,nullptr,&maskLayout),"mask layout");
    VkDescriptorSetLayout sets[2]={descriptorLayout,maskLayout};VkPushConstantRange push{kPushStages,0,sizeof(Push)};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=2;pl.pSetLayouts=sets;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;check(d.CreatePipelineLayout(d.Device,&pl,nullptr,&hdrLayout),"hdr layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=16;pi.poolSizeCount=1;pi.pPoolSizes=&size;check(d.CreateDescriptorPool(d.Device,&pi,nullptr,&maskPool),"mask pool");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;si.maxLod=0;check(d.CreateSampler(d.Device,&si,nullptr,&maskSampler),"mask sampler");
    gbufferPipeline=makePipeline(d,gbufferFS,pipelineLayout,gbufferPass,4,false,VK_CULL_MODE_NONE,true,false);
    hdrCullPipeline=makePipeline(d,hdrFS,hdrLayout,hdrPass,1,true,VK_CULL_MODE_BACK_BIT,false,false);
    hdrPipeline=makePipeline(d,hdrFS,hdrLayout,hdrPass,1,true,VK_CULL_MODE_NONE,false,false);
    shadowPipeline=makePipeline(d,shadowFS,pipelineLayout,shadowPass,0,false,VK_CULL_MODE_NONE,true,true);
    built=true;
}
// One framebuffer of ours per set of BOTW's images (and layer), dropped when one of them is destroyed.
struct Target{VkRenderPass pass;VkImage images[iCount];uint64_t generations[iCount];uint32_t count,layer;VkImageView views[iCount];VkFramebuffer fb;};
static std::vector<Target> targets;
static void retire(const vkroots::VkDeviceDispatch& d,VkImage image){
    for(auto it=targets.begin();it!=targets.end();){
        bool uses=false;for(uint32_t i=0;i<it->count;i++)uses|=it->images[i]==image;
        if(!uses){++it;continue;}
        d.DestroyFramebuffer(d.Device,it->fb,nullptr);for(uint32_t i=0;i<it->count;i++)d.DestroyImageView(d.Device,it->views[i],nullptr);it=targets.erase(it);
    }
    // A mask's set stays in the pool, never written again; the pool is small because the image is replaced only with the scene's size.
    for(auto& m:masks)if(m.image==image&&m.view){d.DestroyImageView(d.Device,m.view,nullptr);m.view=VK_NULL_HANDLE;m.image=VK_NULL_HANDLE;}
}
// The set that binds BOTW's shadow mask image; null when it cannot be had (the blended geometry then stays with the marker path).
static VkDescriptorSet maskSet(const vkroots::VkDeviceDispatch& d,VkImage image){
    uint64_t generation=0;
    {std::lock_guard lk(imageMutex);auto found=images.find(image);if(found==images.end()||found->second.layout!=VK_IMAGE_LAYOUT_GENERAL||found->second.info.format!=VK_FORMAT_R8G8B8A8_UNORM||found->second.info.samples!=VK_SAMPLE_COUNT_1_BIT||!(found->second.info.usage&VK_IMAGE_USAGE_SAMPLED_BIT))return VK_NULL_HANDLE;generation=found->second.generation;}
    for(auto& m:masks)if(m.image==image&&m.generation==generation&&m.view)return m.set;
    if(masks.size()>=16)return VK_NULL_HANDLE;
    Mask m{image,generation,VK_NULL_HANDLE,VK_NULL_HANDLE};VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=maskPool;ai.descriptorSetCount=1;ai.pSetLayouts=&maskLayout;
    if(d.AllocateDescriptorSets(d.Device,&ai,&m.set)!=VK_SUCCESS)return VK_NULL_HANDLE;
    m.view=imageView(d,image,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_ASPECT_COLOR_BIT);
    VkDescriptorImageInfo info{maskSampler,m.view,VK_IMAGE_LAYOUT_GENERAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=m.set;write.dstBinding=0;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&info;d.UpdateDescriptorSets(d.Device,1,&write,0,nullptr);
    masks.push_back(m);return m.set;
}
static VkFramebuffer target(const vkroots::VkDeviceDispatch& d,VkRenderPass pass,const VkImage* list,const VkFormat* formats,uint32_t count,uint32_t layer,VkExtent2D extent){
    uint64_t generations[iCount]{};
    {std::lock_guard lk(imageMutex);for(uint32_t i=0;i<count;i++){auto image=images.find(list[i]);if(image==images.end()||image->second.layout!=VK_IMAGE_LAYOUT_GENERAL||image->second.info.format!=formats[i]||image->second.info.samples!=VK_SAMPLE_COUNT_1_BIT||image->second.info.extent.width!=extent.width||image->second.info.extent.height!=extent.height||layer>=image->second.info.arrayLayers)return VK_NULL_HANDLE;generations[i]=image->second.generation;}}
    for(auto& t:targets)if(t.pass==pass&&t.count==count&&t.layer==layer&&std::equal(list,list+count,t.images)&&std::equal(generations,generations+count,t.generations))return t.fb;
    Target t{};t.pass=pass;t.count=count;t.layer=layer;uint32_t made=0;
    try{
        for(;made<count;made++){
            t.images[made]=list[made];t.generations[made]=generations[made];bool depth=formats[made]==VK_FORMAT_D32_SFLOAT||formats[made]==VK_FORMAT_D16_UNORM;
            VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=list[made];ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=formats[made];ci.subresourceRange={VkImageAspectFlags(depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,layer,1};
            check(d.CreateImageView(d.Device,&ci,nullptr,&t.views[made]),"integrate view");
        }
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fi.renderPass=pass;fi.attachmentCount=count;fi.pAttachments=t.views;fi.width=extent.width;fi.height=extent.height;fi.layers=1;check(d.CreateFramebuffer(d.Device,&fi,nullptr,&t.fb),"integrate framebuffer");
    }catch(...){for(uint32_t i=0;i<made;i++)d.DestroyImageView(d.Device,t.views[i],nullptr);throw;}
    targets.push_back(t);return t.fb;
}
static void destroy(const vkroots::VkDeviceDispatch& d){
    for(auto& t:targets){d.DestroyFramebuffer(d.Device,t.fb,nullptr);for(uint32_t i=0;i<t.count;i++)d.DestroyImageView(d.Device,t.views[i],nullptr);}targets.clear();kinds.clear();
    for(auto p:{gbufferPipeline,hdrCullPipeline,hdrPipeline,shadowPipeline})if(p)d.DestroyPipeline(d.Device,p,nullptr);
    for(auto p:{gbufferPass,hdrPass,shadowPass})if(p)d.DestroyRenderPass(d.Device,p,nullptr);
    for(auto& m:masks)if(m.view)d.DestroyImageView(d.Device,m.view,nullptr);masks.clear();
    if(maskPool)d.DestroyDescriptorPool(d.Device,maskPool,nullptr);if(hdrLayout)d.DestroyPipelineLayout(d.Device,hdrLayout,nullptr);if(maskLayout)d.DestroyDescriptorSetLayout(d.Device,maskLayout,nullptr);if(maskSampler)d.DestroySampler(d.Device,maskSampler,nullptr);
    built=false;
}

// What has been seen and done in the frame being recorded. Cleared at its marker and at the present.
struct Tail{float m[28];uint32_t count;};
struct Frame{
    VkImage material=VK_NULL_HANDLE,albedo=VK_NULL_HANDLE,normal=VK_NULL_HANDLE,depth=VK_NULL_HANDLE,hdr=VK_NULL_HANDLE,hdrDepth=VK_NULL_HANDLE;VkExtent2D extent{};
    bool tried=false,opaque=false,composite=false,triedBlend=false,blended=false;uint32_t id=0;Camera camera;
    // Shadows: the cascade array, the layer whose passes are being recorded, what its caster draws carry.
    VkImage shadowImage=VK_NULL_HANDLE;VkExtent2D shadowExtent{};int shadowLayer=-1;uint32_t cast=0;Tail tails[4]{};uint32_t tailCount=0;
    float bias[3]{};bool hasBias=false;VkViewport viewport{};VkRect2D scissor{};
    float lightView[12]{};bool light=false;float sunColour[3]{};bool sun=false;VkImage mask=VK_NULL_HANDLE;
    int watching=0; // 1: caster draws of the current layer, 2: draws of the lighting pass
    uint32_t vertices[3]{};int64_t ticks=0;const char* why[3]={"","",""}; // G-buffer, HDR blend, shadow casters
    uint32_t shadowPasses=0,watched=0,unread=0;
    // While the sun is not found: the unit vector in the lighting uniforms nearest to the one
    // expected (largest difference of a component), its w, the float nearest to that w elsewhere
    // in the block, its byte offset; the lighting draws read and the largest block.
    float sunNear[4]={9,0,0,0};uint32_t sunDraws=0,sunBytes=0;
};
static Frame frame;
static uint32_t lastMarker=0;static bool haveMarker=false;
struct Done{uint32_t frame=0;bool opaque=false,blended=false;uint32_t cast=0;Camera camera;};
static Done done;
// The sun last read from a lighting pass: it changes over minutes, and a frame whose lighting
// uniforms were not recognised (route-76: 1 in 400) uses it instead of leaving the blended
// geometry to the marker path for that frame.
struct Sun{float direction[3]{},colour[3]{};ULONGLONG at=0;};
static Sun lastSun;

static void sync(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};b.srcAccessMask=b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    c.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&b,0,nullptr,0,nullptr);
}
static void multiply(const float* a,const float* b,float* out){for(int i=0;i<4;i++)for(int j=0;j<4;j++){float v=0;for(int k=0;k<4;k++)v+=a[i*4+k]*b[k*4+j];out[i*4+j]=v;}}
struct Clock{LARGE_INTEGER start;Clock(){QueryPerformanceCounter(&start);}~Clock(){LARGE_INTEGER end;QueryPerformanceCounter(&end);frame.ticks+=end.QuadPart-start.QuadPart;}};

// Opaque and cutout geometry into the G-buffer. On success the frame has its camera.
static const char* injectOpaque(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    if(!(parts.load()&kOpaque))return "off";
    if(!fixtureReady.load(std::memory_order_acquire)||resourcesFailed||failed)return "resources";
    if(!presentation::enabled.load()||!presentation::eligible.load())return "disabled";
    if(!haveMarker)return "no_marker";
    if(frame.extent.width!=hostSceneW.load()||frame.extent.height!=hostSceneH.load())return "extent";
    if(!frame.hdr||frame.hdrDepth!=frame.depth)return "no_hdr";
    Camera camera;if(auto why=earlyCamera(lastMarker+1,camera))return why;
    auto saved=nativeState(cb);if(saved.overflow)return "native_state";
    Clock clock;
    try{
        const auto& d=*c.pDeviceDispatch;if(!resourcesInitialized)initResources(c,cb);build(d);
        const VkImage list[iCount]={frame.material,frame.albedo,frame.normal,frame.hdr,frame.depth};
        const VkFormat formats[iCount]={VK_FORMAT_R8G8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_D32_SFLOAT};
        auto fb=target(d,gbufferPass,list,formats,iCount,0,frame.extent);if(!fb)return "target";
        auto generation=frameGpu(c,cb);if(!generation)return "scene";
        gpuRecorded[cb].push_back(generation);
        auto timing=beginTiming(c,cb,camera.id,1);
        sync(c,cb);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=gbufferPass;begin.framebuffer=fb;begin.renderArea={{0,0},frame.extent};c.CmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0,float(frame.extent.height),float(frame.extent.width),-float(frame.extent.height),0,1};VkRect2D scissor{{0,0},frame.extent};c.CmdSetViewport(cb,0,1,&viewport);c.CmdSetScissor(cb,0,1,&scissor);
        Push push{};float view[16]{};for(int i=0;i<12;i++)view[i]=camera.view[i];view[15]=1;multiply(camera.device,view,push.matrix);
        push.params[0]=float(frame.extent.width);push.params[1]=float(frame.extent.height);push.params[2]=albedoScale.load();push.params[3]=blockLight.load();
        for(int i=0;i<3;i++){push.extra[i]=camera.view[i];push.extra[4+i]=camera.view[4+i];}
        push.extra[3]=std::floor(materialClass.load())+256*std::floor(materialFlags.load());push.extra[7]=std::floor(normalAlpha.load())+256*std::floor(skyFloor.load()*255);
        WorldPipes pipes;pipes.opaque=gbufferPipeline;uint32_t drawn[4]{};liveWorld(c,cb,*generation,camera,push,pipes,drawn);
        c.CmdEndRenderPass(cb);sync(c,cb);restoreNative(c,cb,saved);
        if(timing>=0)c.CmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timingPool,uint32_t(timing)*2+1);
        frame.opaque=true;frame.camera=camera;frame.id=camera.id;frame.vertices[0]=drawn[0];done={};done.frame=camera.id;done.opaque=true;done.camera=camera;
        return nullptr;
    }catch(const std::exception& e){failed=true;event("{\"event\":\"integrate_error\",\"where\":\"gbuffer\",\"message\":\"%s\"}",e.what());return "error";}
}
// Blended geometry into the HDR colour, lit with the frame's sun.
static const char* injectBlend(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    if(!(parts.load()&kBlend))return "off";
    if(frame.sun){std::copy_n(frame.lightView+8,3,lastSun.direction);std::copy_n(frame.sunColour,3,lastSun.colour);lastSun.at=GetTickCount64();}
    else if(!lastSun.at||GetTickCount64()-lastSun.at>2000)return "no_sun";
    if(!frame.mask)return "no_mask";
    if(failed||resourcesFailed)return "resources";
    auto saved=nativeState(cb);if(saved.overflow)return "native_state";
    Clock clock;
    try{
        const auto& d=*c.pDeviceDispatch;const Camera& camera=frame.camera;
        const VkImage list[2]={frame.hdr,frame.depth};const VkFormat formats[2]={VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_D32_SFLOAT};
        auto fb=target(d,hdrPass,list,formats,2,0,frame.extent);if(!fb)return "target";
        auto shade=maskSet(d,frame.mask);if(!shade)return "mask";
        auto generation=frameGpu(c,cb);if(!generation)return "scene";
        gpuRecorded[cb].push_back(generation);
        auto timing=beginTiming(c,cb,camera.id,2);
        sync(c,cb);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=hdrPass;begin.framebuffer=fb;begin.renderArea={{0,0},frame.extent};c.CmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0,float(frame.extent.height),float(frame.extent.width),-float(frame.extent.height),0,1};VkRect2D scissor{{0,0},frame.extent};c.CmdSetViewport(cb,0,1,&viewport);c.CmdSetScissor(cb,0,1,&scissor);
        Push push{};float view[16]{};for(int i=0;i<12;i++)view[i]=camera.view[i];view[15]=1;multiply(camera.device,view,push.matrix);
        push.params[0]=float(frame.extent.width);push.params[1]=float(frame.extent.height);push.params[2]=std::max(1.f,float(frame.extent.height)/1080*1.25f);
        for(int i=0;i<3;i++){push.extra[i]=lastSun.direction[i];push.extra[4+i]=lastSun.colour[i]*albedoScale.load();}push.extra[3]=ambient.load();push.extra[7]=blockLight.load();
        c.CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,hdrLayout,0,1,&generation->set,0,nullptr);c.CmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,hdrLayout,1,1,&shade,0,nullptr);
        WorldPipes pipes;pipes.blendCull=hdrCullPipeline;pipes.blend=hdrPipeline;uint32_t drawn[4]{};liveWorld(c,cb,*generation,camera,push,pipes,drawn);
        c.CmdEndRenderPass(cb);sync(c,cb);restoreNative(c,cb,saved);
        if(timing>=0)c.CmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timingPool,uint32_t(timing)*2+1);
        frame.blended=true;frame.vertices[1]=drawn[1]+drawn[2]+drawn[3];if(done.frame==frame.id)done.blended=true;
        return nullptr;
    }catch(const std::exception& e){failed=true;event("{\"event\":\"integrate_error\",\"where\":\"hdr\",\"message\":\"%s\"}",e.what());return "error";}
}
// The cascade whose passes have just ended: the same opaque geometry as casters, with the
// matrices most of that cascade's own caster draws carried.
static const char* castShadows(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    int layer=frame.shadowLayer;if(layer<0||layer>31||(frame.cast&(1u<<layer))||!(parts.load()&kCast))return nullptr;
    if(!frame.opaque||failed||resourcesFailed)return "resources";
    const Tail* best=nullptr;for(uint32_t i=0;i<frame.tailCount;i++)if(!best||frame.tails[i].count>best->count)best=&frame.tails[i];
    if(!best||best->count<3)return "no_matrix";
    if(frame.viewport.width<=0||!frame.shadowExtent.width)return "no_viewport";
    auto saved=nativeState(cb);if(saved.overflow)return "native_state";
    Clock clock;
    try{
        const auto& d=*c.pDeviceDispatch;const VkImage list[1]={frame.shadowImage};const VkFormat formats[1]={VK_FORMAT_D16_UNORM};
        auto fb=target(d,shadowPass,list,formats,1,uint32_t(layer),frame.shadowExtent);if(!fb)return "target";
        auto generation=frameGpu(c,cb);if(!generation)return "scene";
        gpuRecorded[cb].push_back(generation);
        auto timing=layer==0?beginTiming(c,cb,frame.id,3):-1;
        sync(c,cb);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=shadowPass;begin.framebuffer=fb;begin.renderArea={{0,0},frame.shadowExtent};c.CmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);
        c.CmdSetViewport(cb,0,1,&frame.viewport);c.CmdSetScissor(cb,0,1,&frame.scissor);
        float scale=biasScale.load();c.CmdSetDepthBias(cb,frame.hasBias?frame.bias[0]*scale:0,frame.hasBias?frame.bias[1]:0,frame.hasBias?frame.bias[2]*scale:0);
        Push push{};float view[16]{};for(int i=0;i<12;i++)view[i]=best->m[i];view[15]=1;multiply(best->m+12,view,push.matrix);
        push.params[0]=frame.viewport.width;push.params[1]=std::abs(frame.viewport.height);push.params[2]=1;
        WorldPipes pipes;pipes.opaque=shadowPipeline;uint32_t drawn[4]{};liveWorld(c,cb,*generation,frame.camera,push,pipes,drawn);
        c.CmdEndRenderPass(cb);sync(c,cb);restoreNative(c,cb,saved);
        if(timing>=0)c.CmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timingPool,uint32_t(timing)*2+1);
        frame.cast|=1u<<layer;frame.vertices[2]+=drawn[0];if(done.frame==frame.id)done.cast=frame.cast;
        return nullptr;
    }catch(const std::exception& e){failed=true;event("{\"event\":\"integrate_error\",\"where\":\"shadow\",\"message\":\"%s\"}",e.what());return "error";}
}

// The first uniform block of one of the sets bound for Cemu's next draw (set 0 is the vertex
// shader's, set 1 the fragment shader's), as its bytes are now.
static const uint8_t* uniformBytes(const NativeState& native,uint32_t setIndex,size_t& bytes){
    auto& slot=native.slots[setIndex];if(!slot.set)return nullptr;
    survey::Range range{};{std::lock_guard lk(imageMutex);auto found=survey::uniforms.find(slot.set);if(found==survey::uniforms.end()||found->second.empty())return nullptr;range=found->second.begin()->second;}
    if(range.range<16||range.range>8192)return nullptr;
    bytes=size_t(range.range);return survey::peek(range.buffer,range.offset+(slot.offsetCount?slot.offsets[0]:0),range.range);
}
// Called for Cemu's draws while `frame.watching` is set.
static void nativeDraw(VkCommandBuffer cb){
    auto& native=nativeState(cb);size_t bytes=0;++frame.watched;
    if(frame.watching==1){
        // Report 44: the last 112 bytes of a caster's vertex uniforms are the light's view (3x4,
        // a rotation) and the cascade's projection (4x4).
        auto data=uniformBytes(native,0,bytes);if(!data||bytes<112){++frame.unread;return;}
        Tail tail{};memcpy(tail.m,data+bytes-112,112);tail.count=1;
        for(float v:tail.m)if(!std::isfinite(v))return;
        for(int i=0;i<3;i++)for(int j=i;j<3;j++){float dot=0;for(int k=0;k<3;k++)dot+=tail.m[i*4+k]*tail.m[j*4+k];if(std::abs(dot-(i==j?1:0))>2e-3f)return;}
        Tail* same=nullptr;for(uint32_t i=0;i<frame.tailCount;i++)if(!memcmp(frame.tails[i].m,tail.m,sizeof(tail.m)))same=&frame.tails[i];
        if(same)++same->count;else if(frame.tailCount<4)frame.tails[frame.tailCount++]=tail;else return;
        if(native.hasBias){std::copy_n(native.bias,3,frame.bias);frame.hasBias=true;}
        if(native.hasViewport&&native.hasScissor){frame.viewport=native.viewport;frame.scissor=native.scissor;}
        if(same&&same->count>=3&&!frame.light){std::copy_n(same->m,12,frame.lightView);frame.light=true;}
        if(same&&same->count>=6)frame.watching=0;
    }else if(frame.watching==2){
        // The lighting shaders hold minus the sun's direction in view space in a vec4 whose w is
        // the red of the sun's colour, another vec4 of the same block.
        auto data=uniformBytes(native,1,bytes);if(!data||bytes<32){++frame.unread;return;}
        float want[3];for(int i=0;i<3;i++){want[i]=0;for(int k=0;k<3;k++)want[i]-=frame.camera.view[i*4+k]*frame.lightView[8+k];}
        size_t n=std::min<size_t>(bytes/16,256);float v[4],colour[4];
        ++frame.sunDraws;frame.sunBytes=std::max<uint32_t>(frame.sunBytes,uint32_t(bytes));
        for(size_t i=0;i<n;i++){
            memcpy(v,data+i*16,16);if(std::abs(v[0]-want[0])>2e-3f||std::abs(v[1]-want[1])>2e-3f||std::abs(v[2]-want[2])>2e-3f||!(v[3]>0))continue;
            for(size_t j=0;j<n;j++){
                if(j==i)continue;memcpy(colour,data+j*16,16);
                if(std::abs(colour[0]-v[3])>1e-4f*std::max(1.f,v[3])||!(colour[1]>=0&&colour[1]<1000&&colour[2]>=0&&colour[2]<1000))continue;
                std::copy_n(colour,3,frame.sunColour);frame.sun=true;frame.watching=0;return;
            }
        }
        size_t end=std::min<size_t>(bytes,4096);
        for(size_t at=0;at+16<=end;at+=4){
            memcpy(v,data+at,16);float length=v[0]*v[0]+v[1]*v[1]+v[2]*v[2];if(!(std::abs(length-1)<4e-3f))continue;
            float error=std::max({std::abs(v[0]-want[0]),std::abs(v[1]-want[1]),std::abs(v[2]-want[2])});if(!(error<frame.sunNear[0]))continue;
            float nearest=0,gap=1e30f;for(size_t other=0;other+4<=end;other+=4){if(other==at+12)continue;float f;memcpy(&f,data+other,4);if(std::abs(f-v[3])<gap){gap=std::abs(f-v[3]);nearest=f;}}
            frame.sunNear[0]=error;frame.sunNear[1]=std::isfinite(v[3])?v[3]:-1;frame.sunNear[2]=std::isfinite(nearest)?nearest:-1;frame.sunNear[3]=float(at);
        }
    }
}

// Before Cemu begins a render pass (no pass is open): recognise where the frame is and record ours.
static void beforePass(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,VkFramebuffer fb){
    if(!presentation::enabled.load(std::memory_order_relaxed)){frame.watching=0;return;}
    const Kind& k=kindOf(fb);frame.watching=0;
    // A pass on anything but the cascade layer being filled ends that layer (a pass without attachments is none of ours to judge).
    if(frame.shadowLayer>=0&&!k.empty&&!(k.type==kShadow&&k.images[iDepth]==frame.shadowImage&&int(k.layer)==frame.shadowLayer)){
        if(auto why=castShadows(c,cb))frame.why[2]=why;
        frame.shadowLayer=-1;
    }
    switch(k.type){
        case kGBuffer:if(!frame.tried){frame.material=k.images[iMaterial];frame.albedo=k.images[iAlbedo];frame.normal=k.images[iNormal];frame.depth=k.images[iDepth];frame.extent=k.extent;if(k.images[iHdr]){frame.hdr=k.images[iHdr];frame.hdrDepth=k.images[iDepth];}}break;
        case kSceneColour:if(!frame.tried){frame.hdr=k.images[iHdr];frame.hdrDepth=k.images[iDepth];}break;
        case kLinearDepth:if(!frame.tried&&frame.material&&k.extent.width==frame.extent.width&&k.extent.height==frame.extent.height){frame.tried=true;if(auto why=injectOpaque(c,cb))frame.why[0]=why;}break;
        case kShadow:
            if(!frame.opaque||!(parts.load()&kCast))break;
            if(!frame.shadowImage){frame.shadowImage=k.images[iDepth];frame.shadowExtent=k.extent;}
            if(k.images[iDepth]!=frame.shadowImage||(frame.cast&(1u<<std::min(k.layer,31u))))break;
            if(frame.shadowLayer!=int(k.layer)){frame.shadowLayer=int(k.layer);frame.tailCount=0;}
            frame.watching=1;++frame.shadowPasses;break;
        case kLighting:if(frame.opaque&&!frame.mask)frame.mask=k.images[iNormal];if(frame.opaque&&frame.light&&!frame.sun)frame.watching=2;break;
        case kComposite:if(frame.opaque&&k.images[iHdr]==frame.hdr)frame.composite=true;break;
        case kSmallHdr:if(frame.opaque&&frame.composite&&!frame.triedBlend&&k.extent.width*2<frame.extent.width){frame.triedBlend=true;if(auto why=injectBlend(c,cb))frame.why[1]=why;}break;
        default:break;
    }
}
// At the frame's post-HDR marker: which parts the marker path still has to draw, and one line of what was done.
static int markerParts(uint32_t id,const Camera* camera){
    int left=kPartOpaque|kPartTranslucent;float viewError=-1,deviceError=-1;
    if(done.frame==id&&done.opaque){
        left&=~kPartOpaque;if(done.blended)left&=~kPartTranslucent;
        if(camera&&camera->id==id){viewError=deviceError=0;for(int i=0;i<12;i++)viewError=std::max(viewError,std::abs(camera->view[i]-done.camera.view[i]));for(int i=0;i<16;i++)deviceError=std::max(deviceError,std::abs(camera->device[i]-done.camera.device[i]));}
    }
    static const int64_t hz=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return f.QuadPart;}();
    if(presentation::enabled.load())event("{\"event\":\"integrate_frame\",\"frame\":%u,\"opaque\":%d,\"blended\":%d,\"cast\":%u,\"sun\":[%.6g,%.6g,%.6g],\"light\":[%.6g,%.6g,%.6g],\"vertices\":[%u,%u,%u],\"cpu_us\":%.1f,\"left\":%d,\"view_error\":%.6g,\"device_error\":%.6g,\"why\":[\"%s\",\"%s\",\"%s\"],\"shadow_passes\":%u,\"watched\":%u,\"unread\":%u,\"sun_near\":[%.6g,%.6g,%.6g,%.0f],\"sun_draws\":%u,\"sun_bytes\":%u}",
        id,int(done.frame==id&&done.opaque),int(done.frame==id&&done.blended),done.frame==id?done.cast:0,frame.sunColour[0],frame.sunColour[1],frame.sunColour[2],frame.lightView[8],frame.lightView[9],frame.lightView[10],
        frame.vertices[0],frame.vertices[1],frame.vertices[2],double(frame.ticks)*1e6/double(hz),left,viewError,deviceError,frame.why[0],frame.why[1],frame.why[2],frame.shadowPasses,frame.watched,frame.unread,frame.sunNear[0],frame.sunNear[1],frame.sunNear[2],frame.sunNear[3],frame.sunDraws,frame.sunBytes);
    lastMarker=id;haveMarker=true;frame={};
    return left;
}
static void presented(){frame={};}
}
