// What the game's player carries in Minecraft goes with the game's saves: the bridge tells the mod when the game
// has written a save and when it has loaded one, and which (the mod's BotwSaves keeps and puts back the player's
// things under that name).
//
// Which save: the game's data has the number LastSaveTime_Lower, the time the save was written, in seconds. It is
// the same after a load of that save and is new after every save, the game's own autosaves too (route-340: a save
// from the game's menu took it from 1791466927 to 1791922868, and slot 2's files were written).
// Saved or loaded: PlayReport_PlayTime, the seconds played, goes up by one a second while the game runs
// (route-341: 23203 to 23221 in 18 s) and is part of the save. After a load it is the loaded save's: it has gone
// back, or forward by more than the time that has passed. A new game has both numbers at their start.
// Both are found by rune_items.h's search of the memory.
//
// The message is kInSave: code 1 loaded, 0 saved; a the save's time; b the number of the message; c the seconds
// played. Messages wait while Minecraft is not in control (physical.h empties its queue then), and one may be lost
// on the way, so each of the last four is sent again every two seconds for twenty seconds from its first sending;
// the mod takes each number once. Not for longer: a Minecraft started anew later in the session has its player's
// things as they were left, and old messages would put older ones back.
// lab/save-link.txt 0 switches it off.
namespace saveLink {
struct Told{int kind;uint32_t save,play;int number;uint64_t firstSent=0;};
static std::deque<Told> told;static int numbers=0;
static void tell(int kind,uint32_t save,uint32_t play){
    told.push_back({kind,save,play,++numbers});if(told.size()>4)told.pop_front();
    event("{\"event\":\"save_link\",\"kind\":\"%s\",\"save\":%u,\"play\":%u,\"number\":%d}",kind?"loaded":"saved",save,play,numbers);
}
static void poll(){
    static uint64_t checked=0,seenAt=0,sentAt=0;static uint32_t save=0,play=0,nextSave=0,nextPlay=0;static bool known=false;static int steady=0,sentNumber=0;
    auto now=GetTickCount64();if(now-checked<500)return;checked=now;
    {int on=1;std::ifstream(directory+"/save-link.txt")>>on;if(!on)return;}
    auto places=runeOwned::found.load();
    if(places&&places->times[0]&&places->times[1]&&body::identityActor.load()){
        uint32_t value[2];bool read=true;
        for(int k=0;k<2&&read;k++){
            uint8_t words[32];
            read=nativeDamage::read(places->times[k]-20,words,sizeof(words))&&nativeDamage::be(words)==runeOwned::kNumberWord&&nativeDamage::be(words+24)==runeOwned::kTimeHashes[k];
            if(read)value[k]=nativeDamage::be(words+20);
        }
        if(!read){runeOwned::found.store(nullptr);steady=0;}   // the game's data was made anew (a load): searched again
        else{
            // Twice the same save, half a second apart, before it is believed: the data is filled while the game loads.
            if(steady&&value[0]==nextSave&&value[1]>=nextPlay&&value[1]<=nextPlay+3)steady++;else steady=1;
            nextSave=value[0];nextPlay=value[1];
            if(steady>=2){
                bool jumped=!known||value[1]+1<play||value[1]>play+(now-seenAt)/1000+5;
                if(jumped)tell(1,value[0],value[1]);
                else if(value[0]!=save)tell(0,value[0],value[1]);
                known=true;save=value[0];play=value[1];seenAt=now;
            }
        }
    }
    if(told.empty()||!physicalActive.load())return;
    if(told.back().number!=sentNumber||now-sentAt>=2000){
        for(auto& t:told){
            if(!t.firstSent)t.firstSent=now;
            if(now-t.firstSent<=20000)physical::enqueue({botwcraft::proto::kInSave,uint16_t(t.kind),int32_t(t.save),t.number,int32_t(t.play)});
        }
        sentNumber=told.back().number;sentAt=now;
    }
}
}
