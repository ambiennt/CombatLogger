#pragma once

#include <yaml.h>
#include <string>
#include <map>
#include <base/log.h>
#include "base/playerdb.h"
#include <base/scheduler.h>
#include <Actor/Actor.h>
#include <Actor/ActorDamageSource.h>
#include <Actor/Player.h>
#include <Packet/TextPacket.h>
#include <Net/ServerNetworkHandler.h>
#include <boost/scope_exit.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

struct Settings {
  bool operatorsCanBeInCombat = true;
  int combatTime = 30;
  bool combatTimeMessageEnabled = true;
  std::string initiatedCombatMessage = "You are now in combat. Do not log out!";
  std::string inCombatDurationMessage = "You are in combat for %time% more seconds!";
  std::string endedCombatMessage = "You are no longer in combat.";
  std::string logoutWhileInCombatMessage = "%name% logged out while in combat!";
  std::vector<std::string> bannedCommandsVector;
  std::string usedBlockedCommandMessage = "You cannot use this command while in combat!";

  template <typename IO> static inline bool io(IO f, Settings &settings, YAML::Node &node) {
      return f(settings.operatorsCanBeInCombat, node["operatorsCanBeInCombat"]) &&
             f(settings.combatTime, node["combatTime"]) &&
             f(settings.combatTimeMessageEnabled, node["combatTimeMessageEnabled"]) &&
             f(settings.initiatedCombatMessage, node["initiatedCombatMessage"]) &&
             f(settings.inCombatDurationMessage, node["inCombatDurationMessage"]) &&
             f(settings.endedCombatMessage, node["endedCombatMessage"]) &&
             f(settings.logoutWhileInCombatMessage, node["logoutWhileInCombatMessage"]) &&
             f(settings.bannedCommandsVector, node["bannedCommands"]) &&
             f(settings.usedBlockedCommandMessage, node["usedBlockedCommandMessage"]);
  }
};

DEF_LOGGER("CombatLogger");

struct Combat {
  uint64_t xuid;
  int time;
};

extern Settings settings;

extern std::unordered_map<uint64_t, Combat> inCombat;
