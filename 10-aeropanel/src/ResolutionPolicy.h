#pragma once
// Native, independently implemented switching policy. No display or process API calls.
namespace resolution {
enum class Presence { Absent, Present, Unknown };
enum class Action { None, Apply, Restore };
struct Policy {
    bool active=false,gameSeen=false,launcherConsumed=false;
    Action Decide(bool enabled,int platform,Presence game,Presence launcher){
        if(!enabled)return active?Action::Restore:Action::None;
        if(game==Presence::Unknown||launcher==Presence::Unknown)return Action::None;
        bool running=game==Presence::Present,launching=launcher==Presence::Present;
        if(!launching)launcherConsumed=false;
        if(active&&running)gameSeen=true;
        if(!active&&(running||(platform<2&&launching&&!launcherConsumed)))return Action::Apply;
        if(active&&!running&&(platform==3||gameSeen||!launching))return Action::Restore;
        return Action::None;
    }
    void Applied(Presence game){active=true;gameSeen=game==Presence::Present;}
    void Restored(Presence launcher){active=false;gameSeen=false;launcherConsumed=launcher==Presence::Present;}
};
}
