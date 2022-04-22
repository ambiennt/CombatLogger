#include "main.h"
#include <dllentry.h>

DEFAULT_SETTINGS(settings);

static Mod::PlayerDatabase &db = Mod::PlayerDatabase::GetInstance();
static Mod::Scheduler::Token token;
static std::set<std::string> bannedCommands;
static bool running = false;

inline Mod::Scheduler::Token getToken(void) { return token; }

namespace CombatLogger {

std::unordered_map<uint64_t, struct Combat> inCombat;

constexpr const char* dimIdToString(DimensionID d) {
	switch (d) {
		case DimensionID::Overworld:
			return "overworld";
		case DimensionID::Nether:
			return "nether";
		case DimensionID::TheEnd:
			return "the end";

		default: return "unknown";
	}
}

bool isInCombatWith(uint64_t thisXuid, uint64_t thatXuid) {
	//return (getInCombat()[thisXuid].xuid == thatXuid);
	auto it = CombatLogger::getInCombat().find(thisXuid);
	if (it == CombatLogger::getInCombat().end()) return false;
	return (it->second.xuid == thatXuid);
}

void handleCombatDeathSequence(Player *dead, Player *killer) {

	std::string deathStr = "§c" + dead->mPlayerName + " was slain";

	if (killer) {

		int32_t kpCurrHealth = killer->getHealthAsInt();
		int32_t kpCurrAbsorption = killer->getAbsorptionAsInt();

		std::string kpName = killer->mPlayerName + " §a[" + std::to_string(kpCurrHealth);
		if (settings.useResourcePackGlyphsInDeathMessage) {
			kpName += "" + ((kpCurrAbsorption > 0) ? " " + std::to_string(kpCurrAbsorption) + "]§c" : "]§c"); // glyph 0xE1FE, 0xE1FF
		}
		else {
			kpName += "§c❤" + ((kpCurrAbsorption > 0) ? " §a" + std::to_string(kpCurrAbsorption) + "§e❤§a]§c" : "§a]§c");
		}

		deathStr += " by " + kpName;
	}

	auto pos = dead->getPosGrounded();
	auto dimId = dead->mDimensionId;

	deathStr += " at " + std::to_string((int32_t)pos.x) + ", " + std::to_string((int32_t)pos.y) + ", " + std::to_string((int32_t)pos.z) +
		((dimId != DimensionID::Overworld) ? (" [" + std::string(dimIdToString(dimId)) + "]") : "");
	auto deathMsgPkt = TextPacket::createTextPacket<TextPacketType::SystemMessage>(deathStr);

	dead->mLevel->forEachPlayer([&](Player &p) -> bool {
		p.sendNetworkPacket(deathMsgPkt);
		return true;
	});

	// command stuff
	if (settings.executeDeathCommands) {

		auto& cs = Mod::CommandSupport::GetInstance();

		cs.ExecuteCommand(std::make_unique<Mod::CustomCommandOrigin>(),
			"runas \"" + dead->mPlayerName + "\" \"" + settings.deathCommand + "\"");

		if (killer) {
			// make 2 origins to not double delete pointer
			cs.ExecuteCommand(std::make_unique<Mod::CustomCommandOrigin>(),
				"runas \"" + killer->mPlayerName + "\" \"" + settings.killerCommand + "\"");
		}
	}
}

} // namespace CombatLogger

