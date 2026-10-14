// Immutable GPU generations. Recording references and real completion fences retain ownership.
#pragma once
static uint64_t gpuResidentBytes=0;
// Textures and descriptor sets whose last GPU use has finished (their owner
// is released only when no recording and no unfinished submission holds it)
// are kept and reused instead of destroyed. Creating an image, two memory
// allocations and a descriptor pool for every frame cost the render thread
// several milliseconds a frame (route-38); contents stay immutable per owner.
static std::vector<Texture> texturePool;
struct PooledSet {VkDescriptorPool pool;VkDescriptorSet set;};
static std::vector<PooledSet> setPool;
static uint64_t gpuPoolHits=0,gpuPoolMisses=0,gpuPartialAtlas=0,gpuHudReused=0;
static void destroyTexture(const vkroots::VkDeviceDispatch& d,Texture& t){
    auto dev=d.Device;if(t.view)d.DestroyImageView(dev,t.view,nullptr);if(t.image)d.DestroyImage(dev,t.image,nullptr);if(t.memory)d.FreeMemory(dev,t.memory,nullptr);
    if(t.staging)d.DestroyBuffer(dev,t.staging,nullptr);if(t.stagingMemory)d.FreeMemory(dev,t.stagingMemory,nullptr);t={};
}
// Vertex buffers are pooled the same way: the entity, particle and outline
// meshes change with every Minecraft frame. Persistently mapped, sized in
// powers of two, written only before the commands that draw them are recorded.
struct Arena {VkBuffer b=VK_NULL_HANDLE;VkDeviceMemory m=VK_NULL_HANDLE;uint8_t* mapped=nullptr;size_t capacity=0;};
static std::vector<Arena> bufferPool;
static void destroyArena(const vkroots::VkDeviceDispatch& d,Arena& a){if(a.b)d.DestroyBuffer(d.Device,a.b,nullptr);if(a.m)d.FreeMemory(d.Device,a.m,nullptr);a={};}
struct GpuPart {const vkroots::VkDeviceDispatch* d=nullptr;Texture t{};Arena a{};uint64_t version=0,hash=0;uint32_t count=0,split=0;size_t bytes=0;
    size_t charge=0;PooledSet own{}; // an entity texture is bound through its own set
    presentation::Blob mesh; // batches and origin of an entity mesh
    ~GpuPart(){gpuResidentBytes-=charge;if(!d)return;
        if(t.image&&t.mapped&&texturePool.size()<24)texturePool.push_back(t);else destroyTexture(*d,t);
        if(a.b&&bufferPool.size()<64)bufferPool.push_back(a);else destroyArena(*d,a);
        if(own.pool){if(setPool.size()<48)setPool.push_back(own);else d->DestroyDescriptorPool(d->Device,own.pool,nullptr);}}
};
struct GpuScene {const vkroots::VkDeviceDispatch* d=nullptr;uint64_t session=0,clear=0,version=0,frame=0;std::shared_ptr<GpuPart> atlas,hud;std::map<presentation::Key,std::shared_ptr<GpuPart>> sections;VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;
    std::map<uint32_t,std::shared_ptr<GpuPart>> textures;std::shared_ptr<GpuPart> entities,avatar,dynamic;std::shared_ptr<const presentation::Dynamic> dynamicSource;
    ~GpuScene(){if(!pool)return;if(set&&setPool.size()<48)setPool.push_back({pool,set});else d->DestroyDescriptorPool(d->Device,pool,nullptr);}
};
static std::shared_ptr<GpuScene> gpuLive;
// The scene of the frame being recorded: every place that draws it in one frame (G-buffer,
// cascades, HDR, marker) uses this one. Dropped at the present.
static std::shared_ptr<GpuScene> gpuFrame;
static std::unordered_map<VkCommandBuffer,std::vector<std::shared_ptr<GpuScene>>> gpuRecorded;
struct GpuFlight {VkFence fence;uint64_t serial;std::vector<std::shared_ptr<GpuScene>> refs;};
static std::vector<GpuFlight> gpuFlights;
static uint64_t gpuUploads=0,gpuUploadedBytes=0;
static void pollGpu(const vkroots::VkDeviceDispatch& d){
    for(auto i=gpuFlights.begin();i!=gpuFlights.end();)if(i->fence&&d.GetFenceStatus(d.Device,i->fence)==VK_SUCCESS){
        event("{\"event\":\"live_gpu_complete\",\"submission\":%llu,\"fence\":%llu}",i->serial,(uint64_t)i->fence);d.DestroyFence(d.Device,i->fence,nullptr);i=gpuFlights.erase(i);
    }else ++i;
}
static void clearGpu(const vkroots::VkDeviceDispatch& d){
    for(auto& f:gpuFlights)d.DestroyFence(d.Device,f.fence,nullptr);gpuFlights.clear();gpuRecorded.clear();gpuLive.reset();gpuFrame.reset();
    for(auto& t:texturePool)destroyTexture(d,t);texturePool.clear();
    for(auto& a:bufferPool)destroyArena(d,a);bufferPool.clear();
    for(auto& s:setPool)d.DestroyDescriptorPool(d.Device,s.pool,nullptr);setPool.clear();
}
static void finishGpuSubmit(const vkroots::VkQueueDispatch& q,VkQueue queue,uint32_t count,const VkSubmitInfo* info,uint64_t serial){
    std::vector<std::shared_ptr<GpuScene>> refs;for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<info[i].commandBufferCount;j++){
        // The flight keeps them from here on: left with the command buffer they stayed alive until
        // Cemu began it again, a whole turn of its pool later (the play session after route-80
        // ran out of the payload budget on 1080p HUD pictures).
        auto found=gpuRecorded.find(info[i].pCommandBuffers[j]);if(found!=gpuRecorded.end()){refs.insert(refs.end(),found->second.begin(),found->second.end());found->second.clear();}
    }
    if(refs.empty())return;
    const auto& d=*q.pDeviceDispatch;VkFence fence=VK_NULL_HANDLE;VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if(d.CreateFence(d.Device,&ci,nullptr,&fence)!=VK_SUCCESS){resourcesFailed=true;gpuFlights.push_back({VK_NULL_HANDLE,serial,std::move(refs)});return;}
    VkSubmitInfo end{VK_STRUCTURE_TYPE_SUBMIT_INFO};auto result=q.QueueSubmit(queue,1,&end,fence);
    gpuFlights.push_back({fence,serial,std::move(refs)});if(result!=VK_SUCCESS)resourcesFailed=true;
    event("{\"event\":\"live_gpu_fence\",\"submission\":%llu,\"fence\":%llu,\"result\":%d}",serial,(uint64_t)fence,result);
}
// A w x h RGBA texture with a persistently mapped staging buffer: from the
// pool when one of that size is free, otherwise created.
static Texture acquireTexture(const vkroots::VkDeviceDispatch& d,uint32_t w,uint32_t h){
    auto pooled=std::find_if(texturePool.begin(),texturePool.end(),[&](const Texture& t){return t.w==w&&t.h==h;});
    if(pooled!=texturePool.end()){Texture t=*pooled;texturePool.erase(pooled);++gpuPoolHits;return t;}
    // A different size is wanted (new atlas): the oldest spare makes room.
    if(texturePool.size()>=24){destroyTexture(d,texturePool.front());texturePool.erase(texturePool.begin());}
    ++gpuPoolMisses;Texture t;t.w=w;t.h=h;size_t size=size_t(w)*h*4;
    try{
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=size;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateBuffer(d.Device,&bi,nullptr,&t.staging),"staging buffer");
        VkMemoryRequirements req;d.GetBufferMemoryRequirements(d.Device,t.staging,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
        ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(d.AllocateMemory(d.Device,&ai,nullptr,&t.stagingMemory),"staging memory");
        check(d.BindBufferMemory(d.Device,t.staging,t.stagingMemory,0),"staging bind");void* ptr=nullptr;check(d.MapMemory(d.Device,t.stagingMemory,0,size,0,&ptr),"staging map");t.mapped=static_cast<uint8_t*>(ptr);
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;ci.extent={w,h,1};ci.mipLevels=1;ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateImage(d.Device,&ci,nullptr,&t.image),"texture image");
        d.GetImageMemoryRequirements(d.Device,t.image,&req);ai.allocationSize=req.size;ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(d.AllocateMemory(d.Device,&ai,nullptr,&t.memory),"texture memory");check(d.BindImageMemory(d.Device,t.image,t.memory,0),"texture bind");
        t.view=imageView(d,t.image,ci.format,VK_IMAGE_ASPECT_COLOR_BIT);
    }catch(...){destroyTexture(d,t);throw;}
    return t;
}
// Whole image from `pixels` (w*h*4 bytes).
static void fillTexture(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,Texture& t,const uint8_t* pixels){
    memcpy(t.mapped,pixels,size_t(t.w)*t.h*4);
    barrier(c,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={t.w,t.h,1};c.CmdCopyBufferToImage(cb,t.staging,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier(c,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
}
// The previous atlas copied on the GPU, then only the changed rectangles from
// `pixels`. False (nothing recorded) when the rectangles do not apply.
static bool patchTexture(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,Texture& t,const Texture& prior,const uint8_t* pixels,const std::vector<presentation::Rect>& dirty){
    if(prior.w!=t.w||prior.h!=t.h||dirty.empty())return false;
    size_t total=0;for(const auto& r:dirty){if(!r.w||!r.h||r.x>t.w||r.y>t.h||r.w>t.w-r.x||r.h>t.h-r.y)return false;total+=size_t(r.w)*r.h*4;}
    if(total>size_t(t.w)*t.h)return false; // more than a quarter of the atlas: a whole upload is simpler
    std::vector<VkBufferImageCopy> copies;size_t offset=0;
    for(const auto& r:dirty){
        for(uint32_t y=0;y<r.h;y++)memcpy(t.mapped+offset+size_t(y)*r.w*4,pixels+4*(size_t(r.y+y)*t.w+r.x),size_t(r.w)*4);
        VkBufferImageCopy copy{};copy.bufferOffset=offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageOffset={int32_t(r.x),int32_t(r.y),0};copy.imageExtent={r.w,r.h,1};copies.push_back(copy);
        offset+=size_t(r.w)*r.h*4;
    }
    barrier(c,cb,prior.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    barrier(c,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy whole{};whole.srcSubresource=whole.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};whole.extent={t.w,t.h,1};
    c.CmdCopyImage(cb,prior.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&whole);
    barrier(c,cb,prior.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_SHADER_READ_BIT);
    c.CmdCopyBufferToImage(cb,t.staging,t.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,uint32_t(copies.size()),copies.data());
    barrier(c,cb,t.image,VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    gpuUploadedBytes+=total;return true;
}
// `data`: width, height (u32 each), then RGBA rows. `prior`/`blob` allow the
// atlas to be patched from its predecessor.
static std::shared_ptr<GpuPart> uploadImage(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb,const std::vector<uint8_t>& data,uint64_t version,uint64_t hash,const GpuPart* prior=nullptr,const presentation::Bytes* blob=nullptr){
    uint32_t w=0,h=0;if(data.size()<8)throw std::runtime_error("texture size");memcpy(&w,data.data(),4);memcpy(&h,data.data()+4,4);
    if(!w||!h||data.size()!=8+size_t(w)*h*4)throw std::runtime_error("texture size");
    auto part=std::make_shared<GpuPart>();part->d=c.pDeviceDispatch;part->version=version;part->hash=hash;part->bytes=data.size();
    auto charge=part->bytes*2;if(gpuResidentBytes+charge>(512u<<20))throw std::runtime_error("live GPU payload budget (512 MiB incl staging)");part->charge=charge;gpuResidentBytes+=charge;
    part->t=acquireTexture(*part->d,w,h);++gpuUploads;
    if(prior&&blob&&blob->base&&blob->base==prior->version&&patchTexture(c,cb,part->t,prior->t,data.data()+8,blob->dirty)){++gpuPartialAtlas;return part;}
    fillTexture(c,cb,part->t,data.data()+8);gpuUploadedBytes+=part->bytes;return part;
}
static Arena acquireBuffer(const vkroots::VkDeviceDispatch& d,size_t size){
    size_t capacity=64u<<10;while(capacity<size)capacity<<=1;
    auto pooled=std::find_if(bufferPool.begin(),bufferPool.end(),[&](const Arena& a){return a.capacity==capacity;});
    if(pooled!=bufferPool.end()){Arena a=*pooled;bufferPool.erase(pooled);++gpuPoolHits;return a;}
    ++gpuPoolMisses;Arena a;a.capacity=capacity;
    try{
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=capacity;ci.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;check(d.CreateBuffer(d.Device,&ci,nullptr,&a.b),"vertex buffer");
        VkMemoryRequirements req;d.GetBufferMemoryRequirements(d.Device,a.b,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
        ai.memoryTypeIndex=memoryType(d,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);check(d.AllocateMemory(d.Device,&ai,nullptr,&a.m),"vertex memory");
        check(d.BindBufferMemory(d.Device,a.b,a.m,0),"vertex bind");void* ptr=nullptr;check(d.MapMemory(d.Device,a.m,0,capacity,0,&ptr),"vertex map");a.mapped=static_cast<uint8_t*>(ptr);
    }catch(...){destroyArena(d,a);throw;}
    return a;
}
// RenVertex rows into a pooled vertex buffer.
static std::shared_ptr<GpuPart> uploadRows(const vkroots::VkDeviceDispatch& d,const std::vector<uint8_t>& rows,uint64_t version){
    auto part=std::make_shared<GpuPart>();part->d=&d;part->version=version;part->bytes=rows.size();
    auto charge=part->bytes;if(gpuResidentBytes+charge>(512u<<20))throw std::runtime_error("live GPU payload budget (512 MiB incl staging)");part->charge=charge;gpuResidentBytes+=charge;
    part->count=uint32_t(rows.size()/32);part->a=acquireBuffer(d,std::max<size_t>(rows.size(),32));if(!rows.empty())memcpy(part->a.mapped,rows.data(),rows.size());
    ++gpuUploads;gpuUploadedBytes+=part->bytes;return part;
}
static PooledSet acquireSet(const vkroots::VkDeviceDispatch& d){
    if(!setPool.empty()){auto s=setPool.back();setPool.pop_back();return s;}
    PooledSet s{};VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2};VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pi.maxSets=1;pi.poolSizeCount=1;pi.pPoolSizes=&ps;check(d.CreateDescriptorPool(d.Device,&pi,nullptr,&s.pool),"live descriptor pool");
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};ai.descriptorPool=s.pool;ai.descriptorSetCount=1;ai.pSetLayouts=&descriptorLayout;
    if(d.AllocateDescriptorSets(d.Device,&ai,&s.set)!=VK_SUCCESS){d.DestroyDescriptorPool(d.Device,s.pool,nullptr);throw std::runtime_error("live descriptor set");}
    return s;
}
static void writeSet(const vkroots::VkDeviceDispatch& d,VkDescriptorSet set,VkImageView first,VkImageView second){
    VkDescriptorImageInfo ims[2]={{sampler,first,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{sampler,second,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet ws[2]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};for(int i=0;i<2;i++){ws[i].dstSet=set;ws[i].dstBinding=i;ws[i].descriptorCount=1;ws[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;ws[i].pImageInfo=&ims[i];}d.UpdateDescriptorSets(d.Device,2,ws,0,nullptr);
}
static std::shared_ptr<GpuScene> updateLiveGpu(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    const auto& d=*c.pDeviceDispatch;pollGpu(d);auto state=presentation::scene.load();auto hud=presentation::hud.load();if(!state)return nullptr;
    if(gpuLive&&gpuLive->session==state->session&&gpuLive->clear==state->clear&&gpuLive->version==state->version){
        if(gpuLive->frame==(hud?hud->frame:0))return gpuLive;
        // A new HUD frame with the pixels already on the GPU changes nothing.
        if(hud&&hud->session==state->session&&gpuLive->hud&&gpuLive->hud->hash==hud->hash&&gpuLive->hud->bytes==hud->texture.size()){gpuLive->frame=hud->frame;++gpuHudReused;return gpuLive;}
    }
    // At most 128 fenced submissions in the pinned queue. Preserve old generations; fault rather than unsafe reuse.
    if(gpuFlights.size()>128){resourcesFailed=true;event("{\"event\":\"live_gpu_overload\",\"flights\":%zu}",gpuFlights.size());return nullptr;}
    auto next=std::make_shared<GpuScene>();next->d=&d;next->session=state->session;next->clear=state->clear;next->version=state->version;
    // Own partial uploads before recording their transfer commands. A later
    // allocation failure must not free resources already referenced by this CB.
    gpuRecorded[cb].push_back(next);
    bool reuse=gpuLive&&gpuLive->session==state->session&&gpuLive->clear==state->clear;
    if(state->atlas){if(reuse&&gpuLive->atlas&&gpuLive->atlas->version==state->atlas->version)next->atlas=gpuLive->atlas;else {
        auto partial=gpuPartialAtlas;next->atlas=uploadImage(c,cb,state->atlas->data,state->atlas->version,state->atlas->hash,reuse&&gpuLive->atlas?gpuLive->atlas.get():nullptr,state->atlas.get());
        event("{\"event\":\"live_atlas_upload\",\"session\":%llu,\"version\":%llu,\"hash\":%llu,\"partial\":%d,\"rects\":%zu}",state->session,state->atlas->version,state->atlas->hash,int(gpuPartialAtlas!=partial),state->atlas->dirty.size());}}
    for(auto& [key,blob]:state->sections){auto prior=reuse?gpuLive->sections.find(key):std::map<presentation::Key,std::shared_ptr<GpuPart>>::iterator{};
        if(reuse&&prior!=gpuLive->sections.end()&&prior->second->version==blob->version)next->sections[key]=prior->second;else{
            next->sections[key]=uploadRows(d,blob->data,blob->version);next->sections[key]->split=blob->split;auto [x,y,z]=key;
            event("{\"event\":\"live_section_upload\",\"session\":%llu,\"clear\":%llu,\"version\":%llu,\"section\":[%d,%d,%d],\"vertices\":%u,\"hash\":%llu}",state->session,state->clear,blob->version,x,y,z,next->sections[key]->count,blob->hash);
        }
    }
    if(hud&&hud->session==state->session){next->frame=hud->frame;
        if(reuse&&gpuLive->hud&&(gpuLive->frame==hud->frame||(gpuLive->hud->hash==hud->hash&&gpuLive->hud->bytes==hud->texture.size())))next->hud=gpuLive->hud;else{
        next->hud=uploadImage(c,cb,hud->texture,hud->frame,hud->hash);
        event("{\"event\":\"live_hud_upload\",\"session\":%llu,\"frame\":%llu,\"hash\":%llu,\"bytes\":%zu}",state->session,hud->frame,hud->hash,hud->texture.size());
    }}
    // Entity textures (skins, mobs, particle sheets), each with the set that binds it.
    for(auto& [id,blob]:state->textures){
        std::shared_ptr<GpuPart> prior;if(reuse){auto found=gpuLive->textures.find(id);if(found!=gpuLive->textures.end())prior=found->second;}
        if(prior&&prior->version==blob->version)next->textures[id]=prior;else{
            auto part=uploadImage(c,cb,blob->data,blob->version,blob->hash);next->textures[id]=part;part->own=acquireSet(d);writeSet(d,part->own.set,part->t.view,overlayTexture.view);
            event("{\"event\":\"live_texture_upload\",\"session\":%llu,\"id\":%u,\"width\":%u,\"height\":%u,\"hash\":%llu}",state->session,id,part->t.w,part->t.h,blob->hash);
        }
    }
    // Entities and particles, the player's own model (third person), and the world-entity geometry.
    auto mesh=[&](const presentation::Blob& blob,const std::shared_ptr<GpuPart>& prior){
        if(!blob||blob->data.empty())return std::shared_ptr<GpuPart>();
        if(reuse&&prior&&prior->version==blob->version)return prior;
        auto part=uploadRows(d,blob->data,blob->version);part->mesh=blob;return part;
    };
    next->entities=mesh(state->entities,gpuLive?gpuLive->entities:nullptr);next->avatar=mesh(state->avatar,gpuLive?gpuLive->avatar:nullptr);
    if(state->dynamic&&!state->dynamic->data.empty()){
        if(reuse&&gpuLive->dynamic&&gpuLive->dynamic->version==state->dynamic->version)next->dynamic=gpuLive->dynamic;else next->dynamic=uploadRows(d,state->dynamic->data,state->dynamic->version);
        next->dynamicSource=state->dynamic;
    }
    {auto s=acquireSet(d);next->pool=s.pool;next->set=s.set;}
    writeSet(d,next->set,next->atlas?next->atlas->t.view:atlasTexture.view,next->hud?next->hud->t.view:overlayTexture.view);
    if(reuse)for(auto& [key,part]:gpuLive->sections)if(!next->sections.contains(key)){auto [x,y,z]=key;event("{\"event\":\"live_section_delete\",\"section\":[%d,%d,%d],\"version\":%llu}",x,y,z,state->version);}
    gpuLive=next;event("{\"event\":\"live_gpu_generation\",\"session\":%llu,\"clear\":%llu,\"version\":%llu,\"sections\":%zu,\"hud_frame\":%llu,\"uploads\":%llu,\"upload_bytes\":%llu,\"flights\":%zu,\"pool_hits\":%llu,\"pool_misses\":%llu,\"partial_atlas\":%llu,\"hud_reused\":%llu,\"resident_bytes\":%llu}",next->session,next->clear,next->version,next->sections.size(),next->frame,gpuUploads,gpuUploadedBytes,gpuFlights.size(),gpuPoolHits,gpuPoolMisses,gpuPartialAtlas,gpuHudReused,gpuResidentBytes);return next;
}
// One scene per presented frame: asked for at up to six places in a frame, Minecraft's newer
// publications in between would each become a generation of their own (measured: 132 in 120 frames).
static std::shared_ptr<GpuScene> frameGpu(const vkroots::VkCommandBufferDispatch& c,VkCommandBuffer cb){
    if(!gpuFrame)gpuFrame=updateLiveGpu(c,cb);
    return gpuFrame;
}
