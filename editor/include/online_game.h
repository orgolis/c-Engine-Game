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
 void ui_message(int action,const std::string& file,const std::string& id,const std::string& text);
 bool force_player(uint32_t,const float*,int kind);
 void share_xp(uint32_t,float);
 void combat_visual(int,const std::string&,const glm::vec3&,const glm::vec3&,float=0);
 void test_aim();
 bool interact(uint32_t ecs_target);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
