// A search of the game's memory for a text, for finding where the game keeps something (a lab tool, as the
// procs dump; report 87: the names of Link's inventory). lab/memfind.txt: "<number> <text>", a space in the text
// read as _ (for the route's set: word); a new number starts a search on a thread of its own. Up to 64 places go
// to lab/memfind.jsonl, each with 0x40 bytes before and 0x100 from the text, in hex.
namespace memfind {
static std::atomic<bool> busy{false};static std::string wanted;static int run=0;
static DWORD WINAPI search(void*){
    auto began=GetTickCount64();std::vector<uint8_t> chunk((1u<<20)+256);std::vector<uint32_t> hits;
    const auto& t=wanted;
    for(uint64_t at=0x10000000;at<0x50000000&&hits.size()<64;at+=1u<<20){
        size_t n=size_t(std::min<uint64_t>(chunk.size(),0x50000000-at));
        if(!nativeDamage::read(uint32_t(at),chunk.data(),n))continue;
        for(size_t i=0;i+t.size()<=n&&i<(1u<<20)&&hits.size()<64;i++)
            if(chunk[i]==uint8_t(t[0])&&!memcmp(&chunk[i],t.data(),t.size()))hits.push_back(uint32_t(at+i));
    }
    if(FILE* f=fopen((directory+"/memfind.jsonl").c_str(),"a")){
        fprintf(f,"{\"run\":%d,\"text\":\"%s\",\"hits\":%zu,\"ms\":%llu}\n",run,t.c_str(),hits.size(),(unsigned long long)(GetTickCount64()-began));
        for(uint32_t h:hits){
            uint8_t raw[0x140];char text[2*sizeof(raw)+1]="";
            if(nativeDamage::read(h-0x40,raw,sizeof(raw)))nativeDamage::hex(raw,sizeof(raw),text);
            fprintf(f,"{\"run\":%d,\"at\":%u,\"from\":%u,\"hex\":\"%s\"}\n",run,h,h-0x40,text);
        }
        fclose(f);
    }
    busy=false;return 0;
}
static void poll(){
    static uint64_t checked=0;if(GetTickCount64()-checked<1000||busy.load())return;checked=GetTickCount64();
    int n=0;std::string t;std::ifstream file(directory+"/memfind.txt");if(!(file>>n)||n==run)return;std::getline(file>>std::ws,t);
    for(auto& c:t)if(c==' ')c='_';while(!t.empty()&&(t.back()==13||t.back()==10))t.pop_back();if(t.size()<4)return;
    run=n;wanted=t;busy=true;if(auto thread=CreateThread(nullptr,0,search,nullptr,0,nullptr))CloseHandle(thread);else busy=false;
}
}
