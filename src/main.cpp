#include "main.h"
#include <dllentry.h>

DEFAULT_SETTINGS(settings);

bool CombatLogger::isInCombatWith(uint64_t thisXuid, uint64_t thatXuid) {
	//return (getInCombat()[thisXuid].xuid == thatXuid);
	auto it = CombatLogger::getInCombat().find(thisXuid);
	if (it == CombatLogger::getInCombat().end()) return false;
	return (it->second.xuid == thatXuid);
}

void CombatLogger::clearCombatTokenIfNeeded() {
	if (CombatLogger::getInCombat().empty() && CombatLogger::IS_RUNNING) { // if there are no active combats
		Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [](auto) {
			Mod::Scheduler::ClearInterval(CombatLogger::SCHEDULER_TOKEN);
		});
		CombatLogger::IS_RUNNING = false;
	}
}

std::string CombatLogger::dimIdToString(DimensionID d) {
	switch (d) {
		case DimensionID::Overworld: return std::string("overworld");
		case DimensionID::Nether: return std::string("nether");
		case DimensionID::TheEnd: return std::string("the end");
		default: return std::string("unknown");
	}
}

void CombatLogger::handleCombatDeathSequence(Player &dead, Player *killer) {

	std::string deathStr("§c" + dead.mPlayerName + " was slain");

	if (killer) {
		int32_t kpCurrHealth = killer->getHealthAsInt();
		int32_t kpCurrAbsorption = killer->getAbsorptionAsInt();

		std::string kpName(killer->mPlayerName + " §a[" + std::to_string(kpCurrHealth));
		if (settings.useResourcePackGlyphsInDeathMessage) {
			kpName += HEALTH_GLYPH;
			if (kpCurrAbsorption > 0) {
				kpName += " " + std::to_string(kpCurrAbsorption) + ABSORPTION_GLYPH;
			}
		}
		else {
			kpName += HEALTH_ASCII;
			if (kpCurrAbsorption > 0) {
				kpName += " " + std::to_string(kpCurrAbsorption) + ABSORPTION_ASCII;
			}
		}
		kpName += "§a]§c";
		deathStr += " by " + kpName;
	}

	auto pos = dead.getPosGrounded();
	auto dimId = dead.mDimensionId;

	deathStr += " at " + std::to_string((int32_t)pos.x) + ", " + std::to_string((int32_t)pos.y) + ", " + std::to_string((int32_t)pos.z) +
		((dimId != DimensionID::Overworld) ? (" [" + dimIdToString(dimId) + "]") : "");
	auto deathMsgPkt = TextPacket::createTextPacket<TextPacketType::SystemMessage>(deathStr);

	dead.mLevel->forEachPlayer([&deathMsgPkt](Player &p) -> bool {
		p.sendNetworkPacket(deathMsgPkt);
		return true;
	});

	// command stuff
	if (settings.executeDeathCommands) {

		CMD_SUPPORT.ExecuteCommand(std::make_unique<Mod::CustomCommandOrigin>(),
			"runas \"" + dead.mPlayerName + "\" \"" + settings.deathCommand + "\"");

		if (killer) {
			CMD_SUPPORT.ExecuteCommand(std::make_unique<Mod::CustomCommandOrigin>(),
				"runas \"" + killer->mPlayerName + "\" \"" + settings.killerCommand + "\"");
		}
	}
}

void CombatLogger::dropPlayerInventory(Player &player) {
	player.drop(player.getPlayerUIItem(), false);
	player.dropEquipment();
	player.getRawInventory().dropContents(*(player.mRegion), player.getPos(), false);
}

bool ChestGravestone::isSafeBlock(const Block &block, bool isAboveBlock) {
	if (!block.mLegacyBlock) return false;
	auto& legacy = *block.mLegacyBlock.get();
	if (isAboveBlock) {
		if (legacy.isUnbreakableBlock()) return false;
	}
	return (legacy.isAirBlock() ||
			legacy.hasBlockProperty(BlockProperty::Liquid) ||
			legacy.hasBlockProperty(BlockProperty::TopSnow) ||
			(legacy.getMaterialType() == MaterialType::ReplaceablePlant));
}

