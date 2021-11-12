#pragma once

#include <yaml.h>
#include <base/log.h>
#include <hook.h>
#include "base/playerdb.h"
#include <base/scheduler.h>
#include <Actor/Actor.h>
#include <Actor/Mob.h>
#include <Actor/Player.h>
#include <Actor/ServerPlayer.h>
#include <Actor/ActorDamageSource.h>
#include <Level/Level.h>
#include <Level/GameRuleIds.h>
#include <Level/DimensionIds.h>
#include <Container/Container.h>
#include <Container/SimpleContainer.h>
#include <Container/PlayerInventory.h>
#include <Packet/TextPacket.h>
#include <Net/ServerNetworkHandler.h>
#include <Item/ItemStack.h>
#include <BlockActor/BlockActor.h>
#include <Block/BlockSource.h>
#include <Block/VanillaBlocks.h>
#include <Math/Vec3.h>
#include <Packet/CommandRequestPacket.h>

#include <boost/scope_exit.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

inline struct Settings {
	bool operatorsCanBeInCombat = true;
	unsigned int combatTime = 30;
	bool combatTimeMessageEnabled = true;
	std::string initiatedCombatMessage = "You are now in combat. Do not log out!";
	std::string combatTimeMessage = "You are in combat for %time% more seconds!";
	std::string endedCombatMessage = "You are no longer in combat.";
	std::string logoutWhileInCombatMessage = "%name% logged out while in combat!";
	std::vector<std::string> bannedCommandsVector;
	std::string usedBannedCombatCommandMessage = "You cannot use this command while in combat!";
	bool setChestGravestoneOnLog = false;

	template <typename IO> static inline bool io(IO f, Settings &settings, YAML::Node &node) {
		return f(settings.operatorsCanBeInCombat, node["operatorsCanBeInCombat"]) &&
			   f(settings.combatTime, node["combatTime"]) &&
			   f(settings.combatTimeMessageEnabled, node["combatTimeMessageEnabled"]) &&
			   f(settings.initiatedCombatMessage, node["initiatedCombatMessage"]) &&
			   f(settings.combatTimeMessage, node["combatTimeMessage"]) &&
			   f(settings.endedCombatMessage, node["endedCombatMessage"]) &&
			   f(settings.logoutWhileInCombatMessage, node["logoutWhileInCombatMessage"]) &&
			   f(settings.bannedCommandsVector, node["bannedCommands"]) &&
			   f(settings.usedBannedCombatCommandMessage, node["usedBannedCombatCommandMessage"]) &&
			   f(settings.setChestGravestoneOnLog, node["setChestGravestoneOnLog"]);
	}
} settings;

DEF_LOGGER("CombatLogger");

struct Combat {
	uint64_t xuid;
	int time;
};

extern std::unordered_map<uint64_t, Combat> inCombat;