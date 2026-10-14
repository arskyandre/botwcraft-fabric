// Link's skeleton, read from the game's memory (report 79): what Steve's six parts follow in a scene of the game's.
// BetterVR 2e06b31 leads (resources/.../patch_CTRL_Bones.asm): a model unit's object at +0x9C holds the bones' local
// matrices at +0xC, 0x40 bytes a bone (flag, scale, 3x4). Observed here (route-304, the probe below): that object is
// laid out as nw::g3d lays a skeleton out. +0 is its resource, an "FSKL" block (count of bones at +8, the bones at the
// offset in +0x14, 0x40 bytes each: offset of the name, index, parent at +6). +0x10 is the bones' matrices in the
// world, 3x4 rows, 0x30 bytes a bone: Skl_Root stood 0.98 above the actor's place with the actor's turn, Head 1.5.
// Link's model has one unit, 121 bones. Everything here only reads.
#pragma once
namespace linkBones {
static std::atomic<bool> probeOn{false};   // lab/bones.txt
static uint32_t u16(uint32_t p){return (uint32_t(body::base[p])<<8)|body::base[p+1];}
// The resource's bone `index`: its name (letters, digits, underscores) and its parent's index.
static bool resBone(uint32_t res,uint32_t count,uint32_t index,char name[32],int& parent){
    if(index>=count||!body::readable(res+0x14,4))return false;
    uint32_t array=res+0x14+body::u32(res+0x14),bone=array+index*0x40;if(!body::readable(bone,0x40))return false;
    uint32_t text=bone+body::u32(bone);if(!body::readable(text&~3u,36))return false;
    int n=0;for(;n<31;n++){char c=char(body::base[text+n]);if(!c)break;if(!(isalnum(static_cast<unsigned char>(c))||c=='_'))return false;name[n]=c;}
    name[n]=0;parent=u16(bone+6)==0xFFFF?-1:int(u16(bone+6));return n>0;
}
// The units of the player's model: unit, its skeleton object, the resource, the count of bones.
struct Unit{uint32_t unit=0,skeleton=0,res=0,bones=0;};
static unsigned units(uint32_t actor,Unit out[32]){
    if(!body::readable(actor+0x330,4))return 0;
    uint32_t model=body::u32(actor+0x330);if(!body::readable(model,0x28))return 0;
    uint32_t count=body::u32(model+0x1C),array=body::u32(model+0x24);if(!count||count>32||!body::readable(array,count*4))return 0;
    for(uint32_t i=0;i<count;i++){
        out[i]={};uint32_t holder=body::u32(array+i*4);if(!body::readable(holder,4))continue;
        Unit u;u.unit=body::u32(holder);if(!body::readable(u.unit,0xA8))continue;
        u.skeleton=body::u32(u.unit+0x9C);if(!body::readable(u.skeleton,0x40)){out[i]=u;continue;}
        u.res=body::u32(u.skeleton);if(body::readable(u.res,0x20)&&!memcmp(body::base+u.res,"FSKL",4))u.bones=u16(u.res+8);else u.res=0;
        out[i]=u;
    }
    return count;
}

// The bones Steve's parts follow. Their axes when Link stands as his model was made (BetterVR's table of the
// skeleton at rest, src/hooking/skeleton.cpp; Head and Skl_Root seen so in route-304): Skl_Root has the model's own
// (x left, y up, z forward); Spine_2 and Head have x up, y forward, z left.
enum {bRoot,bSpine,bHead,bArmL,bWristL,bArmR,bWristR,bLegL,bAnkleL,bLegR,bAnkleR,bCount};
static const char* const wanted[bCount]={"Skl_Root","Spine_2","Head","Arm_1_L","Wrist_L","Arm_1_R","Wrist_R","Leg_1_L","Ankle_L","Leg_1_R","Ankle_R"};
struct Pose{float m[bCount][12],actor[12];};
// The IPC worker only. False when the player's model has no such skeleton or a matrix is not a place near the actor
// with three axes of about one unit (within 100 units of the actor); `why` then says which (hundreds: the check, the rest: the bone or the count).
static int why=0;
static bool read(Pose& out){
    static uint32_t knownRes=0,refused=0;static int index[bCount];
    uint32_t actor=body::player.load();why=100;if(!body::readable(actor+0x1F8,0x30))return false;
    for(int j=0;j<12;j++){out.actor[j]=body::f32(actor+0x1F8+j*4);if(!std::isfinite(out.actor[j]))return false;}
    Unit list[32];unsigned count=units(actor,list);why=200+int(count);
    for(unsigned i=0;i<count;i++){
        auto& u=list[i];if(!u.res||u.bones<bCount||u.bones>512||u.res==refused)continue;
        if(u.res!=knownRes){
            int found[bCount];std::fill_n(found,int(bCount),-1);int have=0;
            for(uint32_t b=0;b<u.bones;b++){char name[32];int parent;if(!resBone(u.res,u.bones,b,name,parent))continue;
                for(int k=0;k<bCount;k++)if(found[k]<0&&!strcmp(name,wanted[k])){found[k]=int(b);++have;}}
            if(have!=bCount){refused=u.res;why=300+have;continue;}
            std::copy_n(found,int(bCount),index);knownRes=u.res;
        }
        uint32_t world=body::u32(u.skeleton+0x10);
        for(int k=0;k<bCount;k++){
            if(!body::readable(world+uint32_t(index[k])*0x30,0x30)){why=400+k;return false;}   // a bone at a time: readable() takes 0x8E0 bytes at most
            float* m=out.m[k];for(int j=0;j<12;j++){m[j]=body::f32(world+uint32_t(index[k])*0x30+j*4);if(!std::isfinite(m[j])){why=500+k;return false;}}
            for(int c=0;c<3;c++){float l=m[c]*m[c]+m[4+c]*m[4+c]+m[8+c]*m[8+c];if(!(l>.25f&&l<4)){why=600+k;return false;}}
            for(int a=0;a<3;a++)if(!(std::abs(m[a*4+3]-out.actor[a*4+3])<100)){why=700+k;return false;}   // a scene may move the model away from the actor's own place
        }
        why=0;return true;
    }
    return false;
}

// Once a second while lab/bones.txt is 1: the actor's matrix, each unit's skeleton object as words, and the bones'
// names with their parents once for each resource.
static void probe(){
    static uint64_t last=0;uint64_t now=GetTickCount64();if(!probeOn.load()||now-last<1000)return;last=now;
    uint32_t actor=body::player.load();Unit list[32];unsigned count=units(actor,list);if(!count)return;
    static uint32_t named[8]{};static unsigned namedCount=0;
    {char text[400];int at=0;for(int i=0;i<12;i++){float f=body::f32(actor+0x1F8+i*4);at+=snprintf(text+at,sizeof(text)-at,"%s%.6g",i?",":"",std::isfinite(f)?f:0.f);}
        event("{\"event\":\"skeleton_actor\",\"actor\":%u,\"units\":%u,\"matrix\":[%s]}",actor,count,text);}
    for(unsigned i=0;i<count;i++){
        auto& u=list[i];if(!u.skeleton)continue;
        char words[16*9+1];int at=0;for(int w=0;w<16;w++)at+=snprintf(words+at,sizeof(words)-at,"%s%08X",w?" ":"",body::u32(u.skeleton+w*4));
        event("{\"event\":\"skeleton_raw\",\"index\":%u,\"unit\":%u,\"skeleton\":%u,\"res\":%u,\"bones\":%u,\"words\":\"%s\"}",i,u.unit,u.skeleton,u.res,u.bones,words);
        if(!u.res||!u.bones||u.bones>512)continue;
        bool known=false;for(unsigned k=0;k<namedCount;k++)known|=named[k]==u.res;
        if(known)continue;
        for(uint32_t first=0;first<u.bones;first+=24){
            char text[24*44];int n=0;text[0]=0;
            for(uint32_t b=first;b<std::min(u.bones,first+24);b++){char name[32];int parent=-1;if(!resBone(u.res,u.bones,b,name,parent))strcpy(name,"?");
                n+=snprintf(text+n,sizeof(text)-n,"%s\"%s:%d\"",b>first?",":"",name,parent);}
            event("{\"event\":\"skeleton_bones\",\"index\":%u,\"res\":%u,\"first\":%u,\"names\":[%s]}",i,u.res,first,text);
        }
        if(namedCount<8)named[namedCount++]=u.res;
    }
}
}
