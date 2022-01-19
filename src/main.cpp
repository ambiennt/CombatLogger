#include "main.h"
#include <dllentry.h>

DEFAULT_SETTINGS(settings);

std::set<std::string> bannedCommands;
bool running = false;
bool first = true;
Mod::Scheduler::Token token;

static Mod::PlayerDatabase &db = Mod::PlayerDatabase::GetInstance();

__forceinline Mod::Scheduler::Token getToken(void) {
	return token;
}

struct Combat {
	uint64_t xuid;
	int32_t time;
};

static std::unordered_map<uint64_t, struct Combat> inCombat;

__forceinline std::unordered_map<uint64_t, Combat>& getInCombat(void) {
	return inCombat;
}

constexpr const char* dimIdToString(DimensionIds d) {
	switch (d) {
		case DimensionIds::Overworld:
			return "overworld";
		case DimensionIds::Nether:
			return "nether";
		case DimensionIds::TheEnd:
			return "the end";

		default: return "Unknown";
	}
}

void handleCombatDeathSequence(Player *dead, Player *killer) {

	std::string deathStr = "§c" + dead->mPlayerName + " was slain";

	if (killer) {

		float kpCurrHealth = killer->getAttributeInstanceFromId(AttributeIds::Health)->currentVal;
		float kpCurrAbsorption = killer->getAttributeInstanceFromId(AttributeIds::Absorption)->currentVal;

		std::string kpName = killer->mPlayerName + " §a[" + std::to_string((int) kpCurrHealth);
		if (settings.useResourcePackGlyphsInDeathMessage) {
			kpName += "" + (kpCurrAbsorption > 0.0f ? " " + std::to_string((int) kpCurrAbsorption) + "]§c" : "]§c"); // glyph 0xE1FE, 0xE1FF
		}
		else {
			kpName += "§c❤" + (kpCurrAbsorption > 0.0f ? " §a" + std::to_string((int) kpCurrAbsorption) + "§e❤§a]§c" : "§a]§c");
		}

		deathStr += " by " + kpName;
	}

	const auto& pos = dead->getPos();
	const DimensionIds dim = (DimensionIds)(dead->mDimensionId);

	deathStr += " at " + std::to_string((int) pos.x) + ", " + std::to_string((int) (pos.y - 1.62f)) + ", " + std::to_string((int) pos.z) +
		((dim != DimensionIds::Overworld) ? (" [" + std::string(dimIdToString(dim)) + "]") : "");
	auto deathMsgPkt = TextPacket::createTextPacket<TextPacketType::SystemMessage>(deathStr);

	LocateService<Level>()->forEachPlayer([&](Player &p) -> bool {
		p.sendNetworkPacket(deathMsgPkt);
		return true;
	});

	// command stuff
	if (settings.executeDeathCommands) {

		auto& cs = Mod::CommandSupport::GetInstance();

		auto origin1 = std::make_unique<Mod::CustomCommandOrigin>();
		cs.ExecuteCommand(std::move(origin1), "execute @a[name=\"" + dead->mPlayerName + "\"] ~ ~ ~ function death");

		if (killer) {
			auto origin2 = std::make_unique<Mod::CustomCommandOrigin>(); // make 2 origins to not double delete pointer
			cs.ExecuteCommand(std::move(origin2), "execute \"" + killer->mPlayerName + "\" ~ ~ ~ function killer");
		}
	}
}