bool ChestGravestone::isSafeRegion(const BlockSource &region, int32_t leadX, int32_t leadY, int32_t leadZ) {
	return (isSafeBlock(region.getBlock(leadX, leadY + 1, leadZ), true) &&
			isSafeBlock(region.getBlock(leadX + 1, leadY + 1, leadZ), true) &&
			isSafeBlock(region.getBlock(leadX, leadY, leadZ), false) &&
			isSafeBlock(region.getBlock(leadX + 1, leadY, leadZ), false));
}

std::pair<BlockPos, BlockPos> ChestGravestone::tryGetSafeChestGravestonePos(const Player &player) {

	BlockPos leading = player.getBlockPosGrounded();
	auto& region = *player.mRegion;

	if (!isSafeRegion(region, leading.x, leading.y, leading.z)) {

		// if we can't find a safe pos, just use original
		constexpr int32_t MAX_SAFE_POS_DISPLACEMENT = 3;

		for (int32_t i = 0; i < MAX_SAFE_POS_DISPLACEMENT; i++) {
			if (isSafeRegion(region, leading.x - i, leading.y, leading.z)) {
				leading = BlockPos(leading.x - i, leading.y, leading.z);
				break;
			}
			else if (isSafeRegion(region, leading.x + i, leading.y, leading.z)) {
				leading = BlockPos(leading.x + i, leading.y, leading.z);
				break;
			}
			else if (isSafeRegion(region, leading.x, leading.y, leading.z - i)) {
				leading = BlockPos(leading.x, leading.y, leading.z - i);
				break;
			}
			else if (isSafeRegion(region, leading.x, leading.y, leading.z + i)) {
				leading = BlockPos(leading.x, leading.y, leading.z + i);
				break;
			}
			else if (isSafeRegion(region, leading.x, leading.y - i, leading.z)) {
				leading = BlockPos(leading.x, leading.y - i, leading.z);
				break;
			}
			else if (isSafeRegion(region, leading.x, leading.y + i, leading.z)) {
				leading = BlockPos(leading.x, leading.y + i, leading.z);
				break;
			}
		}
	}

	switch (player.mDimensionId) {
		case DimensionID::Overworld: {
			int32_t lowerBounds = ((player.mLevel->getWorldGeneratorType() == GeneratorType::Flat) ? 1 : 5);
			leading.y = (int32_t)std::clamp(leading.y, lowerBounds, 255);
			break;
		}
		case DimensionID::Nether: {
			leading.y = (int32_t)std::clamp(leading.y, 5, 122);
			break;
		}
		case DimensionID::TheEnd: {
			leading.y = (int32_t)std::clamp(leading.y, 0, 255);
			break;
		}
		default: break;
	}

	return std::make_pair(leading, BlockPos(leading.x + 1, leading.y, leading.z));
}

void ChestGravestone::transferPlayerInventoryToChest(Player &player, Container& chestContainer) {

	constexpr int32_t PLAYER_ARMOR_SLOT_COUNT = 4;
	for (int32_t i = 0; i < PLAYER_ARMOR_SLOT_COUNT; i++) {
		ItemStack armorCopy(player.getArmor((ArmorSlot)i));
		chestContainer.addItemToFirstEmptySlot(armorCopy);
		player.setArmor((ArmorSlot)i, ItemStack::EMPTY_ITEM);
	}

	ItemStack offhandCopy(player.getOffhandSlot());
	chestContainer.addItemToFirstEmptySlot(offhandCopy);
	player.setOffhandSlot(ItemStack::EMPTY_ITEM);

	auto& playerInventory = player.getRawInventory();
	for (int32_t i = 0; i < playerInventory.getContainerSize(); i++) {
		ItemStack inventoryCopy(playerInventory.getItem(i));
		chestContainer.addItemToFirstEmptySlot(inventoryCopy);
		playerInventory.setItem(i, ItemStack::EMPTY_ITEM);
	}

	ItemStack UIItemCopy(player.getPlayerUIItem());
	chestContainer.addItemToFirstEmptySlot(UIItemCopy);
	player.setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM);
}

