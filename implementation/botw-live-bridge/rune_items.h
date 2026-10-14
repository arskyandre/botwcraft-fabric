// The runes the game's player owns, for the mod to give their items (report 70: the mod gives
// the item when the game itself marks the rune as owned).
//
// Source (the decompilation, gdtFlag.h): a flag of the game's data has its value and, after it, the hash of its
// name. Observed (route-237): for IsGet_Obj_RemoteBomb, _Magnetglove, _StopTimer and _IceMaker one place in memory
// has the word 10298410, a word whose lowest bit is the value, the hash, and 0. Where the four are is found by a
// search of the memory on a thread of its own (as nativeDamage's), again when one of them no longer holds its hash.
// What is owned goes to the mod (kInRunes) when it changes and every two seconds: bit i is the rune the game
// numbers i (runes.h); the two bombs go with the one flag.
// lab/runes-owned.txt, a number: sent in place of what the game says (a save that owns every rune).
// The paraglider (report 76: Steve glides only after the Old Man has given the paraglider):
// the same kind of flag, IsGet_PlayerStole2, sent as bit 5. The runes do not wait for it: if it is not found, the
// bit stays down.
namespace runeOwned {
constexpr uint32_t kFlagWord=0x10298410;
constexpr uint32_t kHashes[]{0x191BCCF9,0x795E7BBC,0x7504085D,0x5992B256,0xFE4D1501}; // crc32 of IsGet_Obj_RemoteBomb, _Magnetglove, _StopTimer, _IceMaker; IsGet_PlayerStole2
// Two numbers of the game's data kept the same way (a flag of another kind, 32 bytes: the word 102984C8, three words, the
// greatest value, the value, the hash, 0; route-339 to 342): LastSaveTime_Lower and PlayReport_PlayTime, for save_link.h. `times` holds where
// their values are.
constexpr uint32_t kNumberWord=0x102984C8;
constexpr uint32_t kTimeHashes[]{0x0BA7CF40,0x73C29681}; // crc32 of LastSaveTime_Lower, PlayReport_PlayTime
struct Found{uint32_t at[5]{};unsigned count[5]{};uint32_t times[2]{};unsigned timeCount[2]{};uint64_t ms=0;};
static std::atomic<std::shared_ptr<const Found>> found;static std::atomic<bool> scanning{false};
static DWORD WINAPI locate(void*){
    auto result=std::make_shared<Found>();auto began=GetTickCount64();std::vector<uint8_t> chunk((1u<<20)+32);
    for(uint64_t at=0x10000000;at<0x50000000;at+=1u<<20){
        size_t n=size_t(std::min<uint64_t>(chunk.size(),0x50000000-at));
        if(!nativeDamage::read(uint32_t(at),chunk.data(),n))continue;
        for(size_t i=0;i+16<=n&&i<(1u<<20);i+=4){
            if(i+32<=n&&nativeDamage::be(&chunk[i])==kNumberWord&&!nativeDamage::be(&chunk[i+28])){
                uint32_t name=nativeDamage::be(&chunk[i+24]);
                for(int k=0;k<2;k++)if(name==kTimeHashes[k]&&!result->timeCount[k]++)result->times[k]=uint32_t(at+i+20);
                continue;
            }
            if(nativeDamage::be(&chunk[i])!=kFlagWord||nativeDamage::be(&chunk[i+12]))continue;
            uint32_t hash=nativeDamage::be(&chunk[i+8]);
            for(int k=0;k<5;k++)if(hash==kHashes[k]&&!result->count[k]++)result->at[k]=uint32_t(at+i);
        }
    }
    result->ms=GetTickCount64()-began;found.store(result);scanning=false;return 0;
}
static void poll(){
    static uint64_t checked=0,sentAt=0,nextScan=0;static int sent=-1;static const Found* told=nullptr;
    auto now=GetTickCount64();if(now-checked<500)return;checked=now;
    auto places=found.load();int mask=-1;
    if(places&&places.get()!=told){told=places.get();event("{\"event\":\"rune_flags\",\"at\":[%u,%u,%u,%u],\"count\":[%u,%u,%u,%u],\"paraglider_at\":%u,\"paraglider_count\":%u,\"times_at\":[%u,%u],\"times_count\":[%u,%u],\"ms\":%llu}",places->at[0],places->at[1],places->at[2],places->at[3],places->count[0],places->count[1],places->count[2],places->count[3],places->at[4],places->count[4],places->times[0],places->times[1],places->timeCount[0],places->timeCount[1],(unsigned long long)places->ms);}
    if(places){
        mask=0;
        for(int k=0;k<4&&mask>=0;k++){
            uint8_t words[16];uint32_t at=places->at[k];
            if(!at||!nativeDamage::read(at,words,sizeof(words))||nativeDamage::be(words)!=kFlagWord||nativeDamage::be(words+8)!=kHashes[k])mask=-1;
            else if(nativeDamage::be(words+4)&1)mask|=k?1<<(k+1):3;
        }
        if(mask>=0){uint8_t words[16];uint32_t at=places->at[4];
            if(at&&nativeDamage::read(at,words,sizeof(words))&&nativeDamage::be(words)==kFlagWord&&nativeDamage::be(words+8)==kHashes[4]&&(nativeDamage::be(words+4)&1))mask|=32;}
        if(mask<0)found.store(nullptr);
    }
    if(mask<0&&!scanning.load()&&now>=nextScan&&body::identityActor.load()){scanning=true;nextScan=now+5000;if(auto thread=CreateThread(nullptr,0,locate,nullptr,0,nullptr))CloseHandle(thread);else scanning=false;}
    int lab=-1;std::ifstream(directory+"/runes-owned.txt")>>lab;if(lab>=0)mask=lab&63;
    if(mask<0||!physicalActive.load())return;
    if(mask!=sent||now-sentAt>=2000){
        physical::enqueue({botwcraft::proto::kInRunes,0,mask,0,0});sentAt=now;
        if(mask!=sent){sent=mask;event("{\"event\":\"runes_owned\",\"mask\":%d,\"lab\":%d}",mask,int(lab>=0));}
    }
}
}
