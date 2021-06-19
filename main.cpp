#include "global.h"
#include <dllentry.h>

Settings settings;
std::unordered_map<uint64_t, Combat> inCombat;
std::set<std::string> bannedCommands;
bool running = false;
bool first = true;
Mod::Scheduler::Token token;
DEFAULT_SETTINGS(settings);

Mod::Scheduler::Token getToken() { return token; }
std::unordered_map<uint64_t, Combat>& getInCombat() {
  return inCombat;
}

void dllenter() {}
void dllexit() {}
void PreInit() {
  Mod::PlayerDatabase::GetInstance().AddListener(SIG("initialized"), [](Mod::PlayerEntry const &entry) {});
  Mod::PlayerDatabase::GetInstance().AddListener(SIG("left"), [](Mod::PlayerEntry const &entry) {
    auto &db = Mod::PlayerDatabase::GetInstance();
    if (getInCombat().count(entry.xuid)) {
      auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
      uint64_t xuid = getInCombat()[entry.xuid].xuid;
      getInCombat().erase(entry.xuid);
      Inventory *invSource =
          CallServerClassMethod<PlayerInventory *>("?getSupplies@Player@@QEAAAEAVPlayerInventory@@XZ", entry.player)
              ->invectory.get();//->inventory.get(); //there is a typo in the main header which misspells "inventory" with "invectory"
      CallServerClassMethod<void>("?dropAll@Inventory@@UEAAX_N@Z", invSource, false);
      CallServerClassMethod<void>("?dropEquipment@Player@@UEAAXXZ", entry.player);

      std::string annouce = boost::replace_all_copy(settings.logoutWhileInCombatMessage, "%name%", entry.name);
      auto packetAnnouce = TextPacket::createTextPacket<TextPacketType::SystemMessage>(annouce);
      LocateService<Level>()->forEachPlayer([&](Player const &p) -> bool {
        p.sendNetworkPacket(packetAnnouce);
        return true;
      });
      entry.player->kill();
      if (getInCombat().count(xuid)) {
        if (getInCombat()[xuid].xuid == entry.xuid) {
          auto entry = db.Find(xuid);
          if (entry) { 
              entry->player->sendNetworkPacket(packet); 
          }
          getInCombat().erase(xuid);
        }
      }
      if (getInCombat().empty() && running) {
        Mod::Scheduler::SetTimeOut(
            Mod::Scheduler::GameTick(1), [=](auto) { Mod::Scheduler::ClearInterval(getToken()); });
        running = false;
      }
    }
  });
}
void PostInit() {
  for (std::string &str : settings.bannedCommandsVector) { 
      bannedCommands.emplace(str);
  }
  settings.bannedCommandsVector.clear();
}

THook(void*, "?actuallyHurt@Player@@UEAAXHAEBVActorDamageSource@@_N@Z", Player &player, int dmg, ActorDamageSource *source, bool bypassArmor) {
  auto &db = Mod::PlayerDatabase::GetInstance();
  auto it = db.Find((Player *) &player);
  if (!it) return original(player, dmg, source, bypassArmor);

  if (!settings.operatorsCanBeInCombat) {
    if (it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any) return original(player, dmg, source, bypassArmor);
  }

  if (source->isChildEntitySource() || source->isEntitySource()) {
    Actor *ac = LocateService<Level>()->fetchEntity(source->getEntityUniqueID(), false);
    if (ac && ac->getEntityTypeId() == ActorType::Player_0) {
      auto &db = Mod::PlayerDatabase::GetInstance();
      auto entry = db.Find((Player *) ac);
      if (!entry) return original(player, dmg, source, bypassArmor);

      if (!settings.operatorsCanBeInCombat) {
        if (entry->player->getCommandPermissionLevel() > CommandPermissionLevel::Any) return original(player, dmg, source, bypassArmor);
      }
      auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.initiatedCombatMessage);
      if (!getInCombat().count(entry->xuid)) { 
          entry->player->sendNetworkPacket(packet);
      }
      getInCombat()[entry->xuid].xuid = it->xuid;
      getInCombat()[entry->xuid].time = settings.combatTime;
      if (!getInCombat().count(it->xuid)) { 
          it->player->sendNetworkPacket(packet);
      }
      getInCombat()[it->xuid].xuid = entry->xuid;
      getInCombat()[it->xuid].time = settings.combatTime;
      if (!running) {
        running = true;
        token = Mod::Scheduler::SetInterval(Mod::Scheduler::GameTick(20), [=](auto) {
          if (running) {
            auto &db = Mod::PlayerDatabase::GetInstance();
            for (auto it = getInCombat().begin(); it != getInCombat().end();) {
              auto player = db.Find(it->first);
              if (!player) {
                it->second.time--;
                continue;
              }
              if (--it->second.time > 0) {
                std::string annouce =
                    boost::replace_all_copy(settings.inCombatDurationMessage, "%time%", std::to_string(it->second.time));
                auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(annouce);       
                if (settings.combatTimeMessageEnabled) {
                  player->player->sendNetworkPacket(packet);
                }                  
                ++it;
              } else {
                auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
                player->player->sendNetworkPacket(packet);
                it = getInCombat().erase(it);
              }
            }
            if (getInCombat().empty() && running) {
              Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) { Mod::Scheduler::ClearInterval(getToken()); });
              running = false;
            }
          }
        });
      }
    }
  }
  return original(player, dmg, source, bypassArmor);
}

THook(void*, "?die@Player@@UEAAXAEBVActorDamageSource@@@Z", Player &thi, void *src) {
  auto &db = Mod::PlayerDatabase::GetInstance();
  auto it = db.Find((Player *) &thi);
  if (!it) return original(thi, src);

  if (!settings.operatorsCanBeInCombat) {
    if (it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any) return original(thi, src);
  }
  if (getInCombat().count(it->xuid)) {
    auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
    Combat &combat = getInCombat()[it->xuid];
    getInCombat().erase(it->xuid);
    it->player->sendNetworkPacket(packet);
    if (getInCombat().count(combat.xuid)) {
      if (getInCombat()[combat.xuid].xuid == it->xuid) { 
          auto entry = db.Find(combat.xuid);
          if (entry) { 
            entry->player->sendNetworkPacket(packet);
          }
          getInCombat().erase(combat.xuid); 
      }
    }
    if (getInCombat().empty() && running) {
      Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) { Mod::Scheduler::ClearInterval(getToken()); });
      running = false;
    }
  }
  return original(thi, src);
}

THook(
    void*, "?handle@ServerNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVCommandRequestPacket@@@Z",
    ServerNetworkHandler &snh, NetworkIdentifier const &netid, void *pk) {
  auto &db = Mod::PlayerDatabase::GetInstance();
  auto it = db.Find(netid);
  if (!it || (!settings.operatorsCanBeInCombat && (it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any))) return original(snh, netid, pk);
  
  std::string command(direct_access<std::string>(pk, 0x28));
  command = command.substr(1);
  std::vector<std::string> results;
  boost::split(results, command, [](char c) { return c == ' '; });
  if (bannedCommands.count(results[0]) && getInCombat().count(it->xuid)) {
      auto packet = TextPacket::createTextPacket<TextPacketType::SystemMessage>(settings.usedBlockedCommandMessage);
      it->player->sendNetworkPacket(packet);
      return nullptr;
  }
  return original(snh, netid, pk);
}