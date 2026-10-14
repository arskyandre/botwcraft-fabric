// BetterVR 2e06b31 source leads; loaded USv208 words independently checked.
// Only the calculated render opacity is overridden. Actor/model/material memory
// is never changed here, so native opacity writes remain current on release.
namespace playerRender {
static uint32_t linkActor(uint32_t address){
    if(!body::readable(address,12))return 0;
    auto data=body::u32(address),id=body::u32(address+4);
    if(!data||!id||!body::readable(data+0x3C,12)||body::u32(data+0x3C)!=id)return 0;
    auto actor=body::u32(data+0x40);
    return body::readable(actor+0x50,4)&&body::u32(actor+0x50)==id?actor:0;
}
static int resolveOwner(uint32_t actor);
// Asked about fifty times a frame, almost always for actors that are not the
// player's; each answer costs up to twenty bounded guest reads. Attachments
// change rarely, so an answer is kept for 100 ms (per calling thread).
static int owner(uint32_t actor){
    struct Slot {uint32_t actor=0,player=0;int kind=-1;uint64_t at=0;};
    static thread_local Slot slots[64];auto now=GetTickCount64();auto& slot=slots[(actor>>4)&63];auto player=body::player.load();
    if(slot.actor==actor&&slot.player==player&&now-slot.at<100)return slot.kind;
    slot={actor,player,resolveOwner(actor),now};return slot.kind;
}
static int resolveOwner(uint32_t actor){
    auto player=body::player.load();
    if(!body::readable(actor,0x438)||!body::readable(player+0x50,4))return -1;
    if(actor==player)return 0;
    for(unsigned index=0;index<6;index++)if(linkActor(player+0x1D9C+index*12)==actor)return int(index)+1;
    // Resolve current active parent, not a requested/previous attachment.
    if(memcmp(guest+actor+0x10,"Weapon_",7)==0&&linkActor(actor+0x610)==player)return 7;
    return -1;
}
static void inventory(uint32_t actor,int kind){
    auto model=body::u32(actor+0x330);if(!body::readable(model,0x28))return;
    auto count=body::u32(model+0x1C),array=body::u32(model+0x24);
    if(count>32||!count||!body::readable(array,count*4))return;
    char name[65]{};memcpy(name,guest+actor+0x10,64);
    event("{\"event\":\"player_model\",\"actor\":%u,\"proc_id\":%u,\"kind\":%d,\"name\":\"%s\",\"model\":%u,\"units\":%u,\"opacity\":%.9g,\"related\":%.9g,\"flush\":%u,\"body_epoch\":%llu}",actor,body::u32(actor+0x50),kind,name,model,count,body::f32(actor+0x33C),body::f32(actor+0x340),guest[actor+0x436],body::lifecycleEpoch.load());
    for(unsigned index=0;index<count;index++){
        auto holder=body::u32(array+index*4);if(!body::readable(holder,4))continue;
        auto unit=body::u32(holder);if(!body::readable(unit,0xA8))continue;
        auto table=body::u32(unit+0xA4);unsigned materials=(guest[unit+0x9A]<<8)|guest[unit+0x9B];
        event("{\"event\":\"player_unit\",\"actor\":%u,\"model\":%u,\"unit\":%u,\"index\":%u,\"vtable\":%u,\"materials\":%u,\"table\":%u}",actor,model,unit,index,body::u32(unit+0x74),materials,table);
        if(materials>32||!materials||!body::readable(table,materials*0x40))continue;
        for(unsigned m=0;m<materials;m++){
            auto material=body::u32(table+m*0x40);if(!body::readable(material,8))continue;
            int32_t relative=int32_t(body::u32(material+4));if(relative<=0||relative>0x04000000)continue;
            uint32_t address=material+4+relative;
            // Names need not be aligned; validate an enclosing bounded range.
            if(!body::readable(address&~3u,68))continue;
            char text[64]{};memcpy(text,guest+address,63);
            event("{\"event\":\"player_material\",\"actor\":%u,\"unit\":%u,\"index\":%u,\"name\":\"%s\"}",actor,unit,m,text);
        }
    }
}
static void opacity(CPU* c,uint32_t actor){
    CpuScope timing(opacityCpu);frameMarks::tally(frameMarks::pOpacity);
    c->ip=c->spr.lr;guest=static_cast<uint8_t*>(getGuest());
    int kind=owner(actor);
    if(kind<0){
        // Read-only witnesses for native actors. The calculated result and
        // every native render-state byte pass through unchanged.
        if(fullEvidence.load()&&body::readable(actor,0x438)){
            auto name=reinterpret_cast<const char*>(guest+actor+0x10);
            bool witness=!memcmp(name,"Npc_",4)||!memcmp(name,"Enemy_",6)||!memcmp(name,"Item_",5)||!memcmp(name,"RemoteBomb",10);
            static thread_local uint64_t last=0;auto now=GetTickCount64();
            if(witness&&now-last>=100){last=now;char copy[65]{};memcpy(copy,name,64);
                event("{\"event\":\"other_opacity\",\"actor\":%u,\"proc_id\":%u,\"name\":\"%s\",\"native\":%.9g,\"applied\":%.9g,\"body_epoch\":%llu}",actor,body::u32(actor+0x50),copy,c->fpr[2],c->fpr[2],body::lifecycleEpoch.load());
            }
        }
        return;
    }
    ViewProbe target;bool hide=cameraProbe.load()&&(viewTarget(target)||scenePresent.load());
    if(hide&&kind==6&&scenePresent.load()){int slate=sceneSlate.load();if(slate==1||(slate==2&&sceneSlateHeld.load()))hide=false;}
    double native=c->fpr[2];if(hide)c->fpr[2]=0;
    // A bounded cache per owner identity; kind 7 includes multiple weapons.
    struct Seen {uint32_t actor=0,id=0;uint64_t inventoryAt=0,logAt=0;};
    static thread_local Seen seen[16];auto now=GetTickCount64();
    auto id=body::u32(actor+0x50);Seen* slot=nullptr;
    for(auto& item:seen)if(item.actor==actor&&item.id==id){slot=&item;break;}
    if(!slot){slot=&seen[0];for(auto& item:seen)if(item.inventoryAt<slot->inventoryAt)slot=&item;*slot={actor,id,0,0};}
    if(now-slot->inventoryAt>=1000){slot->inventoryAt=now;inventory(actor,kind);}
    if(fullEvidence.load()&&now-slot->logAt>=100){slot->logAt=now;event("{\"event\":\"player_opacity\",\"actor\":%u,\"proc_id\":%u,\"kind\":%d,\"native\":%.9g,\"applied\":%.9g,\"hide\":%d,\"lease\":%llu,\"body_epoch\":%llu,\"target_epoch\":%llu,\"lr\":%u}",actor,body::u32(actor+0x50),kind,native,c->fpr[2],int(hide),hide?target.id:0,body::lifecycleEpoch.load(),targetEpoch.load(),c->spr.lr);}
}
static void actorOpacity(CPU* c){opacity(c,c->gpr[3]);}
static void weaponOpacity(CPU* c){
    // Hook before the original epilogue restores r31. Emulate displaced lwz
    // r0,0x14(r1); its saved caller LR is restored by the native epilogue.
    guest=static_cast<uint8_t*>(getGuest());
    if(!body::readable(c->gpr[1]+0x14,4,0x10000)){c->ip=c->spr.lr;return;}
    c->gpr[0]=body::u32(c->gpr[1]+0x14);opacity(c,c->gpr[31]);
}
}