namespace ChestGravestone {

bool isSafeBlock(Block const& block, bool isAboveBlock) {
	if (!block.mLegacyBlock) return false;
	auto& legacy = *block.mLegacyBlock.get();
	if (isAboveBlock) {
		if (legacy.isUnbreakableBlock()) return false;
	}
	return (legacy.isAirBlock() ||
			legacy.hasBlockProperty(BlockProperty::Liquid) ||
			legacy.hasBlockProperty(BlockProperty::TopSnow) ||
			(legacy.getMaterial() == MaterialType::ReplaceablePlant));
}

bool isSafeRegion(BlockSource &region, int32_t leadX, int32_t leadY, int32_t leadZ) {
	return (isSafeBlock(region.getBlock(leadX, leadY + 1, leadZ), true) &&
			isSafeBlock(region.getBlock(leadX + 1, leadY + 1, leadZ), true) &&
			isSafeBlock(region.getBlock(leadX, leadY, leadZ), false) &&
			isSafeBlock(region.getBlock(leadX + 1, leadY, leadZ), false));
}

std::pair<BlockPos, BlockPos> tryGetSafeChestGravestonePos(Player const &player) {

	BlockPos leading(player.getPosGrounded());
	auto& region = *player.mRegion;

	if (!isSafeRegion(region, leading.x, leading.y, leading.z)) {

		// if we can't find a safe pos, just use original
		const int32_t MAX_SAFE_POS_DISPLACEMENT = 3;

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

} // namespace CombatLogger

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
				auto& playerInventory = entry.player->getRawInventory();

				if (settings.setChestGravestoneOnLog) {

					auto [leadingChestPos, pairedChestPos] = ChestGravestone::tryGetSafeChestGravestonePos(*entry.player);

					region.setBlock(leadingChestPos, *VanillaBlocks::mChest, 3, nullptr);
					region.setBlock(pairedChestPos, *VanillaBlocks::mChest, 3, nullptr);

					auto chestBlock_1 = region.getBlockEntity(leadingChestPos);
					auto chestBlock_2 = region.getBlockEntity(pairedChestPos);
					auto chestContainer = chestBlock_1->getContainer();

					// copy player inventory to chest
					const int32_t playerArmorSlots = 4;
					for (int32_t i = 0; i < playerArmorSlots; i++) {
						ItemStack armorCopy(entry.player->getArmor((ArmorSlot)i));
						chestContainer->addItemToFirstEmptySlot(armorCopy);
						entry.player->setArmor((ArmorSlot)i, ItemStack::EMPTY_ITEM);
					}

					ItemStack offhandCopy(entry.player->getOffhandSlot());
					chestContainer->addItemToFirstEmptySlot(offhandCopy);
					entry.player->setOffhandSlot(ItemStack::EMPTY_ITEM);

					const int32_t playerInventorySlots = playerInventory.getContainerSize();
					for (int32_t i = 0; i < playerInventorySlots; i++) {
						ItemStack inventoryCopy(playerInventory.getItem(i));
						chestContainer->addItemToFirstEmptySlot(inventoryCopy);
						playerInventory.setItem(i, ItemStack::EMPTY_ITEM);
					}

					ItemStack UIItemCopy(entry.player->getPlayerUIItem());
					chestContainer->addItemToFirstEmptySlot(UIItemCopy);
					entry.player->setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM);

					if (settings.enableExtraItemsForChestGravestone && !settings.extraItems.empty()) {

						for (auto& it : settings.extraItems) {

							// ensure valid aux range
							it.aux = (int32_t)std::clamp(it.aux, 0, 32767);
				
							// hack - so we can test for valid item ID and get its stack size outside of loop
							// we need to pass in the aux to the temp instance because different auxes have different stack sizes
							CommandItem cmi(it.id);
							ItemStack currentExtraItem;
							cmi.createInstance(&currentExtraItem, 0, it.aux, false);

							if (!currentExtraItem.isNull()) {

								int32_t maxStackSize = (int32_t)currentExtraItem.getMaxStackSize();
								int32_t countNew = (int32_t)std::min(it.count, maxStackSize * playerInventorySlots); // ensure valid count range

								while (countNew > 0) {

									int32_t currentStack = (int32_t)std::min(maxStackSize, countNew);
									cmi.createInstance(&currentExtraItem, currentStack, it.aux, false);
									countNew -= currentStack;

									if (!it.customName.empty()) {
										currentExtraItem.setCustomName(it.customName);
									}

									if (!it.lore.empty()) {
										currentExtraItem.setCustomLore(it.lore);
									}

									if (!it.enchants.empty()) {

										for (auto& enchantList : it.enchants) {

											for (auto& enchant : enchantList) {

												EnchantmentInstance instance;
												instance.type  = (Enchant::Type)std::clamp(enchant.first, 0, 36); // more valid range checks
												instance.level = (int32_t)std::clamp(enchant.second, -32768, 32767);
												EnchantUtils::applyEnchant(currentExtraItem, instance, true);
											}
										}
									}
									// so the extra item can stack if dead player already had this item in their inventory
									chestContainer->addItem(currentExtraItem);
								}
							}
						}
					}

					std::string chestName = entry.player->mPlayerName + "'s Gravestone";
					chestBlock_1->setCustomName(chestName);
					chestBlock_2->setCustomName(chestName);

					((ChestBlockActor*)chestBlock_1)->mNotifyPlayersOnChange = true;
					chestBlock_1->onChanged(region);
				}
				else {
					entry.player->drop(entry.player->getPlayerUIItem(), false);
					entry.player->dropEquipment();
					playerInventory.dropContents(region, entry.player->getPos(), false);
				}
			}

			std::string annouceStr = boost::replace_all_copy(settings.logoutWhileInCombatMessage, "%name%", entry.name);
			auto combatLogAnnouncePkt = TextPacket::createTextPacket<TextPacketType::SystemMessage>(annouceStr);

			lvl.forEachPlayer([&](Player &p) -> bool {
				p.sendNetworkPacket(combatLogAnnouncePkt);
				return true;
			});

			uint64_t otherXuid = CombatLogger::getInCombat()[entry.xuid].xuid;
			if (CombatLogger::isInCombat(otherXuid)) { // if other player is in combat

				if (CombatLogger::isInCombatWith(entry.xuid, otherXuid)) { // if other player is in combat with this player

					CombatLogger::clearCombatStatus(otherXuid);

					auto attacker = db.Find(otherXuid);
					if (attacker) {
						// endCombatPkt can be locally scoped here because we don't need to send it to the player who left
						auto endCombatPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
						attacker->player->sendNetworkPacket(endCombatPkt);
						CombatLogger::handleCombatDeathSequence(entry.player, attacker->player);
					}
				}
			}
			CombatLogger::clearCombatStatus(entry.xuid);

			if (CombatLogger::getInCombat().empty() && running) { // if there are no active combats and scheduler is running

				Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [](auto) {
					Mod::Scheduler::ClearInterval(getToken());
				});
				running = false;
			}

			ExperienceOrb::spawnOrbs(region, entry.player->getPos(),
				entry.player->getOnDeathExperience(), ExperienceOrb::DropType::FromPlayer, nullptr);
			entry.player->mSpawnedXp = true;
			entry.player->spawnDeathParticles();
			entry.player->resetPlayerLevel();
			entry.player->kill(); // won't actually kill the player until they join back
		}
	});
}
void PostInit() {
	for (auto& str : settings.bannedCommandsVector) {
		bannedCommands.emplace(str);
	}
	settings.bannedCommandsVector.clear();
}

