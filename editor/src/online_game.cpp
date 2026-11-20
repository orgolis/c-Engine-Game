#include "online_game.h"
#include "script_system.h"
#include <cstdint>
#include <cctype>
#include <future>
#include "net_session.h"
#include "lobby_client.h"
#include "game_session_protocol.h"
#include "ecs_bridge.h"
#include "scene_playback_manager.h"
#include "script_api_editor.h"
#include "player_weapon.h"
#include "player_abilities.h"
#include "play_ui.h"
#include "scene.h"
#include "entity.h"
#include "transform.h"
#include "mesh_renderer_component.h"
#include "ecs/gameplay_skills.h"
#include "ecs/gameplay_loadout.h"
#include "ecs/gameplay_life.h"
#include "ecs/gameplay_inventory.h"
#include "ecs/gameplay_interaction.h"
#include "ecs/gameplay_progression.h"
#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace schizo::editor {
namespace ecs=schizo::ecs;namespace wire=engine::network::game;
using wire::Writer;using wire::Reader;using wire::Message;
namespace {
void vec(Writer& w,glm::vec3 v){w.pod(v.x);w.pod(v.y);w.pod(v.z);}
glm::vec3 vec(Reader& r){float x=r.number(),y=r.number(),z=r.number();return{x,y,z};}
template<class T>void component(Writer& out,ecs::World& w,ecs::Entity e,void(*save)(const void*,std::vector<uint8_t>&)){
 std::vector<uint8_t> data;if(auto* c=w.try_get<T>(e))save(c,data);out.blob(data);
}
template<class T>void component(Reader& in,ecs::World& w,ecs::Entity e,void(*load)(void*,const uint8_t*,size_t)){
 auto data=in.blob();if(data.empty())return;if(!w.has<T>(e))w.add<T>(e,T{});load(&w.get<T>(e),data.data(),data.size());
}
bool safe_asset(const std::string& path){return path.empty()||(path.rfind("assets/",0)==0&&path.find("..") ==std::string::npos&&path.size()<512);}

std::vector<std::string> split(const std::string& value) {
    std::vector<std::string> parts;
    std::istringstream input(value);
    for (std::string part; std::getline(input, part, ',');)
        if (!part.empty()) parts.push_back(part);
    return parts;
}

struct SessionMode {
    std::string key, title, objective, victory_flag, victory_text, diagnostic_actor;
    int players = 2, rounds_to_win = 0, health = 0, skill_points = 0;
    float arena_radius = 0, respawn_seconds = 0, wipe_seconds = 0;
    glm::vec3 guest_offset{};
};

uint64_t fingerprint(const std::string& scene, std::vector<std::string> files) {
    files.insert(files.end(), {scene, scene + ".gameplay", "config/multiplayer.ini"});
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it("assets/gameplay", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file() && it->path().extension() == ".items") files.push_back(it->path().generic_string());
    std::sort(files.begin(), files.end());
    uint64_t hash = 14695981039346656037ull;
    for (const auto& path : files) {
        std::ifstream file(path, std::ios::binary);
        char buffer[16384];
        while (file) {
            file.read(buffer, sizeof buffer);
            for (std::streamsize i = 0; i < file.gcount(); ++i)
                hash = (hash ^ static_cast<uint8_t>(buffer[i])) * 1099511628211ull;
        }
    }
    return hash;
}

}
struct OnlineGame::Impl {
 bool configured=false,session=false,is_client=false,menu=true,ready=false,hello=false,first_state=true,initialized=false,show_session=false;
 int mode=0,selected_mode=1,host_score=0,guest_score=0,round=1;uint32_t epoch=1;
 std::map<int,std::string> maps;std::string start_scene,travel,status="",result;
 std::map<int, SessionMode> modes;
 std::map<std::string,std::string> settings;
 std::vector<std::string> flags, hidden_ui, sync_ui, compatibility_files;
 std::vector<std::pair<std::string,int>> supplies;
 std::string endpoint = "127.0.0.1:7777", previous_endpoint;
 bool editing_endpoint = false, local_session = false;
 // Lobby codes (config lobby_server=host:port): online_lobby_<n> hosts mode n
 // with a code, online_code is where one is typed, online_join_code joins.
 std::string lobby_server, code_input, previous_code, shown_code;
 bool editing_code = false, via_lobby = false;
 std::future<engine::network::lobby::JoinResult> lookup;
 void start_code_join(){
  namespace lb=engine::network::lobby;
  if(lookup.valid())return;
  if(code_input.empty())code_input=previous_code;
  const std::string code=lb::normalize_code(code_input);
  if(code.empty()){status=lb::explain_error("code");return;}
  lb::Endpoint server;
  if(lobby_server.empty()||!lb::parse_endpoint(lobby_server,server)){status="This game has no lobby server (lobby_server in config/multiplayer.ini).";return;}
  code_input=code;status="Looking for game "+code+"...";
  lookup=std::async(std::launch::async,[server,game=settings["game_id"],code]{return lb::resolve(server,game,code);});
 }
 std::future<std::string> preparation;
 std::string preparing_scene;

 bool events_bound=false;
 ecs::WeaponState base_weapon;
 ecs::SkillTree base_tree;
 ecs::UnlockedSkills base_skills;
 const SessionMode& rules() const {
     static const SessionMode defaults;
     auto it = modes.find(mode);
     return it == modes.end() ? defaults : it->second;
 }
 bool rounds() const { return rules().rounds_to_win > 0; }
 bool shares_ui(const std::string& file) const {
     return std::find(sync_ui.begin(), sync_ui.end(), file) != sync_ui.end();
 }
 void action(const std::string&);


 bool culled=false;std::set<uint32_t> tracked_objects;
 bool full_snapshot=true;std::map<std::pair<uint32_t,int>,std::vector<uint8_t>> sent_components;std::set<uint32_t> sent_metadata;
 template<class T>void delta(Writer& out,ecs::World& w,ecs::Entity e,void(*save)(const void*,std::vector<uint8_t>&),int slot){if(!w.valid(e)){out.blob({});return;}std::vector<uint8_t> bytes;if(auto* value=w.try_get<T>(e))save(value,bytes);auto& previous=sent_components[{uint32_t(e),slot}];const auto* tags=w.try_get<ecs::GameplayTags>(e);bool player=tags&&tags->has("network.fighter");if(!full_snapshot&&!player&&previous==bytes)out.blob({});else out.blob(bytes);previous=std::move(bytes);}
 int diagnostic_second=-1;
 char address[128]="127.0.0.1";int port=7777;
 float age=0,send_timer=0,connect_timer=0,countdown=0,round_delay=0,empty_time=0;uint64_t snapshot=0,last_snapshot=0;
 NetSession* net=nullptr;ScenePlaybackManager* play=nullptr;EcsSceneBridge* bridge=nullptr;EditorScriptCtx* ctx=nullptr;PlayEffects* effects=nullptr;
 std::shared_ptr<scene::Scene> scene;
 ecs::AttributeSet base_attributes;ecs::Inventory base_inventory;ecs::WeaponLoadout base_loadout;ecs::AbilityBar base_bar;ecs::Progression base_progress;
 std::map<uint32_t,bool> peer_ready;std::map<uint32_t,float> welcomes;
 std::map<uint32_t,uint32_t> authored;std::vector<uint32_t> authored_local;
 std::map<uint64_t,uint32_t> fighters;std::map<uint32_t,uint32_t> replica;std::set<uint32_t> spawned,scene_ids;
 std::set<std::string> sent_skills;std::map<std::string,std::string> sent_equipment;
 struct Remote {std::string weapon,ability;glm::vec3 origin{},aim{0,0,-1};uint32_t buttons=0;uint64_t seq=0;int pressed=-1;bool held=false,moving=false;float timeout=0;};
 uint64_t input_seq=0;struct Prediction{uint64_t seq;std::string weapon;};std::vector<Prediction> predicted;
 std::map<uint64_t,uint32_t> held_guns;std::map<uint64_t,uint32_t> respawns;std::map<uint64_t,uint32_t> received_respawns;
 std::map<uint32_t,Remote> inputs;std::map<uint64_t,float> death_time;
 glm::vec3 spawn_point{};
 struct Smooth{glm::vec3 p,s;glm::quat q;};std::map<uint32_t,Smooth> smooth;
 struct Visual {int type=0;uint32_t id=0;std::string a,b,c;glm::vec3 p{},d{};float s=1,r=0;uint32_t flags=0;};
 std::vector<Visual> visuals;std::map<std::pair<uint32_t,std::string>,Visual> bones;
 std::map<uint32_t,Visual> clips;std::map<uint32_t,uint64_t> clip_serial;uint64_t next_clip=1;
 std::map<std::string,std::string> texts;std::set<std::string> shown_ui;
 ecs::Entity entity(uint32_t id){auto e=scene?scene->GetEntityById(id):nullptr;return e?static_cast<ecs::Entity>(bridge->ecs_entity_id(e->GetTransform())):ecs::null_entity;}
 uint32_t local_id(){auto* p=play?play->GetPlayerEntity():nullptr;return p?p->GetId():0;}
 uint64_t own_avatar()const{return net?net->status().my_avatar:0;}

