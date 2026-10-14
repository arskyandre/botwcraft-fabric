// Lab: one of the game's own objects asked for at a place with a scale. For step 5 of report 48
// (Minecraft's blocks as ground and walls for BOTW's own things): an invisible scalable box such as
// the game's AirWall where Minecraft has blocks.
//
// The request is the game's ordinary one, ActorCreator::requestCreateActor (037B5E8C, 133 callers in
// the game; r3 creator, r4 name, r5 heap, r6 handle, r7 parameter pack, r8 map object, r9 lane), with
// the creator and the heap read where those callers read them (*0x1047C2B8, *(*0x10463F6C + 0x10)).
// The parameter pack is built as the game's own add (031F9870) builds it: the owner proc, a count
// and a write position, then for each entry the key's string pointer, a type byte and the value;
// "@P" is the place and "@S" the scale, both vectors (type 4). A value for the object's AI (what a
// map entry gives under !Parameters, such as an AirWall's Shape) follows the game's addAITreeParam:
// the entry "=AT" (a true, type 3) and then the value under its own name (a string, type 5, inline).
// It is asked in the player's damage-manager update, where the water questions are asked (water.h).
//
// lab/spawn.txt, one request a line in BOTW's coordinates: `name x y z [sx sy sz] [key=text ...]`;
// the file is renamed to spawn.done.txt when read. What was asked and the game's answer go to the
// control log.
//
// A line that begins with `map` is asked for as the map asks: ActorCreator::requestCreateMapActor
// (037B6040; r6 the entry's iterator, r7 its record), with a record copied from the first AirWall the
// map itself created (actors::airWallRecord) and a map entry of the bridge's own in the game's form
// (BYML version 2, big-endian): !Parameters with the line's key=text pairs, HashId, UnitConfigName
// and, unless the line says pack=1, Scale and Translate. With pack=1 the place and the scale go in a
// parameter pack instead (r10). Each such object keeps its record and entry for the session.
// Route-141: asked for so, with Shape: Box, an AirWall is a box of exactly the scale's half extents
// at the place (the harvest's candidate: shape type 5), from the entry or from the pack alike.
//
// `weapon name [life]` asks for a weapon in the player's hands, in place of the one he carries: the
// game's CreatePlayerEquipActorMgr::requestCreateWeapon (02A15998, named so by the game's own string;
// r3 the manager, *0x10463708; r4 the slot, 0 for a sword, an axe, a spear; r5 the name as a string
// object; r6 its life; r7 its modifier, here none: eight zero bytes; r8 not used). The inventory is
// not asked. Report 57.
//
// Route-151: the weapon so asked for is created (the creation is in lab/creates.jsonl) and nobody takes
// it; in the game the player's own code asks the manager for it afterwards. Route-152: every actor
// takes a weapon into a slot through 020080F0 (actor, slot, weapon, 0, 0), the player too (slot 0
// his sword, slot 2 his bow).
// `hold name [slot]` hands the running object of that name nearest the player (within 20 units; asked for
// with a line of its own before) to the player by that function (slot 0 unless given). Tried for five seconds.
//
// `delete n` asks the game to remove the n-th object asked for as the map asks (from 0):
// BaseProc::deleteLater (0378A374; the object from its record's +0x24, where the game writes it once
// the object exists; reason 0). `slots` writes each record's object and its state to the control log.
//
// Route-137: an AirWall asked for with a scale of 40, 30, 2 and no Shape came as a sphere of radius
// 40 (the collision harvest's candidate: shape type 0, bounds 80 x 80 x 80), where the map's own
// AirWall of scale 5000, 900, 25 is a box of exactly those half extents (type 5). Route-139: the
// same with Shape=Box as an AI value in the pack; a sphere of radius 6 for a scale of 6, 4, 1 either way.
#pragma once
namespace spawn {
constexpr uint32_t kCreator=0x1047C2B8,kHeapOwner=0x10463F6C,kEquipCreator=0x10463708,kStringTable=0x10263910;
constexpr uint32_t kNameSlots=16,kNameBytes=32,kEachUpdate=4,kEntrySlots=8,kEntryBytes=0x200,kDeleteLater=0x0378A374,kWalls=256,kWallBytes=0x160;
struct Request{char name[kNameBytes];float at[3];float scale[3];bool scaled;char key[2][kNameBytes];char text[2][kNameBytes];int extras;bool map,pack;int remove;int weaponLife;bool hold;uint64_t since;int slot;uint32_t gone,goneId;int wall;uint32_t screens;};
static std::mutex mutex;static std::deque<Request> queue;static std::atomic<uint32_t> waiting{0};
static Request current;static uint32_t thisUpdate=0,asked=0,accepted=0,entriesUsed=0;static std::atomic<uint32_t> entryRoomAt{0},entriesListed{0};static std::vector<std::string> names;
// The weapon last handed to the player by `hold` (swing.h asks and reads this).
static std::atomic<uint32_t> handed{0};static uint32_t holding=0;
// walls.h: records and entries for kWalls objects asked for as the map asks (`wall` in a request: the slot and one),
// kWallBytes each (the record's 0x68, and from 0x70 an entry of 228 bytes: AirWall, Shape Box, a scale and a
// place), in the pack's own memory (r6 of Bridge_SpawnNext). Route-162: coreinit's OSAllocFromSystem, asked for
// 128 KB from the pack, answered 0.
static std::atomic<uint32_t> wallRoom{0};static uint32_t wallSerial=0;
static void ask(const Request& r){std::lock_guard lock(mutex);if(queue.size()<4096){queue.push_back(r);queue.back().screens=body::stageScreens.load();++waiting;}}
// The player's manager and something waiting: this update asks (r12 = 3).
static bool due(CPU* c){
    if(!waiting.load(std::memory_order_relaxed))return false;
    uint32_t actor=0,player=body::identityActor.load();
    if(!player||!actors::word(c->gpr[3]+8,actor)||actor!=player)return false;
    thisUpdate=0;return true;
}
static void put(uint8_t* at,uint32_t v){v=__builtin_bswap32(v);memcpy(at,&v,4);}
static void put(uint8_t* at,float f){uint32_t v;memcpy(&v,&f,4);put(at,v);}
// One object's map entry in the game's form, written to `d`: key table, string table, the value nodes and
// last the entry's own dictionary, whose offset comes back in rootAt. 0: no room.
static size_t entryDocument(uint8_t* d,size_t room,const Request& r,uint32_t hashId,bool placed,uint32_t& rootAt){
    std::vector<std::string> keys{"!Parameters","HashId","UnitConfigName"},strings{r.name};
    if(placed){keys.push_back("Scale");keys.push_back("Translate");}
    for(int i=0;i<r.extras;i++){keys.push_back(r.key[i]);strings.push_back(r.text[i]);}
    for(auto* v:{&keys,&strings}){std::sort(v->begin(),v->end());v->erase(std::unique(v->begin(),v->end()),v->end());}
    auto indexOf=[](const std::vector<std::string>& v,const char* s){return uint32_t(std::find(v.begin(),v.end(),s)-v.begin());};
    std::vector<uint8_t> out(16,0);
    auto w32=[&](uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(uint8_t(v>>s));};
    auto table=[&](const std::vector<std::string>& v){
        uint32_t start=uint32_t(out.size());w32(0xC2000000u|uint32_t(v.size()));uint32_t offset=4+4*uint32_t(v.size()+1);
        for(auto& s:v){w32(offset);offset+=uint32_t(s.size())+1;}w32(offset);
        for(auto& s:v){out.insert(out.end(),s.begin(),s.end());out.push_back(0);}
        while(out.size()%4)out.push_back(0);return start;};
    uint32_t keyAt=table(keys),stringAt=table(strings);
    auto floats=[&](const float* v){uint32_t start=uint32_t(out.size());w32(0xC0000003u);w32(0xD2D2D200u);for(int i=0;i<3;i++){uint32_t u;memcpy(&u,&v[i],4);w32(u);}return start;};
    uint32_t scaleAt=placed?floats(r.scale):0,placeAt=placed?floats(r.at):0,paramsAt=uint32_t(out.size());
    struct Entry{uint32_t key,type,value;};auto byKey=[](const Entry& a,const Entry& b){return a.key<b.key;};
    std::vector<Entry> params;for(int i=0;i<r.extras;i++)params.push_back({indexOf(keys,r.key[i]),0xA0,indexOf(strings,r.text[i])});std::sort(params.begin(),params.end(),byKey);
    w32(0xC1000000u|uint32_t(params.size()));for(auto& e:params){w32((e.key<<8)|e.type);w32(e.value);}
    rootAt=uint32_t(out.size());
    std::vector<Entry> entries{{indexOf(keys,"!Parameters"),0xC1,paramsAt},{indexOf(keys,"HashId"),0xD3,hashId},{indexOf(keys,"UnitConfigName"),0xA0,indexOf(strings,r.name)}};
    if(placed){entries.push_back({indexOf(keys,"Scale"),0xC0,scaleAt});entries.push_back({indexOf(keys,"Translate"),0xC0,placeAt});}
    std::sort(entries.begin(),entries.end(),byKey);
    w32(0xC1000000u|uint32_t(entries.size()));for(auto& e:entries){w32((e.key<<8)|e.type);w32(e.value);}
    out[0]='B';out[1]='Y';out[2]=0;out[3]=2;
    auto header=[&](size_t at,uint32_t v){for(int i=0;i<4;i++)out[at+i]=uint8_t(v>>(24-8*i));};header(4,keyAt);header(8,stringAt);header(12,rootAt);
    if(out.size()>room)return 0;
    memcpy(d,out.data(),out.size());return out.size();
}
// Bridge_SpawnNext: r3 room for the strings a request names (kNameSlots of kNameBytes, kept for the
// session: the game keeps the pointers), r4 room for a pack (0xD0 bytes of the hook's stack), r5 room
// for the records and entries of objects asked for as the map asks (kEntrySlots of kEntryBytes).
// Leaves the request's registers and r12 = 1 (the ordinary request), 2 (the map's) or 3 (one to go:
// r3 the object, r4 the reason), or r12 = 0 for none.
static void next(CPU* c){
    c->ip=c->spr.lr;c->gpr[12]=0;auto base=static_cast<uint8_t*>(getGuest());
    uint32_t room=c->gpr[3],pack=c->gpr[4],entryRoom=c->gpr[5];entryRoomAt=entryRoom;
    if(!base||pack<0x01000000||pack>=0xA0000000-0x100||thisUpdate>=kEachUpdate)return;
    if(c->gpr[6]>=0x01000000&&c->gpr[6]<0xA0000000-kWalls*kWallBytes)wallRoom=c->gpr[6];
    {std::lock_guard lock(mutex);if(queue.empty())return;current=queue.front();queue.pop_front();--waiting;}
    // Asked before a screen behind which the map can change: the object to go, or the map the request was made
    // for, may be no more (the game takes every object of a map away itself).
    if((current.gone||current.map||current.remove>=0)&&current.screens!=body::stageScreens.load()){event("{\"event\":\"actor_spawn\",\"state\":\"dropped-with-the-map\",\"name\":\"%s\",\"proc\":%u,\"wall\":%d}",current.name,current.gone,current.wall);return;}
    if(current.gone){   // one object by its address, if it is still that object (its id) and not already going
        uint32_t id=0,state=0;bool known=actors::word(current.gone+0x50,id)&&id==current.goneId&&actors::word(current.gone+0x54,state)&&(state>>24)!=3;
        if(!known){event("{\"event\":\"actor_spawn\",\"state\":\"nothing-to-delete\",\"proc\":%u,\"id\":%u}",current.gone,current.goneId);return;}
        c->gpr[3]=current.gone;c->gpr[4]=0;c->gpr[12]=3;++thisUpdate;return;
    }
    if(current.remove>=0){
        uint32_t proc=0,state=0;bool known=uint32_t(current.remove)<entriesUsed&&actors::word(entryRoom+uint32_t(current.remove)*kEntryBytes+0x24,proc)&&proc>=0x10000000&&actors::word(proc+0x54,state);
        if(!known||(state>>24)==3){event("{\"event\":\"actor_spawn\",\"state\":\"nothing-to-delete\",\"slot\":%d,\"proc\":%u,\"proc_state\":%u}",current.remove,proc,state>>24);return;}
        c->gpr[3]=proc;c->gpr[4]=0;c->gpr[12]=3;++thisUpdate;return;
    }
    uint32_t creator=0,owner=0,heap=0;bool roomLeft=true;
    if(current.hold){
        uint32_t weapon=actors::seekFound.load(),player=body::identityActor.load(),state=0;
        if(!weapon||!player||!actors::word(weapon+0x54,state)||(state>>24)==3){
            if(GetTickCount64()-current.since<5000){std::lock_guard lock(mutex);queue.push_back(current);++waiting;thisUpdate=kEachUpdate;return;}
            event("{\"event\":\"actor_spawn\",\"state\":\"nothing-to-hold\",\"name\":\"%s\",\"found\":%u}",current.name,weapon);actors::seekName[0]=0;return;
        }
        actors::seekName[0]=0;holding=weapon;c->gpr[3]=player;c->gpr[4]=uint32_t(current.slot);c->gpr[5]=weapon;c->gpr[6]=0;c->gpr[7]=0;c->gpr[12]=5;++thisUpdate;return;
    }
    if(current.weaponLife>0){
        size_t slot=std::find(names.begin(),names.end(),current.name)-names.begin();uint32_t equip=0;
        if(slot==names.size()&&slot<kNameSlots){names.push_back(current.name);char padded[kNameBytes]{};strncpy(padded,current.name,kNameBytes-1);memcpy(base+room+slot*kNameBytes,padded,kNameBytes);}
        if(slot>=kNameSlots||!actors::word(kEquipCreator,equip)||equip<0x10000000){event("{\"event\":\"actor_spawn\",\"state\":\"refused\",\"name\":\"%s\",\"equip_creator\":%u}",current.name,equip);return;}
        auto p=base+pack;memset(p,0,0x20);put(p,room+uint32_t(slot)*kNameBytes);put(p+4,kStringTable);
        c->gpr[3]=equip;c->gpr[4]=0;c->gpr[5]=pack;c->gpr[6]=uint32_t(current.weaponLife);c->gpr[7]=pack+0x10;c->gpr[8]=0;c->gpr[12]=4;++thisUpdate;return;
    }
    auto text=[&](const char* s)->uint32_t{
        size_t slot=std::find(names.begin(),names.end(),s)-names.begin();
        if(slot==names.size()){if(slot>=kNameSlots){roomLeft=false;return 0;}names.push_back(s);char padded[kNameBytes]{};strncpy(padded,s,kNameBytes-1);memcpy(base+room+slot*kNameBytes,padded,kNameBytes);}
        return room+uint32_t(slot)*kNameBytes;};
    uint32_t name=text(current.name),keyPlace=text("@P"),keyScale=text("@S"),keyTree=text("=AT"),keys[2]{};
    for(int i=0;i<current.extras;i++)keys[i]=text(current.key[i]);
    if(!roomLeft||!actors::word(kCreator,creator)||creator<0x10000000||!actors::word(kHeapOwner,owner)||!actors::word(owner+0x10,heap)||heap<0x10000000){
        event("{\"event\":\"actor_spawn\",\"state\":\"refused\",\"name\":\"%s\",\"creator\":%u,\"heap\":%u,\"strings\":%u}",current.name,creator,heap,unsigned(names.size()));return;}
    auto p=base+pack;memset(p,0,0xD0);uint32_t at=8;uint16_t entries=0;
    auto vector=[&](uint32_t key,const float* v){put(p+at,key);p[at+4]=4;for(int i=0;i<3;i++)put(p+at+5+i*4,v[i]);at+=17;++entries;};
    vector(keyPlace,current.at);if(current.scaled)vector(keyScale,current.scale);
    for(int i=0;i<current.extras&&!current.map;i++){
        size_t length=strlen(current.text[i])+1;if(at+6+5+length>8+0xC0)break;
        put(p+at,keyTree);p[at+4]=3;p[at+5]=1;at+=6;++entries;
        put(p+at,keys[i]);p[at+4]=5;memcpy(p+at+5,current.text[i],length);at+=5+uint32_t(length);++entries;
    }
    uint16_t count=__builtin_bswap16(entries),position=__builtin_bswap16(uint16_t(at-8));memcpy(p+4,&count,2);memcpy(p+6,&position,2);
    if(current.map){
        bool ofWalls=current.wall>0;uint32_t roomAt=ofWalls?wallRoom.load():entryRoom,index=ofWalls?uint32_t(current.wall-1):entriesUsed;
        uint32_t bytes=ofWalls?kWallBytes:kEntryBytes,slot=roomAt+index*bytes,rootAt=0,hash=ofWalls?0x4D440000u+(wallSerial++&0xFFFF):0x4D430000u+entriesUsed;auto m=base+slot;size_t size=0;
        bool fits=ofWalls?index<kWalls&&roomAt>=0x01000000:entriesUsed<kEntrySlots&&entryRoom>=0x01000000;
        if(actors::airWallPattern()&&fits){memset(m,0,bytes);size=entryDocument(m+0x70,bytes-0x70,current,hash,!current.pack,rootAt);}
        if(!size){
            static uint64_t told=0;   // a wall's refusal once a second at most: walls.h asks again
            if(!ofWalls||GetTickCount64()-told>=1000){told=GetTickCount64();event("{\"event\":\"actor_spawn\",\"state\":\"refused\",\"name\":\"%s\",\"pattern\":%d,\"pattern_of_this_map\":%d,\"entries_used\":%u,\"wall\":%d,\"room\":%u}",current.name,int(actors::airWallSeen.load()),int(actors::airWallPattern()),entriesUsed,current.wall,roomAt);}
            return;}
        uint32_t askCreator=0,askHeap=0;bool borrowed=false;uint8_t pattern[sizeof(actors::airWallRecord)];
        if(!actors::patternFor(pattern,askCreator,askHeap,borrowed))return;
        memcpy(m,pattern,sizeof(pattern));
        put(m+0x1C,slot+0x70);put(m+0x20,slot+0x70+rootAt);put(m+0x24,0u);put(m+0x28,0u);put(m+0x2C,0xFFFFFFFFu);put(m+0x34,hash);for(int i=0;i<3;i++)put(m+0x38+i*4,current.at[i]);
        if(!ofWalls){++entriesUsed;entriesListed=entriesUsed;}
        {static uint32_t toldHeap=0,toldLive=0;   // what the request goes with, and what the ordinary request would read now
            if(toldHeap!=askHeap||toldLive!=heap){toldHeap=askHeap;toldLive=heap;event("{\"event\":\"actor_spawn\",\"state\":\"map-context\",\"creator\":%u,\"heap\":%u,\"creator_now\":%u,\"heap_now\":%u,\"wall\":%d,\"at\":[%.6g,%.6g,%.6g]}",askCreator,askHeap,creator,heap,current.wall,current.at[0],current.at[1],current.at[2]);}}
        if(borrowed){static uint64_t told=0;if(GetTickCount64()-told>=1000){told=GetTickCount64();event("{\"event\":\"actor_spawn\",\"state\":\"asked-with-a-lent-place\",\"wall\":%d,\"at\":[%.6g,%.6g,%.6g]}",current.wall,current.at[0],current.at[1],current.at[2]);}}
        c->gpr[3]=askCreator;c->gpr[4]=name;c->gpr[5]=askHeap;c->gpr[6]=slot+0x1C;c->gpr[7]=slot;c->gpr[8]=0;c->gpr[9]=1;c->gpr[10]=current.pack?pack:0;c->gpr[12]=2;++thisUpdate;++asked;
        return;
    }
    c->gpr[3]=creator;c->gpr[4]=name;c->gpr[5]=heap;c->gpr[6]=0;c->gpr[7]=pack;c->gpr[8]=0;c->gpr[9]=1;c->gpr[12]=1;++thisUpdate;++asked;
}
// Bridge_SpawnResult: r3 = the game's answer to the request.
static void result(CPU* c){
    c->ip=c->spr.lr;bool yes=(c->gpr[3]&0xFF)!=0;
    if(current.wall>0&&!current.gone)return;   // walls.h tells of them itself, in sums
    if(current.gone){event("{\"event\":\"actor_spawn\",\"state\":\"%s\",\"proc\":%u,\"id\":%u}",yes?"delete-asked":"delete-declined",current.gone,current.goneId);return;}
    if(current.remove>=0){event("{\"event\":\"actor_spawn\",\"state\":\"%s\",\"slot\":%d}",yes?"delete-asked":"delete-declined",current.remove);return;}
    if(current.hold){handed=holding;event("{\"event\":\"actor_spawn\",\"state\":\"handed-to-the-player\",\"name\":\"%s\",\"slot\":%d,\"answer\":%u}",current.name,current.slot,c->gpr[3]);return;}
    if(current.weaponLife>0){event("{\"event\":\"actor_spawn\",\"state\":\"weapon-asked\",\"name\":\"%s\",\"life\":%d,\"r3\":%u}",current.name,current.weaponLife,c->gpr[3]);return;}
    accepted+=yes;
    event("{\"event\":\"actor_spawn\",\"state\":\"%s\",\"name\":\"%s\",\"at\":[%.6g,%.6g,%.6g],\"scale\":[%.6g,%.6g,%.6g],\"scaled\":%d,\"as_the_map\":%d,\"pack\":%d,\"extras\":%d,\"first_key\":\"%s\",\"first_text\":\"%s\",\"asked\":%u,\"accepted\":%u}",
        yes?"accepted":"declined",current.name,current.at[0],current.at[1],current.at[2],current.scale[0],current.scale[1],current.scale[2],int(current.scaled),int(current.map),int(current.pack),current.extras,current.extras?current.key[0]:"",current.extras?current.text[0]:"",asked,accepted);
}
static void poll(){
    static uint64_t checked=0;auto now=GetTickCount64();if(now-checked<250)return;checked=now;
    auto path=directory+"/spawn.txt";if(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES)return;
    {std::ifstream file(path);std::string line;
    while(std::getline(file,line)){
        std::istringstream in(line);std::string name;Request r{};r.remove=-1;if(!(in>>name))continue;
        if(name=="delete"){if(in>>r.remove&&r.remove>=0)ask(r);continue;}
        if(name=="hold"){
            std::string which;if(!(in>>which)||which.size()>=sizeof(actors::seekName))continue;
            bool plain=true;for(char ch:which)plain&=ch>0x20&&ch<0x7F&&ch!='"'&&ch!=92;if(!plain)continue;
            strncpy(r.name,which.c_str(),kNameBytes-1);strncpy(actors::seekName,which.c_str(),sizeof(actors::seekName)-1);actors::seekFound=0;actors::seekWithin=400;r.hold=true;r.since=GetTickCount64();if(!(in>>r.slot)||r.slot<0||r.slot>5)r.slot=0;ask(r);continue;
        }
        if(name=="weapon"){
            std::string which;int life=0;if(!(in>>which)||which.size()>=kNameBytes)continue;
            bool plain=true;for(char ch:which)plain&=ch>0x20&&ch<0x7F&&ch!='"'&&ch!=92;if(!plain)continue;
            if(!(in>>life)||life<1)life=100;
            strncpy(r.name,which.c_str(),kNameBytes-1);r.weaponLife=std::min(life,100000);ask(r);continue;
        }
        if(name=="slots"){
            uint32_t room=entryRoomAt.load(),count=entriesListed.load();std::string rows;
            for(uint32_t n=0;n<count&&room;n++){
                uint32_t proc=0,state=0,id=0;actors::word(room+n*kEntryBytes+0x24,proc);if(proc>=0x10000000){actors::word(proc+0x54,state);actors::word(proc+0x50,id);}
                char row[96];snprintf(row,sizeof(row),"%s{\"slot\":%u,\"proc\":%u,\"proc_state\":%u,\"id\":%u}",n?",":"",n,proc,state>>24,id);rows+=row;
            }
            event("{\"event\":\"actor_spawn\",\"state\":\"slots\",\"used\":%u,\"slots\":[%s]}",count,rows.c_str());continue;
        }
        if(name=="map"){r.map=true;if(!(in>>name))continue;}
        if(!(in>>r.at[0]>>r.at[1]>>r.at[2])||name.size()>=kNameBytes)continue;
        r.scaled=bool(in>>r.scale[0]>>r.scale[1]>>r.scale[2]);if(!r.scaled){for(float& s:r.scale)s=1;in.clear();}
        std::string extra;
        while(r.extras<2&&in>>extra){
            auto eq=extra.find('=');if(eq==std::string::npos||!eq||eq>=kNameBytes||extra.size()-eq-1>=kNameBytes)continue;
            bool plain=true;for(char ch:extra)plain&=ch>0x20&&ch<0x7F&&ch!='"'&&ch!=92;if(!plain)continue;
            if(extra=="pack=1"){r.pack=true;continue;}
            strncpy(r.key[r.extras],extra.substr(0,eq).c_str(),kNameBytes-1);strncpy(r.text[r.extras],extra.substr(eq+1).c_str(),kNameBytes-1);++r.extras;
        }
        bool finite=true;for(float v:r.at)finite&=std::isfinite(v);for(float v:r.scale)finite&=std::isfinite(v)&&v>0;if(!finite)continue;
        strncpy(r.name,name.c_str(),kNameBytes-1);ask(r);
    }}
    MoveFileExA(path.c_str(),(directory+"/spawn.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);
}
}
