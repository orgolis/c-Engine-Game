#pragma once
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <glm/glm.hpp>
namespace schizo::scene {class Scene;}
namespace schizo::editor {
class NetSession;class ScenePlaybackManager;class EcsSceneBridge;struct EditorScriptCtx;class PlayEffects;
// Opt-in project game flow. Settings live in config/multiplayer.ini.
class OnlineGame {
public:
 OnlineGame();~OnlineGame();
 bool enabled()const;bool active()const;bool client()const;bool in_menu()const;
 bool input_blocked()const;
 bool networked()const;
 void configure(const std::string& project_root);
 void end_play();
 std::string take_travel();
 void scene_ready(const std::shared_ptr<scene::Scene>& scene,const std::string& scene_path,ScenePlaybackManager&,EcsSceneBridge&,NetSession&);
 void update(float dt,const std::shared_ptr<scene::Scene>&,const std::string& scene_path,ScenePlaybackManager&,EcsSceneBridge&,NetSession&,EditorScriptCtx&,PlayEffects&);
 void draw_ui(ScenePlaybackManager&,NetSession&);
 void weapon(const std::string&,const glm::vec3&,const glm::vec3&,uint32_t flags);
 void ability(const std::string&,int,const glm::vec3&,const glm::vec3&,bool held,bool moving);
 void effect(const std::string&,const glm::vec3&,const glm::vec3&,float scale,float reach);
 void bone(uint32_t,const std::string&,const glm::vec3&);
 void animation(uint32_t,const std::string&,bool,float,float,bool);
 // 0.8.62: animation by its parameters, not its poses. The host sends a script's
 // montages and graph parameters, and each NPC's look target, aim and flinch;
 // each client's own graphs pose them from that.
 void montage(uint32_t,const std::string& clip,const std::string& slot,float rate,float blend_in,float blend_out);
 void anim_param(uint32_t,const std::string& name,float value);
 void actor_anim(uint32_t,const glm::vec3& look,float look_weight,uint32_t flags);   // flags: 1 aiming, 2 staggered
 struct RemoteAnim{glm::vec3 look{0.0f};float look_weight=0;uint32_t flags=0;};
 const RemoteAnim* remote_anim(uint32_t local_id)const;
 void ui_message(int action,const std::string& file,const std::string& id,const std::string& text);
 bool force_player(uint32_t,const float*,int kind);
 void share_xp(uint32_t,float);
 void combat_visual(int,const std::string&,const glm::vec3&,const glm::vec3&,float=0);
 void test_aim();
 bool interact(uint32_t ecs_target);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