 void reset_scene(){fighters.clear();replica.clear();spawned.clear();scene_ids.clear();inputs.clear();death_time.clear();sent_skills.clear();sent_equipment.clear();visuals.clear();bones.clear();clips.clear();texts.clear();shown_ui.clear();initialized=false;first_state=true;age=0;snapshot=last_snapshot=0;tracked_objects.clear();culled=false;send_timer=0;sent_components.clear();sent_metadata.clear();full_snapshot=true;round_delay=empty_time=0;countdown=rounds()?5:0;}
 void leave(const std::string& why){via_lobby=false;shown_code.clear();if(net&&session&&!local_session){Writer bye(is_client?Message::Leave:Message::Reject,epoch);if(!is_client)bye.str("The host closed the session.");net->send_game(0,bye.bytes);}if(net)net->shutdown();session=false;is_client=false;ready=false;menu=true;mode=0;travel=start_scene;status=why;result.clear();show_session=false;reset_scene();}
 void begin(int m,bool host,bool local_only=false){
  if(!net)return;net->shutdown();local_session=local_only;mode=host?m:0;reset_scene();session=true;is_client=!host;ready=false;hello=false;menu=!host;selected_mode=m;mode=host?m:0;++epoch;host_score=guest_score=0;round=1;result.clear();connect_timer=0;peer_ready.clear();welcomes.clear();
  shown_code.clear();
  bool ok=local_only || (host?(via_lobby?net->host_lobby(lobby_server,settings["game_id"],modes.at(m).players,modes.at(m).title)
                                        :net->host(uint16_t(port),modes.at(m).players-1)):net->join(address,uint16_t(port)));
  if(!ok){session=false;menu=true;via_lobby=false;status="Could not open the connection. Check the address and port.";return;}
  status=host?(via_lobby?"Opening an online game...":"Hosting. Share your address and port with your friends."):"Connecting to host...";
  if(host){travel=via_lobby?std::string():maps[m];ready=false;}   // through a lobby: travel once the code is there
  if(host&&via_lobby)spdlog::info("[online] host mode {} through the lobby server {}",m,lobby_server);
  else spdlog::info("[online] {} mode {} on port {}",local_only?"local":host?"host":"join",m,port);
 }
 void send(Message type,const std::string& text,uint32_t peer=0){Writer w(type,epoch);w.str(text);net->send_game(peer,w.bytes);}
 void welcome(uint32_t peer){Writer w(Message::Welcome,epoch);w.pod(uint32_t(mode));w.str(maps[mode]);w.pod(fingerprint(maps[mode],compatibility_files));net->send_game(peer,w.bytes);welcomes[peer]=age;}
 void place(uint32_t id,glm::vec3 p){if(id==local_id())play->TeleportPlayer(p,glm::quat(1,0,0,0));else if(auto e=scene->GetEntityById(id))e->GetTransform()->SetWorldPosition(p);}
 void fighter_setup(uint64_t avatar,uint32_t id,bool local){
  auto& w=bridge->world();auto e=entity(id);if(!w.valid(e))return;
  if(!local){w.add<ecs::AttributeSet>(e,base_attributes);w.add<ecs::Inventory>(e,base_inventory);w.add<ecs::WeaponLoadout>(e,base_loadout);w.add<ecs::AbilityBar>(e,base_bar);w.add<ecs::Progression>(e,base_progress);w.add<ecs::CombatActor>(e,ecs::CombatActor{});w.add<ecs::SkillTree>(e,base_tree);w.add<ecs::UnlockedSkills>(e,base_skills);w.add<ecs::WeaponState>(e,base_weapon);}
  w.remove<ecs::Mortal>(e);
  ecs::CombatActor body;body.hurt_radius=.65f;body.hurt_height=.5f;w.add<ecs::CombatActor>(e,body);
  if(!w.has<ecs::GameplayTags>(e))w.add<ecs::GameplayTags>(e,ecs::GameplayTags{});
  auto& tags=w.get<ecs::GameplayTags>(e);tags.remove_exact("team.player");tags.add(rounds()?(avatar==1000000?"team.duelist.host":"team.duelist.guest"):"team.player");tags.add("network.fighter");
  if(rounds()){auto& a=w.get<ecs::AttributeSet>(e);if(rules().health>0)a.define("Health",rules().health,0,rules().health);w.get<ecs::Progression>(e).skill_points=rules().skill_points;w.get<ecs::Progression>(e).xp=0;}
  fighters[avatar]=id;death_time[avatar]=0;respawns[avatar]=1;
  if(!local)place(id,spawn_point+rules().guest_offset);
  if(rounds()&&!local){if(std::getenv("GWS_ONLINE_TEST_CLOSE"))place(id,{0,1,-6});scene->GetEntityById(id)->GetTransform()->SetWorldRotation(glm::angleAxis(3.14159265f,glm::vec3(0,1,0)));}
  spdlog::info("[online] fighter {} registered as entity {}",avatar,id);
 }
 void initialize(){
  auto& w=bridge->world();auto e=entity(local_id());if(!w.valid(e)||!w.has<ecs::AttributeSet>(e)||!w.has<ecs::AbilityBar>(e))return;
  if(auto* q=w.try_get<ecs::WeaponState>(e))base_weapon=*q;
  if(auto* q=w.try_get<ecs::SkillTree>(e))base_tree=*q;
  if(auto* q=w.try_get<ecs::UnlockedSkills>(e))base_skills=*q;
  base_attributes=w.get<ecs::AttributeSet>(e);if(auto* a=w.try_get<ecs::Inventory>(e))base_inventory=*a;if(auto* a=w.try_get<ecs::WeaponLoadout>(e))base_loadout=*a;base_bar=w.get<ecs::AbilityBar>(e);if(auto* a=w.try_get<ecs::Progression>(e))base_progress=*a;
  fighter_setup(is_client?own_avatar():1000000,local_id(),true);initialized=true;
  if(rounds()&&std::getenv("GWS_ONLINE_TEST_CLOSE"))place(local_id(),{0,1,6});
 }
 void process(uint32_t peer,const std::vector<uint8_t>& data);
 void snapshot_send();void snapshot_read(Reader&);
 void visuals_send();void visual_read(Reader&);
 void remote_actions(float dt);
 void outcomes(float dt);
 uint32_t map_id(uint32_t host,uint64_t avatar=0){
  if(avatar){if(avatar==own_avatar())return local_id();return net->avatar_entity(avatar);}
  auto it=replica.find(host);if(it!=replica.end())return it->second;
  return 0;
 }
};
OnlineGame::OnlineGame():impl_(std::make_unique<Impl>()){}OnlineGame::~OnlineGame(){if(impl_->session&&!impl_->local_session&&impl_->net){Writer bye(impl_->is_client?Message::Leave:Message::Reject,impl_->epoch);if(!impl_->is_client)bye.str("The host closed the session.");impl_->net->send_game(0,bye.bytes);}}
bool OnlineGame::enabled()const{return impl_->configured;}bool OnlineGame::active()const{return impl_->session;}bool OnlineGame::client()const{return impl_->session&&impl_->is_client;}bool OnlineGame::in_menu()const{return impl_->menu&&impl_->configured;}
bool OnlineGame::networked()const{return impl_->session&&!impl_->local_session;}
bool OnlineGame::input_blocked()const{return impl_->configured&&(impl_->menu||impl_->show_session||(impl_->session&&impl_->is_client&&impl_->first_state));}
void OnlineGame::configure(const std::string& root) {
    auto& p = *impl_;
    std::ifstream input(std::filesystem::path(root) / "config/multiplayer.ini");
    p.configured=false;
    p.settings.clear(); p.modes.clear(); p.maps.clear(); p.supplies.clear();
    if (!input) return;
    auto& cfg = p.settings;
    std::string line;
    while (std::getline(input, line)) {
        auto at = line.find('=');
        if (at != std::string::npos && line.front() != '#') cfg[line.substr(0,at)] = line.substr(at+1);
    }
    p.start_scene = cfg["start_scene"];
    p.lobby_server = cfg["lobby_server"];
    if (const char* e = std::getenv("GWS_LOBBY_SERVER")) p.lobby_server = e;
    while (!p.lobby_server.empty() && std::isspace(static_cast<unsigned char>(p.lobby_server.back()))) p.lobby_server.pop_back();
    p.flags = split(cfg["sync_flags"]);
    p.sync_ui = split(cfg["sync_ui"]);
    p.hidden_ui = split(cfg["session_hide"]);
    p.compatibility_files = split(cfg["fingerprint"]);
    for (const auto& entry : split(cfg["respawn_supplies"])) {
        auto at = entry.find(':');
        if (at != std::string::npos) p.supplies.emplace_back(entry.substr(0,at), std::atoi(entry.c_str()+at+1));
    }
    for (int id = 1; id <= 64; ++id) {
        std::string prefix = "mode." + std::to_string(id) + ".";
        if (!cfg.count(prefix+"scene")) continue;
        auto value = [&](const char* name) { return cfg[prefix+name]; };
        auto number = [&](const char* name) { return std::atoi(value(name).c_str()); };
        auto& m = p.modes[id];
        p.maps[id] = value("scene");
        m.key = value("key"); m.title = value("title"); m.objective = value("objective");
        m.victory_flag = value("victory_flag"); m.victory_text = value("victory_text");
        m.diagnostic_actor = value("diagnostic_actor");
        m.players = std::clamp(number("players"), 2, 32);
        m.rounds_to_win = number("rounds_to_win"); m.health = number("health");
        m.skill_points = number("skill_points"); m.arena_radius = number("arena_radius");
        m.respawn_seconds = number("respawn_seconds"); m.wipe_seconds = number("wipe_seconds");
        std::sscanf(value("guest_offset").c_str(), "%f,%f,%f", &m.guest_offset.x, &m.guest_offset.y, &m.guest_offset.z);
    }
    p.configured = !p.maps.empty() && !p.start_scene.empty() && !cfg["game_id"].empty() && !cfg["menu_ui"].empty();
    if (p.configured) spdlog::info("[online] multiplayer project configuration loaded");
}