void dllenter() {}
void dllexit() {}
void PreInit() {
	Mod::PlayerDatabase::GetInstance().AddListener(SIG("left"), [](Mod::PlayerEntry const &entry) {

		if (getInCombat().count(entry.xuid)) {

			auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
			uint64_t xuid = getInCombat()[entry.xuid].xuid;
			getInCombat().erase(entry.xuid);
			std::string annouce = boost::replace_all_copy(settings.logoutWhileInCombatMessage, "%name%", entry.name);
			auto packetAnnouce = TextPacket::createTextPacket<TextPacketType::SystemMessage>(annouce);

			LocateService<Level>()->forEachPlayer([&](Player const &p) -> bool {
				p.sendNetworkPacket(packetAnnouce);
				return true;
			});

			auto* gr = &LocateService<Level>()->getGameRules();
			bool isKeepInventory = gr->getBool(GameRulesIndex::KeepInventory);

			if (entry.player->isPlayerInitialized() && !isKeepInventory) {

				entry.player->clearVanishEnchantedItems();
				auto playerInventory = entry.player->mInventory->inventory.get();
				auto playerUIItem = entry.player->getPlayerUIItem();
				auto newPos = entry.player->getPos();
				const auto region = entry.player->mRegion;

				if (settings.setChestGravestoneOnLog) {
					newPos.y -= 1.62f;

					int dimId = entry.player->mDimensionId;
					switch ((DimensionIds) dimId) {

						case DimensionIds::Overworld: {
							const auto& generator = LocateService<Level>()->getLevelDataWrapper()->getWorldGenerator();
							float lowerBounds = (generator == GeneratorType::Flat ? 1.0f : 5.0f);

							if (newPos.y > 255.0f) newPos.y = 255.0f;
							else if (newPos.y < lowerBounds) newPos.y = lowerBounds;
							break;
						}

						case DimensionIds::Nether: {
							if (newPos.y > 122.0f) newPos.y = 122.0f;
							else if (newPos.y < 5.0f) newPos.y = 5.0f;
							break;
						}

						case DimensionIds::TheEnd: {
							if (newPos.y > 255.0f) newPos.y = 255.0f;
							else if (newPos.y < 0.0f) newPos.y = 0.0f;
							break;
						}

						default: break;
					}

					BlockPos bp;
					auto normalizedChestPos_1 = bp.getBlockPos(newPos);
					region->setBlock(normalizedChestPos_1, *VanillaBlocks::mChest, 3, nullptr);
					newPos.x += 1.0f;
					auto normalizedChestPos_2 = bp.getBlockPos(newPos);
					region->setBlock(normalizedChestPos_2, *VanillaBlocks::mChest, 3, nullptr);

					auto chestBlock_1 = region->getBlockEntity(normalizedChestPos_1);
					auto chestBlock_2 = region->getBlockEntity(normalizedChestPos_2);
					auto chestContainer = chestBlock_1->getContainer();

					const int playerArmorSlots = 4;
					for (int i = 0; i < playerArmorSlots; i++) {
						auto armorItem = entry.player->getArmor((ArmorSlot) i);
						chestContainer->addItemToFirstEmptySlot(armorItem);
						entry.player->setArmor((ArmorSlot) i, ItemStack::EMPTY_ITEM);
					}

					auto offhandItem = entry.player->getOffhandSlot();
					chestContainer->addItemToFirstEmptySlot(*offhandItem);
					entry.player->setOffhandSlot(ItemStack::EMPTY_ITEM);

					int playerInventorySlots = playerInventory->getContainerSize();
					for (int i = 0; i < playerInventorySlots; i++) {
						auto inventoryItem = playerInventory->getItem(i);
						chestContainer->addItemToFirstEmptySlot(inventoryItem);
						playerInventory->setItem(i, ItemStack::EMPTY_ITEM);
					}

					chestContainer->addItemToFirstEmptySlot(playerUIItem);
					entry.player->setPlayerUIItem(PlayerUISlot::CursorSelected, ItemStack::EMPTY_ITEM);

					if (settings.enableExtraItemsForChestGravestone && !settings.extraItems.empty()) {

						for (auto &it : settings.extraItems) {

							CommandItem cmi;
							cmi.mId = it.id;
							ItemStack currentExtraItem;

							it.count = std::clamp(it.count, 0, 32767);
							it.aux = std::clamp(it.aux, 0, 32767);

							cmi.createInstanceWithoutCommand(&currentExtraItem, 0, it.aux, false);

							int maxStackSize = currentExtraItem.getMaxStackSize();
							int countNew = std::min(it.count, maxStackSize * playerInventorySlots);

							if (!currentExtraItem.isNull()) {

								while (countNew > 0) {

									int currentStack = std::min(maxStackSize, countNew);
									cmi.createInstanceWithoutCommand(&currentExtraItem, currentStack, it.aux, false);
									countNew -= currentStack;

									if (!it.customName.empty()) {
										currentExtraItem.setCustomName(it.customName);
									}

									if (!it.lore.empty()) {
										currentExtraItem.setCustomLore(it.lore);
									}

									if (!it.enchants.empty()) {

										for (auto &enchantList : it.enchants) {

											for (auto &enchant : enchantList) {

												EnchantmentInstance instance;
												instance.type  = (Enchant::Type) std::clamp(enchant.first, 0, 36);
												instance.level = std::clamp(enchant.second, -32768, 32767);
												EnchantUtils::applyEnchant(currentExtraItem, instance, true);
											}
										}
									}
									chestContainer->addItemToFirstEmptySlot(currentExtraItem);
								}
							}
						}
					}

					std::string chestName = entry.player->mPlayerName + "'s Gravestone";
					chestBlock_1->setCustomName(chestName);
					chestBlock_2->setCustomName(chestName);

					((ChestBlockActor*)chestBlock_1)->mNotifyPlayersOnChange = true;
					chestBlock_1->onChanged(*region);
				}
				else {
					entry.player->drop(playerUIItem, false);
					entry.player->dropEquipment();
					playerInventory->dropContents(*region, newPos, false);
				}
			}

			entry.player->kill();

			if (getInCombat().count(xuid)) {

				if (getInCombat()[xuid].xuid == entry.xuid) {

					auto attacker = db.Find(xuid);
					if (attacker) {
						attacker->player->sendNetworkPacket(packet);
						handleCombatDeathSequence(entry.player, attacker->player);
					}
					getInCombat().erase(xuid);
				}
			}

			if (getInCombat().empty() && running) {

				Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) {
					Mod::Scheduler::ClearInterval(getToken());
				});
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

THook(void, "?actuallyHurt@Player@@UEAAXHAEBVActorDamageSource@@_N@Z", Player *player, int dmg, ActorDamageSource &source, bool bypassArmor) {

	auto it = db.Find(player);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return original(player, dmg, source, bypassArmor);
	}

	if (source.isChildEntitySource() || source.isEntitySource()) {

		auto tempAttacker = LocateService<Level>()->fetchEntity(source.getEntityUniqueID(), false);

		if (tempAttacker && (tempAttacker->getEntityTypeId() == ActorType::Player_0) && (player != tempAttacker)) { // if source matches target

			auto attacker = db.Find((Player*) tempAttacker);

			if (!attacker || (!settings.operatorsCanBeInCombat && attacker->player->isOperator())) {
				return original(player, dmg, source, bypassArmor);
			}

			auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.initiatedCombatMessage);
			if (!getInCombat().count(attacker->xuid)) {
				attacker->player->sendNetworkPacket(packet);
			}

			getInCombat()[attacker->xuid].xuid = it->xuid;
			getInCombat()[attacker->xuid].time = settings.combatTime;
			if (!getInCombat().count(it->xuid)) {
				it->player->sendNetworkPacket(packet);
			}
			getInCombat()[it->xuid].xuid = attacker->xuid;
			getInCombat()[it->xuid].time = settings.combatTime;

			if (!running) {

				running = true;
				token = Mod::Scheduler::SetInterval(Mod::Scheduler::GameTick(20), [=](auto) {

					if (running) {
						for (auto it = getInCombat().begin(); it != getInCombat().end();) {
							auto player = db.Find(it->first);
							if (!player) {
								it->second.time--;
								continue;
							}
							if (--it->second.time > 0) {
								std::string annouce = boost::replace_all_copy(settings.combatTimeMessage, "%time%", std::to_string(it->second.time));
								auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(annouce);
								if (settings.combatTimeMessageEnabled) {
									player->player->sendNetworkPacket(packet);
								}
								++it;
							}
							else {
								auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
								player->player->sendNetworkPacket(packet);
								it = getInCombat().erase(it);
							}
						}
						if (getInCombat().empty() && running) {
							Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) {
								Mod::Scheduler::ClearInterval(getToken());
							});
							running = false;
						}
					}
				});
			}
		}
	}
	original(player, dmg, source, bypassArmor);
}