void ChestGravestone::tryAddYAMLItemStacksToChest(Container& chestContainer) {

  	if (settings.enableExtraItemsForChestGravestone && !settings.extraItems.empty()) {

		for (const auto &it : settings.extraItems) {

	  		auto item = ItemRegistry::getItem(it.id);
	  		if (!item) continue;

	  		int32_t newAux = std::clamp(it.aux, 0, 32767);
	  		ItemStack testStack(*item, 1, newAux);
	  		if (testStack.isNull()) continue;

	  		int32_t maxStackSize = (int32_t)testStack.getMaxStackSize();
	  		int32_t countNew = (int32_t)std::min(it.count, maxStackSize * chestContainer.getContainerSize()); // ensure valid count range

	  		while (countNew > 0) {

				int32_t currentStackSize = (int32_t)std::min(maxStackSize, countNew);
				ItemStack currentStack(*item, currentStackSize, newAux);
				countNew -= currentStack;

				if (!it.customName.empty()) {
					currentStack.setCustomName(it.customName);
				}

				if (!it.lore.empty()) {
					currentStack.setCustomLore(it.lore);
				}

				if (!it.enchants.empty()) {
		  			for (const auto &enchantList : it.enchants) {
						for (const auto &[enchId, enchLevel] : enchantList) {

			  				EnchantmentInstance enchInstance(
				  				(Enchant::Type)std::clamp(enchId, 0, 36), // clamp more valid range checks
				  				(int32_t)std::clamp(enchLevel, -32768, 32767)
							);
			  				EnchantUtils::applyUnfilteredEnchant(currentStack, enchInstance, false);
						}
		  			}
				}

				// so the extra item can stack if dead player already had this item in their inventory
				chestContainer.addItem(currentStack);
	  		}
		}
  	}
}

void dllenter() {}
void dllexit() {}
void PreInit() {
	Mod::PlayerDatabase::GetInstance().AddListener(SIG("left"), [](Mod::PlayerEntry const &entry) {

		if (CombatLogger::isInCombat(entry.xuid)) { // if this player is in combat

			auto& lvl = *entry.player->mLevel;
			auto& region = *entry.player->mRegion;
			bool isKeepInventory = lvl.getGameRuleValue<bool>(GameRulesIndex::KeepInventory);

			if (entry.player->isPlayerInitialized() && !isKeepInventory) {

				entry.player->clearVanishEnchantedItems();

				if (settings.setChestGravestoneOnLog) {

					auto [leadingChestPos, pairedChestPos] = ChestGravestone::tryGetSafeChestGravestonePos(*entry.player);

					region.setBlock(leadingChestPos, *VanillaBlocks::mChest, 3, nullptr);
					region.setBlock(pairedChestPos, *VanillaBlocks::mChest, 3, nullptr);

					auto chestBlock_1 = region.getBlockEntity(leadingChestPos);
					auto chestBlock_2 = region.getBlockEntity(pairedChestPos);
					auto chestContainer = chestBlock_1->getContainer();

					ChestGravestone::transferPlayerInventoryToChest(*(entry.player), *chestContainer);
					ChestGravestone::tryAddYAMLItemStacksToChest(*chestContainer);

					std::string chestName(entry.player->mPlayerName + "'s Gravestone");
					chestBlock_1->setCustomName(chestName);
					chestBlock_2->setCustomName(chestName);

					((ChestBlockActor*)chestBlock_1)->mNotifyPlayersOnChange = true;
					chestBlock_1->onChanged(region);
				}
				else {
					CombatLogger::dropPlayerInventory(*(entry.player));
				}
			}

			std::string annouceStr = boost::replace_all_copy(settings.logoutWhileInCombatMessage, "%name%", entry.name);
			auto combatLogAnnouncePkt = TextPacket::createTextPacket<TextPacketType::SystemMessage>(annouceStr);

			lvl.forEachPlayer([&combatLogAnnouncePkt](Player &p) -> bool {
				p.sendNetworkPacket(combatLogAnnouncePkt);
				return true;
			});

			uint64_t otherXuid = CombatLogger::getInCombat()[entry.xuid].xuid;
			if (CombatLogger::isInCombat(otherXuid)) { // if other player is in combat

				if (CombatLogger::isInCombatWith(entry.xuid, otherXuid)) { // if other player is in combat with this player

					CombatLogger::clearCombatStatus(otherXuid);

					auto attacker = PLAYER_DB.Find(otherXuid);
					if (attacker.has_value()) {
						// endCombatPkt can be locally scoped here because we don't need to send it to the player who left
						auto endCombatPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
						attacker->player->sendNetworkPacket(endCombatPkt);
						CombatLogger::handleCombatDeathSequence(*(entry.player), attacker->player);
					}
				}
			}
			CombatLogger::clearCombatStatus(entry.xuid);

			CombatLogger::clearCombatTokenIfNeeded();

			ExperienceOrb::spawnOrbs(region, entry.player->getPos(),
				entry.player->getOnDeathExperience(), ExperienceOrb::DropType::FromPlayer, nullptr);
			entry.player->mSpawnedXp = true;
			entry.player->spawnDeathParticles();
			entry.player->resetPlayerLevel();
			entry.player->kill(); // won't actually kill the player until they join back
		}
	});
}
void PostInit() {}

