// Lab, for report 73 (where the game keeps the way a thing held by Stasis will fly).
// lab/vector-scan.txt `TOKEN x y z DEGREES`: every place in the game's memory (10000000 to 50000000, each fourth
// byte) where three floats point within DEGREES of (x, y, z) and are between 0.05 and 1000000 long, to
// lab/vector-scan-TOKEN.json, with what the same places hold 1.5, 4 and 10 seconds later. On a thread of its own.
// lab/vector-poke.txt `ADDRESS(hex) x y z MS`: for MS the three floats at that place are written every pass of the
// worker (a lab's poke, not at an update point of the game's).
namespace labVector {
static std::atomic<bool> scanning{false};
struct Ask {uint64_t token=0;float want[3]{};float degrees=1;};
static Ask ask;
static DWORD WINAPI scan(void*){
    Ask a=ask;auto began=GetTickCount64();std::vector<uint8_t> chunk((1u<<20)+16);std::vector<std::pair<uint32_t,std::array<float,3>>> found;
    float length=std::sqrt(a.want[0]*a.want[0]+a.want[1]*a.want[1]+a.want[2]*a.want[2]),least=std::cos(a.degrees*.0174532925f);
    auto real=[](const uint8_t* p){uint32_t v=nativeDamage::be(p);float f;memcpy(&f,&v,4);return f;};
    for(uint64_t at=0x10000000;at<0x50000000&&length>0&&found.size()<4000;at+=1u<<20){
        size_t n=size_t(std::min<uint64_t>(chunk.size(),0x50000000-at));
        if(!nativeDamage::read(uint32_t(at),chunk.data(),n))continue;
        for(size_t i=0;i+12<=n&&i<(1u<<20);i+=4){
            uint32_t first=nativeDamage::be(&chunk[i])&0x7F800000u;if(first<0x3A000000u||first>0x4A000000u)continue;   // quick: the first float is of a plain size
            float v[3]{real(&chunk[i]),real(&chunk[i+4]),real(&chunk[i+8])},l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
            if(!std::isfinite(l)||l<.05f||l>1e6f)continue;
            if((v[0]*a.want[0]+v[1]*a.want[1]+v[2]*a.want[2])/(l*length)<least)continue;
            found.push_back({uint32_t(at+i),{v[0],v[1],v[2]}});if(found.size()>=4000)break;
        }
    }
    uint64_t took=GetTickCount64()-began;
    std::vector<std::vector<std::array<float,3>>> later;const DWORD waits[]{1500,2500,6000};
    for(DWORD wait:waits){
        Sleep(wait);std::vector<std::array<float,3>> now;
        for(auto& f:found){uint8_t raw[12]{};std::array<float,3> v{};if(nativeDamage::read(f.first,raw,12))for(int k=0;k<3;k++)v[k]=real(raw+k*4);now.push_back(v);}
        later.push_back(std::move(now));
    }
    std::ofstream out(directory+"/vector-scan-"+std::to_string(a.token)+".json");out.precision(8);
    out<<"{\"token\":"<<a.token<<",\"ms\":"<<took<<",\"found\":[";
    for(size_t i=0;i<found.size();i++){
        out<<(i?",":"")<<"["<<found[i].first<<","<<found[i].second[0]<<","<<found[i].second[1]<<","<<found[i].second[2];
        for(auto& l:later)for(int k=0;k<3;k++){float v=l[i][k];out<<","<<(std::isfinite(v)?v:0.f);}
        out<<"]";
    }
    out<<"]}";
    scanning=false;return 0;
}
// lab/dump.txt `TOKEN ROOT(hex) [ROOT ...]`: each root's first 0x2000 bytes, the 0x400 bytes at every place those
// point to, and the 0x100 bytes at every place these point to (the game's memory only, each place once), to
// lab/dump-TOKEN.bin as records of address, size (two little-endian words) and the bytes. On the worker.
static void dump(uint64_t token,const std::vector<uint32_t>& roots){
    auto began=GetTickCount64();std::unordered_set<uint32_t> seen;std::vector<std::pair<uint32_t,uint32_t>> level{},next;
    for(uint32_t r:roots)if(r>=0x10000000&&r<0x50000000)level.push_back({r&~3u,0x2000});
    FILE* out=fopen((directory+"/dump-"+std::to_string(token)+".bin").c_str(),"wb");if(!out)return;
    uint32_t blocks=0;uint64_t bytes=0;std::vector<uint8_t> data(0x2000);
    for(int depth=0;depth<3&&!level.empty();depth++){
        next.clear();uint32_t child=depth==0?0x400:0x100;
        for(auto [at,size]:level){
            if(!seen.insert(at).second||bytes>(48u<<20))continue;
            if(!nativeDamage::read(at,data.data(),size)){size=std::min<uint32_t>(size,0x100);if(!nativeDamage::read(at,data.data(),size))continue;}
            uint32_t head[2]{at,size};fwrite(head,4,2,out);fwrite(data.data(),1,size,out);++blocks;bytes+=size;
            if(depth<2)for(uint32_t i=0;i+4<=size;i+=4){uint32_t v=nativeDamage::be(&data[i]);if(v>=0x10000000&&v<0x50000000&&!(v&3)&&!seen.count(v))next.push_back({v,child});}
        }
        level.swap(next);
    }
    fclose(out);
    event("{\"event\":\"lab_vector\",\"state\":\"dump\",\"token\":%llu,\"blocks\":%u,\"bytes\":%llu,\"ms\":%llu}",(unsigned long long)token,blocks,(unsigned long long)bytes,(unsigned long long)(GetTickCount64()-began));
}
// lab/find.txt `TOKEN TEXT [TEXT ...]`: every place in the game's memory (10000000 to 80000000) where a TEXT stands
// as a whole string (a zero after it), every aligned word that points at one of these places, and what stands
// around both and at the places the words around such a pointer point to (two steps): lab/find-TOKEN.bin, records
// as dump's, and lab/find-TOKEN.json, the places. On a thread of its own. (Report 82: the game's layouts by name.)
struct Find {uint64_t token=0;std::vector<std::string> texts;};
static Find finding;static std::atomic<bool> finds{false};
static DWORD WINAPI find(void*){
    Find a=finding;auto began=GetTickCount64();std::vector<uint8_t> chunk((1u<<20)+64);
    std::vector<std::pair<uint32_t,int>> hits;std::vector<std::pair<uint32_t,uint32_t>> refs;
    auto each=[&](auto&& with){
        for(uint64_t at=0x10000000;at<0x80000000;at+=1u<<20){
            size_t n=chunk.size();if(!nativeDamage::read(uint32_t(at),chunk.data(),n)){n=1u<<20;if(!nativeDamage::read(uint32_t(at),chunk.data(),n))continue;}
            with(uint32_t(at),n);
        }
    };
    each([&](uint32_t at,size_t n){
        for(size_t t=0;t<a.texts.size()&&hits.size()<512;t++){
            const std::string& s=a.texts[t];size_t len=s.size()+1,limit=std::min<size_t>(n>=len?n-len+1:0,1u<<20);
            for(size_t i=0;i<limit&&hits.size()<512;){
                auto p=static_cast<const uint8_t*>(memchr(&chunk[i],s[0],limit-i));if(!p)break;
                i=size_t(p-chunk.data());if(!memcmp(p,s.c_str(),len))hits.push_back({uint32_t(at+i),int(t)});++i;
            }
        }
    });
    std::unordered_set<uint32_t> wanted;uint32_t lo=~0u,hi=0;for(auto& h:hits){wanted.insert(h.first);lo=std::min(lo,h.first);hi=std::max(hi,h.first);}
    if(!hits.empty())each([&](uint32_t at,size_t){
        for(size_t i=0;i<(1u<<20)&&refs.size()<2048;i+=4){uint32_t v=nativeDamage::be(&chunk[i]);if(v>=lo&&v<=hi&&wanted.count(v))refs.push_back({uint32_t(at+i),v});}
    });
    uint64_t bytes=0;uint32_t blocks=0;
    if(FILE* out=fopen((directory+"/find-"+std::to_string(a.token)+".bin").c_str(),"wb")){
        std::unordered_set<uint32_t> seen;std::vector<uint8_t> data(0x400);std::vector<std::pair<uint32_t,uint32_t>> level,next;
        auto put=[&](uint32_t at,uint32_t size)->bool{
            at&=~3u;if(at<0x10000000||!seen.insert(at).second||bytes>(40u<<20)||!nativeDamage::read(at,data.data(),size))return false;
            uint32_t head[2]{at,size};fwrite(head,4,2,out);fwrite(data.data(),1,size,out);++blocks;bytes+=size;return true;
        };
        for(auto& h:hits)put(h.first-0x80,0x180);
        for(auto& r:refs)if(put(r.first-0x100,0x300))for(uint32_t i=0;i+4<=0x300;i+=4){uint32_t v=nativeDamage::be(&data[i]);if(v>=0x10000000&&v<0x80000000&&!(v&3))level.push_back({v,0x200});}
        for(int depth=0;depth<2;depth++){
            next.clear();
            for(auto [at,size]:level)if(put(at,size)&&depth==0)for(uint32_t i=0;i+4<=size;i+=4){uint32_t v=nativeDamage::be(&data[i]);if(v>=0x10000000&&v<0x80000000&&!(v&3))next.push_back({v,0x100});}
            level.swap(next);
        }
        fclose(out);
    }
    {std::ofstream out(directory+"/find-"+std::to_string(a.token)+".json");
        out<<"{\"token\":"<<a.token<<",\"ms\":"<<GetTickCount64()-began<<",\"texts\":[";for(size_t i=0;i<a.texts.size();i++)out<<(i?",":"")<<"\""<<a.texts[i]<<"\"";
        out<<"],\"hits\":[";for(size_t i=0;i<hits.size();i++)out<<(i?",":"")<<"["<<hits[i].first<<","<<hits[i].second<<"]";
        out<<"],\"refs\":[";for(size_t i=0;i<refs.size();i++)out<<(i?",":"")<<"["<<refs[i].first<<","<<refs[i].second<<"]";
        out<<"]}";}
    event("{\"event\":\"lab_vector\",\"state\":\"find\",\"token\":%llu,\"hits\":%zu,\"refs\":%zu,\"blocks\":%u,\"bytes\":%llu,\"ms\":%llu}",(unsigned long long)a.token,hits.size(),refs.size(),blocks,(unsigned long long)bytes,(unsigned long long)(GetTickCount64()-began));
    finds=false;return 0;
}
static void poll(){
    static uint64_t checked=0,done=0,pokeUntil=0;static uint32_t pokeAt=0;static float poke[3]{};uint64_t now=GetTickCount64();
    if(pokeAt&&now<pokeUntil&&body::readable(pokeAt,12))for(int k=0;k<3;k++){uint32_t raw;memcpy(&raw,&poke[k],4);body::put32(pokeAt+k*4,raw);}
    if(now-checked<200)return;checked=now;
    {auto path=directory+"/vector-poke.txt";
        if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES){uint32_t at=0;float v[3]{};uint64_t ms=0;{std::ifstream file(path);file>>std::hex>>at>>std::dec>>v[0]>>v[1]>>v[2]>>ms;}
            MoveFileExA(path.c_str(),(directory+"/vector-poke.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);
            if(at>=0x10000000&&ms&&std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2])){pokeAt=at;std::copy_n(v,3,poke);pokeUntil=now+std::min<uint64_t>(ms,60000);event("{\"event\":\"lab_vector\",\"state\":\"poke\",\"at\":%u,\"v\":[%.5f,%.5f,%.5f],\"ms\":%llu}",at,v[0],v[1],v[2],(unsigned long long)ms);}}}
    {static uint64_t found=0;Find f;{std::ifstream file(directory+"/find.txt");if(file>>f.token){std::string text;while(file>>text)if(text.size()>=4&&text.find_first_of("\"\\")==std::string::npos)f.texts.push_back(text);}}
        if(f.token&&f.token!=found&&!f.texts.empty()&&!finds.exchange(true)){
            found=f.token;finding=f;if(HANDLE thread=CreateThread(nullptr,0,find,nullptr,0,nullptr))CloseHandle(thread);else finds=false;}}
    {auto path=directory+"/dump.txt";
        if(GetFileAttributesA(path.c_str())!=INVALID_FILE_ATTRIBUTES){uint64_t token=0;std::vector<uint32_t> roots;{std::ifstream file(path);file>>token;uint32_t r;while(file>>std::hex>>r)roots.push_back(r);}
            MoveFileExA(path.c_str(),(directory+"/dump.done.txt").c_str(),MOVEFILE_REPLACE_EXISTING);if(token&&!roots.empty())dump(token,roots);}}
    Ask a;{std::ifstream file(directory+"/vector-scan.txt");if(!(file>>a.token>>a.want[0]>>a.want[1]>>a.want[2]>>a.degrees))return;}
    if(!a.token||a.token==done||scanning.exchange(true))return;
    done=a.token;ask=a;
    if(HANDLE thread=CreateThread(nullptr,0,scan,nullptr,0,nullptr))CloseHandle(thread);else scanning=false;
    event("{\"event\":\"lab_vector\",\"state\":\"scan\",\"token\":%llu,\"want\":[%.5f,%.5f,%.5f],\"degrees\":%.3f}",(unsigned long long)a.token,a.want[0],a.want[1],a.want[2],a.degrees);
}
}
