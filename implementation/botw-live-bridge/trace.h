// Worker-owned traces: current file plus two previous 8 MiB segments.
#pragma once
namespace trace {
static constexpr long long limit=8*1024*1024;
static uint64_t frameRotations=0,poseRotations=0,writeDrops=0;
static uint64_t collisionClears=0,renderClears=0,cacheWipes=0;
static bool room(FILE*& file,const std::string& path,const char* header,uint64_t& rotations,long long cap=limit){
    if(!file)return false;
    if(_ftelli64(file)<cap)return true;
    fflush(file);fclose(file);file=nullptr;
    // A sharing violation must not truncate the current trace. Retry later;
    // dropped rows are counted explicitly while a reader prevents rotation.
    auto old=path+".1",older=path+".2";
    bool moved=GetFileAttributesA(old.c_str())==INVALID_FILE_ATTRIBUTES||MoveFileExA(old.c_str(),older.c_str(),MOVEFILE_REPLACE_EXISTING);
    moved=moved&&MoveFileExA(path.c_str(),old.c_str(),MOVEFILE_REPLACE_EXISTING);
    file=fopen(path.c_str(),moved?"w":"a");
    if(!moved||!file){++writeDrops;return false;}
    ++rotations;if(header)fputs(header,file);return true;
}
// Every claim and release of control with its cause, kept for the whole session in
// control.jsonl. frames.jsonl rotates: of a 670 s play session in route-65 the last 196 s were left.
// Worker-owned like the other traces; a session writes a few kilobytes.
static void control(const char* text){
    static const char* names[]{"authority_release\"","physical_release\"","physical_claim\"","physical_background\"","input_pipe\"","actor_spawn\"","weapon_sensor\"","swing\"","track\"","look_throw\"","native_mover\"","native_ladders\"","native_movers\"","lab_vector\"","walls\"","pad\"","game_event\"","frame_start\"","native_pieces\"","native_filtered\"","native_carried\"","map_context\"","rune_state\"","rune_follow\"","rune_flags\"","runes_owned\"","native_life\"","native_death\"","native_death_hit\"","native_life_to_minecraft\"","save_link\"","native_effect\"","native_effect_to_minecraft\"","native_hearts\"","native_hearts_to_minecraft\"","native_recover\"","native_eaten\"","native_heal_to_minecraft\"","physical_focus\"","physical_pointer\"","native_attack_any\"","physical_claim_reject\"","body_follow\"","body_hold\"","scene_over\"","scene_body\"","scene_slate\"","skeleton_actor\"","skeleton_raw\"","skeleton_bones\"","lifecycle\"","native_health\"","native_damage_to_minecraft\"","native_add_damage\"","native_damage_receive\"","native_attack\"","actor_table\"","actor_hit\"","water_grid\"","native_stamina\"","native_climb\"","native_stamina_fields\"","hud_hide\"","fall_guard\"","ipc_fault\"","native_region_reset\"","native_coverage_park\"","walk_stick\"","walk_drive\"","pouch\""};
    if(strncmp(text,"{\"event\":\"",10)!=0)return;
    bool keep=false;for(auto name:names)keep|=strncmp(text+10,name,strlen(name))==0;
    if(!keep)return;
    static FILE* file=fopen((directory+"/control.jsonl").c_str(),"a");if(!file)return;
    FILETIME now;GetSystemTimeAsFileTime(&now);
    uint64_t ms=(((uint64_t(now.dwHighDateTime)<<32)|now.dwLowDateTime)-116444736000000000ULL)/10000;
    fprintf(file,"{\"unix_ms\":%llu,%s\n",(unsigned long long)ms,text+1);fflush(file);
}
static void stats(){
    std::ofstream f(directory+"/trace-stats.json");
    f<<"{\"segment_limit_bytes\":"<<limit<<",\"retained_previous_segments\":2,\"frame_rotations\":"<<frameRotations
     <<",\"pose_rotations\":"<<poseRotations<<",\"write_drops\":"<<writeDrops
     <<",\"collision_clears\":"<<collisionClears<<",\"render_clears\":"<<renderClears<<",\"cache_wipes\":"<<cacheWipes<<'}';
}
}
