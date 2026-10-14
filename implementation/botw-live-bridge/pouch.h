// Link's inventory, read (report 87: BOTW's inventory is the owner of its items; Minecraft will show mirrors).
//
// Source (the decompilation, uiPauseMenuDataMgr.h): the inventory is a list of PouchItem (sead::OffsetList, the
// node first). Observed (route-409, the Sheikah Slate found by lab/memfind.txt): on the Wii U a PouchItem is
//   +0x00 the next and previous nodes' places (sead::ListNode), +0x08 type (0 sword, 1 bow, 2 arrow, 3 shield,
//   4..6 armour head, upper, lower, 7 material, 8 food, 9 key item), +0x0C use, +0x10 value (the count, or a
//   weapon's durability), +0x14 equipped (byte), +0x15 in the inventory (byte), +0x18 the name (sead's
//   FixedSafeString<64>: the text's place, the class word 0x1021B524, 0x40, the 64 bytes of text).
// Found by a search of the memory on a thread of its own: a FixedSafeString<64> whose place is its own text, with a
// type 0..9 before it, whose nodes go round back to it through items and exactly one node that is not an item (the
// list's head, in the inventory's manager). Then read from the head every half second. Read only.
// What is read goes to lab/pouch.json (when it changes) and a `pouch` event (how many, a hash).
namespace pouch {
constexpr uint32_t kStringWord=0x1021B524,kMost=420;
struct Item{int type,value;bool equipped;char name[65];};
static std::atomic<uint32_t> head{0};static std::atomic<bool> scanning{false};
static bool item(uint32_t at,Item& out){
    uint8_t w[0x64];if(!nativeDamage::read(at,w,sizeof(w)))return false;
    if(nativeDamage::be(w+0x1C)!=kStringWord||nativeDamage::be(w+0x20)!=0x40||nativeDamage::be(w+0x18)!=at+0x24)return false;
    int type=int(nativeDamage::be(w+8));if(type<0||type>9)return false;
    out.type=type;out.value=int(nativeDamage::be(w+0x10));out.equipped=w[0x14]!=0;
    memcpy(out.name,w+0x24,64);out.name[64]=0;return true;
}
static uint32_t next(uint32_t node){uint8_t w[4];return nativeDamage::read(node,w,4)?nativeDamage::be(w):0;}
// From an item: round the list; the one node that is not an item is the head.
static uint32_t headFrom(uint32_t start){
    uint32_t at=start,found=0;Item it;
    for(unsigned k=0;k<=kMost+1;k++){
        at=next(at);if(!at||at&3)return 0;
        if(at==start)return found;
        if(!item(at,it)){if(found)return 0;found=at;}
    }
    return 0;
}
static DWORD WINAPI locate(void*){
    std::vector<uint8_t> chunk((1u<<20)+0x40);uint32_t result=0;
    for(uint64_t at=0x10000000;at<0x50000000&&!result;at+=1u<<20){
        size_t n=size_t(std::min<uint64_t>(chunk.size(),0x50000000-at));
        if(!nativeDamage::read(uint32_t(at),chunk.data(),n))continue;
        for(size_t i=0x18;i+12<=n&&i<(1u<<20)&&!result;i+=4){
            if(nativeDamage::be(&chunk[i+4])!=kStringWord||nativeDamage::be(&chunk[i+8])!=0x40)continue;
            uint32_t base=uint32_t(at+i)-0x18;Item it;
            if(nativeDamage::be(&chunk[i])!=base+0x24||!item(base,it)||!it.name[0])continue;
            result=headFrom(base);
        }
    }
    head=result;scanning=false;return 0;
}
static void poll(){
    static uint64_t checked=0,nextScan=0;static uint32_t told=~0u;
    auto now=GetTickCount64();if(now-checked<500)return;checked=now;
    uint32_t h=head.load();std::vector<Item> items;bool ok=h!=0;
    if(ok){
        uint32_t at=h;Item it;
        for(unsigned k=0;k<=kMost;k++){
            at=next(at);if(!at){ok=false;break;}
            if(at==h)break;
            if(!item(at,it)){ok=false;break;}
            items.push_back(it);
        }
        if(items.size()>kMost)ok=false;
    }
    if(!ok){
        if(h)head=0;
        if(!scanning.load()&&now>=nextScan&&body::identityActor.load()){scanning=true;nextScan=now+5000;if(auto thread=CreateThread(nullptr,0,locate,nullptr,0,nullptr))CloseHandle(thread);else scanning=false;}
        return;
    }
    uint32_t hash=2166136261u;
    for(auto& it:items){for(const char* c=it.name;*c;c++)hash=(hash^uint8_t(*c))*16777619u;hash=(hash^uint32_t(it.value))*16777619u;hash=(hash^uint32_t(it.type*2+it.equipped))*16777619u;}
    if(hash==told)return;told=hash;
    if(FILE* f=fopen((directory+"/pouch.json").c_str(),"w")){
        fprintf(f,"{\"head\":%u,\"items\":[",h);
        for(size_t k=0;k<items.size();k++)fprintf(f,"%s\n{\"type\":%d,\"name\":\"%s\",\"value\":%d,\"equipped\":%d}",k?",":"",items[k].type,items[k].name,items[k].value,int(items[k].equipped));
        fputs("]}\n",f);fclose(f);
    }
    event("{\"event\":\"pouch\",\"head\":%u,\"items\":%zu,\"hash\":%u}",h,items.size(),hash);
}
}