TInstanceHook(void, "?actuallyHurt@Player@@UEAAXHAEBVActorDamageSource@@_N@Z",
	Player, int32_t dmg, ActorDamageSource &source, bool bypassArmor) {

	original(this, dmg, source, bypassArmor);

	auto it = db.Find(this);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return;
	}

	if (source.isChildEntitySource() || source.isEntitySource()) {

		auto tempAttacker = it->player->mLevel->fetchEntity(source.getEntityUniqueID(), false);

		if (tempAttacker && tempAttacker->isInstanceOfPlayer() && (it->player != tempAttacker)) { // if source matches target

			auto attacker = db.Find((Player*)tempAttacker);

			if (!attacker || (!settings.operatorsCanBeInCombat && attacker->player->isOperator())) {
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

			if (!running) {

				running = true;
				token = Mod::Scheduler::SetInterval(Mod::Scheduler::GameTick(20), [=](auto) {

					if (running) {

						for (auto it = CombatLogger::getInCombat().begin(); it != CombatLogger::getInCombat().end();) {

							auto attacker = db.Find(it->first);
							if (!attacker) {
								it->second.time--;
								continue;
							}
							
							if (--it->second.time > 0) {
								std::string combatTimeStr = boost::replace_all_copy(
									settings.combatTimeMessage, "%time%", std::to_string(it->second.time));
								auto combatTimePkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(combatTimeStr);
								if (settings.combatTimeMessageEnabled) {
									attacker->player->sendNetworkPacket(combatTimePkt);
								}
								++it;
							}
							else {
								auto combatEndPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
								attacker->player->sendNetworkPacket(combatEndPkt);
								it = CombatLogger::getInCombat().erase(it);
							}
						}
						if (CombatLogger::getInCombat().empty() && running) {
							Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [](auto) {
								Mod::Scheduler::ClearInterval(getToken());
							});
							running = false;
						}
					}
				});
			}
		}
	}
}

TInstanceHook(void, "?die@Player@@UEAAXAEBVActorDamageSource@@@Z", Player, void* source) {

	original(this, source);

	auto it = db.Find(this);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return;
	}

	if (CombatLogger::isInCombat(it->xuid)) {

		auto endCombatPkt = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);

		uint64_t otherXuid = CombatLogger::getInCombat()[it->xuid].xuid;
		if (CombatLogger::isInCombat(otherXuid)) {

			if (CombatLogger::isInCombatWith(it->xuid, otherXuid)) {

				CombatLogger::clearCombatStatus(otherXuid);

				auto attacker = db.Find(otherXuid);
				if (attacker) {
					attacker->player->sendNetworkPacket(endCombatPkt);
					CombatLogger::handleCombatDeathSequence(it->player, attacker->player);
				}
			}
		}
		CombatLogger::clearCombatStatus(it->xuid);
		it->player->sendNetworkPacket(endCombatPkt);

		if (CombatLogger::getInCombat().empty() && running) {
			Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [](auto) {
				Mod::Scheduler::ClearInterval(getToken());
			});
			running = false;
		}
	}
	else {
		CombatLogger::handleCombatDeathSequence(it->player, nullptr);
	}
}

TClasslessInstanceHook(void, "?handle@ServerNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVCommandRequestPacket@@@Z",
	NetworkIdentifier const &netId, CommandRequestPacket &pkt) {

	if (!pkt.mIsInternalSource) {
	
		auto it = db.Find(netId);

		if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
			return original(this, netId, pkt);
		}

		std::string commandString(pkt.mCommand);
		commandString = commandString.substr(1);
		std::vector<std::string> results;
		boost::split(results, commandString, [](char c) { return c == ' '; });

		if (bannedCommands.count(results[0]) && CombatLogger::isInCombat(it->xuid)) {
			auto packet = TextPacket::createTextPacket<TextPacketType::SystemMessage>(settings.usedBannedCombatCommandMessage);
			return it->player->sendNetworkPacket(packet);
		}
	}
	original(this, netId, pkt);
}