TInstanceHook(void, "?actuallyHurt@Player@@UEAAXHAEBVActorDamageSource@@_N@Z",
	Player, int32_t dmg, const ActorDamageSource &source, bool bypassArmor) {
	original(this, dmg, source, bypassArmor);

	auto it = PLAYER_DB.Find(this);
	if (!it.has_value() || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return;
	}

	if (source.isChildEntitySource() || source.isEntitySource()) {

		auto dmgSourceActor = it->player->mLevel->fetchEntity(source.getEntityUniqueID(), false);
		if (dmgSourceActor && dmgSourceActor->isInstanceOfPlayer() && (it->player != dmgSourceActor)) { // if source matches target

			auto attacker = PLAYER_DB.Find((Player*)dmgSourceActor);
			if (!attacker.has_value() || (!settings.operatorsCanBeInCombat && attacker->player->isOperator())) {
				return;
			}

			auto initiatedCombatPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.initiatedCombatMessage);
			if (!CombatLogger::isInCombat(attacker->xuid)) {
				attacker->player->sendNetworkPacket(initiatedCombatPkt);
			}

			CombatLogger::getInCombat()[attacker->xuid].xuid = it->xuid;
			CombatLogger::getInCombat()[attacker->xuid].time = settings.combatTime;
			if (!CombatLogger::isInCombat(it->xuid)) {
				it->player->sendNetworkPacket(initiatedCombatPkt);
			}
			CombatLogger::getInCombat()[it->xuid].xuid = attacker->xuid;
			CombatLogger::getInCombat()[it->xuid].time = settings.combatTime;

			if (!CombatLogger::IS_RUNNING) {

				CombatLogger::IS_RUNNING = true;
				CombatLogger::SCHEDULER_TOKEN = Mod::Scheduler::SetInterval(Mod::Scheduler::GameTick(20), [](auto) {
					if (CombatLogger::IS_RUNNING) {

						for (auto it = CombatLogger::getInCombat().begin(); it != CombatLogger::getInCombat().end();) {

							auto currentAttackerInList = PLAYER_DB.Find(it->first);
							if (!currentAttackerInList.has_value()) {
								it->second.time--;
								continue;
							}

							if ((--it->second.time) > 0) {
								std::string combatTimeStr = boost::replace_all_copy(settings.combatTimeMessage, "%time%", std::to_string(it->second.time));
								auto combatTimePkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(combatTimeStr);
								if (settings.combatTimeMessageEnabled) {
									currentAttackerInList->player->sendNetworkPacket(combatTimePkt);
								}
								++it;
							}
							else {
								auto combatEndPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
								currentAttackerInList->player->sendNetworkPacket(combatEndPkt);
								it = CombatLogger::getInCombat().erase(it);
							}
						}
						CombatLogger::clearCombatTokenIfNeeded();
					}
				});
			}
		}
	}
}

TInstanceHook(void, "?die@Player@@UEAAXAEBVActorDamageSource@@@Z", Player, void* source) {
	original(this, source);

	auto it = PLAYER_DB.Find(this);
	if (!it.has_value() || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return;
	}

	if (CombatLogger::isInCombat(it->xuid)) {

		auto endCombatPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);

		uint64_t otherXuid = CombatLogger::getInCombat()[it->xuid].xuid;
		if (CombatLogger::isInCombat(otherXuid)) {

			if (CombatLogger::isInCombatWith(it->xuid, otherXuid)) {

				CombatLogger::clearCombatStatus(otherXuid);

				auto attacker = PLAYER_DB.Find(otherXuid);
				if (attacker.has_value()) {
					attacker->player->sendNetworkPacket(endCombatPkt);
					CombatLogger::handleCombatDeathSequence(*(it->player), attacker->player);
				}
			}
		}
		CombatLogger::clearCombatStatus(it->xuid);
		it->player->sendNetworkPacket(endCombatPkt);

		CombatLogger::clearCombatTokenIfNeeded();
	}
	else {
		CombatLogger::handleCombatDeathSequence(*(it->player), nullptr);
	}
}