THook(void, "?die@Player@@UEAAXAEBVActorDamageSource@@@Z", Player *player, void* source) {

	auto it = db.Find(player);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return original(player, source);
	}

	if (getInCombat().count(it->xuid)) {

		auto packet = TextPacket::createTextPacket<TextPacketType::JukeboxPopup>(settings.endedCombatMessage);
		Combat &combat = getInCombat()[it->xuid];
		getInCombat().erase(it->xuid);
		it->player->sendNetworkPacket(packet);

		if (getInCombat().count(combat.xuid)) {

			if (getInCombat()[combat.xuid].xuid == it->xuid) {

				auto attacker = db.Find(combat.xuid);
				if (attacker) {
					attacker->player->sendNetworkPacket(packet);
					handleCombatDeathSequence(player, attacker->player);
				}
				getInCombat().erase(combat.xuid);
			}
		}

		if (getInCombat().empty() && running) {
			Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) {
				Mod::Scheduler::ClearInterval(getToken());
			});
			running = false;
		}
	}
	else {
		handleCombatDeathSequence(player, nullptr);
	}
	original(player, source);
}

THook(void, "?handle@ServerNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVCommandRequestPacket@@@Z",
	ServerNetworkHandler &snh, NetworkIdentifier const &netid, CommandRequestPacket &pkt) {

	auto it = db.Find(netid);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->isOperator())) {
		return original(snh, netid, pkt);
	}

	std::string commandString(pkt.command);
	commandString = commandString.substr(1);
	std::vector<std::string> results;
	boost::split(results, commandString, [](char c) { return c == ' '; });

	if (bannedCommands.count(results[0]) && getInCombat().count(it->xuid)) {
		auto packet = TextPacket::createTextPacket<TextPacketType::SystemMessage>(settings.usedBannedCombatCommandMessage);
		return it->player->sendNetworkPacket(packet);
	}
	original(snh, netid, pkt);
}