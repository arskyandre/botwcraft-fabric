// Event-driven Windows capture into the retained v11 InputBridge. No polling look clock.
#pragma once
#include <deque>
namespace physical {
using namespace botwcraft::proto;
static std::atomic<float> sensitivity{.5f};
static std::atomic<bool> ready{false},gui{false},toggle{false},releaseRequested{false},nativeMenu{false};
// What asked for the release (the trace said "requested-release" for all four up to route-88),
// and for a release decided in poll(): 1 not fresh, 2 window not in front, 4 body epoch, 8 target
// epoch, 16 blocked; when not fresh also 32 no peer, 64 peer fault, 128 native not ready,
// 256 Minecraft's state is old, 512 Minecraft not in a world.
static std::atomic<const char*> releaseWhy{"requested-release"};
static std::atomic<int> stopDetail{0};
static void requestRelease(const char* why){releaseWhy=why;releaseRequested=true;}
static std::atomic<uint64_t> epoch{0},target{0};
static auto& seed=physicalSeed;
static std::mutex mutex;
static std::deque<InputEvent> pending;
static std::atomic<int> cursorX{640},cursorY{360};static std::atomic<uint32_t> width{1280},height{720};
static POINT lastMouse{};static std::atomic<bool> mouseKnown{false};static bool wasGui=false;
static HHOOK keyHook=nullptr,mouseHook=nullptr;static DWORD threadId=0;
// The player wants Minecraft control: set by a claim, cleared only by the
// F8 toggle. While it is set the bridge claims again by itself as soon as the
// native session is ready and the owned window is in front (after a native
// screen or a reload) instead of waiting for a key or a runner.
static std::atomic<bool> wanted{false};
// Where the game's picture is on the screen (poll()): the window's client area, and the picture in it at the
// overlay's proportions. A screen of Minecraft's takes the system's own pointer as its pointer through this.
static std::atomic<int> clientX{0},clientY{0},clientW{0},clientH{0};
static std::atomic<uint64_t> textReset{0};
// Movement keys as the hook last saw them (protocol scan codes): W A S D,
// space, left control, left shift.
constexpr uint16_t kMoveKeys[]{26,4,22,7,44,224,225};
static std::atomic<bool> moveHeld[std::size(kMoveKeys)]{};
// The game's own buttons from the keyboard (G is the Wii U's A, Backspace its B; BOTW picks
// up, talks and opens with A). While Minecraft has no screen open such a key is not sent to Minecraft: it is held in
// padHeld. A key that went down while a screen of Minecraft's was open (a G typed in the chat) is Minecraft's until
// it is up, and one that went down as a button is the game's until it is up.
static uint32_t padButton(UINT vk){return vk=='G'?0x8000u:vk==VK_BACK?0x4000u:0;}
// A screen of the game's own (a notice of a thing taken, a dialogue, a menu) takes control from Minecraft for as long
// as it is up (route-174: G took an apple, the game showed its notice, and no key could send it away). The player
// still wants control, so the keyboard stays with the bridge: the buttons above go on as the game's, and the arrow
// keys are the pad's cross, for a dialogue's choices and a menu's places. Nothing goes to Minecraft meanwhile.
// The menu keys (report 62): Q and E are L and R (the page), Z and C the right stick
// left and right (the kind of thing in the inventory; the pad's own bits for a stick pushed that way), X is X, Y is
// Y, Tab is Plus and M is Minus (the map). Like the arrow keys they are the game's only while a screen or an event
// of its own is up; in play they are Minecraft's.
static uint32_t screenButton(UINT vk){switch(vk){case VK_UP:return 0x200u;case VK_DOWN:return 0x100u;case VK_LEFT:return 0x800u;case VK_RIGHT:return 0x400u;
    case 'Q':return 0x20u;case 'E':return 0x10u;case 'X':return 0x2000u;case 'Y':return 0x1000u;case VK_TAB:return 0x8u;case 'M':return 0x4u;
    case 'Z':return kRightStickLeft;case 'C':return kRightStickRight;default:return padButton(vk);}}
static BYTE* textKeys(){static BYTE held[256]{};static uint64_t seen=0;auto current=textReset.load();if(current!=seen){memset(held,0,sizeof(held));seen=current;}return held;}
static HWND frontWindow(){auto w=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(w,&pid);return pid==GetCurrentProcessId()?w:nullptr;}
// Lab, for automatic runs beside a human at work (lab/background.txt holds 1 when the session starts; 2 is
// the secondary display with the operating system's input, which is nothing to this file): the
// runner's input comes over a pipe as the hooks would have seen it, the hooks take nothing, the cursor is
// left alone, and the session's window counts as in front wherever it is. A play session does not set it.
static std::atomic<bool> background{false};
static HWND sessionWindow(){
    static HWND found=nullptr;if(found&&IsWindow(found)&&IsWindowVisible(found))return found;
    struct Best{HWND window;long area;}best{nullptr,0};
    EnumWindows([](HWND w,LPARAM l)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);
        if(pid!=GetCurrentProcessId()||!IsWindowVisible(w)||GetWindow(w,GW_OWNER))return TRUE;
        RECT r;GetWindowRect(w,&r);long area=(r.right-r.left)*(r.bottom-r.top);auto& b=*reinterpret_cast<Best*>(l);if(area>b.area)b={w,area};return TRUE;},LPARAM(&best));
    return found=best.window;
}
static HWND ownedWindow(){return background.load()?sessionWindow():frontWindow();}
static uint16_t scan(UINT vk,DWORD flags){
    if(vk>='A'&&vk<='Z')return uint16_t(4+vk-'A');
    if(vk>='1'&&vk<='9')return uint16_t(30+vk-'1');
    if(vk=='0')return 39;
    switch(vk){case VK_RETURN:return flags&LLKHF_EXTENDED?88:40;case VK_ESCAPE:return 41;case VK_BACK:return 42;case VK_TAB:return 43;case VK_SPACE:return 44;
    case VK_LCONTROL:return 224;case VK_RCONTROL:return 228;case VK_LSHIFT:return 225;case VK_RSHIFT:return 229;case VK_LMENU:return 226;case VK_RMENU:return 230;
    case VK_LEFT:return 80;case VK_RIGHT:return 79;case VK_UP:return 82;case VK_DOWN:return 81;case VK_DELETE:return 76;case VK_HOME:return 74;case VK_END:return 77;
    case VK_OEM_MINUS:return 45;case VK_OEM_PLUS:return 46;case VK_OEM_4:return 47;case VK_OEM_6:return 48;case VK_OEM_5:return 49;case VK_OEM_1:return 51;case VK_OEM_7:return 52;case VK_OEM_3:return 53;case VK_OEM_COMMA:return 54;case VK_OEM_PERIOD:return 55;case VK_OEM_2:return 56;
    default:return vk>=VK_F1&&vk<=VK_F12?uint16_t(58+vk-VK_F1):0;}
}
static uint64_t pack(float y,float p){uint32_t a,b;memcpy(&a,&y,4);memcpy(&b,&p,4);return (uint64_t(b)<<32)|a;}
static void unpack(uint64_t value,float& y,float& p){uint32_t a=uint32_t(value),b=value>>32;memcpy(&y,&a,4);memcpy(&p,&b,4);}
static float factor(float value){float s=value*.6f+.2f;return s*s*s*8.f*.15f;}
static void enqueue(InputEvent e){std::lock_guard lock(mutex);if(pending.size()>=4095){requestRelease("input-queue-full");return;}pending.push_back(e);}
static bool capture(){return physicalActive.load()&&ownedWindow()&&ready.load()&&!physicalBlocked.load();}
static bool gamesScreen(){return wanted.load()&&!physicalActive.load()&&ownedWindow();}
static bool screenKey(UINT vk,bool down){uint32_t button=screenButton(vk);if(!button)return false;if(down)padHeld|=button;else padHeld&=~button;return true;}
// One key or one mouse movement, from the hooks or from the runner's pipe.
static void keyEvent(UINT vk,UINT scanCode,DWORD flags,bool down){
    if(vk==VK_PACKET){if(down&&gui.load())enqueue({kInText,0,int(scanCode),0,0});return;}
    if(uint32_t button=screenButton(vk)){
        // A key let go is the game's if it went down as the game's, whatever has changed since.
        bool ours=padHeld.load()&button;
        if(down?(ours||gameEvent.load()||(padButton(vk)&&!gui.load())):ours){if(down)padHeld|=button;else padHeld&=~button;return;}
    }
    // An event of the game's (a talk, a scene): the game holds its player still, so Steve stands too (after
    // report 61 he could still move in a dialogue). Nothing goes to Minecraft; its held
    // keys were let go when the event began (poll()). The buttons above and the arrow keys are the game's.
    // H is the horse whistle: the cross's down
    // in play (report 68), the game's while Minecraft has no screen open and until it is let go.
    if(vk=='H'){if(!down){if(whistleHeld.exchange(false))return;}else if(!gui.load()&&!gameEvent.load()){whistleHeld=true;return;}}
    if(gameEvent.load())return;
    if(runes::key(vk,down,gui.load()))return;   // an object held with Magnesis: what is in the hand stays there
    if(auto sc=scan(vk,flags)){enqueue({kInKey,sc,down?1:0,0,0});for(size_t i=0;i<std::size(kMoveKeys);i++)if(kMoveKeys[i]==sc)moveHeld[i]=down;}
    auto held=textKeys();if(vk<256)held[vk]=down?0x80:0;
    held[VK_SHIFT]=held[VK_LSHIFT]|held[VK_RSHIFT];held[VK_CONTROL]=held[VK_LCONTROL]|held[VK_RCONTROL];
    if(down&&gui.load()&&!held[VK_CONTROL]){WCHAR text[8];int count=ToUnicodeEx(vk,scanCode,held,text,8,4,GetKeyboardLayout(0));for(int i=0;i<count;i++)if(text[i]>=32)enqueue({kInText,0,text[i],0,0});}
}
// The mouse's buttons and wheel: the game's while a rune item is in Minecraft's hand (runes.h), else Minecraft's.
static void mouseButton(uint16_t code,bool down){if(runes::mouse(code,down,gui.load()))return;if(!gameEvent.load())enqueue({kInMouseButton,code,down?1:0,0,0});}
static void scrolled(int delta){if(gameEvent.load())return;if(runes::scroll(delta,gui.load()))return;enqueue({kInScroll,0,delta,0,0});}
static void moved(int dx,int dy){
    if(gui.load()){
        cursorX=std::clamp(cursorX+dx,0,int(width)-1);cursorY=std::clamp(cursorY+dy,0,int(height)-1);enqueue({kInCursor,0,cursorX,cursorY,0});
    }else if(dx||dy){
        float previousYaw,previousPitch;unpack(physicalAngles.load(),previousYaw,previousPitch);
        float f=factor(sensitivity.load());float y=std::fmod(previousYaw+dx*f,360.f),p=std::clamp(previousPitch+dy*f,-90.f,90.f);
        physicalAngles=pack(y,p);physicalYaw=y;physicalPitch=p;LARGE_INTEGER q;QueryPerformanceCounter(&q);
        event("{\"event\":\"physical_look\",\"qpc\":%llu,\"dx\":%d,\"dy\":%d,\"sensitivity\":%.9g,\"yaw\":%.9g,\"pitch\":%.9g}",(uint64_t)q.QuadPart,dx,dy,sensitivity.load(),y,p);
    }
}
// The runner's pipe: records of {type, code, a, b}. 1 key (code a virtual key, a down), 2 a character of
// text (code), 3 mouse movement (a, b), 4 mouse button (code 1 to 5, a down), 5 wheel (a).
struct Injected{uint16_t type,code;int32_t a,b;};
static DWORD WINAPI injected(void*){
    wchar_t name[64];swprintf(name,64,L"\\\\.\\pipe\\botw-bridge-input-%lu",GetCurrentProcessId());uint64_t taken=0;
    for(;;){
        HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_INBOUND,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT,1,0,4096,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE){event("{\"event\":\"input_pipe\",\"state\":\"failed\",\"error\":%lu}",GetLastError());return 0;}
        if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){
            event("{\"event\":\"input_pipe\",\"state\":\"connected\"}");Injected e;DWORD got=0;
            while(ReadFile(pipe,&e,sizeof(e),&got,nullptr)&&got==sizeof(e)){
                ++taken;bool down=e.a!=0;
                if(e.type==1&&(e.code==VK_F8||e.code==VK_F7)){if(down){if(e.code==VK_F7){requestRelease("F7-native-menu");nativeMenu=true;}else toggle=true;}continue;}
                if(!capture()){if(e.type==1&&gamesScreen())screenKey(e.code,down);continue;}
                switch(e.type){
                case 1:keyEvent(e.code,MapVirtualKeyW(e.code,MAPVK_VK_TO_VSC),0,down);break;
                case 2:if(down&&gui.load())enqueue({kInText,0,int(e.code),0,0});break;
                case 3:moved(e.a,e.b);break;
                case 4:mouseButton(e.code,down);break;
                case 5:scrolled(e.a);break;}
            }
            event("{\"event\":\"input_pipe\",\"state\":\"closed\",\"taken\":%llu}",taken);
        }
        CloseHandle(pipe);
    }
}
static LRESULT CALLBACK keyboard(int n,WPARAM w,LPARAM l){
    if(n!=HC_ACTION||background.load())return CallNextHookEx(keyHook,n,w,l);
    auto& k=*reinterpret_cast<KBDLLHOOKSTRUCT*>(l);bool down=w==WM_KEYDOWN||w==WM_SYSKEYDOWN;
    if(k.vkCode==VK_F8||k.vkCode==VK_F7){
        static bool held[2]{};auto i=k.vkCode==VK_F7?1:0;
        // A captured toggle's key-up may arrive after focus has changed.
        if(!down)held[i]=false;
        if(ownedWindow()){if(down&&!held[i]){if(i){requestRelease("F7-native-menu");nativeMenu=true;}else toggle=true;}held[i]=down;return 1;}
    }
    // The Windows keys are the system's (they could not be pressed at all before). The Start
    // menu takes the focus, and Minecraft's keys are let go for as long as the window is not in front (poll()).
    if(k.vkCode==VK_LWIN||k.vkCode==VK_RWIN)return CallNextHookEx(keyHook,n,w,l);
    // A key that went down as the system's is the system's until it is up.
    static bool systems[256]{};
    if(k.vkCode<256&&systems[k.vkCode]){if(!down)systems[k.vkCode]=false;return CallNextHookEx(keyHook,n,w,l);}
    if(!capture()){
        if(gamesScreen()&&screenButton(k.vkCode)){screenKey(k.vkCode,down);return 1;}
        return CallNextHookEx(keyHook,n,w,l);
    }
    // Alt does not end control. It is a key of Minecraft's like any other and stays the
    // system's too. The window's menu bar, which Alt let go would open, is kept shut (window()).
    bool alt=k.vkCode==VK_LMENU||k.vkCode==VK_RMENU;
    if(!alt&&(k.flags&LLKHF_ALTDOWN)&&(k.vkCode==VK_TAB||k.vkCode==VK_ESCAPE||k.vkCode==VK_F4))return CallNextHookEx(keyHook,n,w,l);
    // Shortcuts are the system's (after report 61, Alt+F1, Alt+Z, Alt+Enter and Win+Shift+S could not be
    // pressed). With a Windows key held every key is the system's alone; with the left Alt held every key but
    // Minecraft's movement keys (kMoveKeys). The right Alt is left out: on many keyboards it writes characters.
    if(down&&!alt&&k.vkCode<256){
        bool win=(GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000,leftAlt=(GetAsyncKeyState(VK_LMENU)&0x8000)&&!(GetAsyncKeyState(VK_RMENU)&0x8000),move=false;
        if(leftAlt){auto sc=scan(k.vkCode,k.flags);for(auto key:kMoveKeys)move|=key==sc;}
        if(win||(leftAlt&&!move)){systems[k.vkCode]=true;return CallNextHookEx(keyHook,n,w,l);}
    }
    // F5 goes to Minecraft like any other key: the bridge follows its third person views (physicalFrame).
    keyEvent(k.vkCode,k.scanCode,k.flags,down);
    return alt?CallNextHookEx(keyHook,n,w,l):1;
}
static LRESULT CALLBACK mouse(int n,WPARAM w,LPARAM l){
    if(n!=HC_ACTION||background.load()||!capture())return CallNextHookEx(mouseHook,n,w,l);
    auto& m=*reinterpret_cast<MSLLHOOKSTRUCT*>(l);
    if(w==WM_MOUSEMOVE){
        if(gui.load()){
            // A screen of Minecraft's (the inventory): the system's pointer is shown and moves, and Minecraft's pointer
            // is where it is on the game's picture (it had stayed in the middle of the window).
            int cw=clientW.load(),ch=clientH.load(),ow=int(width.load()),oh=int(height.load());
            if(cw>0&&ch>0&&ow>0&&oh>0){
                float s=std::min(float(cw)/ow,float(ch)/oh),left=clientX.load()+(cw-ow*s)/2,top=clientY.load()+(ch-oh*s)/2;
                int x=std::clamp(int((m.pt.x-left)/s),0,ow-1),y=std::clamp(int((m.pt.y-top)/s),0,oh-1);
                if(x!=cursorX.load()||y!=cursorY.load()){cursorX=x;cursorY=y;enqueue({kInCursor,0,x,y,0});}
            }
            mouseKnown=false;return CallNextHookEx(mouseHook,n,w,l);
        }
        if(!mouseKnown){GetCursorPos(&lastMouse);mouseKnown=true;}
        int dx=m.pt.x-lastMouse.x,dy=m.pt.y-lastMouse.y;lastMouse=m.pt;moved(dx,dy);
        // Hook suppresses the OS movement. Keeping lastMouse at the actual cursor permits unbounded relative look.
        GetCursorPos(&lastMouse);return 1;
    }
    int code=0,down=0;
    switch(w){case WM_LBUTTONDOWN:code=1;down=1;break;case WM_LBUTTONUP:code=1;break;case WM_RBUTTONDOWN:code=3;down=1;break;case WM_RBUTTONUP:code=3;break;case WM_MBUTTONDOWN:code=2;down=1;break;case WM_MBUTTONUP:code=2;break;
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:code=HIWORD(m.mouseData)==XBUTTON1?4:5;down=w==WM_XBUTTONDOWN;break;
    case WM_MOUSEWHEEL:scrolled(SHORT(HIWORD(m.mouseData)));return 1;default:break;}
    if(code)mouseButton(uint16_t(code),down!=0);
    return 1;
}
// The session's window, with a procedure of the bridge's before its own. The pointer is hidden and shown on the
// window's own thread (ShowCursor counts for a thread), asked for by a message. While the keyboard is Minecraft's,
// Alt let go does not open the menu bar (SC_KEYMENU).
// Cemu shows the pointer again by itself at every mouse message (MainWindow::OnMouseMove, by the same count), which
// is why one ShowCursor(FALSE) did not hide it (after report 61): the count is taken from what is seen
// (GetCursorInfo), as Cemu's own MainWindow::ShowCursor does, and pointer() asks again while it is seen.
constexpr UINT kPointerMessage=WM_APP+0x3B7;
static WNDPROC windowsOwn=nullptr;static HWND hooked=nullptr;
static bool pointerSeen(){CURSORINFO i{};i.cbSize=sizeof(i);return GetCursorInfo(&i)&&(i.flags&CURSOR_SHOWING);}
static LRESULT CALLBACK window(HWND w,UINT m,WPARAM a,LPARAM l){
    if(m==kPointerMessage){
        bool hide=a!=0,seen=pointerSeen();
        if(hide&&seen){for(int i=0;i<64&&ShowCursor(FALSE)>=0;i++);}else if(!hide&&!seen){for(int i=0;i<64&&ShowCursor(TRUE)<0;i++);}
        return 0;
    }
    if(m==WM_SYSCOMMAND&&(a&0xFFF0)==SC_KEYMENU&&physicalActive.load())return 0;
    return CallWindowProcW(windowsOwn,w,m,a,l);
}
static void pointerHidden(bool hide){
    if(!hooked){
        HWND w=ownedWindow();if(!w)return;
        windowsOwn=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(w,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window)));
        if(!windowsOwn){event("{\"event\":\"physical_pointer\",\"state\":\"window-not-taken\",\"error\":%lu}",GetLastError());return;}
        hooked=w;
    }
    PostMessageW(hooked,kPointerMessage,hide?1:0,0);
}
// Whose the pointer is: the system's (no control, or the window not in front), hidden and kept in the window while
// Steve looks about, or shown and kept in the window as the pointer of a screen of Minecraft's.
enum Pointer{kPointerFree,kPointerLook,kPointerScreen};
static void pointer(Pointer mode){
    static Pointer current=kPointerFree;static RECT kept{};static ULONGLONG looked=0;
    if(background.load())return;
    if(mode==kPointerFree){
        if(current!=kPointerFree){current=kPointerFree;kept={};ClipCursor(nullptr);pointerHidden(false);event("{\"event\":\"physical_pointer\",\"state\":\"free\"}");}
        return;
    }
    HWND w=ownedWindow();if(!w)return;
    // The window's place is measured every time: it changes with Alt+Enter (Cemu's full screen) and when the window is moved.
    RECT r;GetClientRect(w,&r);POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(w,&a);ClientToScreen(w,&b);RECT clip{a.x,a.y,b.x,b.y};
    if(clip.right<=clip.left||clip.bottom<=clip.top)return;
    if(mode!=current||!EqualRect(&clip,&kept)){
        current=mode;kept=clip;clientX=a.x;clientY=a.y;clientW=b.x-a.x;clientH=b.y-a.y;
        SetCursorPos((a.x+b.x)/2,(a.y+b.y)/2);ClipCursor(&clip);mouseKnown=false;pointerHidden(mode==kPointerLook);
        event("{\"event\":\"physical_pointer\",\"state\":\"%s\",\"client\":[%ld,%ld,%ld,%ld]}",mode==kPointerLook?"hidden":"screen",a.x,a.y,b.x-a.x,b.y-a.y);
    }else if(mode==kPointerLook){
        auto now=GetTickCount64();
        if(now-looked>=100){looked=now;if(pointerSeen())pointerHidden(true);}
    }
}
static DWORD WINAPI thread(void*){
    threadId=GetCurrentThreadId();keyHook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(nullptr),0);mouseHook=SetWindowsHookExW(WH_MOUSE_LL,mouse,GetModuleHandleW(nullptr),0);
    FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    event("{\"event\":\"physical_owner\",\"pid\":%lu,\"creation_filetime\":%llu,\"thread\":%lu,\"keyboard\":%d,\"mouse\":%d}",GetCurrentProcessId(),(uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime,threadId,int(keyHook!=nullptr),int(mouseHook!=nullptr));
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(keyHook)UnhookWindowsHookEx(keyHook);if(mouseHook)UnhookWindowsHookEx(mouseHook);return 0;
}
static void start(){
    int value=0;std::ifstream(directory+"/background.txt")>>value;background=value==1;
    auto t=CreateThread(nullptr,0,thread,nullptr,0,nullptr);if(t)CloseHandle(t);
    if(background.load()){t=CreateThread(nullptr,0,injected,nullptr,0,nullptr);if(t)CloseHandle(t);}
    event("{\"event\":\"physical_background\",\"on\":%d}",int(background.load()));
}
// The claim's frame (scale, origin, anchor and the current identities) is the
// native stream's own; no external plan is needed for a claim on it.
static std::shared_ptr<const ViewProbe> streamSeed(){
    if(!nativeRegions::enabled.load()||!walking::ready)return nullptr;
    auto v=std::make_shared<ViewProbe>();v->id=GetTickCount64();v->epoch=body::lifecycleEpoch.load();v->target=targetEpoch.load();v->peerSession=live::session;
    v->actor=body::player.load();v->procId=body::identityProcId.load();v->common=true;v->scale=nativeRegions::scale;
    std::copy_n(nativeRegions::origin,3,v->origin);std::copy_n(nativeRegions::anchor,3,v->anchor);return v;
}
static void stop(const char* reason){
    // Withdrawing control leaves the native world as it is unless the native side itself is not ready.
    if(body::command.load()==5||liveBodyLease.load())body::invalidate(reason,body::allowed()&&strcmp(reason,"device-shutdown")!=0);
    walking::invalidate(reason);
    if(physicalActive.exchange(false)||!physicalBlocked.exchange(true)){
        physicalBlocked=true;ready=false;ClipCursor(nullptr);mouseKnown=false;++textReset;for(auto& held:moveHeld)held=false;padHeld=0;
        {std::lock_guard lock(mutex);pending.clear();}
        // Retried before any later presses if the input ring is temporarily full.
        releaseRequested=true;
        if(!viewProbeBusy.test_and_set()){viewProbe.until=0;viewProbeBusy.clear();}
        event("{\"event\":\"physical_release\",\"reason\":\"%s\",\"why\":%d,\"body_epoch\":%llu,\"target_epoch\":%llu}",reason,stopDetail.exchange(0),body::lifecycleEpoch.load(),targetEpoch.load());
    }
}
static void poll(){
    auto now=GetTickCount64();bool fresh=live::connected&&!live::failed&&body::allowed()&&now-peerViewAt.load()<500&&(live::mc.flags&kMcInWorld);
    ready=fresh;sensitivity=live::mc.sensitivity;width=live::overlayW;height=live::overlayH;
    bool screen=live::mc.flags&kMcScreenOpen;if(screen&&!wasGui){cursorX=width/2;cursorY=height/2;enqueue({kInCursor,0,cursorX,cursorY,0});}wasGui=screen;gui=screen;
    // The window not in front does not end control: Steve stays the player and what is drawn
    // stays his view. Only the keyboard and the mouse are the system's meanwhile (capture() asks for the window in
    // front): Minecraft's held keys and the game's held buttons are let go once, and the pointer is free.
    static bool hadFront=true;static std::atomic<bool> dropKeys{false};bool front=ownedWindow()!=nullptr;
    if(front!=hadFront){
        if(!front&&physicalActive.load()){
            dropKeys=true;++textReset;for(auto& held:moveHeld)held=false;padHeld=0;
            std::lock_guard lock(mutex);pending.clear();
        }
        mouseKnown=false;event("{\"event\":\"physical_focus\",\"front\":%d,\"control\":%d}",int(front),int(physicalActive.load()));hadFront=front;
    }
    // An event of the game's begins: Minecraft's held keys are let go once, and Steve stands for as long as it runs
    // (keyEvent()). It ends: the arrow keys are no longer the game's stick.
    static bool hadEvent=false;bool inEvent=gameEvent.load()&&physicalActive.load();
    if(inEvent!=hadEvent){
        if(inEvent){dropKeys=true;++textReset;for(auto& held:moveHeld)held=false;std::lock_guard lock(mutex);pending.clear();}
        else padHeld&=~0xF00u;
        hadEvent=inEvent;
    }
    if(dropKeys.load()&&live::push({kInReleaseAll,0,0,0,0}))dropKeys=false;
    // A silent Minecraft is waited for as long as the body is held (staleHoldMs), not only 500 ms.
    bool waited=!fresh&&live::connected&&!live::failed&&body::allowed()&&(live::mc.flags&kMcInWorld)&&now-peerViewAt.load()<staleHoldMs.load();
    if(physicalActive.load()&&((!fresh&&!waited)||epoch.load()!=body::lifecycleEpoch.load()||target.load()!=targetEpoch.load()||physicalBlocked.load())){
        stopDetail=(!fresh)|(epoch.load()!=body::lifecycleEpoch.load())<<2|(target.load()!=targetEpoch.load())<<3|physicalBlocked.load()<<4|
            (fresh?0:(!live::connected)<<5|live::failed<<6|(!body::allowed())<<7|(now-peerViewAt.load()>=500)<<8|(!(live::mc.flags&kMcInWorld))<<9);
        stop("focus-or-readiness-or-epoch");
    }
    if(!physicalActive.load()){std::lock_guard lock(mutex);pending.clear();}
    // Focus must have been back for a moment before an automatic claim, so an
    // Alt-Tab passing over the window does not grab the mouse.
    static uint64_t frontSince=0,menuUntil=0;if(!ownedWindow())frontSince=0;else if(!frontSince)frontSince=now;
    if(nativeMenu.load())menuUntil=now+1500; // F7: the native menu is about to open; do not claim in between
    bool pressed=toggle.exchange(false),automatic=!pressed&&wanted.load()&&!physicalActive.load()&&!releaseRequested.load()&&now>=menuUntil&&now-body::specialAt.load()>=300&&fresh&&now-peerViewAt.load()<150&&frontSince&&now-frontSince>=250&&nativeRegions::enabled.load()&&walking::ready;
    if(pressed||automatic){
        if(physicalActive.load()){wanted=false;stop("F8-release");}
        else {auto plan=seed.load();wanted=true; // a press while not ready is honoured as soon as the session is
            bool current=plan&&plan->peerSession==live::session&&plan->epoch==body::lifecycleEpoch.load()&&plan->target==targetEpoch.load();
            if(!current)if(auto own=streamSeed()){seed.store(own);plan=own;}
            if(fresh&&ownedWindow()&&(!walking::lastClaim||walking::ready)&&plan&&plan->peerSession==live::session&&plan->epoch==body::lifecycleEpoch.load()&&plan->target==targetEpoch.load()){
            physicalAngles=pack(live::mc.yaw,live::mc.pitch);physicalYaw=live::mc.yaw;physicalPitch=live::mc.pitch;epoch=plan->epoch;target=plan->target;physicalBlocked=false;mouseKnown=false;++textReset;padHeld=0;
            if(!background.load()){RECT r;GetClientRect(ownedWindow(),&r);POINT a{r.left,r.top},b{r.right,r.bottom};ClientToScreen(ownedWindow(),&a);ClientToScreen(ownedWindow(),&b);RECT clip{a.x,a.y,b.x,b.y};SetCursorPos((a.x+b.x)/2,(a.y+b.y)/2);ClipCursor(&clip);}
            physicalActive=true;
            event("{\"event\":\"physical_claim\",\"body_epoch\":%llu,\"target_epoch\":%llu,\"session\":%llu,\"automatic\":%d}",plan->epoch,plan->target,live::session,int(automatic));
            if(walking::ready){walking::target();body::issuedEpoch=body::lifecycleEpoch.load();body::command=5;body::generation.fetch_add(1);}
        }else if(pressed)event("{\"event\":\"physical_claim_reject\",\"fresh\":%d,\"seed\":%d}",int(fresh),int(bool(plan)));}
    }
    if(releaseRequested.load()){
        if(physicalActive.load())stop(releaseWhy.load());
        if(live::push({kInReleaseAll,0,0,0,0}))releaseRequested=false;
    }
    if(nativeMenu.exchange(false)){inputAction=5;inputUntil=now+250;}
    if(physicalActive.load()){
        walking::target();
        auto plan=seed.load();if(plan&&!viewProbeBusy.test_and_set()){
            viewProbe=*plan;viewProbe.id=now;viewProbe.until=now+750;viewProbe.yaw=physicalYaw.load();viewProbe.pitch=physicalPitch.load();viewProbe.fov=live::mc.fovDeg;viewProbe.aspect=float(width)/height;viewProbe.mcSeq=live::mc.seq;
            double eye[]={live::mc.eyeX,live::mc.eyeY,live::mc.eyeZ};for(int i=0;i<3;i++)viewProbe.eye[i]=plan->anchor[i]+float(eye[i]-plan->origin[i])*plan->scale;
            viewProbeBusy.clear();
        }
        // A coverage pause that became a park (nativeStream::parkPauses; a wait keeps the keys)
        // makes Minecraft release every key (BotwClient calls releaseAll while kBotwLoading).
        // Windows repeats only the key pressed last, so a held W stays released after a jump.
        // Once the park is over and MC has seen it, the movement keys still held are sent again,
        // a held sprint key among them whatever it is bound to. A sprint started by a tap is not
        // restored: that needed a guess at MC's sprint binding (left control was tapped, which is
        // crouch in the player's profile of route-60), and parks are now the rare fallback.
        static uint64_t seenResumes=0,resendAt=0;
        if(nativeStream::parkResumes!=seenResumes){seenResumes=nativeStream::parkResumes;resendAt=now+150;}
        if(resendAt&&now>=resendAt&&!nativeStream::paused){
            resendAt=0;unsigned sent=0;
            if(!gui.load())for(size_t i=0;i<std::size(kMoveKeys);i++)if(moveHeld[i].load()){enqueue({kInKey,kMoveKeys[i],1,0,0});++sent;}
            if(sent)event("{\"event\":\"held_keys_resent\",\"keys\":%u,\"resumes\":%llu}",sent,seenResumes);
        }
        std::lock_guard lock(mutex);while(!pending.empty()){if(!live::push(pending.front())){requestRelease("input-ring-full");break;}pending.pop_front();}
    }
    pointer(!physicalActive.load()||!front?kPointerFree:gui.load()?kPointerScreen:kPointerLook);
    // A scene of the game's (live_target.h). It is shown while the player wants Minecraft's control, also across the
    // moments in which control is not active (a fade inside the scene; the way back after it).
    {static uint64_t sceneLast=0;if(sceneOn.load())sceneLast=now;
        // The game not being ready (a fade that ends a scene: route-301 showed Link on the lift for its last second)
        // does not end the scene; only the way back after it waits for the game.
        bool alive=live::connected&&!live::failed&&now-peerViewAt.load()<500&&(live::mc.flags&kMcInWorld);
        scenePresent=wanted.load()&&alive&&(sceneOn.load()||(body::allowed()&&!physicalActive.load()&&sceneLast&&now-sceneLast<3000));
        // The scene is over: where it left Link. If that is not where Minecraft's player stands, control is given
        // up for a moment, and the stream puts Minecraft's player onto Link before it is claimed again
        // (nativeStream::progress). Otherwise the body is simply written again.
        if(sceneHold.load()&&!sceneOn.load()){
            bool apart=false;double at[3]{},mc[]{live::mc.curX,live::mc.curY,live::mc.curZ};
            if(!physicalActive.load()||body::command.load()!=5)sceneHold=false;
            else if(!body::busy.test_and_set()){   // else at the next poll
                auto pose=body::latest;body::busy.clear();
                if(pose.actor)for(int k=0;k<3;k++){at[k]=(double(pose.observed[k])-nativeRegions::anchor[k])/nativeRegions::scale+nativeRegions::origin[k];apart|=std::isfinite(at[k])&&std::isfinite(mc[k])&&std::abs(at[k]-mc[k])>(k==1?.75:.5);}
                event("{\"event\":\"scene_over\",\"apart\":%d,\"link\":[%.6g,%.6g,%.6g],\"minecraft\":[%.6g,%.6g,%.6g]}",int(apart),at[0],at[1],at[2],mc[0],mc[1],mc[2]);
                if(apart)stop("scene-over");
                sceneHold=false;
            }
        }
    }
    presentation::eligible=(physicalActive.load()&&fresh)||scenePresent.load();
}
}
// Only Steve takes damage. While the player wants Minecraft control:
//  - the store of Link's health (US v208 0x02D452A4, the instruction the
//    SpawnActors pack for this same build names as the health write) keeps
//    the old value when the new one is lower;
//  - Link's own "fall damage start height" (FallDamageStartHeight in his
//    DamageParam resource, botw-decomp resResourceDamageParam.cpp) is raised
//    out of reach, so a landing after Minecraft carried him through the air
//    is not a native fall: no damage reaction, no ragdoll. The original value
//    is put back when control is switched off with F8.
namespace nativeDamage {
static std::atomic<uint64_t> writes{0},blocked{0};
// What was refused on Link is owed to Minecraft's player: quarter hearts, added on the game's
// thread, sent on by the worker (poll). lab/damage-to-minecraft.txt 0: refused and not sent on.
static std::atomic<uint32_t> owedQuarters{0};static std::atomic<bool> forward{true};static std::atomic<uint64_t> forwarded{0};
// lab/damage-guard.txt, for comparison runs: 0 switches both guards off, 2 leaves only the health store guarded.
static std::atomic<int> setting{1};
static bool guarding(){return setting.load()!=0&&physical::wanted.load()&&nativeRegions::enabled.load();}
static bool raising(){return setting.load()==1&&physical::wanted.load()&&nativeRegions::enabled.load();}
// Steve's death as a hit of the game's own, so that the game picks its death by its own rules (report 85): armed by
// hearts() at Steve's death; the next call of the damage manager's addDamage (the game makes idle ones every update)
// carries that hit, and the store of the life is let through for two seconds. The codes are those of the hits
// native_add_damage logs (r4 r8 r9 r10). By Minecraft's cause: a blow (2 5 15 0: Link reels and falls forward),
// fire (6 9 0 0: he staggers, burning, and falls), a shot or a blast (4 6 2 1, a thrown rock's: he is knocked
// down). Any other cause (a fall, drowning, the void) keeps the plain death, the life set to 0, which the game
// already shows by Link's state (in water: the swimmer's). lab/death-hit.txt: 0 = always plain; five numbers
// (r4 r8 r9 r10 damage) = that hit for every cause (to try other codes).
static std::atomic<int32_t> deathCodes[5]{},armedCodes[5]{};static std::atomic<int> deathHitMode{1};static std::atomic<uint64_t> deathHitArmed{0},deathHitOpen{0};
static bool deathHitFor(uint32_t cause,int32_t (&codes)[5]){
    if(deathHitMode.load()==0)return false;
    if(deathHitMode.load()==2){for(int i=0;i<5;i++)codes[i]=deathCodes[i].load();return true;}
    static const int32_t blow[4]{2,5,15,0},fire[4]{6,9,0,0},knock[4]{4,6,2,1};
    const int32_t* pick=nullptr;
    switch(cause){
        case botwcraft::proto::kDeathAttack:pick=blow;break;
        case botwcraft::proto::kDeathFire:pick=fire;break;
        case botwcraft::proto::kDeathProjectile:case botwcraft::proto::kDeathExplosion:pick=knock;break;
        default:return false;
    }
    for(int i=0;i<4;i++)codes[i]=pick[i];codes[4]=400;return true;   // 400 quarters: more than any life with its yellow hearts
}
static void health(CPU* c){
    c->ip=c->spr.lr;auto base=static_cast<uint8_t*>(getGuest());uint32_t address=c->gpr[3];int32_t next=int32_t(c->gpr[4]),old=next;
    SIZE_T copied=0;uint32_t raw=0;
    if(base&&address>=0x10000000&&address<0xA0000000-4&&!(address&3)&&ReadProcessMemory(GetCurrentProcess(),base+address,&raw,4,&copied)&&copied==4)old=int32_t(__builtin_bswap32(raw));
    bool block=guarding()&&next<old&&GetTickCount64()>=deathHitOpen.load();++writes;blocked+=block;
    if(block&&old-next<=4096)owedQuarters+=uint32_t(old-next);
    // Who asked for it: the return addresses up the guest stack (back chain at 0(sp), a caller's
    // return address at 4 of the frame above). Read only; this is how the game's own damage
    // paths (a blow, an arrow, a fall, fire, cold) are told apart and found for the next step.
    char stack[160]="";size_t used=0;
    if(base&&next!=old){
        auto word=[&](uint32_t p,uint32_t& out){uint32_t v=0;SIZE_T n=0;if(p<0x01000000||p>=0xA0000000-4||(p&3)||!ReadProcessMemory(GetCurrentProcess(),base+p,&v,4,&n)||n!=4)return false;out=__builtin_bswap32(v);return true;};
        uint32_t frame=c->gpr[1],above=0,ret=0;
        for(int depth=0;depth<12&&used+12<sizeof(stack)&&word(frame,above)&&above>frame&&above-frame<0x4000;depth++,frame=above)
            if(depth&&word(above+4,ret)&&ret>=0x02000000&&ret<0x05000000)used+=size_t(snprintf(stack+used,sizeof(stack)-used,"%s%u",used?",":"",ret));
    }
    event("{\"event\":\"native_health\",\"address\":%u,\"old\":%d,\"new\":%d,\"blocked\":%d,\"lr\":%u,\"stack\":[%s]}",address,old,next,int(block),c->spr.lr,stack);
    c->gpr[3]=uint32_t(block?old:next);
}
// The game's food heals Steve (Minecraft's own food keeps its hunger). What the game gives
// its player back is owed to Minecraft's player in quarter hearts, added on the game's thread and sent on by the
// worker (poll) at the rate of a hit: a quarter heart is half a health point, a heart of the game's a heart of
// Minecraft's. lab/heal-to-minecraft.txt 0: recorded and not sent on.
static std::atomic<uint32_t> owedHeal{0};static std::atomic<int32_t> eatenWaiting{0};static std::atomic<bool> forwardHeal{true},fromRecover{false};static std::atomic<uint64_t> recovers{0},healsSent{0};
// The hearts of a thing eaten (pack, EatenHearts): r3 the amount in quarter hearts. The whole amount
// is owed: Link, who is never hurt, is mostly at his most life already, and what his own life then rises by
// would be little or nothing.
static void eaten(CPU* c){
    c->ip=c->spr.lr;int32_t amount=int32_t(c->gpr[3]);bool mine=guarding()&&amount>0&&amount<=4096;
    if(mine){owedHeal+=uint32_t(amount);eatenWaiting=amount;}
    event("{\"event\":\"native_eaten\",\"amount\":%d,\"owed\":%d}",amount,int(mine));
}
// The effect of a thing eaten (pack, FoodEffect): r3 the game's effect id, r4 its level, r5 the yellow hearts of
// id 2 (quarter hearts), r6 its time (a float's bits) in the game's own updates, thirty to a second: route-191, an
// Electro Elixir the game shows as 06:10 came as 11100. Sent on to Minecraft by the worker when it has
// control, as it is; the mod knows which ids have a match there (16 Fireproof is
// Fire Resistance, 10 AttackUp Strength, 11 DefenseUp Resistance, 13 AllSpeed Speed, 2 the yellow hearts
// Absorption; the others stay the game's own). The game keeps its own effect on Link either way.
// In the ring: the id, the level (or the yellow hearts), the seconds.
static std::atomic<uint64_t> effectRing[8];static std::atomic<uint32_t> effectHead{0};static uint32_t effectTail=0;
static void effect(CPU* c){
    c->ip=c->spr.lr;int32_t id=int32_t(c->gpr[3]),level=int32_t(c->gpr[4]),yellow=int32_t(c->gpr[5]);uint32_t raw=c->gpr[6];float updates;memcpy(&updates,&raw,4);
    bool mine=guarding()&&id>=0&&id<=40;int amount=id==2?yellow:level;if(!std::isfinite(updates)||updates<0||updates>1080000)updates=0;float seconds=updates/30.f;
    if(mine&&amount>0&&amount<=4096){effectRing[effectHead.load()%8].store((uint64_t(id)<<48)|(uint64_t(amount)<<32)|uint32_t(seconds+.5f),std::memory_order_release);++effectHead;}
    event("{\"event\":\"native_effect\",\"id\":%d,\"level\":%d,\"yellow_hearts\":%d,\"updates\":%.1f,\"seconds\":%.3f,\"owed\":%d}",id,level,yellow,updates,seconds,int(mine&&amount>0));
}
// What waited is given to the player (pack, RecoverLife): r3 his life, r4 the sum, before the game bounds it by
// his most life. What was eaten is owed already; more than that came from elsewhere (the game's own "all of
// it", 02D378A8, adds his most life) and is owed here.
static void recover(CPU* c){
    c->ip=c->spr.lr;int32_t now=int32_t(c->gpr[3]),amount=int32_t(c->gpr[4]);bool mine=guarding();++recovers;
    int32_t known=eatenWaiting.exchange(0),more=amount-known;
    if(mine&&amount>0){fromRecover=true;if(more>0&&more<=4096)owedHeal+=uint32_t(more);}
    // The game calls it with nothing in every update while its menu is open (route-184): only an amount is recorded.
    if(amount)event("{\"event\":\"native_recover\",\"life\":%d,\"amount\":%d,\"eaten\":%d,\"owed_here\":%d,\"calls\":%llu}",now,amount,known,mine&&more>0?more:0,recovers.load());
}
// Steve's hearts are Link's (if Link has 3 hearts, Steve has 3). Two halves. Link's most life (PlayerInfo::sInstance at 10463F38, mMaxHeartValue, a float of
// quarter hearts at +0x64: WiiXLaunch's names and addresses for this build) is Steve's most health, a heart for a
// heart: the worker sends it (poll). And Link's own life, which nothing lowers while Minecraft owns the player,
// shows Steve's health: it is written at the player actor's life (PlayerInfo+0x30, actor+0x540, the address
// setLife stores at), so the game's row of hearts is Steve's, its question before a meal ("You cannot recover
// any more hearts") comes when Steve is whole, and any life the game gives shows as a rise. Never less than a
// quarter heart: Link does not die of Steve's wounds. On the game's thread, at a pad sample, a few times a second.
// lab/hearts.txt 0: off.
//
// Steve's death is Link's (the game's own game over and its reload, so that the game plays like BOTW).
// Minecraft's player died (the mod's kEvPlayerDied, within the last two seconds): Link's life is
// set to nothing, once, and the game does the rest by its own rules. lab/death.txt 0: nothing of the game's
// happens, Steve comes back on the spot as before.
//
// Link's life set anew by the game is Steve's (after Mipha's Grace Steve had all his
// hearts and none of the yellow ones the game gives). The game writes the life from nothing after a load and at
// its own revive after a death (a fairy; Mipha's Grace, which adds its yellow hearts in a second write: route-350,
// 0 to 20 to 40 with a most life of 20). life() below keeps the last of these; the worker sends it on as kInLife
// when it has stood for 300 ms, Minecraft has control and its most health is the game's: what is up to the most
// life as red hearts, what is over it as yellow hearts (Minecraft's absorption). hearts() waits meanwhile, and
// for a second and a half after, so that Steve's old health is not written over the life the game just gave.
// The other way, Steve's yellow hearts (the game's food, a golden apple) show as Link's: life over his most.
static std::atomic<int32_t> setLife{0};static std::atomic<uint64_t> setAt{0},setHold{0},lifeSent{0};
static std::atomic<int32_t> linkMost{0};static std::atomic<int> heartsMode{1},deathMode{1};static std::atomic<uint64_t> healAt{0},heartWrites{0},deathTaken{0};static std::atomic<uint32_t> lifeAddress{0};
static void hearts(){
    static uint32_t calls=0;if(++calls%8||!heartsMode.load()||!guarding())return;
    if(!body::readable(0x10463F38,4))return;
    uint32_t info=body::u32(0x10463F38);if(!info||!body::readable(info,0x68))return;
    uint32_t raw=body::u32(info+0x64);float most;memcpy(&most,&raw,4);if(!(most>=4.f&&most<=160.f))return;
    int32_t quarters=int32_t(most+.5f);linkMost=quarters;
    uint32_t actor=body::u32(info+0x30);if(!actor||!body::readable(actor+0x540,4)||lifeAddress.load()!=actor+0x540)return;
    if(uint64_t died=peerDiedAt.load();died&&died!=deathTaken.load()&&deathMode.load()&&physicalActive.load()&&GetTickCount64()-died<2000){
        deathTaken=died;int32_t was=int32_t(body::u32(actor+0x540));
        int32_t codes[5];
        if(deathHitFor(peerDeathCause.load(),codes)){for(int i=0;i<5;i++)armedCodes[i]=codes[i];deathHitArmed=GetTickCount64();}
        else if(was>0)body::put32(actor+0x540,0);
        event("{\"event\":\"native_death\",\"life_was\":%d,\"most\":%d,\"cause\":%u,\"attacker\":%u}",was,quarters,peerDeathCause.load(),peerDeathAttacker.load());
        return;
    }
    // The armed hit did not end the life in a second and a half: as before.
    if(uint64_t armed=deathHitArmed.load();armed&&GetTickCount64()-armed>1500){
        deathHitArmed=0;int32_t was=int32_t(body::u32(actor+0x540));if(was>0)body::put32(actor+0x540,0);
        event("{\"event\":\"native_death_hit\",\"state\":\"fallback\",\"life_was\":%d}",was);
    }
    if(setLife.load()>0||GetTickCount64()<setHold.load())return;
    float health=live::mc.health,has=live::mc.maxHealth,extra=live::mc.absorption;
    // Not while a heal is on its way to Minecraft (Link's life would fall back and rise again), and not before
    // Minecraft's most health is the game's.
    if(!physicalActive.load()||!(health>0)||owedHeal.load()||GetTickCount64()-healAt.load()<700||std::fabs(has-quarters*.5f)>.01f)return;
    int32_t want=std::clamp(int32_t(std::ceil(health*2-1e-3f)),1,quarters),now=int32_t(body::u32(actor+0x540));
    // Yellow hearts are life over the most life, so only a whole Steve shows them (the game has them no other way).
    if(want==quarters&&extra>0)want+=std::min(int32_t(extra*2+.5f),120);
    if(now<=0||now==want)return;
    body::put32(actor+0x540,uint32_t(want));
    if(++heartWrites<=20||heartWrites%50==0)event("{\"event\":\"native_hearts\",\"life_was\":%d,\"life\":%d,\"most\":%d,\"minecraft_health\":%.3g,\"writes\":%llu}",now,want,quarters,health,heartWrites.load());
}
// The player's life as the game sets it outside a hit (pack, LifeWrite: PlayerInfo::setLife). Recorded with who
// asked for it; the value is stored as the game gave it. A rise that did not come from what he ate (an inn, a
// fairy; not met yet) is owed to Minecraft's player too, by what the life rose; the first value after a load
// (from nothing) is not a rise.
static std::atomic<uint64_t> lifeWrites{0};
static void life(CPU* c){
    c->ip=c->spr.lr;auto base=static_cast<uint8_t*>(getGuest());uint32_t address=c->gpr[3];int32_t next=int32_t(c->gpr[4]),old=next;
    SIZE_T copied=0;uint32_t raw=0;
    if(base&&address>=0x10000000&&address<0xA0000000-4&&!(address&3)&&ReadProcessMemory(GetCurrentProcess(),base+address,&raw,4,&copied)&&copied==4)old=int32_t(__builtin_bswap32(raw));
    ++lifeWrites;lifeAddress=address;
    // From nothing: a load, or the game's own revive. A rise within a second of it belongs to it (Mipha's yellow hearts).
    bool recovered=fromRecover.exchange(false),anew=next>0&&next<=4096&&(old<=0||old>4096||(guarding()&&next>old&&GetTickCount64()-setAt.load()<1000));   // a new game: 0, 65535, 12 (route-351)
    if(anew){setLife=next;setAt=GetTickCount64();}
    else if(!recovered&&guarding()&&old>0&&next>old&&next-old<=4096)owedHeal+=uint32_t(next-old);
    if(next!=old){
        char stack[200]="";size_t used=0;
        auto word=[&](uint32_t p,uint32_t& out){uint32_t v=0;SIZE_T n=0;if(!base||p<0x01000000||p>=0xA0000000-4||(p&3)||!ReadProcessMemory(GetCurrentProcess(),base+p,&v,4,&n)||n!=4)return false;out=__builtin_bswap32(v);return true;};
        uint32_t frame=c->gpr[1],above=0,ret=0;
        for(int depth=0;depth<16&&used+12<sizeof(stack)&&word(frame,above)&&above>frame&&above-frame<0x4000;depth++,frame=above)
            if(word(above+4,ret)&&ret>=0x02000000&&ret<0x05000000)used+=size_t(snprintf(stack+used,sizeof(stack)-used,"%s%u",used?",":"",ret));
        event("{\"event\":\"native_life\",\"address\":%u,\"old\":%d,\"new\":%d,\"writes\":%llu,\"stack\":[%s]}",address,old,next,lifeWrites.load(),stack);
    }
    c->gpr[3]=uint32_t(next);
}
// Climbing costs Link's real stamina, shown in Minecraft's HUD. While Minecraft's player goes up a wall (McState kMcClimbing) the game's own number, the
// current stamina at PlayerInfo+0x68 (a wheel is 1000), falls by the height he rises, at the game's input point,
// and the game may not raise it. The cost is per block of height, not per second (so that Steve
// and Link climb the same height on one wheel of stamina, not counting Link's jumps): a wheel lasts lab/climb-height.txt blocks
// (default 25). Link's own cost (GameROMPlayer, Player_Link.baiprog): StaminaDownAlways 36.5 a second while
// climbing, StaminaRateMovingUp 1.0, so a wheel is about 27 seconds of climbing; 25 blocks (33 of the game's
// metres) is that time at an assumed 1.2 metres a second, which is not measured yet. Afterwards the game
// fills it by its own rules; when it has not for a second and a half with Steve on the ground, the bridge does,
// by lab/stamina-recover.txt units a second (default 300; 0: never). What there is and the most (+0x6C) go to
// Minecraft as kInStamina, in tenths of a unit, when they change and once a second: its HUD shows them, and
// with none left its player falls off the wall.
static std::atomic<float> climbHeight{25.f},recoverRate{300.f};static std::atomic<uint64_t> climbingAt{0},gameRoseAt{0};
static bool climbing(){return GetTickCount64()-climbingAt.load()<250;}
static void climbStamina(){
    static uint64_t last=0,toldAt=0,logged=0;static float told[2]{-1,-1};static double lastY=NAN;
    uint64_t now=GetTickCount64(),passed=std::min<uint64_t>(now-last,100);last=now;
    // The height Minecraft's player rose since the last call (nothing for a jump of more than three blocks: a teleport).
    double rose=0;{botwcraft::proto::McState mc{};if(liveMcNow(mc)&&std::isfinite(mc.curY)){if(std::isfinite(lastY)&&mc.curY>lastY&&mc.curY-lastY<3)rose=mc.curY-lastY;lastY=mc.curY;}else lastY=NAN;}
    if(!guarding()||!physicalActive.load()||!body::readable(0x10463F38,4))return;
    uint32_t info=body::u32(0x10463F38);if(!info||!body::readable(info,0x70))return;
    auto real=[](uint32_t at){uint32_t raw=body::u32(at);float f;memcpy(&f,&raw,4);return f;};
    float has=real(info+0x68),most=real(info+0x6C),was=has;
    // Lab, for the game's extra (yellow) stamina: the food effect ExGutsMaxUp (02FF15C4) writes two floats at
    // the player's +0xCFC and +0xD00 (02D38240); the most the game fills the stamina up to is the player's +0x1324
    // (02D49E9C). One line when any of these change, at most two a second.
    {static uint64_t at=0;static float seen[5]{};uint32_t actor=body::u32(info+0x30);
        if(now-at>=500&&actor&&body::readable(actor+0xCF0,0x20)&&body::readable(actor+0x1320,8)){
            float v[5]{has,most,real(actor+0x1324),real(actor+0xCFC),real(actor+0xD00)};
            if(memcmp(v,seen,sizeof(v))){at=now;memcpy(seen,v,sizeof(v));event("{\"event\":\"native_stamina_fields\",\"has\":%.6g,\"most\":%.6g,\"actor_most\":%.6g,\"cfc\":%.6g,\"d00\":%.6g}",v[0],v[1],v[2],v[3],v[4]);}}}
    if(!(most>=100.f&&most<=10000.f)||!(has>=0.f&&has<=most))return;
    uint32_t flags=live::mc.flags;int how=0;
    if((flags&botwcraft::proto::kMcClimbing)&&!(flags&botwcraft::proto::kMcDead)){
        climbingAt=now;float height=climbHeight.load();if(height>0)has=std::max(0.f,has-float(rose)*1000.f/height);how=1;
    }else if(has<most&&(flags&botwcraft::proto::kMcOnGround)&&now-climbingAt.load()>=1500&&now-gameRoseAt.load()>=1500&&now-walk::dashAt.load()>=1500){
        has=std::min(most,has+recoverRate.load()*float(passed)*.001f);how=2;
    }
    if(has!=was){uint32_t raw;memcpy(&raw,&has,4);body::put32(info+0x68,raw);}
    if(how&&now-logged>=1000){logged=now;event("{\"event\":\"native_climb\",\"stamina\":%.5g,\"most\":%.5g,\"by\":\"%s\"}",has,most,how==1?"climb":"bridge_recover");}
    bool changed=std::fabs(has-told[0])>=1.f||most!=told[1];
    if(now-toldAt>=(changed?100u:1000u)){toldAt=now;told[0]=has;told[1]=most;physical::enqueue({botwcraft::proto::kInStamina,0,int(has*10+.5f),int(most*10+.5f),0});}
}
// Link's stamina (pack, StaminaWrite): a lower value is not stored while Minecraft owns the player, and no
// higher one while its player climbs (above).
// Route-95: Minecraft's player stood on a river bed ten blocks under BOTW's surface, Link was
// held there with him, his stamina ran out in about twelve seconds and he drowned.
static std::atomic<uint64_t> staminaWrites{0},staminaBlocked{0};
static void stamina(CPU* c){
    c->ip=c->spr.lr;auto base=static_cast<uint8_t*>(getGuest());uint32_t info=c->gpr[3];float next=float(c->fpr[2]),old=next;
    SIZE_T copied=0;uint32_t raw=0;
    if(base&&info>=0x10000000&&info<0xA0000000-0x70&&!(info&3)&&ReadProcessMemory(GetCurrentProcess(),base+info+0x68,&raw,4,&copied)&&copied==4){raw=__builtin_bswap32(raw);memcpy(&old,&raw,4);}
    bool block=guarding()&&std::isfinite(old)&&std::isfinite(next)&&((next<old&&!walk::dashing())||(next>old&&climbing()));   // the game's dash pays its own (walk.h)++staminaWrites;staminaBlocked+=block;
    if(!block&&next>old)gameRoseAt=GetTickCount64();
    if(block)c->fpr[2]=old;
    // The game writes it in every frame it changes: one line when blocking starts or stops, and one a second.
    static std::atomic<uint64_t> logged{0};static std::atomic<bool> was{false};auto now=GetTickCount64();
    if(was.exchange(block)!=block||now-logged.load()>=1000){logged=now;event("{\"event\":\"native_stamina\",\"old\":%.6g,\"new\":%.6g,\"blocked\":%d,\"writes\":%llu,\"blocked_writes\":%llu,\"lr\":%u}",old,next,int(block),staminaWrites.load(),staminaBlocked.load(),c->spr.lr);}
}
// The game's damage event for the player (report 51). The player's damage manager adds a hit to
// what it will take from his life in uking::dmg::DamageManagerBase::addDamage (02AFAD44; the
// player's override is 02D45360 and calls the base at 02D4558C once its own checks have let the
// hit through). The pack calls here in place of that call. Arguments as the game passes them:
// r3 manager, r4 a small code, r5 damage in quarter hearts, r6 and r7 two more amounts, r8 to
// r10 codes of the hit. lab/damage-event.txt: 1 refuses the hit there while Minecraft owns the
// player (the game is told the hit did not count, as it tells itself for a weaker hit in the
// same update) and the damage is owed to Minecraft; 0 only records it. Default 1.
static std::atomic<int> eventMode{1};static std::atomic<uint64_t> adds{0},refusedAdds{0},receives{0};
// A refused hit with its attacker, from the game's thread to the worker: quarter hearts in the
// high half, the attacker's proc id (the id of its row in the actor table; 0: not known) in the
// low half. The attack that led to it is the last one the defence hook saw, a moment before.
// Bit 63: the game's fire. Seen in routes 120 and 121 as r4 6 with the code 9 and the kind 0 (a
// tick of a camp's fire, 2 quarter hearts a second) or -1 (6 quarter hearts right after a blow of a
// Bokoblin of that camp, whose club had caught fire). Other kinds of that non-contact path (the
// player's 02D45954 chooses among 0x1E, 0x1C, 0x16, 0x0F, 0x15, 0x11, 0, 0x12) are not met yet.
static std::atomic<uint64_t> hitRing[32];static std::atomic<uint32_t> hitHead{0};static uint32_t hitTail=0;
static std::atomic<uint64_t> lastAttack{0}; // attacker id << 32 | tick
static bool guestRead(uint32_t p,void* out,size_t n){
    auto base=static_cast<uint8_t*>(getGuest());SIZE_T copied=0;
    return base&&p>=0x01000000&&uint64_t(p)+n<=0xA0000000&&ReadProcessMemory(GetCurrentProcess(),base+p,out,n,&copied)&&copied==n;
}
static size_t guestStack(uint32_t frame,char* out,size_t size){
    size_t used=0;uint32_t above=0,ret=0;out[0]=0;
    auto word=[](uint32_t p,uint32_t& v){if((p&3)||!guestRead(p,&v,4))return false;v=__builtin_bswap32(v);return true;};
    for(int depth=0;depth<16&&used+12<size&&word(frame,above)&&above>frame&&above-frame<0x4000;depth++,frame=above)
        if(word(above+4,ret)&&ret>=0x02000000&&ret<0x05000000)used+=size_t(snprintf(out+used,size-used,"%s%u",used?",":"",ret));
    return used;
}
static void hex(const uint8_t* bytes,size_t n,char* out){for(size_t i=0;i<n;i++)snprintf(out+2*i,3,"%02x",bytes[i]);}
static void addDamage(CPU* c){
    c->ip=c->spr.lr;uint32_t mgr=c->gpr[3];
    // Steve died of a cause with a hit of its own (above, deathHitFor): this call carries it, and is let through.
    bool deathHit=false;
    if(uint64_t armed=deathHitArmed.load();armed&&GetTickCount64()-armed<1500){   // every call here is the player's manager (the hook sits in his override)
        c->gpr[4]=uint32_t(armedCodes[0].load());c->gpr[5]=uint32_t(armedCodes[4].load());c->gpr[6]=0;c->gpr[7]=0;
        c->gpr[8]=uint32_t(armedCodes[1].load());c->gpr[9]=uint32_t(armedCodes[2].load());c->gpr[10]=uint32_t(armedCodes[3].load());
        deathHitArmed=0;deathHitOpen=GetTickCount64()+2000;deathHit=true;
        event("{\"event\":\"native_death_hit\",\"state\":\"given\",\"r4\":%d,\"r8\":%d,\"r9\":%d,\"r10\":%d,\"damage\":%d}",armedCodes[0].load(),armedCodes[1].load(),armedCodes[2].load(),armedCodes[3].load(),armedCodes[4].load());
    }
    int32_t damage=int32_t(c->gpr[5]);auto count=++adds;
    // The game calls this several times in every update with no amount and the codes -1 (route-104:
    // r4 2, 4, 5, 7 and 3 in turn). Those are left alone. A hit carries an amount.
    bool hit=damage>0||int32_t(c->gpr[6])>0||int32_t(c->gpr[7])>0;
    bool refuse=hit&&eventMode.load()==1&&guarding()&&!deathHit;
    if(refuse){
        ++refusedAdds;
        if(damage>0){
            auto last=lastAttack.exchange(0);uint32_t attacker=uint32_t(GetTickCount64())-uint32_t(last)<=100?uint32_t(last>>32):0;
            bool fire=int32_t(c->gpr[4])==6&&int32_t(c->gpr[8])==9&&(int32_t(c->gpr[9])==0||int32_t(c->gpr[9])==-1);
            hitRing[hitHead.load()%32].store((uint64_t(fire)<<63)|(uint64_t(std::min(damage,400))<<32)|attacker,std::memory_order_release);++hitHead;
        }
    }
    c->gpr[12]=refuse?1:0;
    // The first twelve calls whatever they are, then hits only, every one of them up to sixty in a second
    // (route-120: two hits in one update, of which the second went unrecorded under the earlier limit of one in 50 ms).
    static std::atomic<uint64_t> second{0};static std::atomic<uint32_t> inSecond{0};auto now=GetTickCount64()/1000;
    if(second.exchange(now)!=now)inSecond=0;
    if(count>12&&(!hit||++inSecond>60))return;
    uint8_t state[0x1A0]{};char dump[2*sizeof(state)+1]="",stack[200]="";uint32_t flags=0;
    if(count<=12&&guestRead(mgr,state,sizeof(state)))hex(state,sizeof(state),dump);
    if(auto actor=body::identityActor.load();actor&&guestRead(actor+0x8DC,&flags,4))flags=__builtin_bswap32(flags);
    guestStack(c->gpr[1],stack,sizeof(stack));
    event("{\"event\":\"native_add_damage\",\"count\":%llu,\"refused\":%d,\"manager\":%u,\"r4\":%d,\"damage\":%d,\"r6\":%d,\"r7\":%d,\"r8\":%d,\"r9\":%d,\"r10\":%d,\"flags\":%u,\"stack\":[%s],\"state\":\"%s\"}",
        (unsigned long long)count,int(refuse),mgr,int32_t(c->gpr[4]),damage,int32_t(c->gpr[6]),int32_t(c->gpr[7]),int32_t(c->gpr[8]),int32_t(c->gpr[9]),int32_t(c->gpr[10]),flags,stack,dump);
}
// Read only: the step before, common to every actor (02AE83C0), with the amounts as they arrive
// and before the damage callbacks of the manager have seen them. Recorded for the player's manager.
static void damageReceive(CPU* c){
    c->ip=c->spr.lr;uint32_t mgr=c->gpr[3],actor=0,onStack[2]{};
    if(!guestRead(mgr+8,&actor,4)||__builtin_bswap32(actor)!=body::identityActor.load()||!actor)return;
    auto count=++receives;static std::atomic<uint64_t> second{0};static std::atomic<uint32_t> inSecond{0};auto now=GetTickCount64()/1000;
    bool hit=int32_t(c->gpr[7])>0||int32_t(c->gpr[8])>0||int32_t(c->gpr[9])>0;
    if(second.exchange(now)!=now)inSecond=0;
    if(count>12&&(!hit||++inSecond>60))return;
    // The pack lowered the stack by 0x10 for this call: the function's stack arguments are at 0x18 and 0x1C.
    guestRead(c->gpr[1]+0x18,onStack,8);char stack[200]="";guestStack(c->gpr[1],stack,sizeof(stack));
    event("{\"event\":\"native_damage_receive\",\"count\":%llu,\"manager\":%u,\"r4\":%d,\"r5\":%d,\"r6\":%d,\"r7\":%d,\"r8\":%d,\"r9\":%d,\"r10\":%d,\"s0\":%d,\"s1\":%d,\"stack\":[%s]}",
        (unsigned long long)count,mgr,int32_t(c->gpr[4]),int32_t(c->gpr[5]),int32_t(c->gpr[6]),int32_t(c->gpr[7]),int32_t(c->gpr[8]),int32_t(c->gpr[9]),int32_t(c->gpr[10]),
        int32_t(__builtin_bswap32(onStack[0])),int32_t(__builtin_bswap32(onStack[1])),stack);
}
// The defence of the one who is hit (pack, DefenceRead; the function at 02AE9E14, common to all
// actors): result = max(int((damage - defence) * rate), the attack's least damage), with the
// defence the manager's +0x4C, the rate 1 or the manager's +0x58 for a kind of attacker, and the
// attack's record in r30 (+0x94 its damage, +0x90 a second amount, +0x9C the least damage,
// +0xA0 a kind, +0xB0 the attacker's link: its unit and id; the unit holds the id at +0x3C and
// the actor at +0x40, read as the game's accessor at 0378D8DC does). For the player's manager, while Minecraft
// owns the player and hits are refused for Minecraft (damage-event 1), the defence is 0 and the
// rate 1: Link's armour does not count, Steve's does.
static std::atomic<uint64_t> attacks{0};
}
namespace swing {static void shape(CPU* c,uint32_t receiver,uint32_t info);}
namespace nativeDamage {
// lab/swing.txt `trace 1` (swing.h): the same for every actor, for finding out what a weapon's blow on a tree or
// a pot looks like to the game: who is hit, the amounts, the attack's record from +0x90 and who the record names.
static std::atomic<bool> traceAll{false};
static void traceAttack(CPU* c,uint32_t mgr,uint32_t info,uint32_t receiver){
    static std::atomic<uint64_t> second{0};static std::atomic<uint32_t> inSecond{0};auto now=GetTickCount64()/1000;
    if(second.exchange(now)!=now)inSecond=0;
    if(++inSecond>40)return;
    auto name=[](uint32_t actor,char* out,size_t size){out[0]=0;if(actor>=0x10000000&&guestRead(actor+0x10,out,size-1)){out[size-1]=0;for(char* ch=out;*ch;ch++)if(*ch<32||*ch>126||*ch=='"'||*ch==92)*ch='?';}};
    uint32_t words[24]{};guestRead(info+0x90,words,sizeof(words));for(auto& w:words)w=__builtin_bswap32(w);
    uint32_t unit=words[8],link[2]{},attacker=0;char hit[48],by[48],record[24*9+1]="",start[36*9+1]="";{uint32_t first[36]{};if(guestRead(info,first,sizeof(first)))for(int i=0;i<36;i++)snprintf(start+i*9,10,"%08x ",__builtin_bswap32(first[i]));}
    if(unit>=0x10000000&&guestRead(unit+0x3C,link,8)&&__builtin_bswap32(link[0])==words[9])attacker=__builtin_bswap32(link[1]);
    name(receiver,hit,sizeof(hit));name(attacker,by,sizeof(by));
    for(int i=0;i<24;i++)snprintf(record+i*9,10,"%08x ",words[i]);
    event("{\"event\":\"native_attack_any\",\"manager\":%u,\"hit\":\"%s\",\"hit_actor\":%u,\"damage\":%d,\"defence\":%d,\"rate\":%.6g,\"attack_damage\":%d,\"second\":%d,\"least\":%d,\"kind\":%d,\"by\":\"%s\",\"by_actor\":%u,\"record_from_90\":\"%s\",\"record_from_0\":\"%s\",\"info\":%u}",
        mgr,hit,receiver,int32_t(c->gpr[29]),int32_t(c->gpr[31]),double(c->fpr[62]),int32_t(words[1]),int32_t(words[0]),int32_t(words[3]),int32_t(words[4]),by,attacker,record,start,info);
}
static void defence(CPU* c){
    c->ip=c->spr.lr;uint32_t mgr=c->gpr[28],info=c->gpr[30],actor=0;
    if(guestRead(mgr+8,&actor,4)&&actor)swing::shape(c,__builtin_bswap32(actor),info);   // a blow of the bridge's own swing: whose rules give its amount (swing.h)
    if(traceAll.load()&&guestRead(mgr+8,&actor,4)&&actor)traceAttack(c,mgr,info,__builtin_bswap32(actor));
    if(!guestRead(mgr+8,&actor,4)||!actor||__builtin_bswap32(actor)!=body::identityActor.load())return;
    int32_t damage=int32_t(c->gpr[29]),before=int32_t(c->gpr[31]);double rate=c->fpr[62];
    bool bare=eventMode.load()==1&&guarding();
    if(bare){c->gpr[31]=0;c->fpr[62]=1.0;}
    auto count=++attacks;static std::atomic<uint64_t> logged{0};auto now=GetTickCount64();
    if(count>40&&now-logged.load()<50)return;
    logged=now;
    uint32_t words[16]{};guestRead(info+0x90,words,sizeof(words));for(auto& w:words)w=__builtin_bswap32(w);
    // Who: the actor behind the link, its name (the text at actor+0x10) and where it is (the translation of the matrix at +0x1F8).
    uint32_t unit=words[8],link[2]{},attacker=0;char name[48]="";float at[3]{};
    if(unit>=0x10000000&&guestRead(unit+0x3C,link,8)&&__builtin_bswap32(link[0])==words[9])attacker=__builtin_bswap32(link[1]);
    if(attacker>=0x10000000){
        if(guestRead(attacker+0x10,name,sizeof(name)-1))for(char& ch:name)if(ch&&(ch<32||ch>126||ch=='"'||ch==92))ch='?';
        lastAttack=(uint64_t(words[9])<<32)|uint32_t(GetTickCount64());
        for(int i=0;i<3;i++){uint32_t raw=0;if(guestRead(attacker+0x204+i*16,&raw,4)){raw=__builtin_bswap32(raw);memcpy(&at[i],&raw,4);}}
    }
    event("{\"event\":\"native_attack\",\"count\":%llu,\"damage\":%d,\"defence\":%d,\"rate\":%.6g,\"bare\":%d,\"second\":%d,\"attack_damage\":%d,\"least\":%d,\"kind\":%d,\"attacker\":%u,\"attacker_id\":%u,\"name\":\"%s\",\"at\":[%.6g,%.6g,%.6g]}",
        (unsigned long long)count,damage,before,rate,int(bare),int32_t(words[0]),int32_t(words[1]),int32_t(words[3]),int32_t(words[4]),attacker,words[9],name,at[0],at[1],at[2]);
}
constexpr uint32_t kHashes[]{0x4CAF950A,0xC795E528,0xA4B94199,0x0745FF39}; // crc32 of FallDamageStartHeight, ..Min, ..PerMeter, ..HighStart
constexpr float kNever=1e9f;
static uint32_t valueAt=0,hashAt=0,owner=0;static float original=0;static bool raised=false;static uint64_t nextScan=0;
static bool read(uint32_t p,void* out,size_t n){
    auto base=static_cast<uint8_t*>(getGuest());SIZE_T copied=0;
    return base&&p>=0x10000000&&uint64_t(p)+n<=0x50000000&&ReadProcessMemory(GetCurrentProcess(),base+p,out,n,&copied)&&copied==n;
}
static uint32_t be(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return __builtin_bswap32(v);}
static float real(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static bool heapLike(uint32_t p){return p>=0x10000000&&p<0x50000000&&!(p&3);}
// Every place where the four parameter name hashes sit 16 bytes apart (one per loaded DamageParam).
static std::vector<uint32_t> scan(){
    std::vector<uint32_t> found;std::vector<uint8_t> chunk((1u<<20)+64);
    for(uint64_t at=0x10000000;at<0x50000000;at+=1u<<20){
        size_t n=size_t(std::min<uint64_t>(chunk.size(),0x50000000-at));
        if(!read(uint32_t(at),chunk.data(),n))continue; // not mapped as a whole: resources live in the large committed heaps
        for(size_t i=0;i+52<=n&&i<(1u<<20);i+=4)
            if(be(&chunk[i])==kHashes[0]&&be(&chunk[i+16])==kHashes[1]&&be(&chunk[i+32])==kHashes[2]&&be(&chunk[i+48])==kHashes[3])found.push_back(uint32_t(at+i));
    }
    return found;
}
// The player's own: an object the actor points to (its parameter set) points
// to the resource that holds it. Several can match (route-43: three); the
// player's is then the only one whose fall damage is switched on at all
// (other resources keep the default start height of 10000).
struct Match {uint32_t at=0,valueAt=0,actorOffset=0,setOffset=0;float value=0;};
static std::vector<Match> ownedBy(uint32_t actor,const std::vector<uint32_t>& candidates){
    std::vector<Match> matches;uint8_t first[0x900],second[0x400],words[16];
    if(!read(actor,first,sizeof(first)))return matches;
    for(size_t i=0;i<sizeof(first);i+=4){
        uint32_t p1=be(first+i);if(!heapLike(p1)||!read(p1,second,sizeof(second)))continue;
        for(size_t j=0;j<sizeof(second);j+=4){
            uint32_t p2=be(second+j);if(!heapLike(p2))continue;
            auto hit=std::lower_bound(candidates.begin(),candidates.end(),p2);
            if(hit==candidates.end()||*hit-p2>=0x1000||std::any_of(matches.begin(),matches.end(),[&](const Match& m){return m.at==*hit;})||!read(*hit,words,sizeof(words)))continue;
            // The value follows the hash by 8 or 12 bytes depending on where the compiler put the vtable pointer.
            float a=real(be(words+8)),b=real(be(words+12));bool okA=std::isfinite(a)&&a>=.5f&&a<=1e7f,okB=std::isfinite(b)&&b>=.5f&&b<=1e7f;
            if(okA!=okB)matches.push_back({*hit,*hit+(okA?8u:12u),uint32_t(i),uint32_t(j),okA?a:b});
        }
    }
    return matches;
}
struct Located {uint32_t actor=0,hashAt=0,valueAt=0;float original=0;size_t candidates=0,matches=0;uint64_t ms=0;std::string detail;};
static std::atomic<std::shared_ptr<const Located>> located;static std::atomic<bool> scanning{false};
// The scan reads a gigabyte of guest memory (about 0.4 s): on its own thread,
// never on the worker, whose stall withdrew control in route-43.
static DWORD WINAPI locate(void* argument){
    auto result=std::make_shared<Located>();result->actor=uint32_t(uintptr_t(argument));auto began=GetTickCount64();
    auto candidates=scan();auto matches=ownedBy(result->actor,candidates);result->candidates=candidates.size();result->matches=matches.size();
    const Match* chosen=matches.size()==1?&matches[0]:nullptr;
    if(!chosen){unsigned enabled=0;for(const auto& m:matches)if(m.value<1000){chosen=&m;++enabled;}if(enabled!=1)chosen=nullptr;}
    if(chosen){result->hashAt=chosen->at;result->valueAt=chosen->valueAt;result->original=chosen->value;}
    for(size_t i=0;i<matches.size()&&i<8;i++){char text[96];snprintf(text,sizeof(text),"%s[%u,%u,%u,%.6g]",i?",":"",matches[i].at,matches[i].actorOffset,matches[i].setOffset,matches[i].value);result->detail+=text;}
    result->ms=GetTickCount64()-began;located.store(result);scanning=false;return 0;
}
static void put(float value){uint32_t v;memcpy(&v,&value,4);v=__builtin_bswap32(v);memcpy(static_cast<uint8_t*>(getGuest())+valueAt,&v,4);}
static void poll(){
    auto now=GetTickCount64();uint32_t actor=body::identityActor.load();
    {static uint64_t checked=0;if(now-checked>=500){checked=now;int value=1;std::ifstream(directory+"/damage-guard.txt")>>value;setting=value;int send=1;std::ifstream(directory+"/damage-to-minecraft.txt")>>send;forward=send!=0;int heal=1;std::ifstream(directory+"/heal-to-minecraft.txt")>>heal;forwardHeal=heal!=0;int follow=1;std::ifstream(directory+"/hearts.txt")>>follow;heartsMode=follow;int death=1;std::ifstream(directory+"/death.txt")>>death;deathMode=death;{int32_t v[5]{};std::ifstream codes(directory+"/death-hit.txt");int read=0;while(read<5&&codes>>v[read])read++;deathHitMode=read==5&&v[4]>0?2:read>=1&&v[0]==0?0:1;if(read==5)for(int i=0;i<5;i++)deathCodes[i]=v[i];}float height=25;std::ifstream(directory+"/climb-height.txt")>>height;if(height>=0&&height<=10000)climbHeight=height;float back=300;std::ifstream(directory+"/stamina-recover.txt")>>back;if(back>=0&&back<=10000)recoverRate=back;int mode=1;std::ifstream(directory+"/damage-event.txt")>>mode;eventMode=mode;}}
    // One BOTW heart is one Minecraft heart: a quarter heart is half a point of Minecraft damage.
    // SkyCraft's message carries Skyrim damage in hundredths and the mod divides it by five
    // (BotwCombat.BOTW_TO_MC_DAMAGE), hence 250 per quarter heart. The attacker is not known
    // here, so Minecraft takes it as generic damage: armour and its other rules apply.
    for(auto head=hitHead.load(std::memory_order_acquire);hitTail!=head;++hitTail){
        auto hit=hitRing[hitTail%32].load(std::memory_order_acquire);uint32_t quarters=uint32_t(hit>>32)&0x7FFFFFFF,attacker=uint32_t(hit);bool fire=hit>>63;
        bool sent=forward.load()&&physicalActive.load();
        // With an attacker Minecraft takes it as a mob's blow: knock-back away from its stand-in, and a raised shield stops it.
        // The game's fire is Minecraft's fire: its armour, fire protection and fire resistance count.
        // A blow that did not pass the defence hook (a thrown rock, a thrown Bokoblin, bare hands; route-376 to 380) has no
        // attacker yet: the objects near Link tell it (actors::hitSource), so Minecraft names who killed its player
        // ("BOTWCraft was slain by Bokoblin").
        bool thrown=false;char by[64]="";
        if(sent&&!attacker&&!fire)::actors::hitSource(attacker,thrown,by);
        auto kind=fire?botwcraft::proto::kHurtFire:thrown?botwcraft::proto::kHurtThrown:attacker?botwcraft::proto::kHurtMelee:botwcraft::proto::kHurtOther;
        if(sent){physical::enqueue({botwcraft::proto::kInHurt,uint16_t(kind),int(quarters)*250,int(attacker),0});++forwarded;}
        event("{\"event\":\"native_damage_to_minecraft\",\"quarter_hearts\":%u,\"minecraft_damage\":%.3g,\"sent\":%d,\"attacker\":%u,\"fire\":%d,\"thrown\":%d,\"found\":\"%s\"}",quarters,quarters*.5,int(sent),attacker,int(fire),int(thrown),by);
    }
    // Life the game gave back. It is kept while Minecraft has no control (the game's own menu, where one eats, has
    // taken it) and sent on when control is back.
    // Steve's most health is Link's most life (hearts(), above): sent when Minecraft's differs, once a second at most.
    {static uint64_t sentAt=0;int32_t quarters=linkMost.load();float has=live::mc.maxHealth;
        if(quarters>0&&heartsMode.load()&&physicalActive.load()&&has>0&&std::fabs(has-quarters*.5f)>.01f&&now-sentAt>=1000){
            sentAt=now;physical::enqueue({botwcraft::proto::kInMaxHealth,0,quarters*50,0,0});
            event("{\"event\":\"native_hearts_to_minecraft\",\"quarter_hearts\":%d,\"minecraft_max_health\":%.3g,\"minecraft_had\":%.3g}",quarters,quarters*.5,has);
        }}
    // The effects of what was eaten, kept like the life while Minecraft has no control. lab/effect.txt, for a run
    // without the food: "id amount seconds", sent once when the file's text changes.
    {static std::string had;static uint64_t checked=0;
        if(now-checked>=500){checked=now;std::string text;std::ifstream file(directory+"/effect.txt");std::getline(file,text);
            if(text!=had){had=text;int id=0,amount=0,seconds=0;if(sscanf(text.c_str(),"%d %d %d",&id,&amount,&seconds)>=2&&id>0&&id<=40&&amount>0){effectRing[effectHead.load()%8].store((uint64_t(id)<<48)|(uint64_t(amount)<<32)|uint32_t(seconds),std::memory_order_release);++effectHead;}}}}
    if(physicalActive.load())for(auto head=effectHead.load(std::memory_order_acquire);effectTail!=head;++effectTail){
        auto one=effectRing[effectTail%8].load(std::memory_order_acquire);int id=int(one>>48),amount=int((one>>32)&0xFFFF),seconds=int(uint32_t(one));
        physical::enqueue({botwcraft::proto::kInEffect,uint16_t(id),amount,seconds,0});
        event("{\"event\":\"native_effect_to_minecraft\",\"id\":%d,\"amount\":%d,\"seconds\":%d}",id,amount,seconds);
    }
    if(owedHeal.load()&&physicalActive.load()){
        auto quarters=owedHeal.exchange(0);bool sent=forwardHeal.load();
        if(sent){physical::enqueue({botwcraft::proto::kInHeal,0,int(quarters)*50,0,0});++healsSent;}
        healAt=now;
        event("{\"event\":\"native_heal_to_minecraft\",\"quarter_hearts\":%u,\"minecraft_health\":%.3g,\"sent\":%d}",quarters,quarters*.5,int(sent));
    }
    // The life the game set anew (above): after a heal of the same moment, which Minecraft takes first.
    if(int32_t most=linkMost.load();setLife.load()>0&&most>0&&heartsMode.load()&&physicalActive.load()&&now-setAt.load()>=300&&std::fabs(live::mc.maxHealth-most*.5f)<=.01f){
        int32_t life=setLife.exchange(0),red=std::min(life,most),yellow=std::min(life-red,120);
        physical::enqueue({botwcraft::proto::kInLife,0,red*50,yellow*50,0});setHold=now+1500;++lifeSent;
        event("{\"event\":\"native_life_to_minecraft\",\"life\":%d,\"most\":%d,\"minecraft_health\":%.3g,\"minecraft_yellow\":%.3g,\"minecraft_had\":[%.3g,%.3g]}",life,most,red*.5,yellow*.5,live::mc.health,live::mc.absorption);
    }
    if(auto quarters=owedQuarters.exchange(0)){
        bool sent=forward.load()&&physicalActive.load();
        if(sent){physical::enqueue({botwcraft::proto::kInHurt,botwcraft::proto::kHurtOther,int(quarters)*250,0,0});++forwarded;}
        event("{\"event\":\"native_damage_to_minecraft\",\"quarter_hearts\":%u,\"minecraft_damage\":%.3g,\"sent\":%d}",quarters,quarters*.5,int(sent));
    }
    if(valueAt){
        // Still the same object? (A reload frees and rebuilds resources.)
        uint8_t check[52];bool same=actor==owner&&read(hashAt,check,sizeof(check))&&be(check)==kHashes[0]&&be(check+16)==kHashes[1];
        if(!same){event("{\"event\":\"fall_guard\",\"state\":\"lost\",\"address\":%u}",valueAt);valueAt=hashAt=owner=0;raised=false;nextScan=now+1000;return;}
        // Steve falls as far without harm as Link does: Link's own height, in blocks, told
        // to Minecraft every five seconds while it has control. lab/safe-fall.txt: another height in blocks; 0: not told.
        {static uint64_t toldAt=0;float scale=float(nativeRegions::scale);
            if(physicalActive.load()&&now-toldAt>=5000&&scale>0&&original>0){
                toldAt=now;float blocks=original/scale;std::ifstream(directory+"/safe-fall.txt")>>blocks;
                if(blocks>=3&&blocks<=1024)physical::enqueue({botwcraft::proto::kInSafeFall,0,int(blocks*100+.5f),0,0});
            }}
        if(raising()!=raised){raised=raising();put(raised?kNever:original);event("{\"event\":\"fall_guard\",\"state\":\"%s\",\"address\":%u,\"original\":%.9g}",raised?"raised":"restored",valueAt,original);}
        return;
    }
    if(auto found=located.exchange(nullptr)){
        if(found->actor==actor&&found->valueAt){hashAt=found->hashAt;valueAt=found->valueAt;original=found->original;owner=actor;raised=false;}
        event("{\"event\":\"fall_guard\",\"state\":\"%s\",\"actor\":%u,\"candidates\":%zu,\"matches\":%zu,\"address\":%u,\"original\":%.9g,\"ms\":%llu,\"detail\":[%s]}",valueAt?"found":"not_found",found->actor,found->candidates,found->matches,valueAt,original,found->ms,found->detail.c_str());
        return;
    }
    if(!actor||!body::allowed()||!nativeRegions::enabled.load()||now<nextScan||scanning.exchange(true))return;
    nextScan=now+30000;
    if(auto thread=CreateThread(nullptr,0,locate,reinterpret_cast<void*>(uintptr_t(actor)),0,nullptr))CloseHandle(thread);else scanning=false;
}
}
// Diagnostic bits for liveGameplayLease: 1 inactive, 2 blocked, 4 not ready,
// 16 native not allowed, 32 stale MC view. (8 was "the window not in front",
// which ended the lease until 2026-10-12; it no longer does.)
static int liveGameplayReason(){
    return (!physicalActive.load())|physicalBlocked.load()<<1|(!physical::ready.load())<<2|
        (!body::allowed())<<4|(GetTickCount64()-peerViewAt.load()>=250)<<5;
}
static bool liveGameplayLease(){
    return physicalActive.load()&&!physicalBlocked.load()&&physical::ready.load()&&
        body::allowed()&&GetTickCount64()-peerViewAt.load()<250;
}
// Nothing is wrong but Minecraft's silence, and it has not lasted long: a freeze of its client (route-295: five of
// 0.3 s in a minute, while a disk was busy). The body is held and control is kept (body.h).
static bool liveGameplayHeld(){   // physical::ready is not asked: it falls with the same silence, after 500 ms
    return physicalActive.load()&&!physicalBlocked.load()&&body::allowed()&&liveBodyLease.load()&&
        live::connected&&!live::failed&&(live::mc.flags&botwcraft::proto::kMcInWorld)&&GetTickCount64()-peerViewAt.load()<staleHoldMs.load();
}
static void physicalFrame(uint32_t frame,ViewProbe& request){
    if(physicalActive.load()&&!physicalBlocked.load()){
        physical::unpack(physicalAngles.load(),request.yaw,request.pitch);
        if(body::command.load()==5){
            auto applied=appliedTarget.load();
            if(applied&&liveBodyLease.load()&&applied->request.bodyEpoch==body::lifecycleEpoch.load()&&applied->request.collisionEpoch==liveCollisionEpoch.load()){
                for(int i=0;i<3;i++)request.eye[i]=applied->feet[i]+(i==1?applied->eyeHeight:0);
                request.mcSeq=applied->request.source.seq;
                // Minecraft's view and how far its camera stands off, as of its newest frame.
                botwcraft::proto::McState newest=applied->request.source;liveMcNow(newest);
                request.view=int(newest.cameraMode);request.distance=newest.cameraDistance*applied->request.scale;std::copy_n(applied->feet,3,request.feet);request.hasFeet=true;
                event("{\"event\":\"live_camera_body\",\"frame\":%u,\"body_seq\":%llu,\"source_seq\":%u,\"eye\":[%.9g,%.9g,%.9g]}",frame,applied->sequence,request.mcSeq,request.eye[0],request.eye[1],request.eye[2]);
            }
        }
        // Sole physical host-state publication at the frozen native-frame boundary.
        // Mapping lifetime uses a nonblocking local claim; no peer wait or disk I/O.
        if(!botwWriteBusy.test_and_set(std::memory_order_acquire)){
            if(live::base){auto dst=live::at<live::p::BotwState>(live::p::kOffBotwState);auto seq=live::atom(dst->seq);auto value=seq.load();seq.store(value+1,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_release);dst->yaw=request.yaw;dst->pitch=request.pitch;seq.store(value+2,std::memory_order_release);}
            botwWriteBusy.clear(std::memory_order_release);
        }

        event("{\"event\":\"physical_frame\",\"frame\":%u,\"mc_seq\":%u,\"yaw\":%.9g,\"pitch\":%.9g}",frame,request.mcSeq,request.yaw,request.pitch);
    }
}
