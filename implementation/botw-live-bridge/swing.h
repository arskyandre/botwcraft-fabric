// Lab, for report 57 (Steve's blow as the swing of a BOTW weapon): the attack sensor of a weapon the
// player holds, made live by the bridge.
//
// A weapon is an actor of its own. Its update of its attack sensor (024AA790, as the BetterVR
// reference names it for this build: Weapon::doAttackMaybe) first asks for its holder (virtual
// +0x4A4; none, nothing is done) and then reads what was set up for the sensor at +0x874:
//   +0x874 mode (0 or 2: live, anything else: dead)   +0x878 flags   +0x87C multiplier of the weapon's power
//   +0x880 scale   +0x884 a kind (BetterVR: 2 is needed for felling trees)   +0x888 a byte
//   +0x88C power against players   +0x890 impact   +0x898 a combo count
//   +0x89C the sensor's contact layer is set   +0x8A0 take the set-up now (the game clears it)
// With +0x8A0 set and a live mode the set-up is copied to +0x93C.., +0x950 is set and from then on
// every update calls the sensor's activation (024A48E8) with the weapon's power times the
// multiplier; with a dead mode +0x950 is cleared and the sensor is switched off (024A58C4). A new
// take of the set-up is a new attack: what the attack before had already hit can be hit again.
//
// The pack calls here at 024AA7C4, where that function reads +0x8A0 (r3 the weapon, r4 its holder).
// lab/swing.txt: `arm <ms> [again ms] [multiplier] [kind]`: for that long the sensor of every
// sword, two-handed weapon or spear whose holder is the player is live, taken anew every `again`
// milliseconds (0: once); after it, dead again. Every weapon met is written to the control log once,
// every take while armed, and the end.
// `trace 1|0`: every attack the game works out for any actor is written to the control log
// (nativeDamage::defence), not only those on the player.
//
// Where the weapon is: the game binds what an actor holds to a bone of the holder's model each
// update (0312575C: the bone's matrix is fetched to the stack and handed, with the held actor, to
// 031253F0, which writes the held actor's own matrix from it). The pack calls here at 0312587C,
// just before that hand-over (r3 the held actor, r4 the bone's matrix: three rows of four, the place
// in the fourth column, r5 the bone's name). BetterVR puts the VR hand's matrix there.
// `put x y z <ms>`: for that long the matrix of every weapon of the player's met by the sensor hook
// gets that place (BOTW's coordinates) and no rotation. Route-149: with the sensor live and the
// sword left on Link's back, 1.1 units from a tree's middle, nothing was hit.
#pragma once
namespace swing {
static std::atomic<uint64_t> armedUntil{0},again{0};static std::atomic<float> multiplier{1};static std::atomic<uint32_t> kind{2};
static std::atomic<uint64_t> putUntil{0};static float putAt[3]{};static std::unordered_set<uint32_t> mine,placed;static std::mutex mutex;static std::unordered_map<uint32_t,uint64_t> lastTake;static std::unordered_set<uint32_t> met,live;static uint32_t takes=0; // weapons are updated on the game's job threads
// Steve's swing (below): the weapon kept for him, the swing under way.
namespace p=botwcraft::proto;
struct Kept {int cls=-1;uint32_t actor=0,id=0;int lifeAt=-1;uint32_t life=0;};
struct Sweep {uint64_t id=0,start=0,end=0;float from[3]{},to[3]{},scale=1;uint32_t target=0;};
static Kept kept;static Sweep sweep;static int wantCls=-1,stage=0,stageCls=-1;static uint64_t stageAt=0,retryAt=0,takenSweep=0,stayUntil=0;static bool keptLive=false;
static std::atomic<uint64_t> shapeUntil{0},sweeps{0};static std::atomic<bool> on{true};
static std::unordered_map<uint32_t,int> kinds;static std::unordered_map<uint32_t,uint64_t> playersOwn;   // actor → kind (actors.h's table); the weapons the player holds → when last met
constexpr int kSlot=3;constexpr float kSpeed=20;   // BOTW units a second
// The lead: for this long before a swing its weapon already moves along the swing's line, sensor dead, from behind the
// start. The game gives a blow the direction its weapon was moving in (the attack's record +0x70, route-257), and a
// thing held by Stasis flies off that way. Without the lead a swing that touched in its first frame had the jump
// from where the last swing ended as its movement: of four lab swings two sent the box where no swing went, and
// a player could not choose where a thing flew, above all how high.
constexpr uint64_t kLeadMs=100;
static void text(uint32_t actor,char* out,size_t size){
    out[0]=0;if(actor<0x10000000||!nativeDamage::guestRead(actor+0x10,out,size-1))return;
    out[size-1]=0;for(char* ch=out;*ch;ch++)if(*ch<32||*ch>126||*ch=='"'||*ch==92)*ch='?';
}
static void sensor(CPU* c){
    c->ip=c->spr.lr;uint32_t weapon=c->gpr[3],holder=c->gpr[4];auto base=static_cast<uint8_t*>(getGuest());
    if(!base||weapon<0x10000000||weapon>=0x50000000-0xA20)return;
    auto m=base+weapon;auto get=[&](uint32_t at){uint32_t v;memcpy(&v,m+at,4);return __builtin_bswap32(v);};
    auto put=[&](uint32_t at,uint32_t v){v=__builtin_bswap32(v);memcpy(m+at,&v,4);};
    char name[40],held[40];text(weapon,name,sizeof(name));std::lock_guard lock(mutex);
    bool players=holder&&holder==body::identityActor.load();
    if(met.size()<64&&met.insert(weapon).second){
        text(holder,held,sizeof(held));float factor;uint32_t raw=get(0x87C);memcpy(&factor,&raw,4);
        event("{\"event\":\"weapon_sensor\",\"state\":\"met\",\"weapon\":%u,\"name\":\"%s\",\"holder\":\"%s\",\"players\":%d,\"type\":%u,\"mode\":%u,\"flags\":%u,\"multiplier\":%.6g,\"kind\":%d,\"layer_set\":%u,\"take\":%u,\"active\":%u}",
            weapon,name,held,int(players),get(0x938),get(0x874),get(0x878),factor,int32_t(get(0x884)),unsigned(m[0x89C]),unsigned(m[0x8A0]),unsigned(m[0x950]));
    }
    if(players)playersOwn[weapon]=GetTickCount64();
    if(weapon==kept.actor){   // the bridge's own: live once at a swing's start, dead after its end
        if(kept.lifeAt>0&&kept.lifeAt<0xA00)put(uint32_t(kept.lifeAt),kept.life);   // it does not wear (route-160: a torch was gone after 8 blows, three times)
        uint64_t now=GetTickCount64();bool during=sweep.id&&now>=sweep.start&&now<sweep.end;float one=1;uint32_t raw;memcpy(&raw,&one,4);
        if(during&&takenSweep!=sweep.id){put(0x874,2);put(0x87C,raw);put(0x884,2);m[0x89C]=0;m[0x8A0]=1;takenSweep=sweep.id;keptLive=true;}
        else if(!during&&keptLive){put(0x874,1);m[0x89C]=0;m[0x8A0]=1;keptLive=false;}
        return;
    }
    if(!players||(strncmp(name,"Weapon_Sword",12)&&strncmp(name,"Weapon_Lsword",13)&&strncmp(name,"Weapon_Spear",12)))return;
    uint64_t now=GetTickCount64();mine.insert(weapon);
    if(now<armedUntil.load()){
        uint64_t every=again.load();auto last=lastTake.find(weapon);
        if(last!=lastTake.end()&&(!every||now-last->second<every)&&live.count(weapon))return;
        float factor=multiplier.load();uint32_t raw;memcpy(&raw,&factor,4);
        put(0x874,2);put(0x87C,raw);put(0x884,kind.load());m[0x89C]=0;m[0x8A0]=1;lastTake[weapon]=now;live.insert(weapon);++takes;
        event("{\"event\":\"weapon_sensor\",\"state\":\"armed\",\"weapon\":%u,\"name\":\"%s\",\"take\":%u,\"multiplier\":%.6g,\"kind\":%u,\"active_before\":%u}",weapon,name,takes,factor,kind.load(),unsigned(m[0x950]));
    }else if(live.erase(weapon)){
        put(0x874,1);m[0x89C]=0;m[0x8A0]=1;lastTake.erase(weapon);
        event("{\"event\":\"weapon_sensor\",\"state\":\"dead\",\"weapon\":%u,\"name\":\"%s\",\"takes\":%u}",weapon,name,takes);
    }
}
// Bridge_WeaponMatrix: r3 the held actor, r4 the matrix it is about to be bound with, r5 the bone's name.
static void matrix(CPU* c){
    c->ip=c->spr.lr;uint32_t weapon=c->gpr[3],at=c->gpr[4],bone=c->gpr[5];auto base=static_cast<uint8_t*>(getGuest());
    if(!base||at<0x01000000||at>=0x50000000-0x30)return;
    std::lock_guard lock(mutex);
    if(weapon==kept.actor){   // a swing: the way in its first six tenths, then at its end
        uint64_t now=GetTickCount64();bool during=sweep.id&&now>=sweep.start&&now<sweep.end,lead=sweep.id&&now<sweep.start&&now+kLeadMs>=sweep.start;if(!during&&!lead&&now>=stayUntil)return;
        float t=during?std::min(1.f,float(now-sweep.start)/(float(sweep.end-sweep.start)*.6f)):lead?-float(sweep.start-now)/(float(sweep.end-sweep.start)*.6f):1;auto m=base+at;
        static const float rows[12]{1,0,0,0, 0,1,0,0, 0,0,1,0};
        for(int i=0;i<12;i++){float f=(i%4==3)?sweep.from[i/4]+(sweep.to[i/4]-sweep.from[i/4])*t:rows[i];uint32_t v;memcpy(&v,&f,4);v=__builtin_bswap32(v);memcpy(m+i*4,&v,4);}
        return;
    }
    if(!mine.count(weapon))return;
    bool put=GetTickCount64()<putUntil.load();auto m=base+at;
    if(placed.size()<16&&placed.insert(weapon^(put?0x80000000u:0)).second){
        char name[40],boneName[24]="";text(weapon,name,sizeof(name));float was[12];
        if(bone>=0x10000000&&nativeDamage::guestRead(bone,boneName,sizeof(boneName)-1))for(char& ch:boneName)if(ch&&(ch<32||ch>126||ch=='"'||ch==92))ch='?';
        for(int i=0;i<12;i++){uint32_t v;memcpy(&v,m+i*4,4);v=__builtin_bswap32(v);memcpy(&was[i],&v,4);}
        event("{\"event\":\"weapon_sensor\",\"state\":\"%s\",\"weapon\":%u,\"name\":\"%s\",\"bone\":\"%s\",\"matrix\":[%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g,%.5g],\"put\":[%.6g,%.6g,%.6g]}",
            put?"bound-and-put":"bound",weapon,name,boneName,was[0],was[1],was[2],was[3],was[4],was[5],was[6],was[7],was[8],was[9],was[10],was[11],putAt[0],putAt[1],putAt[2]);
    }
    if(!put)return;
    static const float rows[12]{1,0,0,0, 0,1,0,0, 0,0,1,0};
    for(int i=0;i<12;i++){float f=(i%4==3)?putAt[i/4]:rows[i];uint32_t v;memcpy(&v,&f,4);v=__builtin_bswap32(v);memcpy(m+i*4,&v,4);}
}
// ---- Steve's swing (report 58) ------------------------------------------------------------------
// Every attack click of Minecraft's player comes as kEvSwing (the class and tier of what he holds,
// where his look ends, the stand-in under it if any). The bridge keeps one BOTW weapon for the class
// he holds, in the player's fourth weapon slot (route-156: slot 3 leaves Link's own sword and bow
// where they are; slot 0 displaces his sword, which is then gone), asked for from the game and
// handed over by spawn.h. A swing: for a fifth of a second that weapon's place goes from just ahead
// of the eye to a little past where the look ends, its sensor taken live once at the start and dead
// at the end. What it touches is the game's to work out; the amount is shaped in the defence of
// the one who is hit (shape, from nativeDamage::defence):
//   an enemy or an animal: Minecraft's amount for this click (the blow of kEvHitActor, which waits
//   for the contact: actors.h), no defence of the game's; none waiting: nothing;
//   a person: nothing;   anything else of BOTW's: the game's own amount times the tool's tier.
// The kept weapon does not wear: its life is written back to what it came with at each of its updates.
// lab/swing-on.txt 0: clicks are not swung (blows on stand-ins go as before report 58).
// lab/swing.txt `steve <class> <tier> x y z [fx fy fz]`: a swing to that place (BOTW's coordinates) from 1.5 above the player
// or from the place given; class -1: with the weapon kept now.
// lab/swing.txt `stay <ms>`: for that long the kept weapon is held at the end of its last swing (to be looked at).
static const char* weaponOf(int cls){
    switch(cls){
        case p::kWeaponBlade:return "Weapon_Sword_001";    // traveller's sword
        case p::kWeaponAxe:return "Weapon_Lsword_032";     // woodcutter's axe
        case p::kWeaponPick:return "Weapon_Lsword_031";    // iron sledgehammer
        case p::kWeaponPierce:return "Weapon_Spear_001";   // traveller's spear
        default:return "Weapon_Sword_043";                 // a hand, or anything else held: a torch (cuts no tree)
    }
}
static bool ownsThings(){return on.load();}
// A swing from `from` to `to` (BOTW's coordinates), beginning `wait` ms from now. With the mutex held.
static void begin(const float* from,const float* to,int tier,uint32_t target,uint64_t wait){
    float length=0;for(int i=0;i<3;i++)length+=(to[i]-from[i])*(to[i]-from[i]);length=std::sqrt(length);
    uint64_t now=GetTickCount64(),ms=uint64_t(std::clamp(length/kSpeed*1000.f/.6f,200.f,400.f));   // the way in the first six tenths, the rest at its end
    sweep.id=++sweeps;sweep.start=now+wait+kLeadMs;sweep.end=sweep.start+ms;sweep.target=target;sweep.scale=tier>1?1+(tier-1)/3.f:1;
    for(int i=0;i<3;i++){sweep.from[i]=from[i];sweep.to[i]=to[i];}
    shapeUntil=sweep.end+200;
    event("{\"event\":\"swing\",\"state\":\"begun\",\"swing\":%llu,\"weapon\":%u,\"class\":%d,\"tier\":%d,\"scale\":%.4g,\"from\":[%.6g,%.6g,%.6g],\"to\":[%.6g,%.6g,%.6g],\"ms\":%llu,\"waits_ms\":%llu,\"target\":%u}",
        (unsigned long long)sweep.id,kept.actor,kept.cls,tier,sweep.scale,from[0],from[1],from[2],to[0],to[1],to[2],(unsigned long long)ms,(unsigned long long)wait,target);
}
// kEvSwing, on the worker.
static void asked(const p::McEvent& e){
    uint32_t what=e.flags&0xFF;int tier=int((e.flags>>8)&0xFF),cls=int(e.weapon);
    if(what==p::kSwingArrow){
        // An arrow of Minecraft's ended on the game's collision: a blow there with the weapon kept now, along the
        // arrow's flight, so that a thing held by Stasis takes it and flies that way.
        std::lock_guard lock(mutex);if(!on.load()||!kept.actor)return;
        float yaw=e.d*.0174532925f,pitch;memcpy(&pitch,&e.formId,4);pitch*=.0174532925f;
        float d[3]{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)},from[3],to[3],mc[]{e.a,e.b,e.c};bool finite=true;
        for(int i=0;i<3;i++){float at=nativeRegions::anchor[i]+float(mc[i]-nativeRegions::origin[i])*nativeRegions::scale;from[i]=at-d[i]*1.2f;to[i]=at+d[i]*.5f;finite=finite&&std::isfinite(from[i])&&std::isfinite(to[i]);}
        if(finite)begin(from,to,1,0,0);
        return;
    }
    if(what>p::kSwingHeld||tier>5||cls>int(p::kWeaponPick)||cls==int(p::kWeaponArrow))return;
    std::lock_guard lock(mutex);wantCls=cls;
    if(what==p::kSwingHeld){event("{\"event\":\"swing\",\"state\":\"held\",\"class\":%d,\"tier\":%d,\"weapon\":\"%s\"}",cls,tier,weaponOf(cls));return;}
    if(!on.load())return;
    float eye[3],to[3],d[3],length=0;double mcEye[]={live::mc.eyeX,live::mc.eyeY,live::mc.eyeZ};float mcTo[]={e.a,e.b,e.c};
    for(int i=0;i<3;i++){eye[i]=nativeRegions::anchor[i]+float(mcEye[i]-nativeRegions::origin[i])*nativeRegions::scale;to[i]=nativeRegions::anchor[i]+(mcTo[i]-nativeRegions::origin[i])*nativeRegions::scale;d[i]=to[i]-eye[i];length+=d[i]*d[i];}
    length=std::sqrt(length);uint32_t target=what==p::kSwingActor?actors::find(e.formId):0;int kind=0;if(auto it=kinds.find(target);it!=kinds.end())kind=it->second;
    if(!kept.actor||kept.cls!=cls||length<.05f||length>20){
        event("{\"event\":\"swing\",\"state\":\"%s\",\"class\":%d,\"kept_class\":%d,\"length\":%.4g,\"on\":%u,\"id\":%u}",length<.05f||length>20?"out-of-reach":"weapon-not-ready",cls,kept.cls,length,what,e.formId);return;}
    // From ahead of the player's own body to a little into what the look ended on (air: to where it ended).
    float from[3],ahead=std::min(.9f,length*.5f),past=what==p::kSwingAir?0:what==p::kSwingActor?.6f:.4f;
    for(int i=0;i<3;i++){d[i]/=length;from[i]=eye[i]+d[i]*ahead;to[i]+=d[i]*past;}
    // On an enemy or an animal Minecraft's amount is on its way (the server's tick): the swing waits for it, at most 150 ms.
    begin(from,to,tier,target,target&&(kind==1||kind==3)?150:0);
}
// A blow of Minecraft's came for that actor (actors::hit, on the worker): is the swing aimed at it and not over? Then it
// begins now, and the blow is to wait `heldMs` for its contact.
static bool aimedAt(uint32_t actor,uint32_t& heldMs){
    std::lock_guard lock(mutex);uint64_t now=GetTickCount64();
    if(!on.load()||!sweep.id||sweep.target!=actor||now>=sweep.end)return false;
    if(now+kLeadMs<sweep.start){uint64_t ms=sweep.end-sweep.start;sweep.start=now+kLeadMs;sweep.end=sweep.start+ms;shapeUntil=sweep.end+200;}
    heldMs=uint32_t(sweep.end-now)+150;return true;
}
// From nativeDamage::defence, on the game's thread: r29 the attack's damage, r31 the defence, f31 the rate.
static void shape(CPU* c,uint32_t receiver,uint32_t info){
    if(GetTickCount64()>shapeUntil.load(std::memory_order_relaxed))return;
    uint32_t words[10]{},link[2]{},attacker=0;if(!nativeDamage::guestRead(info+0x90,words,sizeof(words)))return;
    uint32_t unit=__builtin_bswap32(words[8]);
    if(unit>=0x10000000&&nativeDamage::guestRead(unit+0x3C,link,8)&&link[0]==words[9])attacker=__builtin_bswap32(link[1]);
    if(!attacker||attacker!=body::identityActor.load()||receiver==attacker)return;
    int kind=0;float scale=1;uint64_t id=0;{std::lock_guard lock(mutex);if(auto it=kinds.find(receiver);it!=kinds.end())kind=it->second;scale=sweep.scale;id=sweep.id;}
    int before=int32_t(c->gpr[29]),after=before;char name[40];text(receiver,name,sizeof(name));
    if(kind==1||kind==3){after=actors::blowFor(receiver);c->gpr[31]=0;c->fpr[62]=1.0;}
    else if(kind==2)after=0;
    else after=int(std::lround(before*scale));
    c->gpr[29]=uint32_t(after);
    // A thing (no enemy, no animal, no person) goes where the swing went: lookThrow, native_shapes.h.
    if(kind==0||kind==4){uint8_t m[0x30];float at[3],line[3],length=0;
        if(actors::rd(receiver+0x1F8,m,sizeof(m))){std::lock_guard lock(mutex);for(int i=0;i<3;i++){at[i]=actors::bef(m+12+i*16);line[i]=sweep.to[i]-sweep.from[i];length+=line[i]*line[i];}
            length=std::sqrt(length);if(length>.01f&&std::isfinite(length)){for(float& v:line)v/=length;lookThrow::aim(at,line);}}}
    event("{\"event\":\"swing\",\"state\":\"struck\",\"swing\":%llu,\"hit\":\"%s\",\"hit_actor\":%u,\"kind\":%d,\"the_games_damage\":%d,\"damage\":%d,\"scale\":%.4g}",(unsigned long long)id,name,receiver,kind,before,after,scale);
}
// On the worker: the weapon for what Steve holds, asked for and handed over.
static void keep(uint64_t now){
    static uint64_t listed=0;
    if(now-listed>=250){
        listed=now;std::lock_guard lock(mutex);kinds.clear();for(auto& t:actors::tracked)kinds[t.actor]=t.kind;
        int n=0;for(auto it=playersOwn.begin();it!=playersOwn.end();){if(now-it->second>1000){it=playersOwn.erase(it);continue;}if(n<8)actors::seekNot[n++]=it->first;++it;}
        for(;n<8;n++)actors::seekNot[n]=0;
        int value=1;std::ifstream(directory+"/swing-on.txt")>>value;on=value!=0;
    }
    int cls;{std::lock_guard lock(mutex);cls=wantCls;}
    float at[3];if(cls<0||!on.load()||!physicalActive.load()||!actors::playerAt(at)){stage=0;return;}
    if(kept.actor){uint32_t id=0,state=0;if(!actors::word(kept.actor+0x50,id)||id!=kept.id||!actors::word(kept.actor+0x54,state)||(state>>24)==3){
        event("{\"event\":\"swing\",\"state\":\"weapon-gone\",\"weapon\":%u,\"class\":%d}",kept.actor,kept.cls);std::lock_guard lock(mutex);kept={};}}
    if(kept.actor&&kept.cls==cls){stage=0;return;}
    spawn::Request r{};r.remove=-1;strncpy(r.name,stage?weaponOf(stageCls):weaponOf(cls),spawn::kNameBytes-1);
    if(stage==0&&now>=retryAt){
        r.at[0]=at[0];r.at[1]=at[1]+2.f;r.at[2]=at[2];for(float& s:r.scale)s=1;spawn::ask(r);stage=1;stageCls=cls;stageAt=now;
        event("{\"event\":\"swing\",\"state\":\"weapon-asked\",\"class\":%d,\"weapon\":\"%s\"}",cls,r.name);
    }else if(stage==1&&now-stageAt>=400){
        strncpy(actors::seekName,r.name,sizeof(actors::seekName)-1);actors::seekFound=0;actors::seekWithin=16;   // within four units: the one just asked for
        spawn::handed=0;r.hold=true;r.since=now;r.slot=kSlot;spawn::ask(r);stage=2;stageAt=now;
    }else if(stage==2){
        uint32_t got=spawn::handed.load(),id=0;
        if(got&&actors::word(got+0x50,id)){
            // Its life as it came, kept so every update: the place is that of the actor's own life (actors::lifeOffsetOf).
            uint32_t table=0,life=0;int lifeAt=-1;if(actors::word(got+0xE8,table)&&table>=0x10000000&&table<=0x10600000)lifeAt=actors::lifeOffsetOf(table);
            if(lifeAt<=0||(lifeAt&3)||!actors::word(got+uint32_t(lifeAt),life)||int32_t(life)<=0||int32_t(life)>1000000)lifeAt=-1;
            Kept old;{std::lock_guard lock(mutex);old=kept;kept={stageCls,got,id,lifeAt,life};keptLive=false;}
            // The one before is not sent away by the bridge: the game removes a weapon displaced from a slot itself (routes 155, 157: asked to go, it declined, and its place in memory was another object's seconds later).
            event("{\"event\":\"swing\",\"state\":\"weapon-ready\",\"class\":%d,\"weapon\":\"%s\",\"actor\":%u,\"id\":%u,\"ms\":%llu,\"the_one_before\":%u,\"life_at\":%d,\"life\":%d}",stageCls,r.name,got,id,(unsigned long long)(now-stageAt+400),old.actor,lifeAt,int32_t(life));
            stage=0;
        }else if(now-stageAt>7000){event("{\"event\":\"swing\",\"state\":\"weapon-not-handed\",\"class\":%d,\"weapon\":\"%s\"}",stageCls,r.name);stage=0;retryAt=now+3000;}
    }
}
// Read only: every weapon an actor takes into one of its weapon slots. The function (033BCEB4; the first
// entry of the table at 0x102BB27C) belongs to the part of an actor that holds its weapons (six
// links of 0x10 bytes, the owner at +0x60, its table at +0x64, as BetterVR lays it out): r3 that
// part, r4 the slot, r5 the weapon, r6 and r7 two values handed on to the weapon. Route-151: a
// weapon asked for from CreatePlayerEquipActorMgr is created and then taken by nobody; this is to
// learn how the game itself hands one to the player.
static std::atomic<uint32_t> playerWeapons{0};
static void equipSeen(CPU* c){
    c->ip=c->spr.lr;uint32_t part=c->gpr[3],owner=0,table=0;char who[40]="",what[40]="";
    if(part>=0x10000000){actors::word(part+0x60,owner);actors::word(part+0x64,table);}
    text(owner,who,sizeof(who));text(c->gpr[5],what,sizeof(what));
    if(owner&&owner==body::identityActor.load())playerWeapons=part;
    static std::atomic<uint32_t> seen{0};if(++seen>200)return;
    char stack[200]="";nativeDamage::guestStack(c->gpr[1],stack,sizeof(stack));
    event("{\"event\":\"weapon_sensor\",\"state\":\"taken-into-a-slot\",\"part\":%u,\"table\":%u,\"owner\":\"%s\",\"owner_actor\":%u,\"slot\":%d,\"weapon\":\"%s\",\"weapon_actor\":%u,\"r6\":%d,\"r7\":%d,\"lr\":%u,\"stack\":[%s]}",
        part,table,who,owner,int32_t(c->gpr[4]),what,c->gpr[5],int32_t(c->gpr[6]),int32_t(c->gpr[7]),c->spr.lr,stack);
}
static void poll(){
    static uint64_t checked=0;auto now=GetTickCount64();if(now-checked<50)return;checked=now;
    keep(now);
    auto path=directory+"/swing.txt";if(GetFileAttributesA(path.c_str())==INVALID_FILE_ATTRIBUTES)return;
    {std::ifstream file(path);std::string line;
    while(std::getline(file,line)){
        std::istringstream in(line);std::string word;if(!(in>>word))continue;
        if(word=="trace"){int on=0;in>>on;nativeDamage::traceAll=on!=0;event("{\"event\":\"weapon_sensor\",\"state\":\"trace\",\"on\":%d}",int(on!=0));}
        if(word=="steve"){
            int cls=0,tier=0;float to[3],from[3];
            if(in>>cls>>tier>>to[0]>>to[1]>>to[2]&&std::isfinite(to[0])&&std::isfinite(to[1])&&std::isfinite(to[2])&&actors::playerAt(from)){
                from[1]+=1.5f;{float given[3];if(in>>given[0]>>given[1]>>given[2]&&std::isfinite(given[0])&&std::isfinite(given[1])&&std::isfinite(given[2]))std::copy_n(given,3,from);}
                std::lock_guard lock(mutex);if(cls<0)cls=kept.cls;wantCls=cls;
                if(kept.actor&&kept.cls==cls)begin(from,to,tier,0,0);else event("{\"event\":\"swing\",\"state\":\"weapon-not-ready\",\"class\":%d,\"kept_class\":%d}",cls,kept.cls);
            }
        }
        if(word=="stay"){uint64_t ms=0;in>>ms;std::lock_guard lock(mutex);stayUntil=GetTickCount64()+std::min<uint64_t>(ms,60000);}
        if(word=="put"){
            float at[3];uint64_t ms=0;
            if(in>>at[0]>>at[1]>>at[2]>>ms&&std::isfinite(at[0])&&std::isfinite(at[1])&&std::isfinite(at[2])){
                {std::lock_guard lock(mutex);for(int i=0;i<3;i++)putAt[i]=at[i];}
                putUntil=GetTickCount64()+std::min<uint64_t>(ms,60000);
                event("{\"event\":\"weapon_sensor\",\"state\":\"put-asked\",\"at\":[%.6g,%.6g,%.6g],\"ms\":%llu}",at[0],at[1],at[2],(unsigned long long)ms);
            }
        }
        if(word=="arm"){
            uint64_t ms=0,every=0;float factor=1;uint32_t code=2;in>>ms>>every>>factor>>code;
            if(!std::isfinite(factor)||factor<=0||factor>1000)factor=1;
            again=every;multiplier=factor;kind=code;armedUntil=GetTickCount64()+std::min<uint64_t>(ms,60000);
            event("{\"event\":\"weapon_sensor\",\"state\":\"asked\",\"ms\":%llu,\"again_ms\":%llu,\"multiplier\":%.6g,\"kind\":%u}",(unsigned long long)ms,(unsigned long long)every,factor,code);
        }
    }}
    MoveFileExA(path.c_str(),(directory+"/swing.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);
}
}
