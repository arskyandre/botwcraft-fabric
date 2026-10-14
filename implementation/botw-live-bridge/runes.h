// BOTW's runes worked from Minecraft's rune items (reports 68 and 69: the runes are
// Minecraft items, and holding one, for example the Magnesis item, turns that rune on).
//
// The mod tells which rune item is in the player's main hand (McState.heldRune: the game's number of the rune plus
// one). At every pad sample while the keyboard and mouse are Minecraft's, step() makes the game's rune manager
// agree with it and gives the pad what the mouse did:
//   - the manager (BetterVR, cemu_hooks.h: *10463798) has its flags at +4C (10: a rune is out; 2 and not 10
//     while Magnesis holds an object, route-235)
//     and the chosen rune at +1D4; the rune wanted next is +1D0, and the manager's own update (02A2DCA0) makes it
//     the chosen one, ends the rune that is on and keeps the choice (route-233). Written as the chosen itself the
//     old one was back within a tenth of a second (route-232).
//   - Magnesis, Stasis, Cryonis: held is on. L is pressed until the flag is up, again when the game ended the rune
//     by itself, and B when the item is put away. The right button is A; the left is B while Magnesis holds an
//     object and nothing otherwise (B would end the rune, which the hand would begin again).
//   - the bombs: the right button is L (a bomb, and its blast), the left is R (the throw).
//   - Magnesis with an object held: the wheel is the cross's up and down, some time a notch (the
//     hotbar's place does not change), and the right stick follows the object as Steve's eye sees it. What Magnesis
//     holds (routes 228 and 229): the object at *10467BE0 has a link to it at +88 (its data, its id; the data has
//     the id at +3C and the object at +40). Its middle is the middle of its box (+34C) through its matrix (+1F8).
//     Left and right the stick turns the game's camera and the object with it; up and down it moves the object.
// lab/runes.txt 0: none of this. lab/look-stick.txt: the stick follows Steve's look in play too (report 68).
namespace runes {
constexpr uint32_t kManager=0x10463798,kHolder=0x10467BE0;
constexpr uint32_t kA=0x8000u,kB=0x4000u,kL=0x20u,kR=0x10u,kCrossUp=0x200u,kCrossDown=0x100u,kAiming=0x10u;
constexpr float kLeast=.2f,kNothing=.17f;   // the least push that moves a held object, and under what nothing moves (route-244)
constexpr int kNotchMs=75,kMostWheelMs=1500;   // a notch of the wheel as time on the cross (150: 1.3 blocks a notch, route-236)
// The follow of what Magnesis holds (step(), below), and lab/rune-follow.txt for trying it: what is logged (1: every sample;
// 2: and no follow), the degrees a second a full push turns the object, the metres a second it lifts it, the degrees that
// are near enough, the degrees that are off again, the milliseconds a correction is spread over, the part of a move that
// the object swings past it.
static std::atomic<int> followLog{0};
static std::atomic<float> followTurn{70},followRise{15.9f},followRest{.3f},followWake{.6f},followSpread{150},followSwing{.15f};
// lab/rune-stick.txt: a number, the stick left-right and up-down, milliseconds of it, milliseconds of none after it.
static std::atomic<float> labStick[2]{};static std::atomic<uint64_t> labStickUntil{0},labStickQuiet{0};
static std::atomic<int> enabled{1};
static std::atomic<uint32_t> item{0},buttons{0},labHand{0};   // labHand: lab/rune-hand.txt, the rune in the hand whatever Minecraft holds (1-5; 6: none, with a rune item in the hand of the player's hotbar; the hotbar is theirs)
static std::atomic<bool> holding{false};
static std::atomic<int> wheel{0};
struct Pad{uint32_t bits=0;float rx=0,ry=0;bool stick=false;};

// The mouse, from physical.h. True: the bridge's, not Minecraft's.
static bool mouse(uint16_t code,bool down,bool screen){
    uint32_t bit=code==3?kA:code==1?kB:0;if(!bit)return false;
    if(!down){if(buttons.load()&bit){buttons&=~bit;return true;}return false;}
    if(screen||!enabled.load()||!item.load())return false;
    buttons|=bit;return true;
}
static bool scroll(int delta,bool screen){
    if(screen||!enabled.load()||!holding.load())return false;
    wheel+=delta;return true;
}
// Keys that would change what is in the hand while an object is held: the hotbar's numbers, drop, the other hand.
static bool key(UINT vk,bool down,bool screen){return down&&!screen&&enabled.load()&&holding.load()&&((vk>='1'&&vk<='9')||vk=='Q'||vk=='F');}
static void rest(){holding=false;wheel=0;}

static bool held(float* middle){
    auto real32=[](uint32_t p){uint32_t v=body::u32(p);float x;memcpy(&x,&v,4);return x;};
    if(!body::readable(kHolder,4))return false;
    uint32_t holder=body::u32(kHolder),data=0,id=0xFFFFFFFFu,proc=0;
    if(holder>=0x10000000&&body::readable(holder+0x88,8)){data=body::u32(holder+0x88);id=body::u32(holder+0x8C);}
    if(id!=0xFFFFFFFFu&&data>=0x10000000&&body::readable(data+0x3C,8)&&body::u32(data+0x3C)==id)proc=body::u32(data+0x40);
    if(proc<0x10000000||!body::readable(proc+0x1F8,0x30)||!body::readable(proc+0x34C,0x18))return false;
    float local[3];for(int i=0;i<3;i++)local[i]=(real32(proc+0x34C+i*4)+real32(proc+0x358+i*4))*.5f;
    for(int i=0;i<3;i++){middle[i]=real32(proc+0x1F8+i*16+12);for(int k=0;k<3;k++)middle[i]+=real32(proc+0x1F8+i*16+k*4)*local[k];if(!std::isfinite(middle[i]))return false;}
    return true;
}

// One pad sample with the keyboard and mouse Minecraft's. inHand: McState.heldRune.
static Pad step(uint32_t inHand){
    Pad out;static uint64_t before=0,pressUntil=0,nextTry=0;static uint32_t pressing=0,told[5]{~0u,~0u,~0u,~0u,~0u};static bool ours=false;static int owed=0;
    uint64_t now=GetTickCount64();int passed=before?int(std::min<uint64_t>(now-before,100)):0;before=now;
    if(!enabled.load())inHand=0;
    if(inHand>5)inHand=0;
    if(uint32_t lab=labHand.load())inHand=lab==6?0:lab;
    item=inHand;
    uint32_t manager=0,flags=0,chosen=0xFFFFFFFFu;bool known=false;
    if(body::readable(kManager,4)&&(manager=body::u32(kManager))>=0x10000000&&body::readable(manager+0x4C,4)&&body::readable(manager+0x1D0,8)){flags=body::u32(manager+0x4C);chosen=body::u32(manager+0x1D4);known=true;}
    int lab=runeSelect.exchange(-1);if(known&&lab>=0)put32(manager+0x1D0,uint32_t(lab));
    // With an object held the flags are 2 and the aiming bit is down (route-235): the link is what tells a hold.
    bool aim=inHand>=3,bomb=inHand==1||inHand==2;
    float middle[3]{};bool has=known&&chosen==2&&held(middle),on=(flags&kAiming)||has;
    if(known&&enabled.load()){
        if(inHand&&chosen!=inHand-1){put32(manager+0x1D0,inHand-1);nextTry=std::max(nextTry,now+150);}
        else if(aim&&!on&&!pressing&&now>=nextTry){pressing=kL;pressUntil=now+100;nextTry=now+700;ours=true;}
        else if(!aim&&on&&ours&&!pressing&&now>=nextTry){pressing=kB;pressUntil=now+100;nextTry=now+700;}
        if(!on&&!aim)ours=false;
    }
    if(pressing){if(now<pressUntil)out.bits|=pressing;else pressing=0;}
    uint32_t mouseHeld=buttons.load();
    if(aim){if(mouseHeld&kA)out.bits|=kA;if((mouseHeld&kB)&&has)out.bits|=kB;}
    if(bomb){if(mouseHeld&kA)out.bits|=kL;if(mouseHeld&kB)out.bits|=kR;}
    // The wheel: what it asked for is paid out as time on the cross.
    owed=std::clamp(owed+wheel.exchange(0)*kNotchMs/120,-kMostWheelMs,kMostWheelMs);
    if(!has)owed=0;
    else if(owed>0){out.bits|=kCrossUp;owed=std::max(0,owed-passed);}
    else if(owed<0){out.bits|=kCrossDown;owed=std::min(0,owed+passed);}
    holding=has;
    // The right stick.
    bool follow=lookStick.load()!=0;
    if((has||follow)&&GetTickCount64()-gameViewTick.load()<250){
        uint64_t angles=physicalAngles.load();uint32_t ay=uint32_t(angles),ap=uint32_t(angles>>32);float yaw,pitch;memcpy(&yaw,&ay,4);memcpy(&pitch,&ap,4);
        float gy=gameYaw.load(),gp=gamePitch.load(),camera[2]{gy,gp},up=float(lookStickUp.load()),full=std::max(1.f,lookStickFull.load());bool object=false;float reach=1;
        if(has){
            float dx=middle[0]-steveEye[0].load(),dy=middle[1]-steveEye[1].load(),dz=middle[2]-steveEye[2].load(),flat=std::sqrt(dx*dx+dz*dz);
            if(flat>.3f){gy=std::atan2(-dx,dz)*57.2957795f;gp=-std::atan2(dy,flat)*57.2957795f;up=-1;object=true;reach=std::max(1.f,std::sqrt(flat*flat+dy*dy));}
        }
        if(object||follow){
            float missing[2]{std::remainder(yaw-gy,360.f),(pitch-gp)*up},turn=0,tilt=0,coming[2]{};
            if(!object){turn=std::clamp(missing[0]/full,-1.f,1.f);tilt=std::clamp(missing[1]/full,-1.f,1.f);}
            else{
                // What Magnesis holds answers the stick late. Measured with pushes of the stick alone (route-244, an iron
                // box 9 m off): nothing under a sixth of a push; above it the object turns by about 70 degrees a second
                // times the push's square, and rises by about 16 m a second times it; nothing of a push shows for a
                // tenth of a second, all of it after half a second, and then the object swings past by up to a fifth
                // and comes back within a second. Pushed for as long as it was off the crosshair it went past it and
                // back for as long as it was held (a play session, route-238: the object
                // went up and down without end). So every push is kept with its time, what of them is still to come is taken
                // off what is missing, and the push is for the rest only. Once that is near enough the stick rests
                // until it is clearly off again.
                struct Push{double at;float turn,rise;};static Push past[96]{};static int next=0;static bool resting[2]{};static double before=0,perSecond=0;
                if(perSecond==0){LARGE_INTEGER f;QueryPerformanceFrequency(&f);perSecond=double(f.QuadPart);}
                LARGE_INTEGER q;QueryPerformanceCounter(&q);double clock=double(q.QuadPart)/perSecond,dt=std::clamp(clock-before,.005,.1);
                if(clock-before>.5){for(auto& p:past)p={};resting[0]=resting[1]=false;}
                before=clock;
                double swing=followSwing.load();float rest=followRest.load(),wake=followWake.load(),spread=std::max(.03f,followSpread.load()/1000.f);
                auto left=[&](double age){return age<.1?1.:age<.45?(.45-age)/.35:age<.7?-(age-.45)/.25*swing:age<1.1?-(1.1-age)/.4*swing:0.;};
                for(const auto& p:past){if(p.at==0)continue;double part=left(clock-p.at);coming[0]+=float(p.turn*part);coming[1]+=float(p.rise*part)/reach*57.2957795f;}
                float most[2]{followTurn.load(),followRise.load()/reach*57.2957795f},stick[2]{};   // degrees a second at a full push
                // Lab: a push of the stick's own for a time and then none for a time (lab/rune-stick.txt), or no follow at all.
                bool own=now<labStickQuiet.load()||followLog.load()==2;
                if(now<labStickUntil.load()){stick[0]=labStick[0].load();stick[1]=labStick[1].load();}
                else if(!own)for(int a=0;a<2;a++){
                    float ahead=missing[a]-coming[a],size=std::abs(ahead);
                    if(size<(resting[a]?wake:rest)){resting[a]=true;continue;}
                    resting[a]=false;float push=std::max(kLeast,std::sqrt(std::min(1.f,size/spread/most[a])));stick[a]=ahead<0?-push:push;
                }
                Push made{clock,0,0};for(int a=0;a<2;a++){float size=std::abs(stick[a]),moved=size<kNothing?0:most[a]*size*size*float(dt)*(stick[a]<0?-1:1);(a?made.rise:made.turn)=a?moved*reach/57.2957795f:moved;}
                if(made.turn!=0||made.rise!=0){past[next]=made;next=(next+1)%96;}
                turn=stick[0];tilt=stick[1];
            }
            if(std::isfinite(turn)&&std::isfinite(tilt)){out.rx=turn;out.ry=tilt;out.stick=true;}
            static uint64_t said=0;if(now-said>=250||(object&&followLog.load())){said=now;event("{\"event\":\"%s\",\"ms\":%llu,\"steve\":[%.2f,%.2f],\"game\":[%.2f,%.2f],\"stick\":[%.3f,%.3f],\"coming\":[%.2f,%.2f],\"holding\":%d,\"middle\":[%.3f,%.3f,%.3f],\"game_view\":[%.2f,%.2f]}",object&&followLog.load()?"rune_follow":"look_stick",(unsigned long long)now,yaw,pitch,gy,gp,turn,tilt,coming[0],coming[1],int(object),middle[0],middle[1],middle[2],camera[0],camera[1]);}
        }
    }
    uint32_t state[5]{inHand,known?flags:~0u,chosen,uint32_t(has),out.bits&(kL|kB)};
    if(memcmp(state,told,sizeof(state))||lab>=0){memcpy(told,state,sizeof(state));event("{\"event\":\"rune_state\",\"in_hand\":%u,\"known\":%d,\"flags\":%u,\"chosen\":%d,\"holding\":%d,\"pressing\":%u,\"ours\":%d,\"written\":%d}",inHand,int(known),flags,int(chosen),int(has),out.bits&(kL|kB),int(ours),lab);}
    return out;
}
}
