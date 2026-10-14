// BOTW's actors near the player, published to Minecraft as SkyCraft's ActorTable (report 52).
// Worker-owned and read only: nothing in the game is changed and no game thread waits.
//
// The list (the job lists of ksys::act::BaseProcMgr; geometry as WiiXLaunch read it off
// BaseProcMgr::processJobs, 0378EC20, for this build):
//   *0x1047C244 manager; +0x64 number of job types, +0x68 their buckets
//   bucket 0xC0 bytes = 16 lists of 0xC: +4 first node, +8 count; a list ends at its own address
//   node +4 next, +8 the proc
// A proc (ksys::act::BaseProc, then Actor): +4 name pointer (+0x10 the text itself), +0x50 id,
// +0x54 state (3: being deleted), +0xE8 vtable, +0x1F8 3x4 matrix (translation in its fourth
// column), +0x34C bounding box (from the decompilation's field order; checked against the size).
// Life: the actor's virtual +0x2BC returns a pointer into itself; its body `addi r3,r3,N; blr`
// gives N (the player's is 0x540). Most life: the Life value of the actor's General parameter
// object, as the game reads it in DamageManagerBase::applyDamage (02AF92FC).
//
// The walk is not locked against the game's job threads. Every pointer is range-checked, every
// walk bounded, and an actor is published only while its id, state and vtable still match, so a
// torn read costs one publication, not a wrong actor. The id is the game's own proc id, which is
// not used again; a hit that names an id whose actor is gone reaches nothing.
#pragma once
namespace actors {
namespace p=botwcraft::proto;
constexpr uint32_t kManager=0x1047C244;constexpr float kRange=96,kKeep=112,kThingRange=40,kThingKeep=48; // BOTW units from the player
constexpr uint64_t kWalkMs=250,kPublishMs=33;static std::atomic<uint64_t> censusMs{5000};static std::atomic<bool> solid{true}; // lab/actors.txt: <on> <census ms> <solid>
struct Tracked {
    uint32_t actor=0,id=0,vtable=0;char name[40]{};int kind=0,lifeOffset=-1,mostLife=0;
    float at[3]{},yaw=0,width=0,height=0,life=1;bool dead=false,boxed=false;float box[6]{};float distance=0;
};
// The game's placed water (its BoxWater actors), for water.h: the middle of the top face, the box's own x and z
// directions in the world, and its size. Route-124: the actor's place is that of the plane the physics world holds
// for it (report 54), its matrix is at +0x1F8 like every actor's, and three floats at +0x274 are its scale
// (34, 4, 40 and 90, 8, 50 for the two near the plateau; the world's border AirWall has 5000, 900, 25 there).
struct WaterBox {float top[3],ax[2],az[2],size[3];uint32_t id;};
static std::vector<WaterBox> waterBoxes;
static std::vector<Tracked> tracked;static std::atomic<uint32_t> published{0},walked{0};static double walkMs=0,walkMsMost=0;static std::atomic<bool> enabled{true};
static bool rd(uint32_t p,void* out,size_t n){return p>=0x10000000&&nativeDamage::guestRead(p,out,n);}
static uint32_t be(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
static float bef(const uint8_t* p){uint32_t v=be(p);float f;memcpy(&f,&v,4);return f;}
static bool word(uint32_t p,uint32_t& v){if((p&3)||!nativeDamage::guestRead(p,&v,4))return false;v=__builtin_bswap32(v);return true;}
// 1 enemy, 2 person, 3 animal, 4 a thing that Link's weapons fell or break (a tree, a pot, a crate, a
// barrel; not their far stand-ins). By the actor's name, which is how BOTW's own content is sorted.
static int kindOf(const char* name){
    if(!strncmp(name,"Enemy_",6))return 1;
    if(!strncmp(name,"Npc_",4))return 2;
    if(!strncmp(name,"Animal_",7))return 3;
    size_t length=strlen(name);if(length>4&&!strcmp(name+length-4,"_Far"))return 0;
    if(!strncmp(name,"Obj_Tree",8)||!strcmp(name,"Pot")||strstr(name,"Pot_")||!strncmp(name,"Kibako",6)||!strncmp(name,"Barrel",6)||!strncmp(name,"Obj_BarrelOld",13))return 4;
    return 0;
}
static bool playerAt(float* at){
    uint32_t actor=body::identityActor.load();uint8_t m[0x30];
    if(!actor||!rd(actor+0x1F8,m,sizeof(m)))return false;
    for(int i=0;i<3;i++){at[i]=bef(m+12+i*16);if(!std::isfinite(at[i]))return false;}
    return true;
}
static int lifeOffsetOf(uint32_t vtable){
    static std::unordered_map<uint32_t,int> known;if(auto it=known.find(vtable);it!=known.end())return it->second;
    uint32_t fn=0,code[2]{};int offset=-1;
    if(word(vtable+0x2BC,fn)&&fn>=0x02000000&&fn<0x05000000&&word(fn,code[0])&&word(fn+4,code[1])&&(code[0]>>16)==0x3863&&code[1]==0x4E800020)offset=int(code[0]&0xFFFF);
    if(offset<0||offset>0x4000)offset=-1;
    return known[vtable]=offset;
}
static int mostLifeOf(uint32_t actor){
    uint32_t param=0,list=0,count=0,array=0,object=0,life=0;
    if(!word(actor+0x39C,param)||!word(param+0x78,list)||!word(list+0x240,count)||!word(list+0x244,array)||!count)return 0;
    if(!word(array+(count>1?4:0),object)||!word(object+0x3C,life))return 0;
    return int32_t(life)>0&&int32_t(life)<1000000?int(life):0;
}
// One actor's present state. False: it is gone (id, state or vtable no longer match).
static bool refresh(Tracked& t){
    uint8_t block[0x368];
    if(!rd(t.actor,block,sizeof(block))||be(block+0x50)!=t.id||block[0x54]==3||be(block+0xE8)!=t.vtable)return false;
    const uint8_t* m=block+0x1F8;float fx=bef(m+8),fz=bef(m+40);
    for(int i=0;i<3;i++){t.at[i]=bef(m+12+i*16);if(!std::isfinite(t.at[i])||std::abs(t.at[i])>100000)return false;}
    if(std::isfinite(fx)&&std::isfinite(fz)&&(fx!=0||fz!=0))t.yaw=float(std::atan2(-fx,fz)*180/3.14159265358979);
    for(int i=0;i<6;i++)t.box[i]=bef(block+0x34C+i*4);
    float dx=t.box[3]-t.box[0],dy=t.box[4]-t.box[1],dz=t.box[5]-t.box[2];
    t.boxed=std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(dz)&&dx>.1f&&dy>.1f&&dz>.1f&&dx<40&&dy<40&&dz<40;
    // The box is the body's, about its feet (route-107: a Bokoblin -1.01..0.99, -0.01..2.25, -0.38..1.09; an
    // insect 0.35 x 0.17 x 0.49). Minecraft's boxes are square: the narrower side, so that a long body does not
    // block what is beside it. Without a box, a person.
    t.width=t.boxed?std::clamp(std::min(dx,dz),.3f,8.f):1.0f;t.height=t.boxed?std::clamp(t.box[4],.3f,16.f):1.8f;
    // A thing: what a blow has to find is its trunk or its body, not a tree's crown (a box of at most a block by three).
    if(t.kind==4){t.width=std::clamp(t.width,.6f,1.3f);t.height=std::clamp(t.height,.6f,3.9f);}
    if(t.lifeOffset>=0){
        uint32_t life=0;
        if(word(t.actor+uint32_t(t.lifeOffset),life)&&int32_t(life)>=0&&int32_t(life)<1000000){
            if(int(life)>t.mostLife)t.mostLife=int(life);
            t.life=t.mostLife>0?float(int(life))/float(t.mostLife):1;t.dead=t.kind==1&&int(life)==0;
        }
    }
    return true;
}
// lab/procs.txt (any content): the next walk writes every proc it meets, of whatever kind, to lab/procs.jsonl
// (name, id, place when it has one, distance from the player) and the file is renamed to procs.done.txt.
// For finding out what stands where (a camp's fire, its barrels) before deciding what to list.
static FILE* procDump=nullptr;
// For spawn.h (`hold name`): the running object of that name nearest the player, found by the walk.
// seekNot: objects of that name that are not meant (the weapons the player already holds); seekWithin: the square of how near it must be.
static char seekName[40]="";static std::atomic<uint32_t> seekFound{0},seekNot[8];static std::atomic<float> seekWithin{400};
static std::vector<std::pair<uint32_t,uint32_t>> mapThings;   // object and id of the map's own things, from the last walk (keepPattern)
// The job lists: every proc once, kept if it is of a listed kind and near the player.
static void walk(const float* player){
    std::vector<std::pair<uint32_t,uint32_t>> things;
    uint32_t manager=0,types=0,buckets=0;std::vector<Tracked> next;std::unordered_set<uint32_t> seen;uint32_t procs=0;std::vector<WaterBox> boxes;float seekNearest=seekWithin.load();uint32_t sought=0; // within 20 units unless told
    if(!word(kManager,manager)||!word(manager+0x64,types)||!word(manager+0x68,buckets)||types>32||buckets<0x10000000){walked=0;tracked.clear();return;}
    if(GetFileAttributesA((directory+"/procs.txt").c_str())!=INVALID_FILE_ATTRIBUTES){
        MoveFileExA((directory+"/procs.txt").c_str(),(directory+"/procs.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);
        if((procDump=fopen((directory+"/procs.jsonl").c_str(),"a")))fprintf(procDump,"{\"player\":[%.6g,%.6g,%.6g],\"procs\":[",player[0],player[1],player[2]);
    }
    bool firstDumped=true;
    for(uint32_t type=0;type<types;type++){
        uint8_t bucket[0xC0];if(!rd(buckets+type*0xC0,bucket,sizeof(bucket)))continue;
        for(uint32_t list=0;list<16;list++){
            uint32_t head=buckets+type*0xC0+list*0xC,node=be(bucket+list*0xC+4);int32_t count=int32_t(be(bucket+list*0xC+8));
            for(int32_t n=0;n<count&&n<4096&&node&&node!=head;n++){
                uint8_t link[0xC];if((node&3)||!rd(node,link,sizeof(link)))break;
                uint32_t proc=be(link+8);node=be(link+4);
                if(proc<0x10000000||(proc&3)||!seen.insert(proc).second)continue;
                ++procs;uint8_t header[0x58];if(!rd(proc,header,sizeof(header))||header[0x54]==3)continue;
                Tracked t;t.actor=proc;t.id=be(header+0x50);
                uint32_t text=be(header+4);
                if(text==proc+0x10)memcpy(t.name,header+0x10,sizeof(t.name)-1);else if(!rd(text,t.name,sizeof(t.name)-1))continue;
                t.name[sizeof(t.name)-1]=0;t.kind=kindOf(t.name);
                if(!strncmp(t.name,"DgnObj_",7)||!strncmp(t.name,"FldObj_",7)||!strncmp(t.name,"TwnObj_",7))things.push_back({proc,t.id});
                if(seekName[0]&&!strcmp(t.name,seekName)){
                    uint8_t m[0x30];float d2=0;bool placed=rd(proc+0x1F8,m,sizeof(m));
                    for(int i=0;i<3&&placed;i++){float v=bef(m+12+i*16);placed=std::isfinite(v);d2+=(v-player[i])*(v-player[i]);}
                    for(auto& no:seekNot)placed&=no.load()!=proc;
                    if(placed&&d2<seekNearest){seekNearest=d2;sought=proc;}
                }
                if(!strcmp(t.name,"BoxWater")){
                    uint8_t raw[0x88];
                    if(rd(proc+0x1F8,raw,sizeof(raw))){
                        WaterBox w{};w.id=t.id;bool fine=true;
                        for(int i=0;i<3;i++){w.top[i]=bef(raw+12+i*16);w.size[i]=bef(raw+0x7C+i*4);fine&=std::isfinite(w.top[i])&&std::abs(w.top[i])<100000&&std::isfinite(w.size[i])&&w.size[i]>.01f&&w.size[i]<20000;}
                        w.ax[0]=bef(raw);w.ax[1]=bef(raw+32);w.az[0]=bef(raw+8);w.az[1]=bef(raw+40);
                        for(float v:{w.ax[0],w.ax[1],w.az[0],w.az[1]})fine&=std::isfinite(v)&&std::abs(v)<=1.001f;
                        if(fine&&boxes.size()<64)boxes.push_back(w);
                    }
                }
                if(procDump){
                    uint8_t m[0x30];float at[3]{NAN,NAN,NAN};bool printable=t.name[0]!=0;
                    for(const char* ch=t.name;*ch;ch++)printable&=*ch>=0x20&&*ch<0x7F&&*ch!='"'&&*ch!=92;
                    if(rd(proc+0x1F8,m,sizeof(m)))for(int i=0;i<3;i++)at[i]=bef(m+12+i*16);
                    bool placed=std::isfinite(at[0])&&std::isfinite(at[1])&&std::isfinite(at[2])&&std::abs(at[0])<100000&&std::abs(at[1])<100000&&std::abs(at[2])<100000;
                    float d2=0;for(int i=0;i<3;i++)d2+=(at[i]-player[i])*(at[i]-player[i]);
                    if(printable){
                        if(placed)fprintf(procDump,"%s{\"name\":\"%s\",\"id\":%u,\"proc\":%u,\"job\":%u,\"at\":[%.6g,%.6g,%.6g],\"distance\":%.5g}",firstDumped?"":",",t.name,t.id,proc,type,at[0],at[1],at[2],std::sqrt(d2));
                        else fprintf(procDump,"%s{\"name\":\"%s\",\"id\":%u,\"proc\":%u,\"job\":%u}",firstDumped?"":",",t.name,t.id,proc,type);
                        firstDumped=false;
                        // Which kinds have a life at all (what can be struck: a tree, a pot), and its box about its place.
                        uint32_t vt=0;int at540=-1;
                        if(placed&&word(proc+0xE8,vt)&&vt>=0x10000000&&vt<=0x10600000&&(at540=lifeOffsetOf(vt))>=0){
                            uint32_t life=0;uint8_t box[0x18];float b[6]{};word(proc+uint32_t(at540),life);if(rd(proc+0x34C,box,sizeof(box)))for(int i=0;i<6;i++)b[i]=bef(box+i*4);
                            bool sane=true;for(float v:b)sane&=std::isfinite(v)&&std::abs(v)<1000;
                            fprintf(procDump,",{\"name\":\"%s\",\"id\":%u,\"life_at\":%d,\"life\":%d,\"most\":%d,\"box\":[%.4g,%.4g,%.4g,%.4g,%.4g,%.4g]}",t.name,t.id,at540,int(life),mostLifeOf(proc),sane?b[0]:0,sane?b[1]:0,sane?b[2]:0,sane?b[3]:0,sane?b[4]:0,sane?b[5]:0);
                        }
                        // Lab (report 84): a weapon of the game's near Link that is not his (a thrown Rock_Weapon, route-379): every
                        // place in its first 0x800 bytes, and one pointer further, that points at an Enemy_* actor (its thrower?).
                        if(placed&&d2<16.f&&(strstr(t.name,"_Weapon")||!strncmp(t.name,"Weapon_",7))){
                            auto enemy=[&](uint32_t v,char* n)->bool{uint8_t h[0x58];if(v<0x10000000||v>=0x50000000||(v&3)||!rd(v,h,sizeof(h)))return false;uint32_t text=be(h+4);
                                if(text==v+0x10)memcpy(n,h+0x10,24);else if(!rd(text,n,24))return false;n[24]=0;return !strncmp(n,"Enemy_",6);};
                            static uint32_t w[0x200];fprintf(procDump,",{\"name\":\"%s\",\"id\":%u,\"holders\":[",t.name,t.id);bool first=true;
                            if(rd(proc,w,sizeof(w)))for(uint32_t i=0;i<0x200;i++){
                                uint32_t v=__builtin_bswap32(w[i]);char n[32];if(v<0x10000000||v>=0x50000000||(v&3))continue;
                                if(enemy(v,n)){fprintf(procDump,"%s\"+%x=%s@%u\"",first?"":",",i*4,n,v);first=false;continue;}
                                uint32_t u[16];if(!rd(v,u,sizeof(u)))continue;
                                for(uint32_t k=0;k<16;k++){uint32_t x=__builtin_bswap32(u[k]);if(enemy(x,n)){fprintf(procDump,"%s\"+%x>+%x=%s@%u\"",first?"":",",i*4,k*4,n,x);first=false;}}
                            }
                            fputs("]}",procDump);
                        }
                        // The bytes after the matrix of the two kinds whose size is wanted next (a box of water, a wall of air): where their scale is.
                        if(!strncmp(t.name,"BoxWater",8)||!strcmp(t.name,"AirWall")){
                            uint8_t raw[0x1A0];if(rd(proc+0x1F8,raw,sizeof(raw))){char text[2*sizeof(raw)+1];nativeDamage::hex(raw,sizeof(raw),text);fprintf(procDump,",{\"name\":\"%s\",\"id\":%u,\"from_1f8\":\"%s\"}",t.name,t.id,text);}
                        }
                    }
                }
                if(!t.kind||proc==body::identityActor.load())continue;
                if(!word(proc+0xE8,t.vtable)||t.vtable<0x10000000||t.vtable>0x10600000)continue;
                if(!refresh(t))continue;
                float d2=0;for(int i=0;i<3;i++)d2+=(t.at[i]-player[i])*(t.at[i]-player[i]);t.distance=std::sqrt(d2);
                if(auto old=std::find_if(tracked.begin(),tracked.end(),[&](const Tracked& o){return o.id==t.id&&o.actor==t.actor;});old!=tracked.end()){
                    if(t.distance>(t.kind==4?kThingKeep:kKeep))continue;
                    t.lifeOffset=old->lifeOffset;t.mostLife=std::max(old->mostLife,t.mostLife);
                }else{
                    if(t.distance>(t.kind==4?kThingRange:kRange))continue;
                    t.lifeOffset=lifeOffsetOf(t.vtable);t.mostLife=mostLifeOf(proc);
                }
                refresh(t);next.push_back(t);
            }
        }
    }
    if(procDump){fputs("]}\n",procDump);fclose(procDump);procDump=nullptr;}
    std::sort(next.begin(),next.end(),[](const Tracked& a,const Tracked& b){return a.distance<b.distance;});
    if(next.size()>p::kMaxActors)next.resize(p::kMaxActors);
    tracked.swap(next);walked=procs;waterBoxes.swap(boxes);seekFound=sought;mapThings.swap(things);
}
// What hit Link when the defence hook did not say (the worker, as the hit goes to Minecraft; route-376 to 380): the
// objects near him: a weapon within 8 of the game's metres (a rock bounces off), an enemy within 3.5. A weapon of the game's that is not his and whose holder is an
// enemy is a thrown one (a Bokoblin's rock, Rock_Weapon: weapon+0x1B8 points at a record whose +8 is the enemy that
// threw it; the player's own weapons hold no enemy there); else the nearest enemy (bare hands, or a Bokoblin a Moblin
// threw). id: that enemy's id (the one Minecraft's stand-in carries); name: what was found, for the log.
static bool hitSource(uint32_t& id,bool& thrown,char* name){   // name: 64 bytes
    float player[3];uint32_t manager=0,types=0,buckets=0,self=body::identityActor.load();
    if(!playerAt(player)||!word(kManager,manager)||!word(manager+0x64,types)||!word(manager+0x68,buckets)||types>32||buckets<0x10000000)return false;
    auto nameOf=[](uint32_t proc,char* out)->bool{uint8_t h[0x58];if(proc<0x10000000||(proc&3)||!rd(proc,h,sizeof(h)))return false;uint32_t text=be(h+4);
        if(text==proc+0x10)memcpy(out,h+0x10,31);else if(!rd(text,out,31))return false;out[31]=0;return true;};
    float bestThrown=8.f*8.f,bestNear=3.5f*3.5f,looseD2=1e30f;char loose[48]="";uint32_t thrower=0,closest=0;char thrownName[32]="",nearName[32]="";
    std::unordered_set<uint32_t> seen;
    for(uint32_t type=0;type<types;type++){
        uint8_t bucket[0xC0];if(!rd(buckets+type*0xC0,bucket,sizeof(bucket)))continue;
        for(uint32_t list=0;list<16;list++){
            uint32_t head=buckets+type*0xC0+list*0xC,node=be(bucket+list*0xC+4);int32_t count=int32_t(be(bucket+list*0xC+8));
            for(int32_t n=0;n<count&&n<4096&&node&&node!=head;n++){
                uint8_t link[0xC];if((node&3)||!rd(node,link,sizeof(link)))break;
                uint32_t proc=be(link+8);node=be(link+4);
                if(proc<0x10000000||(proc&3)||proc==self||!seen.insert(proc).second)continue;
                char own[32];uint8_t m[0x30];if(!nameOf(proc,own)||!rd(proc+0x1F8,m,sizeof(m)))continue;
                float d2=0;for(int i=0;i<3;i++){float v=bef(m+12+i*16);if(!std::isfinite(v)){d2=1e30f;break;}d2+=(v-player[i])*(v-player[i]);}
                if(!strncmp(own,"Enemy_",6)&&d2<bestNear){uint32_t i2=0;if(word(proc+0x50,i2)){bestNear=d2;closest=i2;snprintf(nearName,sizeof(nearName),"%s",own);}}
                else if((strstr(own,"_Weapon")||!strncmp(own,"Weapon_",7))&&d2<bestThrown){
                    uint32_t record=0,holder=0,holderId=0;char who[32]="";
                    bool held=word(proc+0x1B8,record)&&record>=0x10000000&&word(record+8,holder);
                    if(holder!=self&&d2<looseD2){looseD2=d2;snprintf(loose,sizeof(loose),"%s/%s",own,held&&nameOf(holder,who)?who:"-");}
                    if(held&&nameOf(holder,who)&&!strncmp(who,"Enemy_",6)&&word(holder+0x50,holderId)){
                        bestThrown=d2;thrower=holderId;snprintf(thrownName,sizeof(thrownName),"%s",own);
                    }
                }
            }
        }
    }
    if(thrower){id=thrower;thrown=true;snprintf(name,32,"%s",thrownName);return true;}
    if(closest){id=closest;thrown=false;snprintf(name,32,"%s",nearName);return true;}
    // Nothing: the nearest loose weapon, its holder and how far (for the log).
    if(loose[0])snprintf(name,64,"none;%s@%.1f",loose,std::sqrt(looseD2));
    return false;
}
static void publish(){
    auto table=live::at<p::ActorTable>(p::kOffActorTable);auto seq=live::atom(table->seq);auto value=seq.load(std::memory_order_relaxed);
    seq.store(value+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);
    uint32_t count=0;
    for(auto& t:tracked){
        auto& r=table->actors[count++];r={};r.formId=t.id;r.flags=(t.kind==1?p::kActorHostile:0)|(t.dead?p::kActorDead:0);
        // A body the player cannot walk through: enemies and people, and animals of some size (not an insect, a fish, a bird overhead).
        // A thing is not: the game's own collision for it is already in the grid Steve walks on.
        if(solid.load()&&!t.dead&&t.kind!=4&&(t.kind!=3||(t.boxed&&t.width>=.6f&&t.height>=.7f)))r.flags|=p::kActorSolid;
        // Minecraft's coordinates: (BOTW - anchor) / scale + origin, as everywhere in the bridge.
        r.x=(t.at[0]-nativeRegions::anchor[0])/nativeRegions::scale+nativeRegions::origin[0];
        r.y=(t.at[1]-nativeRegions::anchor[1])/nativeRegions::scale+nativeRegions::origin[1];
        r.z=(t.at[2]-nativeRegions::anchor[2])/nativeRegions::scale+nativeRegions::origin[2];
        r.yaw=t.yaw;r.width=t.width/nativeRegions::scale;r.height=t.height/nativeRegions::scale;r.healthFrac=t.life;
        // The table's name has 24 bytes: the kind's prefix is left out ("Bokoblin_Junior").
        const char* shown=t.name+(t.kind==1?6:t.kind==2?4:t.kind==3?7:!strncmp(t.name,"Obj_",4)?4:0);snprintf(r.name,sizeof(r.name),"%s",shown);
    }
    table->count=count;seq.store(value+2,std::memory_order_release);published=count;
}
static void census(const float* player){
    static FILE* file=fopen((directory+"/actors.jsonl").c_str(),"a");if(!file)return;
    FILETIME now;GetSystemTimeAsFileTime(&now);uint64_t ms=(((uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime)-116444736000000000ULL)/10000;
    fprintf(file,"{\"unix_ms\":%llu,\"procs\":%u,\"player\":[%.6g,%.6g,%.6g],\"actors\":[",(unsigned long long)ms,walked.load(),player[0],player[1],player[2]);
    bool first=true;
    for(auto& t:tracked){
        fprintf(file,"%s{\"id\":%u,\"actor\":%u,\"vtable\":%u,\"name\":\"%s\",\"kind\":%d,\"at\":[%.6g,%.6g,%.6g],\"yaw\":%.4g,\"distance\":%.4g,\"life_offset\":%d,\"most_life\":%d,\"life\":%.4g,\"dead\":%d,\"boxed\":%d,\"width\":%.4g,\"height\":%.4g,\"box\":[%.5g,%.5g,%.5g,%.5g,%.5g,%.5g]}",
            first?"":",",t.id,t.actor,t.vtable,t.name,t.kind,t.at[0],t.at[1],t.at[2],t.yaw,t.distance,t.lifeOffset,t.mostLife,t.life,int(t.dead),int(t.boxed),t.width,t.height,t.box[0],t.box[1],t.box[2],t.box[3],t.box[4],t.box[5]);
        first=false;
    }
    fputs("]}\n",file);fflush(file);
}
// Read only, for step 5 of report 48: every call of the game's actor creation, 037B6040(r3, name, r5,
// its map iterator, a record of 0x68 bytes with the place, r8, r9, r10), as the `spawnactors` reference
// uses it. lab/creates.jsonl, the first 3000 calls of a session and after that the names asked for in
// lab/creates.txt (one prefix a line).
static std::atomic<uint32_t> createsSeen{0};
// The map entry an actor is created from, as the game holds it: a dictionary node of a BYML document
// (version 2, big endian: "BY", version, offsets of the key table, the string table and the root; a
// dictionary is C1, a count of three bytes, then eight bytes an entry: key index (3), type, value;
// arrays C0 and dictionaries C1 hold an offset from the document's start, floats D2 and integers
// D1/D3 the value itself, strings A0 an index into the string table). One level of arrays is opened.
static void entry(FILE* file,uint32_t doc,uint32_t node){
    uint8_t head[16]{},top[4]{};uint32_t keys=0,strings=0;
    if(!rd(doc,head,sizeof(head))||head[0]!='B'||head[1]!='Y'||!rd(node,top,4)||top[0]!=0xC1){fputs("null",file);return;}
    keys=doc+be(head+4);strings=doc+be(head+8);uint32_t count=(uint32_t(top[1])<<16)|(uint32_t(top[2])<<8)|top[3];if(count>64)count=64;
    auto text=[&](uint32_t table,uint32_t index,char* out,size_t size){
        out[0]=0;uint8_t t[4];uint32_t offset=0;if(!rd(table,t,4)||t[0]!=0xC2)return;
        uint32_t n=(uint32_t(t[1])<<16)|(uint32_t(t[2])<<8)|t[3];if(index>=n||!word(table+4+index*4,offset))return;
        if(!rd(table+offset,out,size-1))return;out[size-1]=0;
        for(char* ch=out;*ch;ch++)if(*ch<0x20||*ch>=0x7F||*ch=='"'||*ch==92){*ch=0;break;}
    };
    fputc('{',file);
    for(uint32_t i=0;i<count;i++){
        uint8_t e[8];if(!rd(node+4+i*8,e,8))break;
        uint32_t key=(uint32_t(e[0])<<16)|(uint32_t(e[1])<<8)|e[2],value=be(e+4);char name[48];text(keys,key,name,sizeof(name));
        fprintf(file,"%s\"%s\":{\"key\":%u,\"type\":%u,\"raw\":%u",i?",":"",name,key,unsigned(e[3]),value);
        if(e[3]==0xA0){char s[64];text(strings,value,s,sizeof(s));fprintf(file,",\"string\":\"%s\"",s);}
        if(e[3]==0xD2){float f;uint32_t v=value;memcpy(&f,&v,4);fprintf(file,",\"float\":%.9g",f);}
        if(e[3]==0xC0||e[3]==0xC1){
            uint8_t inner[0x60]{};char hexed[2*sizeof(inner)+1]="";if(rd(doc+value,inner,sizeof(inner)))nativeDamage::hex(inner,sizeof(inner),hexed);
            fprintf(file,",\"at\":%u,\"bytes\":\"%s\"",doc+value,hexed);
        }
        if(e[3]==0xC1){
            // One level down: what a map entry gives its object under !Parameters (an AirWall's shape).
            uint8_t it[4];
            if(rd(doc+value,it,4)&&it[0]==0xC1){
                uint32_t m=(uint32_t(it[1])<<16)|(uint32_t(it[2])<<8)|it[3];if(m>16)m=16;fputs(",\"dict\":{",file);
                for(uint32_t j=0;j<m;j++){
                    uint8_t d[8];if(!rd(doc+value+4+j*8,d,8))break;
                    uint32_t k2=(uint32_t(d[0])<<16)|(uint32_t(d[1])<<8)|d[2],v2=be(d+4);char n2[48];text(keys,k2,n2,sizeof(n2));
                    fprintf(file,"%s\"%s\":{\"type\":%u,\"raw\":%u",j?",":"",n2,unsigned(d[3]),v2);
                    if(d[3]==0xA0){char s[64];text(strings,v2,s,sizeof(s));fprintf(file,",\"string\":\"%s\"",s);}
                    if(d[3]==0xD2){float f;memcpy(&f,&v2,4);fprintf(file,",\"float\":%.9g",f);}
                    fputc('}',file);
                }
                fputc('}',file);
            }
        }
        fputc('}',file);
    }
    fputc('}',file);
}
// The last AirWall the map itself asked for: its record (ksys::map::Object, 0x68 bytes) and the creator and
// heap of that call, as the pattern for a map entry of the bridge's own (spawn.h).
// The record belongs to the map that was loaded then: its +4 is a place in a table of the map's own
// (PlacementActors::mActorData in the decompilation, mapObject.h), and a shrine's table is another. Route-203:
// a wall asked for inside a shrine with the open world's record, and the game ran code at address 0 within the
// second (Cemu's crash log). So the pattern holds only until the next screen behind which the map can change
// (body::stageScreens), and the map's next AirWall gives a new one.
static uint8_t airWallRecord[0x68];static std::atomic<bool> airWallSeen{false};static uint32_t airWallCreator=0,airWallHeap=0;
static std::atomic<uint32_t> airWallScreens{0};
// Walls in a map that has no AirWall of its own (a shrine; report 77). What the record's +4 names is the map's
// entry for a kind of actor (PlacementActors::mActorData in the decompilation: the kind's name and flags), and +8
// the map unit. The last request of the present map for the kind kBorrowKind lends its table place, map unit,
// creator and heap; everything else of the record stays the open world's AirWall's. The game then reads that
// kind's entry where it would read AirWall's.
// Observed (routes 285 to 290, the Cryonis shrine): with LinkTagAnd lending (a tag, no body of its own) the walls
// were made and had no body in the physics world; with the shrine's lift lending (every shrine has one, and it has
// a body) the four walls of the player's blocks stood with their boxes in the world, Steve was stopped by one, and
// leaving the shrine sent them away without a fault. Whether a guardian's shot stops at them was not tested.
// lab/wall-borrow.txt: another kind's name, or 0 for none (no walls in such a map, as before).
constexpr const char* kBorrowKind="DgnObj_EntranceElevator_A_01";
static std::mutex borrowLock;static char borrowName[64]{};static uint8_t borrowBytes[3]{};static uint32_t borrowCreator=0,borrowHeap=0;
static std::atomic<bool> borrowSeen{false};static std::atomic<uint32_t> borrowScreens{0};
static bool borrowedPattern(){return airWallSeen.load()&&borrowSeen.load()&&borrowScreens.load()==body::stageScreens.load();}
static bool ownPattern(){return airWallSeen.load()&&airWallScreens.load()==body::stageScreens.load();}
static bool airWallPattern(){return ownPattern()||borrowedPattern();}
// A screen that is not a change of map (a scene skipped with X: route-295, where the walls of the Cryonis shrine
// were never asked for after it; a fade) leaves the map and its things as they were, and the map asks for nothing
// again, so the pattern would stay unusable. While a pattern holds, some of the map's own things are remembered
// (kinds DgnObj_, FldObj_, TwnObj_: object and id). After a screen, once the game has run for a second: if one of
// them is still there, the map was not unloaded (unloading deletes them all) and the pattern holds again.
struct Anchor{uint32_t proc,id;};
static std::vector<Anchor> anchors;static uint32_t anchorScreens=0;static bool anchorOwn=false,anchorLent=false;
static void keepPattern(const std::vector<Anchor>& now){   // the worker, after each walk
    static uint64_t lastCall=0,steadySince=0;uint64_t t=GetTickCount64();uint32_t screens=body::stageScreens.load();
    if(t-lastCall>600)steadySince=t;lastCall=t;   // the walk rests while the game is not ready
    bool own=ownPattern(),lent=borrowedPattern();
    if(own||lent){anchors=now;if(anchors.size()>32)anchors.resize(32);anchorScreens=screens;anchorOwn=own;anchorLent=lent;return;}
    if(anchors.empty()||screens==anchorScreens||t-steadySince<1000)return;
    unsigned alive=0;for(auto& a:anchors)for(auto& n:now)if(a.proc==n.proc&&a.id==n.id){++alive;break;}
    if(alive){
        if(anchorOwn&&airWallScreens.load()==anchorScreens)airWallScreens=screens;
        if(anchorLent&&borrowScreens.load()==anchorScreens)borrowScreens=screens;
    }
    event("{\"event\":\"map_context\",\"kind\":\"%s\",\"things\":%u,\"still_there\":%u,\"screens_then\":%u,\"screens\":%u,\"own\":%d,\"lent\":%d}",alive?"kept":"gone",unsigned(anchors.size()),alive,anchorScreens,screens,int(ownPattern()),int(borrowedPattern()));
    if(alive)anchorScreens=screens;else anchors.clear();
}
// The record, creator and heap to ask with (the game's thread). False without a pattern.
static bool patternFor(uint8_t* record,uint32_t& creator,uint32_t& heap,bool& borrowed){
    if(ownPattern()){memcpy(record,airWallRecord,sizeof(airWallRecord));creator=airWallCreator;heap=airWallHeap;borrowed=false;return true;}
    if(!borrowedPattern())return false;
    std::lock_guard lock(borrowLock);
    memcpy(record,airWallRecord,sizeof(airWallRecord));record[4]=borrowBytes[0];record[5]=borrowBytes[1];record[8]=borrowBytes[2];
    creator=borrowCreator;heap=borrowHeap;borrowed=true;return true;
}
static void created(CPU* c){
    c->ip=c->spr.lr;auto count=++createsSeen;
    char name[64]{};if(!rd(c->gpr[4],name,sizeof(name)-1))return;
    for(char* ch=name;*ch;ch++)if(*ch<0x20||*ch>=0x7F||*ch=='"'||*ch==92){*ch=0;break;}
    if(!strcmp(name,"AirWall")&&c->gpr[7]>=0x10000000&&c->gpr[10]==0){
        uint8_t record[sizeof(airWallRecord)];
        if(rd(c->gpr[7],record,sizeof(record))){memcpy(airWallRecord,record,sizeof(record));airWallCreator=c->gpr[3];airWallHeap=c->gpr[5];airWallScreens=body::stageScreens.load();airWallSeen=true;}
    }
    if(c->gpr[7]>=0x10000000&&c->gpr[10]==0){
        std::lock_guard lock(borrowLock);uint8_t record[0x10]{};
        if(borrowName[0]&&!strcmp(name,borrowName)&&rd(c->gpr[7],record,sizeof(record))){
            bool fresh=!borrowSeen.load()||borrowScreens.load()!=body::stageScreens.load();
            borrowBytes[0]=record[4];borrowBytes[1]=record[5];borrowBytes[2]=record[8];borrowCreator=c->gpr[3];borrowHeap=c->gpr[5];borrowScreens=body::stageScreens.load();borrowSeen=true;
            if(fresh)event("{\"event\":\"map_context\",\"kind\":\"lends\",\"name\":\"%s\",\"table_place\":%u,\"map\":%u,\"creator\":%u,\"heap\":%u,\"screens\":%u}",name,unsigned(record[4]<<8|record[5]),unsigned(record[8]),c->gpr[3],c->gpr[5],body::stageScreens.load());
        }
    }
    // The first of the map's own requests after each such screen, and every AirWall: the record's place in the
    // map's table, to see how the tables of two maps differ.
    if(c->gpr[7]>=0x10000000){
        static uint32_t screens=~0u,told=0;uint32_t now=body::stageScreens.load();if(now!=screens){screens=now;told=0;}
        uint8_t record[0x10]{};
        if((told<8||!strcmp(name,"AirWall"))&&rd(c->gpr[7],record,sizeof(record))){++told;event("{\"event\":\"map_context\",\"kind\":\"request\",\"name\":\"%s\",\"record\":%u,\"table_place\":%u,\"map\":%u,\"heap\":%u,\"count\":%u,\"screens\":%u}",name,c->gpr[7],unsigned(record[4]<<8|record[5]),unsigned(record[8]),c->gpr[5],unsigned(count),now);}
    }
    // The creator and the heap the map itself asks with (its records are the game's own memory, the bridge's are the pack's): told when they change.
    if(c->gpr[7]>=0x10000000){
        static uint32_t creator=0,heap=0;
        if(c->gpr[3]!=creator||c->gpr[5]!=heap){creator=c->gpr[3];heap=c->gpr[5];event("{\"event\":\"map_context\",\"creator\":%u,\"heap\":%u,\"name\":\"%s\",\"count\":%u,\"pattern_creator\":%u,\"pattern_heap\":%u}",creator,heap,name,unsigned(count),airWallCreator,airWallHeap);}
    }
    if(count>3000&&strncmp(name,"AirWall",7)&&strncmp(name,"BoxWater",8))return;
    static std::mutex lock;static FILE* file=nullptr;std::lock_guard guard(lock);
    if(!file&&!(file=fopen((directory+"/creates.jsonl").c_str(),"a")))return;
    uint8_t record[0x68]{};char text[2*sizeof(record)+1]="";if(rd(c->gpr[7],record,sizeof(record)))nativeDamage::hex(record,sizeof(record),text);
    uint32_t sp=c->gpr[1],above=0,ret=0;word(sp,above);if(above)word(above+4,ret);
    fprintf(file,"{\"count\":%u,\"name\":\"%s\",\"r3\":%u,\"r5\":%u,\"r6\":%u,\"r7\":%u,\"r8\":%u,\"r9\":%u,\"r10\":%u,\"lr\":%u,\"record\":\"%s\"",
        count,name,c->gpr[3],c->gpr[5],c->gpr[6],c->gpr[7],c->gpr[8],c->gpr[9],c->gpr[10],ret,text);
    static int opened=0;
    if(!strncmp(name,"AirWall",7)||!strncmp(name,"BoxWater",8)||(!strcmp(name,"Pot")&&opened<2)||(!strncmp(name,"Enemy_Bokoblin",14)&&opened<4)){
        ++opened;uint8_t head[0x20]{};char hexed[2*sizeof(head)+1]="";uint32_t doc=be(record+0x1C),node=be(record+0x20);
        if(rd(doc,head,sizeof(head)))nativeDamage::hex(head,sizeof(head),hexed);
        fprintf(file,",\"document\":%u,\"node\":%u,\"document_head\":\"%s\",\"entry\":",doc,node,hexed);entry(file,doc,node);
    }
    fputs("}\n",file);
    if(count%50==0||count>3000)fflush(file);
}
// The actor behind an id, if it is still that actor.
static uint32_t find(uint32_t id){
    for(auto& t:tracked)if(t.id==id){uint32_t now=0;return word(t.actor+0x50,now)&&now==id?t.actor:0;}
    return 0;
}
// A blow from Minecraft (kEvHitActor: the stand-in's id and Minecraft's damage after its own rules).
// The worker queues it for the actor; the game's thread that runs that actor's damage manager
// takes it in the manager's update (pack, ActorHit, at 02AEDC50: after the update has cleared
// the manager's state of the frame, before it looks at contacts) and the pack then calls the
// game's own receiving step, 02AE83C0, with the amounts and codes of a plain blow as the game
// passed them for a Bokoblin's on the player (route-104: r4 2, r5 0, r6 1, kind 17, last 1).
// From there on everything is the game's: its callbacks, the actor's own addDamage, later its
// applyDamage, its reaction and its death.
// lab/damage-to-botw.txt: BOTW life per point of Minecraft damage (0: blows are not passed on).
// A blow that Minecraft marks as fire (lava, a fire block, fire aspect, a flame arrow, burning) is passed in the form
// the game gave its own fire on the player in route-121: (manager, 6, 5, 0, damage, 0, 0, 9; kind 0, 0).
// lab/fire-blow.txt 0: passed as a plain blow like the rest.
// Report 58: while the bridge's swing of a BOTW weapon is on its way to that actor (swing.h), the blow waits (`held`
// milliseconds from `tick`): the swing's own contact takes it (blowFor) and the game works the hit out itself, with
// Minecraft's amount. A swing that touches nothing leaves the blow to be given here as before. Things of the fourth
// kind are the swing's alone: BOTW's rules, no amount of Minecraft's.
struct Blow {std::atomic<uint32_t> actor{0};std::atomic<int32_t> amount{0};std::atomic<uint32_t> tick{0};std::atomic<bool> fire{false};std::atomic<uint32_t> held{0};};
static std::atomic<bool> fireBlows{true};
static Blow blows[16];static std::atomic<int> waiting{0};static std::atomic<float> toBotw{3};static std::atomic<uint64_t> queued{0},given{0},lost{0};
static void hit(const p::McEvent& e){
    float factor=toBotw.load();uint32_t actor=find(e.formId);int amount=int(std::lround(e.a*factor));
    // People are not struck: BOTW's own Link cannot hurt them either, and some of them carry the game's story.
    int kind=0;for(auto& t:tracked)if(t.id==e.formId)kind=t.kind;
    bool ok=actor&&kind!=2&&factor>0&&e.a>0&&physicalActive.load();if(ok&&amount<1)amount=1;
    if(ok&&kind==4&&swing::ownsThings()){event("{\"event\":\"actor_hit\",\"state\":\"left-to-the-swing\",\"id\":%u,\"actor\":%u,\"minecraft_damage\":%.4g}",e.formId,actor,e.a);return;}
    uint32_t held=0;bool swung=ok&&swing::aimedAt(actor,held);
    if(ok){
        ok=false;
        for(auto& b:blows){uint32_t none=0;if(b.actor.compare_exchange_strong(none,actor)){b.amount=amount;b.fire=(e.flags&p::kHitFire)!=0&&fireBlows.load();b.tick=uint32_t(GetTickCount64());b.held=swung?held:0;++waiting;++queued;ok=true;break;}}
    }
    event("{\"event\":\"actor_hit\",\"state\":\"%s\",\"id\":%u,\"actor\":%u,\"minecraft_damage\":%.4g,\"amount\":%d,\"weapon\":%u,\"flags\":%u,\"held_for_the_swing_ms\":%u}",ok?"queued":"dropped",e.formId,actor,e.a,amount,e.weapon,e.flags,swung?held:0);
}
// The blow waiting for that actor, taken by the swing's contact (the game's own thread): its amount, or 0 for none.
static int blowFor(uint32_t actor){
    for(auto& b:blows){uint32_t a=actor;if(b.actor.load()==actor){int amount=b.amount.load();if(b.actor.compare_exchange_strong(a,0)){--waiting;++given;return amount;}}}
    return 0;
}
static void take(CPU* c){
    c->ip=c->spr.lr;c->gpr[12]=0;if(waiting.load(std::memory_order_relaxed)<=0)return;
    uint32_t manager=c->gpr[3],actor=0;if(!word(manager+8,actor)||!actor)return;
    for(auto& b:blows)if(b.actor.load()==actor){
        if(uint32_t(GetTickCount64())-b.tick.load()<b.held.load())return;   // the swing is on its way
        int amount=b.amount.load();bool fire=b.fire.load();uint32_t age=uint32_t(GetTickCount64())-b.tick.load();uint32_t mine=actor;if(!b.actor.compare_exchange_strong(mine,0))return;--waiting;
        auto base=static_cast<uint8_t*>(getGuest());uint32_t sp=c->gpr[1];
        if(age>1000||amount<1||!base||sp<0x01000000||sp>=0xA0000000-16){++lost;return;}
        // 02AE83C0(manager, 2, 0, 1, damage, second amount, 0, 0; on the stack: kind 17, 1). The two stack
        // arguments go into this function's own outgoing slots, which it fills the same way before its own calls.
        uint32_t kind=__builtin_bswap32(fire?0:17),last=__builtin_bswap32(fire?0:1);memcpy(base+sp+8,&kind,4);memcpy(base+sp+12,&last,4);
        if(fire){c->gpr[4]=6;c->gpr[5]=5;c->gpr[6]=0;c->gpr[7]=uint32_t(amount);c->gpr[8]=0;c->gpr[9]=0;c->gpr[10]=9;}
        else{c->gpr[4]=2;c->gpr[5]=0;c->gpr[6]=1;c->gpr[7]=uint32_t(amount);c->gpr[8]=uint32_t(amount);c->gpr[9]=0;c->gpr[10]=0;}
        c->gpr[12]=1;++given;
        event("{\"event\":\"actor_hit\",\"state\":\"given\",\"actor\":%u,\"manager\":%u,\"amount\":%d,\"age_ms\":%u,\"fire\":%d}",actor,manager,amount,age,int(fire));
        return;
    }
}
// lab/track.txt `PROC(hex) ID MS`: for that long the place of that object, every 30 ms, as `track` in control.jsonl
// (what a hit on a thing held by Stasis does to it, report 71).
static void trackLab(){
    static uint32_t proc=0,id=0;static uint64_t until=0,checked=0,said=0;uint64_t now=GetTickCount64();
    if(now-checked>=200){checked=now;auto path=directory+"/track.txt";
        if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES){uint64_t ms=0;uint32_t p=0,i=0;{std::ifstream file(path);file>>std::hex>>p>>std::dec>>i>>ms;}
            MoveFileExA(path.c_str(),(directory+"/track.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);if(p&&ms){proc=p;id=i;until=now+std::min<uint64_t>(ms,120000);}}}
    if(!proc||now>=until||now-said<30)return;
    said=now;uint8_t header[0x58],m[0x30];float at[3];
    if(!rd(proc,header,sizeof(header))||be(header+0x50)!=id||!rd(proc+0x1F8,m,sizeof(m))){event("{\"event\":\"track\",\"ms\":%llu,\"gone\":1}",(unsigned long long)now);proc=0;return;}
    for(int i=0;i<3;i++)at[i]=bef(m+12+i*16);
    // The middle: that of its box (+0x34C, two corners) through its matrix, as runes.h takes it for what Magnesis holds.
    uint8_t box[0x18];float middle[3]{at[0],at[1],at[2]};
    if(rd(proc+0x34C,box,sizeof(box))){float local[3];for(int i=0;i<3;i++)local[i]=(bef(box+i*4)+bef(box+12+i*4))*.5f;for(int i=0;i<3;i++){middle[i]=at[i];for(int k=0;k<3;k++)middle[i]+=bef(m+i*16+k*4)*local[k];}}
    event("{\"event\":\"track\",\"ms\":%llu,\"id\":%u,\"at\":[%.4f,%.4f,%.4f],\"middle\":[%.4f,%.4f,%.4f]}",(unsigned long long)now,id,at[0],at[1],at[2],middle[0],middle[1],middle[2]);
}
static void poll(){
    static uint64_t walkedAt=0,publishedAt=0,censusAt=0,checked=0;auto now=GetTickCount64();
    if(now-checked>=1000){checked=now;int value=1,hard=1;uint64_t every=5000;std::ifstream(directory+"/actors.txt")>>value>>every>>hard;enabled=value!=0;solid=hard!=0;censusMs=std::clamp<uint64_t>(every,100,600000);float factor=3;std::ifstream(directory+"/damage-to-botw.txt")>>factor;toBotw=std::isfinite(factor)?std::clamp(factor,0.f,100.f):3;int asFire=1;std::ifstream(directory+"/fire-blow.txt")>>asFire;fireBlows=asFire!=0;
        // A blow whose actor never came by (asleep, gone): given up after a second, so that its slot is free again.
        for(auto& b:blows)if(auto a=b.actor.load();a&&uint32_t(now)-b.tick.load()>1000&&b.actor.compare_exchange_strong(a,0)){--waiting;++lost;}}
    if(!live::base||!live::connected||live::failed)return;
    float player[3];bool world=enabled.load()&&body::allowed()&&playerAt(player);
    if(!world){if(!tracked.empty()||published.load()){tracked.clear();publish();}return;}
    if(now-walkedAt>=kWalkMs){
        walkedAt=now;LARGE_INTEGER a,b,f;QueryPerformanceCounter(&a);walk(player);QueryPerformanceCounter(&b);QueryPerformanceFrequency(&f);
        walkMs=double(b.QuadPart-a.QuadPart)*1000/double(f.QuadPart);walkMsMost=std::max(walkMsMost,walkMs);
        {std::vector<Anchor> now;now.reserve(mapThings.size());for(auto& [proc,id]:mapThings)now.push_back({proc,id});keepPattern(now);}
    }
    else if(now-publishedAt>=kPublishMs){std::erase_if(tracked,[](Tracked& t){return !refresh(t);});}
    else return;
    publishedAt=now;publish();
    if(now-censusAt>=censusMs.load()){censusAt=now;census(player);event("{\"event\":\"actor_table\",\"procs\":%u,\"published\":%u,\"walk_ms\":%.3f,\"walk_ms_most\":%.3f}",walked.load(),published.load(),walkMs,walkMsMost);walkMsMost=0;}
}
}