void OnlineGame::end_play() {
    auto& p=*impl_;
    if (p.configured && p.bridge) {
        auto& policy=p.bridge->world().registry().ctx().emplace<ecs::DamageAuthority>();
        policy.authoritative=true; policy.friendly_fire=false;
    }
    if (p.session) p.leave({});
    p.travel.clear(); p.scene.reset(); p.ready=false; p.menu=false;
    p.show_session=false; p.editing_endpoint=false; p.status.clear();
    p.preparing_scene.clear(); p.reset_scene();
}

std::string OnlineGame::take_travel() {
    auto& p=*impl_;
    // Compiling an authored script may take seconds. Keep drawing the current
    // scene and polling input while its replacement's native code is prepared.
    if (p.preparation.valid()) {
        if (p.preparation.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return {};
        const auto error=p.preparation.get();
        const bool current=p.preparing_scene==p.travel;
        p.preparing_scene.clear();
        if(current && !error.empty()) {
            spdlog::error("[online] scene preparation failed: {}",error);
            p.leave("Could not prepare the scene script. See the project log.");
            return {};
        }
        if(current)return std::exchange(p.travel,{});
    }
    if(p.travel.empty() || !p.session || p.is_client) return std::exchange(p.travel,{});
    p.preparing_scene=p.travel;
    p.status="Loading "+p.rules().title+"...";
    p.preparation=std::async(std::launch::async,[path=p.travel]() -> std::string {
        std::ifstream input(path);
        if(!input) return "Cannot open "+path;
        std::set<std::string> scripts;
        for(std::string line;std::getline(input,line);)
            if(line.rfind("SCRIPT_PATH=",0)==0 && std::filesystem::path(line).extension()==".cpp")
                scripts.insert(line.substr(12));
        for(const auto& script:scripts) {
            std::string error;
            if(!prepare_cpp_script(script,error))return script+": "+error;
        }
        return {};
    });
    return {};
}
void OnlineGame::scene_ready(const std::shared_ptr<scene::Scene>& scene,const std::string& scene_path,ScenePlaybackManager& play,EcsSceneBridge& bridge,NetSession& net){auto& p=*impl_;p.scene=scene;p.play=&play;p.bridge=&bridge;p.net=&net;p.reset_scene();p.smooth.clear();p.respawns.clear();p.received_respawns.clear();p.predicted.clear();p.held_guns.clear();p.authored.clear();p.authored_local.clear();for(const auto& object:scene->GetEntities())if(object){p.authored_local.push_back(object->GetId());p.authored[object->GetId()]=uint32_t(p.authored_local.size());}if(p.session)for(const auto& flag:p.flags)bridge.set_world_flag(flag,0);if(auto* player=play.GetPlayerEntity())p.spawn_point=player->GetTransform()->GetWorldPosition();for(const auto& e:scene->GetEntities())if(e)p.scene_ids.insert(e->GetId());std::error_code ec;p.menu=!p.session&&std::filesystem::equivalent(scene_path,p.start_scene,ec);p.ready=p.session;if(p.session&&!p.is_client)p.status=p.local_session?"Local game":"Hosting on UDP port "+std::to_string(p.port);if(p.is_client){Writer w(Message::Ready,p.epoch);net.send_game(0,w.bytes);}play.SetGamePaused(false);play.SetUiHasInput(p.menu);play_ui().script_pause=false;}

void OnlineGame::Impl::process(uint32_t peer,const std::vector<uint8_t>& data){
 Reader r{data};uint32_t generation;auto type=r.header(generation);
 if(is_client){
  if(type==Message::Welcome){int m=int(r.pod<uint32_t>());auto path=r.str(256);if(!maps.count(m)||maps[m]!=path)throw std::runtime_error("host selected an unknown map");if(r.pod<uint64_t>()!=fingerprint(path,compatibility_files)){leave("Map or combat files differ from the host. Both players need matching project files.");return;}if(generation!=epoch||mode!=m||menu){epoch=generation;mode=m;menu=false;ready=false;travel=path;status="Loading shared scene...";}return;}
  if(type==Message::Reject){leave(r.str());return;}
  if(generation!=epoch||!ready)return;
  if(type==Message::State)snapshot_read(r);else if(type==Message::Visual)visual_read(r);
  return;
 }
 if(type==Message::Hello){if(r.str(80)!=settings["game_id"]){send(Message::Reject,"Incompatible game version.",peer);net->disconnect_peer(peer);return;}welcome(peer);return;}
 if(type==Message::Leave){peer_ready.erase(peer);net->disconnect_peer(peer);return;}
 if(generation!=epoch)return;
 if(type==Message::Ready){peer_ready[peer]=true;full_snapshot=true;status="Player connected.";send_timer=0;return;}
 if(!peer_ready[peer]||!initialized)return;
 auto avatar=net->peer_avatar(peer);auto fit=fighters.find(avatar);if(fit==fighters.end())return;
 auto& w=bridge->world();auto e=entity(fit->second);if(!w.valid(e))return;
 auto& input=inputs[peer];
 if(type==Message::Weapon){auto id=r.str(64);auto origin=vec(r)+net->origin_offset();auto aim=vec(r);auto seq=r.pod<uint64_t>();auto flags=r.pod<uint32_t>();if(seq<=input.seq||flags>15||glm::length(aim)<.5f||glm::length(aim)>1.5f)return;
  auto* loadout=w.try_get<ecs::WeaponLoadout>(e);bool allowed=false;if(loadout)for(size_t i=0;i<loadout->slots.size();++i)if(loadout->slots[i].weapon_id==id){ecs::select_weapon(w,e,int(i));allowed=true;break;}if(!allowed&&loadout)allowed=ecs::equip_weapon_from_inventory(w,e,id,loadout->current,&bridge->events());if(!allowed)return;
  auto object=scene->GetEntityById(fit->second);if(!object||glm::distance(origin,object->GetTransform()->GetWorldPosition())>5)return;
  input.weapon=id;input.seq=seq;input.origin=origin;input.aim=glm::normalize(aim);input.buttons|=flags&5u;input.buttons=(input.buttons&5u)|(flags&10u);input.timeout=.3f;
  object->GetTransform()->SetWorldRotation(glm::angleAxis(std::atan2(-aim.x,-aim.z),glm::vec3(0,1,0)));
 }else if(type==Message::Ability){auto id=r.str(64);int slot=r.pod<int32_t>();auto origin=vec(r)+net->origin_offset();auto aim=vec(r);auto flags=r.pod<uint32_t>();if(slot< -1||slot>2||flags>3||glm::length(aim)<.5f||glm::length(aim)>1.5f)return;
  auto* bar=w.try_get<ecs::AbilityBar>(e);if(!bar)return;
  if(slot>=0&&!ecs::equip_ability(w,e,id,slot))return;
  auto object=scene->GetEntityById(fit->second);if(!object||glm::distance(origin,object->GetTransform()->GetWorldPosition())>5)return;
  input.ability=id;if(slot>=0)input.pressed=slot;input.origin=origin;input.aim=glm::normalize(aim);input.held=flags&1;input.moving=flags&2;input.timeout=.3f;
 }else if(type==Message::Skill){auto id=r.str(64);ecs::learn_skill(w,e,id,&bridge->events());}
 else if(type==Message::Equip){auto id=r.str(64);ecs::equip_from_inventory(w,e,id,&bridge->events());}
 else if(type==Message::UseItem){auto id=r.str(64);if(auto* inv=w.try_get<ecs::Inventory>(e))for(size_t i=0;i<inv->items.size();++i)if(inv->items[i].def_id==id){ecs::use_item(w,e,*inv,i,&bridge->events());break;}}
 else if(type==Message::Interact){auto stable=r.pod<uint32_t>(),runtime=r.pod<uint32_t>();uint32_t id=stable&&stable<=authored_local.size()?authored_local[stable-1]:runtime;auto target=entity(id);auto object=scene->GetEntityById(id);auto actor=scene->GetEntityById(fit->second);auto* use=w.valid(target)?w.try_get<ecs::Interactable>(target):nullptr;if(use&&object&&actor&&object->IsActive()&&glm::distance(object->GetTransform()->GetWorldPosition(),actor->GetTransform()->GetWorldPosition())<=use->range+.25f){if(ecs::interact(w,e,target,&bridge->events())&&!use->enabled){object->SetActive(false);play->RemoveBodyForEntity(id);}}}
 else if(type==Message::Leave){peer_ready.erase(peer);net->disconnect_peer(peer);}
}

void OnlineGame::weapon(const std::string& id,const glm::vec3& origin,const glm::vec3& aim,uint32_t flags){auto& p=*impl_;if(!p.session||!p.ready||!p.net)return;if(p.is_client){Writer w(Message::Weapon,p.epoch);w.str(id);vec(w,origin-p.net->origin_offset());vec(w,aim);w.pod(++p.input_seq);w.pod(flags&15u);if(flags&16u&&p.predicted.size()<1024)p.predicted.push_back({p.input_seq,id});p.net->send_game(0,w.bytes);}}
void OnlineGame::ability(const std::string& id,int pressed,const glm::vec3& origin,const glm::vec3& aim,bool held,bool moving){auto& p=*impl_;if(!p.session||!p.is_client||!p.ready||!p.net)return;Writer w(Message::Ability,p.epoch);w.str(id);w.pod(int32_t(pressed));vec(w,origin-p.net->origin_offset());vec(w,aim);w.pod(uint32_t(held?1:0)|uint32_t(moving?2:0));p.net->send_game(0,w.bytes);}
void OnlineGame::effect(const std::string& name,const glm::vec3& at,const glm::vec3& dir,float scale,float reach){auto& p=*impl_;if(p.session&&!p.is_client&&p.visuals.size()<1024)p.visuals.push_back({1,0,name,{},{},at,dir,scale,reach});}
void OnlineGame::bone(uint32_t id,const std::string& name,const glm::vec3& rotation){auto& p=*impl_;if(p.session&&!p.is_client)p.bones[{id,name}]={2,id,name,{},{},rotation};}
void OnlineGame::animation(uint32_t id,const std::string& name,bool loop,float speed,float blend,bool restart){auto& p=*impl_;if(p.session&&!p.is_client){p.clips[id]={3,id,name,{},{},{},{},speed,blend,uint32_t(loop?1:0)|uint32_t(restart?2:0)};p.clip_serial[id]=p.next_clip++;}}
void OnlineGame::ui_message(int action,const std::string& file,const std::string& id,const std::string& text){auto& p=*impl_;if(!p.session||p.is_client||(!p.shares_ui(file)&&!(file==p.settings["hud_ui"]&&id==p.settings["objective_id"])))return;if(action==1)p.shown_ui.insert(file);if(action==2)p.shown_ui.erase(file);if(action==3)p.texts[file+"\n"+id]=text;}

void OnlineGame::Impl::remote_actions(float dt){
 auto& w=bridge->world();const auto ray=physics_world_ray(play->GetPhysicsWorld());
 for(auto& [peer,in]:inputs){auto it=fighters.find(net->peer_avatar(peer));if(it==fighters.end())continue;auto e=entity(it->second);if(!w.valid(e))continue;in.timeout-=dt;
  if(auto* ws=w.try_get<ecs::WeaponState>(e)){if(in.buttons&4)ecs::reload_weapon(*ws);if(auto* inv=w.try_get<ecs::Inventory>(e))ecs::sync_ammo_with_inventory(*ws,*inv);ws->aiming=in.buttons&8;
   std::vector<ecs::ShotTrace> traces;int n=ecs::trigger_weapon(w,e,in.origin,in.aim,*ws,in.buttons&1,(in.buttons&2)&&in.timeout>0,&bridge->events(),snapshot+peer,&ray,&traces);
   if(n>0)if(auto* d=ecs::find_weapon(ws->weapon_id)){effects->gun_fired(*d,in.origin,in.aim,traces);if(std::getenv("GWS_ONLINE_DIAGNOSTICS")){auto end=traces.empty()?in.origin:traces.front().end;spdlog::info("[online-shot] peer={} hit={} from={},{},{} to={},{},{} ammo={}",peer,traces.empty()?-1:int(traces.front().hit),in.origin.x,in.origin.y,in.origin.z,end.x,end.y,end.z,ws->ammo_in_mag);}}
  }
  in.buttons&=~5u;
  if(auto* bar=w.try_get<ecs::AbilityBar>(e)){ecs::AbilityCastInput cast;cast.pressed=in.pressed;cast.held=in.held&&in.timeout>0;cast.origin=in.origin;cast.aim=in.aim;cast.caster_pos=scene->GetEntityById(it->second)->GetTransform()->GetWorldPosition();cast.moving=in.moving;std::vector<ecs::AbilityFired> fired;ecs::step_ability_bar(w,e,*bar,cast,&ray,&bridge->events(),dt,&fired);
   for(auto& f:fired){if(auto* d=ecs::find_ability_def(f.id)){effects->ability_fired(*d,f,in.origin);} }
  }in.pressed=-1;
  if(auto* requests=w.try_get<ecs::AbilityRequests>(e);requests&&!requests->ids.empty()){auto ids=std::move(requests->ids);requests->ids.clear();for(auto& id:ids)if(auto* d=ecs::find_ability_def(id)){ecs::AbilityCastInput cast;cast.origin=in.origin;cast.aim=in.aim;cast.caster_pos=scene->GetEntityById(it->second)->GetTransform()->GetWorldPosition();auto f=ecs::ability_detail::fire(w,e,*d,-1,cast,&ray,&bridge->events());effects->ability_fired(*d,f,in.origin);}}
 }
}

void OnlineGame::Impl::snapshot_send(){
 auto& w=bridge->world();full_snapshot=full_snapshot||(snapshot%30==0);Writer out(Message::State,epoch);out.pod(++snapshot);out.pod(int32_t(host_score));out.pod(int32_t(guest_score));out.pod(int32_t(round));out.pod(countdown);out.str(result);out.pod(ctx->environment_darkness);out.pod(ctx->environment_whiteout);
 for(const auto& flag:flags)out.pod(int32_t(bridge->world_flag(flag)));
 std::vector<std::shared_ptr<scene::Entity>> records;
 for(const auto& object:scene->GetEntities())if(object){auto e=entity(object->GetId());const auto* skin=std::as_const(*object).GetSkinnedMeshComponent();bool player=false,player_child=false;for(auto& [a,id]:fighters){if(id==object->GetId())player=true;if(auto root=scene->GetEntityById(id))for(auto* parent=object->GetTransform()->GetParent();parent;parent=parent->GetParent())if(parent==root->GetTransform())player_child=true;}
  if(player||(w.valid(e)&&(w.has<ecs::CombatActor>(e)||w.has<ecs::Interactable>(e)))||tracked_objects.count(object->GetId())||object->GetName()=="script_visual"||(skin&&!skin->gltf_path.empty()&&!player_child&&object->GetName().rfind("[net]",0)!=0)){records.push_back(object);tracked_objects.insert(object->GetId());}
 }
 out.pod(uint32_t(records.size()));
 for(auto& object:records){uint32_t id=object->GetId();auto e=entity(id);uint64_t avatar=0,ack=0;for(auto& [a,fid]:fighters)if(fid==id)avatar=a;for(auto& [peer,input]:inputs)if(net->peer_avatar(peer)==avatar)ack=input.seq;
  out.pod(id);out.pod(authored.count(id)?authored[id]:0u);out.pod(avatar);out.pod(ack);out.pod(avatar?respawns[avatar]:0u);out.pod(uint32_t(object->IsActive()));bool metadata=full_snapshot||!sent_metadata.count(id);out.str(metadata?object->GetName():"");
  auto* mesh=std::as_const(*object).GetMeshComponent();out.str(metadata&&mesh?mesh->mesh_path:"");auto* skin=std::as_const(*object).GetSkinnedMeshComponent();out.str(metadata&&skin?skin->gltf_path:"");sent_metadata.insert(id);auto primitive=object->GetComponent<scene::MeshRendererComponent>();out.pod(uint32_t(metadata&&bool(primitive)));if(metadata&&primitive){out.pod(uint32_t(primitive->GetMeshType()));auto c=primitive->GetColor();out.pod(c.x);out.pod(c.y);out.pod(c.z);out.pod(c.w);}
  const auto* t=object->GetTransform();vec(out,t->GetWorldPosition()-net->origin_offset());auto q=t->GetWorldRotation();out.pod(q.w);out.pod(q.x);out.pod(q.y);out.pod(q.z);vec(out,t->GetLocalScale());
  out.pod(int32_t(skin?skin->clip_index:0));out.pod(skin?skin->speed:1.f);
  
  delta<ecs::AttributeSet>(out,w,e,ecs::serialize_attribute_set,0);delta<ecs::GameplayTags>(out,w,e,ecs::serialize_gameplay_tags,1);
  delta<ecs::Progression>(out,w,e,ecs::serialize_progression,2);delta<ecs::Inventory>(out,w,e,ecs::serialize_inventory,3);delta<ecs::UnlockedSkills>(out,w,e,ecs::serialize_unlocked_skills,4);delta<ecs::AbilityBar>(out,w,e,ecs::serialize_ability_bar,5);
  delta<ecs::WeaponState>(out,w,e,ecs::serialize_weapon_state,6);
  auto* ws=w.valid(e)?w.try_get<ecs::WeaponState>(e):nullptr;out.pod(ws?ws->rounds_fired:0u);out.pod(ws?ws->reloading:0.f);delta<ecs::WeaponLoadout>(out,w,e,ecs::serialize_loadout,7);delta<ecs::Equipment>(out,w,e,ecs::serialize_equipment,8);delta<ecs::Interactable>(out,w,e,ecs::serialize_interactable,9);delta<ecs::Pickup>(out,w,e,ecs::serialize_pickup,10);
 }
 out.pod(uint32_t(clips.size()));for(const auto& [id,v]:clips){out.pod(id);out.pod(clip_serial[id]);out.str(v.a);out.pod(v.s);out.pod(v.r);out.pod(v.flags);}
 out.pod(uint32_t(bones.size()));for(const auto& [key,v]:bones){out.pod(v.id);out.str(v.a);vec(out,v.p);}
 out.pod(uint32_t(shown_ui.size()));for(const auto& file:shown_ui)out.str(file);
 out.pod(uint32_t(texts.size()));for(const auto& [key,value]:texts){out.str(key);out.str(value);}
 if(out.bytes.size()>262144){spdlog::error("[online] snapshot exceeds packet budget: {}",out.bytes.size());return;}
 for(auto& [peer,yes]:peer_ready)if(yes)net->send_game(peer,out.bytes);full_snapshot=false;
}

void OnlineGame::Impl::snapshot_read(Reader& in){
 auto serial=in.pod<uint64_t>();if(serial<=last_snapshot)return;last_snapshot=serial;host_score=in.pod<int32_t>();guest_score=in.pod<int32_t>();round=in.pod<int32_t>();countdown=in.number();result=in.str();ctx->environment_darkness=in.number();ctx->environment_whiteout=in.number();
 for(const auto& flag:flags)bridge->set_world_flag(flag,in.pod<int32_t>());
 uint32_t count=in.pod<uint32_t>();if(count>2048)throw std::runtime_error("too many actors");auto& w=bridge->world();std::set<uint32_t> seen;
 for(uint32_t i=0;i<count;++i){auto host=in.pod<uint32_t>();auto stable=in.pod<uint32_t>();auto avatar=in.pod<uint64_t>();auto ack=in.pod<uint64_t>();auto respawn=in.pod<uint32_t>();bool active=in.pod<uint32_t>()!=0;auto name=in.str(256);auto mesh=in.str(512);auto skin=in.str(512);if(!safe_asset(mesh)||!safe_asset(skin))throw std::runtime_error("invalid asset path");
  bool primitive=in.pod<uint32_t>()!=0;uint32_t primitive_type=0;glm::vec4 color(1);if(primitive){primitive_type=in.pod<uint32_t>();color={in.number(),in.number(),in.number(),in.number()};}auto pos=vec(in)+net->origin_offset();float qw=in.number(),qx=in.number(),qy=in.number(),qz=in.number();glm::quat rotation(qw,qx,qy,qz);auto scale=vec(in);int clip=in.pod<int32_t>();float speed=in.number();uint32_t id=map_id(host,avatar);
  if(!avatar&&stable&&stable<=authored_local.size())id=authored_local[stable-1];
  if(!id&&!avatar){auto object=scene->CreateEntity(name);id=object->GetId();replica[host]=id;spawned.insert(host);if(!mesh.empty()){auto* mc=object->GetMeshComponent();mc->mesh_path=mesh;mc->use_asset_mesh=true;}if(!skin.empty())object->GetSkinnedMeshComponent()->gltf_path=skin;if(primitive&&primitive_type<8)object->AddComponent<scene::MeshRendererComponent>(static_cast<scene::MeshType>(primitive_type),color);}
  seen.insert(host);auto object=scene->GetEntityById(id);
  if(object){replica[host]=id;object->SetActive(active);if(!active)play->RemoveBodyForEntity(id);if(id!=local_id()) {smooth[id]={pos,scale,rotation};if(first_state){auto* t=object->GetTransform();t->SetWorldPosition(pos);t->SetWorldRotation(rotation);t->SetLocalScale(scale);}}else if(first_state||received_respawns[avatar]!=respawn){play->TeleportPlayer(pos,rotation);received_respawns[avatar]=respawn;}
   if(!skin.empty()){auto* sk=object->GetSkinnedMeshComponent();sk->clip_index=clip;sk->speed=speed;}
  }
  // New replicas need their gameplay mapping immediately, before applying bytes.
  if(object&&bridge->ecs_entity_id(object->GetTransform())==kNoEcsEntity)bridge->sync_and_run(scene);
  auto e=object?entity(id):ecs::null_entity;bool usable=w.valid(e);
  if(!usable){for(int part=0;part<7;++part)in.blob();in.pod<uint32_t>();in.number();for(int part=0;part<4;++part)in.blob();continue;}
  component<ecs::AttributeSet>(in,w,e,ecs::deserialize_attribute_set);component<ecs::GameplayTags>(in,w,e,ecs::deserialize_gameplay_tags);
  component<ecs::Progression>(in,w,e,ecs::deserialize_progression);component<ecs::Inventory>(in,w,e,ecs::deserialize_inventory);component<ecs::UnlockedSkills>(in,w,e,ecs::deserialize_unlocked_skills);
  if(id==local_id()&&!first_state)in.blob();else component<ecs::AbilityBar>(in,w,e,ecs::deserialize_ability_bar);
  auto weapon_data=in.blob();if(!weapon_data.empty()){ecs::WeaponState received;ecs::deserialize_weapon_state(&received,weapon_data.data(),weapon_data.size());if(!w.has<ecs::WeaponState>(e))w.add<ecs::WeaponState>(e,received);
   auto& ws=w.get<ecs::WeaponState>(e);if(id==local_id()){predicted.erase(std::remove_if(predicted.begin(),predicted.end(),[&](auto& p){return p.seq<=ack;}),predicted.end());int pending=0;for(auto& p:predicted)if(p.weapon==received.weapon_id)++pending;if(ws.weapon_id==received.weapon_id){ws.ammo_in_mag=std::max(0,received.ammo_in_mag-pending);ws.reserve=received.reserve;}}else{ws.weapon_id=received.weapon_id;ws.ammo_in_mag=received.ammo_in_mag;ws.reserve=received.reserve;}}
  auto rounds=in.pod<uint32_t>();auto reload=in.number();if(auto* ws=w.try_get<ecs::WeaponState>(e);ws&&id!=local_id()){ws->rounds_fired=rounds;ws->reloading=reload;}
  component<ecs::WeaponLoadout>(in,w,e,ecs::deserialize_loadout);component<ecs::Equipment>(in,w,e,ecs::deserialize_equipment);component<ecs::Interactable>(in,w,e,ecs::deserialize_interactable);component<ecs::Pickup>(in,w,e,ecs::deserialize_pickup);if(avatar){fighters[avatar]=id;ecs::CombatActor body;body.hurt_radius=.65f;body.hurt_height=.5f;w.add<ecs::CombatActor>(e,body);w.remove<ecs::Mortal>(e);}
 }
 for(auto it=spawned.begin();it!=spawned.end();){if(!seen.count(*it)){if(auto object=scene->GetEntityById(map_id(*it)))scene->RemoveEntity(object);replica.erase(*it);it=spawned.erase(it);}else++it;}
 auto nclips=in.pod<uint32_t>();if(nclips>2048)throw std::runtime_error("too many clips");for(uint32_t i=0;i<nclips;++i){auto id=in.pod<uint32_t>();auto serial=in.pod<uint64_t>();auto name=in.str(128);float speed=in.number(),blend=in.number();auto flags=in.pod<uint32_t>();auto local=map_id(id);if(local&&clip_serial[id]!=serial&&ctx->play_animation&&ctx->play_animation(local,name,flags&1,speed,blend,flags&2))clip_serial[id]=serial;}
 auto nbones=in.pod<uint32_t>();if(nbones>4096)throw std::runtime_error("too many bones");for(uint32_t i=0;i<nbones;++i){auto id=in.pod<uint32_t>();auto name=in.str(128);auto r=vec(in);float a[]{r.x,r.y,r.z};if(auto local=map_id(id);local&&ctx->set_bone_rotation)ctx->set_bone_rotation(local,name,a);}
 auto nui=in.pod<uint32_t>();if(nui>16)throw std::runtime_error("too many UI files");std::set<std::string> now;for(uint32_t i=0;i<nui;++i){auto file=in.str(256);if(shares_ui(file)){now.insert(file);if(!play_ui().shown(file))play_ui().show(file);}}for(auto& file:shown_ui)if(!now.count(file))play_ui().hide(file);shown_ui=std::move(now);
 auto nt=in.pod<uint32_t>();if(nt>128)throw std::runtime_error("too much UI text");for(uint32_t i=0;i<nt;++i){auto key=in.str(512),text=in.str(2048);auto at=key.find('\n');if(at!=std::string::npos){auto file=key.substr(0,at), id=key.substr(at+1);if(shares_ui(file)||(file==settings["hud_ui"]&&id==settings["objective_id"]))play_ui().set_text(file,id,text);}}
 if(first_state)spdlog::info("[online] client synchronized scene: {} actors, epoch {}",count,epoch);
 first_state=false;status="Connected.";
}

void OnlineGame::Impl::visuals_send(){if(visuals.empty())return;Writer out(Message::Visual,epoch);out.pod(uint32_t(visuals.size()));for(auto& v:visuals){out.pod(uint32_t(v.type));out.pod(v.id);out.str(v.a);vec(out,v.p-net->origin_offset());vec(out,(v.type==6||v.type==9)?v.d-net->origin_offset():v.d);out.pod(v.s);out.pod(v.r);out.pod(v.flags);}for(auto& [peer,yes]:peer_ready)if(yes)net->send_game(peer,out.bytes);visuals.clear();}
void OnlineGame::Impl::visual_read(Reader& in){auto n=in.pod<uint32_t>();if(n>1024)throw std::runtime_error("too many effects");for(uint32_t i=0;i<n;++i){int type=int(in.pod<uint32_t>());auto id=in.pod<uint32_t>();auto name=in.str(128);auto p=vec(in)+net->origin_offset(),d=vec(in);float s=in.number(),reach=in.number();auto flags=in.pod<uint32_t>();(void)flags;
 if(type==1&&ctx->spawn_effect){float at[]{p.x,p.y,p.z},dir[]{d.x,d.y,d.z};ctx->spawn_effect(name.c_str(),at,dir,s,reach);}
 if(type==4&&map_id(id)==local_id())play->TeleportPlayer(p,glm::quat(1,0,0,0));
 if(type==5&&map_id(id)==local_id())play->LaunchPlayer(flags==1?play->GetPlayerMovementState().velocity+d:d);
 if(type==6)if(auto* weapon=ecs::find_weapon(name)){ecs::ShotTrace trace;trace.start=p;trace.end=d+net->origin_offset();trace.tracer=true;trace.hit=static_cast<ecs::ShotTrace::Hit>(std::clamp(int(s),0,2));trace.normal=-glm::normalize(trace.end-p);effects->gun_fired(*weapon,p,glm::normalize(trace.end-p),{trace});}
 if(type==7)if(auto* ability=ecs::find_ability_def(name)){ecs::AbilityFired f;f.id=name;f.point=p;f.dir=d;f.launched=ability->speed>0;effects->ability_fired(*ability,f,p);}
 if(type==8)if(auto* ability=ecs::find_ability_def(name)){ecs::AbilityImpact impact;impact.id=name;impact.point=p;impact.normal=d;impact.on_actor=s>0;effects->ability_impact(*ability,impact);}
 if(type==9)if(auto* ability=ecs::find_ability_def(name))effects->ability_flight(*ability,p,d+net->origin_offset(),s);
 }}

void OnlineGame::Impl::outcomes(float dt){
 auto& w=bridge->world();std::vector<uint64_t> dead;int alive=0;for(auto& [avatar,id]:fighters){auto e=entity(id);auto* a=w.valid(e)?w.try_get<ecs::AttributeSet>(e):nullptr;if(!a)continue;if(auto object=scene->GetEntityById(id)){auto p=object->GetTransform()->GetWorldPosition();if(p.y< -25||(rounds()&&(rules().arena_radius>0 && (std::abs(p.x)>rules().arena_radius||std::abs(p.z)>rules().arena_radius))))a->set("Health",0);}if(a->get("Health")<=0){dead.push_back(avatar);death_time[avatar]+=dt;if(auto* tags=w.try_get<ecs::GameplayTags>(e)){tags->add("status.stunned");tags->add("state.dead");}}else{++alive;death_time[avatar]=0;}}
 auto restore=[&](uint64_t avatar){auto id=fighters[avatar];auto e=entity(id);if(!w.valid(e))return;auto* a=w.try_get<ecs::AttributeSet>(e);if(!a)return;
  if(auto* active=w.try_get<ecs::ActiveEffects>(e)){for(auto& fx:active->effects)if(fx.def.revert_on_end)ecs::apply_modifiers(*a,fx.applied,-1);active->effects.clear();}
  if(auto* h=a->find("Health"))h->current=h->max;
  if(auto* bar=w.try_get<ecs::AbilityBar>(e)){bar->cooldown.assign(bar->slots.size(),0);bar->resting_cooldown.clear();bar->casting=-1;bar->channelling=false;}
  if(auto* ws=w.try_get<ecs::WeaponState>(e)){if(auto* def=ecs::find_weapon(ws->weapon_id))ws->ammo_in_mag=def->mag_size;ws->reloading=ws->cooldown=0;}
  if(auto* tags=w.try_get<ecs::GameplayTags>(e)){tags->remove_exact("status.stunned");tags->remove_exact("status.shredded");tags->remove_exact("state.dead");}
  if(auto* inv=w.try_get<ecs::Inventory>(e))for(const auto& [item,n]:supplies){int have=ecs::inventory_count(*inv,item);if(have<n)ecs::inventory_add(*inv,ecs::ItemInstance{item,n-have});}
  auto object=scene->GetEntityById(id);auto pos=spawn_point+(avatar==1000000?glm::vec3(0):rules().guest_offset);
  if(rounds()&&std::getenv("GWS_ONLINE_TEST_CLOSE"))pos.z=avatar==1000000?6.f:-6.f;place(id,pos);if(rounds()&&avatar!=1000000)object->GetTransform()->SetWorldRotation(glm::angleAxis(3.14159265f,glm::vec3(0,1,0)));++respawns[avatar];death_time[avatar]=0;
 };
 if(rounds()){
  bool completed=host_score>=rules().rounds_to_win||guest_score>=rules().rounds_to_win;
  if(fighters.size()<2){result=(host_score||guest_score)?"Opponent disconnected. Return to the starting screen or wait for a new opponent.":"Waiting for an opponent";countdown=5;}
  else if(!completed&&round_delay<=0&&countdown<=0&&!dead.empty()){
   if(dead.size()==fighters.size())result="Draw - both fighters fell";
   else if(std::find(dead.begin(),dead.end(),uint64_t(1000000))!=dead.end()){++guest_score;result="Challenger wins the round";}else{++host_score;result="Host wins the round";}
   if(host_score>=rules().rounds_to_win||guest_score>=rules().rounds_to_win)result=host_score>=rules().rounds_to_win?"HOST WINS THE DUEL":"CHALLENGER WINS THE DUEL";else round_delay=5;
   spdlog::info("[online] duel round {} result: {} ({}-{})",round,result,host_score,guest_score);
  }else if(round_delay>0){round_delay-=dt;if(round_delay<=0){++round;for(auto& [a,id]:fighters)restore(a);countdown=3;result.clear();}}
  else if(fighters.size()>=2&&countdown>0){countdown=std::max(0.f,countdown-dt);if(countdown<=0)result.clear();}
  bool freeze=fighters.size()<2||countdown>0||round_delay>0||host_score>=rules().rounds_to_win||guest_score>=rules().rounds_to_win;
  for(auto& [a,id]:fighters){auto e=entity(id);if(auto* t=w.try_get<ecs::GameplayTags>(e)){if(freeze)t->add("status.stunned");else t->remove_exact("status.stunned");}}
 }else{
  if(!rules().victory_flag.empty()&&bridge->world_flag(rules().victory_flag))result=rules().victory_text;
  if(alive==0)empty_time+=dt;else empty_time=0;
  if(rules().wipe_seconds>0&&empty_time>=rules().wipe_seconds){for(auto& [a,id]:fighters)restore(a);empty_time=0;}
  else if(rules().respawn_seconds>0&&alive>0)for(auto a:dead)if(death_time[a]>=rules().respawn_seconds)restore(a);
 }
}

void OnlineGame::update(float dt,const std::shared_ptr<scene::Scene>& scene,const std::string& scene_path,ScenePlaybackManager& play,EcsSceneBridge& bridge,NetSession& net,EditorScriptCtx& ctx,PlayEffects& effects){
 auto& p=*impl_;if(!p.configured)return;p.net=&net;p.play=&play;p.bridge=&bridge;p.ctx=&ctx;p.effects=&effects;
 if(!p.events_bound){p.events_bound=true;bridge.events().subscribe("ui.click",[this](const ecs::GameplayEvent& event){impl_->action(event.param);});bridge.events().subscribe("item.used",[this](const ecs::GameplayEvent& event){auto& p=*impl_;if(p.session&&p.is_client&&event.target==p.entity(p.local_id()))if(auto* item=ecs::find_item(event.param);item&&item->kind==ecs::ItemKind::Consumable)p.send(Message::UseItem,event.param);});}
 if(!play.IsPlaying())return;
 if(p.scene!=scene)scene_ready(scene,scene_path,play,bridge,net);
 p.age+=dt;
 if(std::getenv("GWS_ONLINE_TEST_CLOSE")&&p.session&&p.rounds()&&p.initialized&&p.age<.35f)p.place(p.local_id(),{0,1,p.is_client?-6.f:6.f});
 // Test-only environment switches exercise the same menu actions as a player.
 // A code looked up in the background: join its relay port, or say why not.
 if(p.lookup.valid()&&p.lookup.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  auto found=p.lookup.get();
  if(found.ok&&!p.session){
   std::snprintf(p.address,sizeof p.address,"%s",found.relay_host.c_str());p.port=found.relay_port;p.via_lobby=false;
   spdlog::info("[online] code {} -> relay {}:{}",p.code_input,found.relay_host,found.relay_port);
   p.begin(p.modes.begin()->first,false);p.status="Joining game "+p.code_input+"...";
  }else if(!found.ok)p.status=found.error;
 }
 // Hosting with a code: tell the host its code once the lobby is open.
 if(p.session&&!p.is_client&&p.via_lobby){
  const auto st=net.lobby_state();
  if(st==NetSession::LobbyState::Failed){p.leave("Could not open an online game: "+net.lobby_error());return;}
  if(st==NetSession::LobbyState::Open&&p.shown_code!=net.lobby_code()){
   if(p.shown_code.empty())p.travel=p.maps[p.mode];   // the lobby is open: into the mode's world
   p.shown_code=net.lobby_code();
   spdlog::info("[online] lobby code {} (server {})",p.shown_code,p.lobby_server);
   p.status="Online. Your code: "+p.shown_code+"  -- friends type it under Join with code.";
  }
 }
 // Test switches: GWS_ONLINE_LOBBY=<mode key> hosts it with a code; GWS_ONLINE_CODE=<code> joins one.
 if(!p.session&&p.menu){static bool test_lobby_done=false;if(!test_lobby_done){if(const char* key=std::getenv("GWS_ONLINE_LOBBY")){for(const auto& [id,m]:p.modes)if(m.key==key){test_lobby_done=true;p.via_lobby=true;p.begin(id,true);return;}}else if(const char* code=std::getenv("GWS_ONLINE_CODE")){test_lobby_done=true;p.code_input=code;p.start_code_join();}}}
 if(!p.session&&p.menu){if(const char* host=std::getenv("GWS_ONLINE_HOST")){int mode=0;for(const auto& [id,m]:p.modes)if(m.key==host)mode=id;if(!mode)return;if(const char* port=std::getenv("GWS_ONLINE_PORT"))p.port=std::clamp(std::atoi(port),1024,65535);p.begin(mode,true);}else if(const char* join=std::getenv("GWS_ONLINE_JOIN")){std::snprintf(p.address,sizeof p.address,"%s",join);if(const char* port=std::getenv("GWS_ONLINE_PORT"))p.port=std::clamp(std::atoi(port),1024,65535);p.begin(1,false);}}
 auto& w=bridge.world();auto& policy=w.registry().ctx().emplace<ecs::DamageAuthority>();policy.authoritative=!p.session||!p.is_client;policy.friendly_fire=p.rounds();
 if(!p.session)return;
 for(const auto& file:p.hidden_ui)if(play_ui().shown(file))play_ui().hide(file);
 if(p.is_client&&p.initialized){
  auto e=p.entity(p.local_id());if(auto* skills=w.try_get<ecs::UnlockedSkills>(e))for(auto& id:skills->unlocked)if(p.sent_skills.insert(id).second)p.send(Message::Skill,id);
  if(auto* equipment=w.try_get<ecs::Equipment>(e))for(auto& slot:equipment->slots)if(!slot.item.def_id.empty()&&p.sent_equipment[slot.slot]!=slot.item.def_id){p.sent_equipment[slot.slot]=slot.item.def_id;p.send(Message::Equip,slot.item.def_id);}
 }
 for(auto& message:net.take_game_messages())try{p.process(message.peer,message.data);}catch(const std::exception& ex){spdlog::warn("[online] rejected malformed packet from {}: {}",message.peer,ex.what());}
 if(p.is_client){
  p.connect_timer+=dt;if(net.status().connected){if(!p.hello){p.send(Message::Hello,p.settings["game_id"]);p.hello=true;}}else if(p.hello){p.leave("The host disconnected.");return;}
  if(p.connect_timer>15&&(p.menu||p.first_state)){p.leave("Could not join. Verify the address, UDP port and matching game version.");return;}
 }else{
  auto peers=net.peers();for(auto it=p.fighters.begin();it!=p.fighters.end();)if(it->first!=1000000&&!net.avatar_entity(it->first))it=p.fighters.erase(it);else++it;for(auto it=p.peer_ready.begin();it!=p.peer_ready.end();){if(std::find(peers.begin(),peers.end(),it->first)==peers.end()){auto a=net.peer_avatar(it->first);p.inputs.erase(it->first);for(auto f=p.fighters.begin();f!=p.fighters.end();)if(f->first!=1000000&&!net.avatar_entity(f->first))f=p.fighters.erase(f);else++f;(void)a;it=p.peer_ready.erase(it);}else++it;}
  for(auto peer:peers)if(!p.peer_ready[peer]&&p.age-p.welcomes[peer]>1)p.welcome(peer);
 }
 if(!p.ready)return;
 if(!p.initialized&&p.age>.25f)p.initialize();
 if(!p.initialized || (p.local_session && play.IsGamePaused()))return;
 if(p.is_client){
  for(auto& [id,target]:p.smooth)if(auto object=scene->GetEntityById(id)){auto* t=object->GetTransform();float blend=std::min(1.f,dt/.07f);t->SetWorldPosition(glm::mix(t->GetWorldPosition(),target.p,blend));t->SetWorldRotation(glm::slerp(t->GetWorldRotation(),target.q,blend));t->SetLocalScale(target.s);if(auto* physics=play.GetPhysicsWorld()){auto body=play.BodyForEntity(id);if(body!=0xFFFFFFFFu)physics->set_transform(body,t->GetWorldPosition(),t->GetWorldRotation());}}
 }else{
  if(!p.peer_ready.empty())bridge.sync_and_run(scene);
  for(auto& [peer,ready]:p.peer_ready)if(ready){auto avatar=net.peer_avatar(peer);auto id=net.avatar_entity(avatar);if(avatar&&id&&!p.fighters.count(avatar))p.fighter_setup(avatar,id,false);}
  if(std::getenv("GWS_ONLINE_TEST_CULL")&&!p.culled&&p.age>5&&p.fighters.size()>1){p.culled=true;int count=0;w.each<ecs::Mortal,ecs::AttributeSet>([&](ecs::Entity e,ecs::Mortal&,ecs::AttributeSet& a){auto* tags=w.try_get<ecs::GameplayTags>(e);if(tags&&tags->has("team.enemy")&&count<3){a.set("Health",0);++count;}});spdlog::info("[online-check] culled {} enemies to check corpse and loot replication",count);}
  p.remote_actions(dt);p.outcomes(dt);p.send_timer-=dt;
  if(p.send_timer<=0){p.send_timer=.1f;try{if(std::any_of(p.peer_ready.begin(),p.peer_ready.end(),[](const auto& peer){return peer.second;})){p.snapshot_send();p.visuals_send();}else p.visuals.clear();}catch(const std::exception& ex){spdlog::warn("[online] snapshot deferred: {}",ex.what());}}
 }
 if(std::getenv("GWS_ONLINE_DIAGNOSTICS")&&int(p.age)!=p.diagnostic_second){p.diagnostic_second=int(p.age);auto e=p.entity(p.local_id());auto* a=w.valid(e)?w.try_get<ecs::AttributeSet>(e):nullptr;float boss=0;for(auto& object:scene->GetEntities())if(object&&object->GetName()==p.rules().diagnostic_actor){auto be=p.entity(object->GetId());if(auto* ba=w.try_get<ecs::AttributeSet>(be))boss=ba->get("Health");}spdlog::info("[online-test] {} mode={} players={} hp={} boss={} score={}:{} snapshot={}",p.is_client?"client":"host",p.mode,p.fighters.size(),a?a->get("Health"):0,boss,p.host_score,p.guest_score,p.last_snapshot);if(!p.is_client&&!p.inputs.empty()){auto& input=p.inputs.begin()->second;auto id=p.fighters[p.net->peer_avatar(p.inputs.begin()->first)];auto obj=scene->GetEntityById(id);auto pos=obj?obj->GetTransform()->GetWorldPosition():glm::vec3(0);spdlog::info("[online-input] seq={} buttons={} origin={},{},{} remote={},{},{}",input.seq,input.buttons,input.origin.x,input.origin.y,input.origin.z,pos.x,pos.y,pos.z);}}
 for(auto& [avatar,id]:p.fighters)if(id!=p.local_id()){
  auto root=scene->GetEntityById(id);auto e=p.entity(id);if(!root||!w.valid(e))continue;auto* state=w.try_get<ecs::WeaponState>(e);auto* def=state?ecs::find_weapon(state->weapon_id):nullptr;if(!def||def->model.empty())continue;
  std::shared_ptr<scene::Entity> body;for(auto& child:root->GetChildren())if(child){auto* skin=std::as_const(*child).GetSkinnedMeshComponent();if(skin&&!skin->gltf_path.empty()){body=child;break;}}if(!body)continue;
  auto gun=scene->GetEntityById(p.held_guns[avatar]);if(!gun){gun=scene->CreateEntity(root->GetName()+":Weapon");gun->SetParent(body);p.held_guns[avatar]=gun->GetId();}
  auto* skin=gun->GetSkinnedMeshComponent();if(skin->gltf_path!=def->model){skin->gltf_path=def->model;skin->drive_from_gameplay=true;skin->clips="idle=00_Idle,fire=01_Fire,reload=02_Reload,draw=03_Draw";skin->attach_bone=def->grip_bone.empty()?"weapon_socket":def->grip_bone;skin->attach_pos=def->grip_pos;skin->attach_rot=def->grip_rot;}
 }
 for(auto it=p.held_guns.begin();it!=p.held_guns.end();)if(!p.fighters.count(it->first)){if(auto object=scene->GetEntityById(it->second))scene->RemoveEntity(object);it=p.held_guns.erase(it);}else++it;
 if(!p.rules().objective.empty())play_ui().set_text(p.settings["hud_ui"],p.settings["objective_id"],p.rules().objective);
}

void OnlineGame::Impl::action(const std::string& id) {
    if (!configured || !play || !play->IsPlaying()) return;
    if (id == "online_return") { leave("Session closed."); return; }
    if (menu && !session) {
        for(const auto& [n,m]:modes) {
            if (!m.rounds_to_win && id=="online_solo_"+std::to_string(n)) {
                editing_endpoint=false;
                begin(n,true,true);
                return;
            }
        }
        for (const auto& [n,m] : modes) if (id == "online_lobby_" + std::to_string(n)) {
            editing_endpoint = editing_code = false;
            if (lobby_server.empty()) { status = "This game has no lobby server (lobby_server in config/multiplayer.ini)."; return; }
            via_lobby = true;
            begin(n, true);
            return;
        }
        if (id == "online_code") {
            previous_code = code_input;
            code_input.clear();
            editing_code = true;
            editing_endpoint = false;
            status = "Type the code the host was given, then Join with code (or Enter).";
            return;
        }
        if (id == "online_join_code") {
            editing_code = false;
            start_code_join();
            return;
        }
        if (id == "online_endpoint") {
            previous_endpoint = endpoint;
            endpoint.clear();
            editing_endpoint = true;
            status = "Enter a host address and UDP port. Press Enter to save.";
            return;
        }
        const bool join = id == "online_join";
        int choice = 0;
        for (const auto& [n,m] : modes) if (id == "online_host_" + std::to_string(n)) choice=n;
        if (!choice && !join) return;
        if (editing_endpoint && endpoint.empty()) endpoint=previous_endpoint;
        editing_endpoint=false;
        const size_t colon=endpoint.rfind(':');
        auto host=endpoint.substr(0,colon);
        auto digits=colon==std::string::npos ? std::to_string(port) : endpoint.substr(colon+1);
        if(host.empty() || host.size()>=sizeof(address) || digits.empty() || digits.size()>5 ||
           digits.find_first_not_of("0123456789")!=std::string::npos ||
           std::atoi(digits.c_str())<1 || std::atoi(digits.c_str())>65535) {
            status="Enter an address and a port from 1 to 65535."; return;
        }
        std::snprintf(address,sizeof(address),"%s",host.c_str());
        port=std::atoi(digits.c_str());
        begin(join ? modes.begin()->first : choice,!join);
    } else if (session) {
        if (id=="online_back") show_session=false;
        if (id=="online_restart" && !is_client && ready && !result.empty()) {
            ++epoch; host_score=guest_score=0; round=1; result.clear(); show_session=false;
            for(auto& [peer,yes]:peer_ready){yes=false;welcome(peer);}
            travel=maps[mode];
        }
    }
}

// Project documents use the normal PlayUi canvas, mouse routing and viewport clip.
void OnlineGame::draw_ui(ScenePlaybackManager& play,NetSession&) {
    auto& p=*impl_;
    if (!p.configured || !play.IsPlaying()) return;
    auto& ui=play_ui();
    if (p.session && !p.menu && ImGui::IsKeyPressed(ImGuiKey_F10,false)) p.show_session=!p.show_session;
    if (p.editing_code) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
            p.code_input=p.previous_code; p.editing_code=false; p.status.clear();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter,false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter,false)) {
            p.editing_code=false; p.start_code_join();
        } else {
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace,true)&&!p.code_input.empty())p.code_input.pop_back();
            auto append=[&](unsigned c){if(std::isalnum(static_cast<int>(c))&&c<127&&p.code_input.size()<8)p.code_input.push_back(static_cast<char>(std::toupper(static_cast<int>(c))));};
            const auto& io=ImGui::GetIO();
            if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V,false)) {
                if(const char* clipboard=ImGui::GetClipboardText())for(const char* c=clipboard;*c;++c)append(static_cast<unsigned char>(*c));
            } else for(auto c:io.InputQueueCharacters) append(c);
        }
    }
    if (p.editing_endpoint) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape,false)) {
            p.endpoint=p.previous_endpoint; p.editing_endpoint=false; p.status.clear();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter,false)) {
            if(p.endpoint.empty())p.endpoint=p.previous_endpoint;
            p.editing_endpoint=false; p.status.clear();
        } else {
            if(ImGui::IsKeyPressed(ImGuiKey_Backspace,true)&&!p.endpoint.empty())p.endpoint.pop_back();
            auto append=[&](unsigned c){if(c>32&&c<127&&p.endpoint.size()<132)p.endpoint.push_back(static_cast<char>(c));};
            const auto& io=ImGui::GetIO();
            if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V,false)) {
                if(const char* clipboard=ImGui::GetClipboardText())for(const char* c=clipboard;*c;++c)append(*c);
            } else for(auto c:io.InputQueueCharacters) append(c);
        }
    }
    auto document=[&](const char* key,bool visible) {
        const auto& file=p.settings[key];
        if(!file.empty() && ui.shown(file)!=visible) {
            if(visible) ui.show(file); else ui.hide(file);
        }
        return file;
    };
    const bool connecting=p.session&&(!p.ready||(p.is_client&&p.first_state));
    auto menu=document("menu_ui",p.menu&&!p.session);
    auto panel=document("session_ui",p.session&&(connecting||p.show_session));
    auto hud=document("status_ui",p.session&&!connecting&&!p.show_session);
    if(p.menu&&!p.session) {
        ui.set_text(menu,"online_endpoint",p.endpoint+(p.editing_endpoint?"_":""));
        ui.set_text(menu,"online_code",p.code_input.empty()&&!p.editing_code?std::string("Click to type a code"):p.code_input+(p.editing_code?"_":""));
        ui.set_text(menu,"message",p.status);
    }
    if(p.session) {
        std::string details=p.rules().title+"   /   "+(p.local_session?"Solo":p.is_client?"Guest":"Host")+"   /   "+std::to_string(p.fighters.size())+" players";
        if(!p.shown_code.empty())details+="   /   code "+p.shown_code;
        ui.set_visible(panel,"online_restart",!p.is_client && p.ready && !p.result.empty());
        ui.set_visible(panel,"online_back",!connecting);
        ui.set_text(panel,"details",details); ui.set_text(panel,"message",p.status);
        ui.set_text(hud,"details",details+"   /   F10 session");
        std::string message=p.result;
        if(p.rounds() && p.fighters.size()>1) {
            if(p.countdown>0)message="Starting in "+std::to_string(int(std::ceil(p.countdown)));
            message+="   "+std::to_string(p.host_score)+" - "+std::to_string(p.guest_score);
        }
        if(p.initialized) {
            auto e=p.entity(p.local_id()); auto& w=p.bridge->world();
            if(auto* a=w.try_get<ecs::AttributeSet>(e);a&&a->get("Health")<=0)message="Down";
            else if(auto* t=w.try_get<ecs::GameplayTags>(e);t&&!p.settings["mark_tag"].empty()&&t->has(p.settings["mark_tag"]))message=p.settings["mark_hint"];
        }
        ui.set_text(hud,"message",message);
    }
}
bool OnlineGame::force_player(uint32_t id,const float* v,int kind){auto& p=*impl_;if(!p.session||p.is_client||id==p.local_id())return false;for(auto& [a,fid]:p.fighters)if(fid==id){Impl::Visual effect;effect.type=kind==2?4:5;effect.id=id;effect.flags=uint32_t(kind);if(kind==2)effect.p={v[0],v[1],v[2]};else effect.d={v[0],v[1],v[2]};p.visuals.push_back(effect);return true;}return false;}
void OnlineGame::share_xp(uint32_t id,float amount){auto& p=*impl_;if(!p.session||p.is_client||id!=p.local_id()||p.rounds())return;auto& w=p.bridge->world();for(auto& [a,fid]:p.fighters)if(fid!=id){auto e=p.entity(fid);if(w.valid(e))ecs::grant_xp(w,e,amount,&p.bridge->events());}}
void OnlineGame::combat_visual(int type,const std::string& name,const glm::vec3& at,const glm::vec3& dir,float extra){auto& p=*impl_;if(p.session&&!p.is_client&&p.visuals.size()<1024)p.visuals.push_back({type,0,name,{},{},at,dir,extra});}
void OnlineGame::test_aim(){auto& p=*impl_;if(!std::getenv("GWS_ONLINE_TEST_AIM")||!p.session||!p.initialized||p.fighters.size()<2)return;auto* camera=p.play->GetPlaybackCamera();if(!camera)return;for(auto& [avatar,id]:p.fighters)if(id!=p.local_id())if(auto object=p.scene->GetEntityById(id)){auto delta=object->GetTransform()->GetWorldPosition()+glm::vec3(0,.5f,0)-camera->GetTransform()->GetWorldPosition();if(glm::length(delta)>.01f)camera->GetTransform()->SetWorldRotation(glm::quatLookAtRH(glm::normalize(delta),glm::vec3(0,1,0)));break;}}
bool OnlineGame::interact(uint32_t target){auto& p=*impl_;if(!p.session||!p.is_client)return false;for(auto& object:p.scene->GetEntities())if(object&&p.bridge->ecs_entity_id(object->GetTransform())==target){Writer out(Message::Interact,p.epoch);auto id=object->GetId();out.pod(p.authored.count(id)?p.authored[id]:0u);uint32_t host=0;for(auto& [remote,local]:p.replica)if(local==id){host=remote;break;}out.pod(host);p.net->send_game(0,out.bytes);break;}return true;}
} // namespace schizo::editor
