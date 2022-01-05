#include "main.h"
#include <dllentry.h>

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
	Mod::PlayerDatabase::GetInstance().AddListener(SIG("left"), [](Mod::PlayerEntry const &entry) {
		auto &db = Mod::PlayerDatabase::GetInstance();
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
			GameRulesIndex keepInventoryId = GameRulesIndex::KeepInventory;
			bool isKeepInventory = CallServerClassMethod<bool>("?getBool@GameRules@@QEBA_NUGameRuleId@@@Z", gr, &keepInventoryId);

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
							const auto& generator = LocateService<Level>()->GetLevelDataWrapper()->getWorldGenerator();
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
					auto entry = db.Find(xuid);
					if (entry) {
						entry->player->sendNetworkPacket(packet);
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

THook(void, "?actuallyHurt@Player@@UEAAXHAEBVActorDamageSource@@_N@Z", Player &player, int dmg, ActorDamageSource *source, bool bypassArmor) {
	auto &db = Mod::PlayerDatabase::GetInstance();
	auto it = db.Find((Player *) &player);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any)) {
		return original(player, dmg, source, bypassArmor);
	}

	if (source->isChildEntitySource() || source->isEntitySource()) {
		Actor *ac = LocateService<Level>()->fetchEntity(source->getEntityUniqueID(), false);
		if (ac && (ac->getEntityTypeId() == ActorType::Player_0) && (&player != ac)) { // if source matches target
			auto &db = Mod::PlayerDatabase::GetInstance();
			auto entry = db.Find((Player *) ac);

			if (!entry || (!settings.operatorsCanBeInCombat && entry->player->getCommandPermissionLevel() > CommandPermissionLevel::Any)) {
				return original(player, dmg, source, bypassArmor);
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
										boost::replace_all_copy(settings.combatTimeMessage, "%time%", std::to_string(it->second.time));
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
							Mod::Scheduler::SetTimeOut(Mod::Scheduler::GameTick(1), [=](auto) { Mod::Scheduler::ClearInterval(getToken()); });
							running = false;
						}
					}
				});
			}
		}
	}
	original(player, dmg, source, bypassArmor);
}

THook(void, "?die@Player@@UEAAXAEBVActorDamageSource@@@Z", Player &thi, void *src) {
	auto &db = Mod::PlayerDatabase::GetInstance();
	auto it = db.Find((Player *) &thi);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any)) {
		return original(thi, src);
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
	original(thi, src);
}

THook(void, "?handle@ServerNetworkHandler@@UEAAXAEBVNetworkIdentifier@@AEBVCommandRequestPacket@@@Z",
	ServerNetworkHandler &snh, NetworkIdentifier const &netid, CommandRequestPacket &pkt) {

	auto &db = Mod::PlayerDatabase::GetInstance();
	auto it = db.Find(netid);

	if (!it || (!settings.operatorsCanBeInCombat && it->player->getCommandPermissionLevel() > CommandPermissionLevel::Any)) {
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