#include "imgui.h"
#include "imgui_internal.h"
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "AIM.h"

#include "bin/dMenu.h"
#include "bin/ScreenKeyboardBridge.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>

#include "bin/ime/IMEWidgets.h"
#include "bin/Utils.h"
#include "Translator.h"

#include <type_traits>
#include "RE/M/Misc.h"

inline const char* getSafeFormName(const RE::TESForm* a_form)
{
	if (!a_form) {
		return TR("aim_unknown_name", "<unknown>");
	}
	const char* name = a_form->GetName();
	if (name && name[0] != '\0') {
		return name;
	}
	return TR("aim_unnamed_name", "<unnamed>");
}

// Spawn inventory items directly on the player (no console scripts)
inline void giveItemToPlayer(RE::TESForm* a_form, std::int32_t a_count)
{
	if (!a_form) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		ERROR("AIM: PlayerCharacter singleton is null, cannot give item");
		return;
	}

	auto* boundObject = a_form->As<RE::TESBoundObject>();
	if (!boundObject) {
		ERROR("AIM: selected form {:08X} is not a TESBoundObject, cannot give item", a_form->GetFormID());
		return;
	}

	if (a_count <= 0) {
		a_count = 1;
	}

	player->AddObjectToContainer(boundObject, nullptr, a_count, player);
	INFO("AIM: gave player {:d}x form {:08X}", a_count, a_form->GetFormID());
}

// Learn spells/powers directly on the player
inline void learnSpellForPlayer(RE::TESForm* a_form)
{
	if (!a_form) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		ERROR("AIM: PlayerCharacter singleton is null, cannot learn spell");
		return;
	}

	auto* spell = a_form->As<RE::SpellItem>();
	if (!spell) {
		ERROR("AIM: selected form {:08X} is not a SpellItem, cannot learn spell", a_form->GetFormID());
		return;
	}

	if (player->HasSpell(spell)) {
		INFO("AIM: player already knows spell {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_spell_known", "Already known spell: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
		return;
	}

	if (player->AddSpell(spell)) {
		INFO("AIM: learned spell {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_spell_learned", "Learned spell: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
	} else {
		ERROR("AIM: failed to learn spell {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_spell_failed", "Failed to learn spell: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
	}
}

// Learn a shout directly on the player
inline void learnShoutForPlayer(RE::TESForm* a_form)
{
	if (!a_form) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		ERROR("AIM: PlayerCharacter singleton is null, cannot learn shout");
		return;
	}

	auto* shout = a_form->As<RE::TESShout>();
	if (!shout) {
		ERROR("AIM: selected form {:08X} is not a TESShout, cannot learn shout", a_form->GetFormID());
		return;
	}

	if (player->HasShout(shout)) {
		INFO("AIM: player already knows shout {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_shout_known", "Already known shout: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
		return;
	}

	if (player->AddShout(shout)) {
		INFO("AIM: learned shout {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_shout_learned", "Learned shout: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
	} else {
		ERROR("AIM: failed to learn shout {:08X}", a_form->GetFormID());
		std::string msg = fmt::format(fmt::runtime(TR("aim_notify_shout_failed", "Failed to learn shout: {}")), getSafeFormName(a_form));
		RE::DebugNotification(msg.c_str());
	}
}

// Unlock a word of power directly on the player
inline void unlockWordForPlayer(RE::TESForm* a_form)
{
	if (!a_form) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		ERROR("AIM: PlayerCharacter singleton is null, cannot unlock word");
		return;
	}

	auto* word = a_form->As<RE::TESWordOfPower>();
	if (!word) {
		ERROR("AIM: selected form {:08X} is not a TESWordOfPower, cannot unlock word", a_form->GetFormID());
		return;
	}

	player->UnlockWord(word);
	INFO("AIM: unlocked word {:08X}", a_form->GetFormID());
	std::string msg = fmt::format(fmt::runtime(TR("aim_notify_word_unlocked", "Unlocked word: {}")), getSafeFormName(a_form));
	RE::DebugNotification(msg.c_str());
}

// Spawn NPCs at the player using native PlaceObjectAtMe
inline void spawnNPCAtPlayer(RE::TESForm* a_form, std::int32_t a_count)
{
	if (!a_form) {
		return;
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		ERROR("AIM: PlayerCharacter singleton is null, cannot spawn NPC");
		return;
	}

	// NPCs are TESActorBase, which ultimately derive from TESBoundObject,
	// so PlaceObjectAtMe can be used safely.
	auto* baseObject = a_form->As<RE::TESBoundObject>();
	if (!baseObject) {
		ERROR("AIM: selected form {:08X} is not a TESBoundObject, cannot spawn NPC", a_form->GetFormID());
		return;
	}

	if (a_count <= 0) {
		a_count = 1;
	}

	for (std::int32_t i = 0; i < a_count; ++i) {
		auto placed = player->PlaceObjectAtMe(baseObject, true);
		if (!placed) {
			ERROR("AIM: failed to place NPC for form {:08X}", a_form->GetFormID());
			break;
		}
	}

	INFO("AIM: spawned {:d} NPC(s) from form {:08X}", a_count, a_form->GetFormID());
}

static bool _init = false;

// Item type filters shown as checkboxes
static std::vector<std::pair<std::string, bool>> _types = {
	{ "Weapon", false },
	{ "Armor", false },
	{ "Ammo", false },
	{ "Book", false },
	{ "Ingredient", false },
	{ "Key", false },
	{ "Misc", false },
	{ "NPC", false },
	{ "Potion/Poison/Food", false },
	{ "Spell/Power", false },
	{ "Shout/Word", false }
};

static const char* TYPE_KEYS[] = {
	"aim_type_weapon",
	"aim_type_armor",
	"aim_type_ammo",
	"aim_type_book",
	"aim_type_ingredient",
	"aim_type_key",
	"aim_type_misc",
	"aim_type_npc",
	"aim_type_potion_poison_food",
	"aim_type_spell_power",
	"aim_type_shout_word"
};

static std::vector<std::pair<RE::TESFile*, bool>> _mods;
static int _selectedModIndex = -1;

static std::vector<std::pair<std::string, RE::TESForm*>> _items;  // items to show on AIM
static RE::TESForm* _selectedItem;
static std::string _amountText = "1";

static bool _cached = false;

static ImGuiTextFilter _modFilter;
static ImGuiTextFilter _itemFilter;

namespace
{
	void SetFilterText(ImGuiTextFilter& filter, const std::string& value)
	{
		const std::size_t length = (std::min)(value.size(), static_cast<std::size_t>(IM_ARRAYSIZE(filter.InputBuf) - 1));
		std::memcpy(filter.InputBuf, value.data(), length);
		filter.InputBuf[length] = '\0';
		filter.Build();
	}

	std::string DigitsOnly(std::string value)
	{
		value.erase(
			std::remove_if(
				value.begin(),
				value.end(),
				[](unsigned char c) { return !std::isdigit(c); }),
			value.end());
		return value;
	}

	template <class ApplyFn>
	void MaybeOpenGamepadKeyboardForLastItem(
		const std::string& currentValue,
		ApplyFn&& apply,
		ScreenKeyboardBridge::RequestOptions options = {})
	{
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (!ctx || ctx->NavInputSource != ImGuiInputSource_Gamepad) {
			return;
		}

		if (!ImGui::IsItemFocused() && !ImGui::IsItemActive()) {
			return;
		}

		if (!ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false)) {
			return;
		}

		ScreenKeyboardBridge::GetSingleton().RequestTextBox(
			currentValue,
			[apply = std::forward<ApplyFn>(apply)](std::string text) mutable {
				apply(std::move(text));
			},
			options);
	}
}

void AIM::init()
{
	if (_init) {
		return;
	}
	RE::TESDataHandler* data = RE::TESDataHandler::GetSingleton();
	std::unordered_set<RE::TESFile*> mods;

	// only show mods with valid items
	Utils::loadUsefulPlugins<RE::TESObjectWEAP>(mods);
	Utils::loadUsefulPlugins<RE::TESObjectARMO>(mods);
	Utils::loadUsefulPlugins<RE::TESAmmo>(mods);
	Utils::loadUsefulPlugins<RE::TESObjectBOOK>(mods);
	Utils::loadUsefulPlugins<RE::IngredientItem>(mods);
	Utils::loadUsefulPlugins<RE::TESKey>(mods);
	Utils::loadUsefulPlugins<RE::TESObjectMISC>(mods);
	Utils::loadUsefulPlugins<RE::TESNPC>(mods);
	Utils::loadUsefulPlugins<RE::AlchemyItem>(mods);
	Utils::loadUsefulPlugins<RE::SpellItem>(mods);
	Utils::loadUsefulPlugins<RE::TESShout>(mods);
	Utils::loadUsefulPlugins<RE::TESWordOfPower>(mods);

	for (auto mod : mods) {
		if (mod) {
			_mods.push_back({ mod, false });
		} else {
			ERROR("AIM: skipping null plugin pointer while building mod list");
		}
	}

	_selectedModIndex = _mods.empty() ? -1 : 0;

	_init = true;
	INFO("AIM initialized v2");
}

template <class T>
inline void cacheItems(RE::TESDataHandler* a_data)
{
	RE::TESFile* selectedMod = _mods[_selectedModIndex].first;
	for (auto form : a_data->GetFormArray<T>()) {
		if (!selectedMod->IsFormInMod(form->GetFormID())) {
			continue;
		}

		if constexpr (std::is_same_v<T, RE::SpellItem>) {
			auto spellType = form->GetSpellType();
			if (spellType != RE::MagicSystem::SpellType::kSpell &&
                spellType != RE::MagicSystem::SpellType::kPower &&
                spellType != RE::MagicSystem::SpellType::kLesserPower) {
				continue;
			}
		}

		std::string name;
		if (form->GetFullNameLength() != 0) {
			name = form->GetFullName();
		}

		if constexpr (std::is_same_v<T, RE::TESWordOfPower>) {
			auto* word = form->As<RE::TESWordOfPower>();
			if (name.empty() && word && !word->translation.empty()) {
				name = word->translation.c_str();
			}
		}

		if (name.empty()) {
			continue;
		}

		if (form->IsArmor()) {
			if (form->As<RE::TESObjectARMO>()->IsHeavyArmor()) {
				name += fmt::format(" ({})", TR("aim_tag_heavy", "Heavy"));
			} else {
				name += fmt::format(" ({})", TR("aim_tag_light", "Light"));
			}
		}

		// For NPCs, show the effective level in the list
		if constexpr (std::is_same_v<T, RE::TESNPC>) {
			std::uint16_t level = form->GetLevel();
			name += fmt::format(" ({})", fmt::format(fmt::runtime(TR("aim_tag_level_fmt", "Lv {}")), static_cast<int>(level)));
		}

		// For weapons, show base damage and type
		if constexpr (std::is_same_v<T, RE::TESObjectWEAP>) {
			auto* weap = form->As<RE::TESObjectWEAP>();
			if (weap) {
				auto damage = weap->GetAttackDamage();
				const char* typeLabel = "";
				switch (weap->GetWeaponType()) {
				case RE::WEAPON_TYPE::kOneHandSword:
					typeLabel = TR("aim_weap_1h_sword", "1H Sword");
					break;
				case RE::WEAPON_TYPE::kOneHandDagger:
					typeLabel = TR("aim_weap_dagger", "Dagger");
					break;
				case RE::WEAPON_TYPE::kOneHandAxe:
					typeLabel = TR("aim_weap_1h_axe", "1H Axe");
					break;
				case RE::WEAPON_TYPE::kOneHandMace:
					typeLabel = TR("aim_weap_mace", "Mace");
					break;
				case RE::WEAPON_TYPE::kTwoHandSword:
					typeLabel = TR("aim_weap_2h_sword", "2H Sword");
					break;
				case RE::WEAPON_TYPE::kTwoHandAxe:
					typeLabel = TR("aim_weap_2h_axe", "2H Axe");
					break;
				case RE::WEAPON_TYPE::kBow:
					typeLabel = TR("aim_weap_bow", "Bow");
					break;
				case RE::WEAPON_TYPE::kCrossbow:
					typeLabel = TR("aim_weap_crossbow", "Crossbow");
					break;
				case RE::WEAPON_TYPE::kStaff:
					typeLabel = TR("aim_weap_staff", "Staff");
					break;
				default:
					typeLabel = TR("aim_weap_generic", "Weapon");
					break;
				}

				name += fmt::format(" ({})", fmt::format(fmt::runtime(TR("aim_tag_weapon_dmg_fmt", "DMG {} {}")), static_cast<int>(damage), typeLabel));
			}
		}

		// For ammo, show damage
		if constexpr (std::is_same_v<T, RE::TESAmmo>) {
			auto* ammo = form->As<RE::TESAmmo>();
			if (ammo) {
				float dmg = ammo->GetRuntimeData().data.damage;
				name += fmt::format(" ({})", fmt::format(fmt::runtime(TR("aim_tag_ammo_dmg_fmt", "DMG {:.1f}")), dmg));
			}
		}

		// For alchemy, show type
		if constexpr (std::is_same_v<T, RE::AlchemyItem>) {
			auto* alchemy = form->As<RE::AlchemyItem>();
			if (alchemy) {
				if (alchemy->IsPoison()) {
					name += fmt::format(" ({})", TR("aim_tag_poison", "Poison"));
				} else if (alchemy->IsFood()) {
					name += fmt::format(" ({})", TR("aim_tag_food", "Food"));
				} else {
					name += fmt::format(" ({})", TR("aim_tag_potion", "Potion"));
				}
			}
		}

		// For spells, show subtype
		if constexpr (std::is_same_v<T, RE::SpellItem>) {
			auto* spell = form->As<RE::SpellItem>();
			if (spell) {
				const char* typeLabel = TR("aim_tag_spell", "Spell");
				switch (spell->GetSpellType()) {
				case RE::MagicSystem::SpellType::kSpell:
					typeLabel = TR("aim_tag_spell", "Spell");
					break;
				case RE::MagicSystem::SpellType::kPower:
					typeLabel = TR("aim_tag_power", "Power");
					break;
				case RE::MagicSystem::SpellType::kLesserPower:
					typeLabel = TR("aim_tag_lesser_power", "Lesser Power");
					break;
				default:
					typeLabel = TR("aim_tag_spell", "Spell");
					break;
				}
				name += fmt::format(" ({})", typeLabel);
			}
		}

		if constexpr (std::is_same_v<T, RE::TESShout>) {
			name += fmt::format(" ({})", TR("aim_tag_shout", "Shout"));
		}

		if constexpr (std::is_same_v<T, RE::TESWordOfPower>) {
			name += fmt::format(" ({})", TR("aim_tag_word", "Word"));
		}

		_items.push_back({ name, form });
	}
}

// Present all filtered items to the user under the "Items" section
void cache()
{
	_items.clear();
	auto data = RE::TESDataHandler::GetSingleton();
	if (!data || _selectedModIndex == -1) {
		return;
	}
	if (_types[0].second)
		cacheItems<RE::TESObjectWEAP>(data);
	if (_types[1].second)
		cacheItems<RE::TESObjectARMO>(data);
	if (_types[2].second)
		cacheItems<RE::TESAmmo>(data);
	if (_types[3].second)
		cacheItems<RE::TESObjectBOOK>(data);
	if (_types[4].second)
		cacheItems<RE::IngredientItem>(data);
	if (_types[5].second)
		cacheItems<RE::TESKey>(data);
	if (_types[6].second)
		cacheItems<RE::TESObjectMISC>(data);
	if (_types[7].second)
		cacheItems<RE::TESNPC>(data);
	if (_types[8].second)
		cacheItems<RE::AlchemyItem>(data);
	if (_types[9].second)
		cacheItems<RE::SpellItem>(data);
	if (_types[10].second) {
		cacheItems<RE::TESShout>(data);
		cacheItems<RE::TESWordOfPower>(data);
	}

	// filter out unplayable weapons in 2nd pass
	for (auto it = _items.begin(); it != _items.end();) {
		auto form = it->second;
		auto formtype = form->GetFormType();
		switch (formtype) {
		case RE::FormType::Weapon:
			if (form->As<RE::TESObjectWEAP>()->weaponData.flags.any(RE::TESObjectWEAP::Data::Flag::kNonPlayable)) {
				it = _items.erase(it);
				continue;
			}
			break;
		default:
			break;
		}
		++it;
	}

	_cached = true;
}

void AIM::show()
{
	// Use consistent padding and alignment
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));

	// Render the mod filter box
	IMEWidgets::TextFilter(TR("aim_mod_filter", "Mod Name"), _modFilter);
	MaybeOpenGamepadKeyboardForLastItem(
		_modFilter.InputBuf,
		[](std::string text) {
			SetFilterText(_modFilter, text);
		});

	// Render the mod dropdown menu
	if (!_mods.empty() && _selectedModIndex >= 0 && _selectedModIndex < static_cast<int>(_mods.size())) {
		const char* comboLabel = TR("aim_mod_combo", "Mods");
		const char* preview    = _mods[_selectedModIndex].first->GetFilename().data();

		bool comboOpen = ImGui::BeginCombo(comboLabel, preview);
		if (comboOpen) {
			for (int i = 0; i < static_cast<int>(_mods.size()); i++) {
				if (_modFilter.PassFilter(_mods[i].first->GetFilename().data())) {
					bool isSelected = (_mods[_selectedModIndex].first == _mods[i].first);
					if (ImGui::Selectable(_mods[i].first->GetFilename().data(), isSelected)) {
						_selectedModIndex = i;
						_cached = false;
					}
					if (isSelected) {
						ImGui::SetItemDefaultFocus();
					}
				}
			}
			ImGui::EndCombo();
		}

		// Allow scrolling or using arrow keys over the closed combo to change mods
		ImVec2 comboMin = ImGui::GetItemRectMin();
		ImVec2 comboMax = ImGui::GetItemRectMax();
		ImVec2 mousePos = ImGui::GetIO().MousePos;
		bool   hovered  = mousePos.x >= comboMin.x && mousePos.x <= comboMax.x &&
                        mousePos.y >= comboMin.y && mousePos.y <= comboMax.y;

		auto& io = ImGui::GetIO();

		auto advanceIndex = [&](int direction) {
			if (direction == 0) {
				return;
			}

			int newIndex  = _selectedModIndex;
			const int modCount = static_cast<int>(_mods.size());

			for (;;) {
				newIndex += direction;
				if (newIndex < 0 || newIndex >= modCount) {
					break;
				}

				if (_modFilter.PassFilter(_mods[newIndex].first->GetFilename().data())) {
					_selectedModIndex = newIndex;
					_cached = false;
					break;
				}
			}
		};

		if (!comboOpen && hovered) {
			// Mouse wheel navigation
			if (io.MouseWheel != 0.0f) {
				int direction = io.MouseWheel > 0.0f ? -1 : 1;
				advanceIndex(direction);
			}

			// Keyboard navigation (Up/Down arrows)
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				advanceIndex(-1);
			} else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				advanceIndex(1);
			}
		}
	}

	// Item type filtering
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Compact, auto-wrapping category layout to avoid overflow on narrow widths.
	const float minColumnWidth = 190.0f;
	float       availableWidth = ImGui::GetContentRegionAvail().x;
	int         columnCount = static_cast<int>(availableWidth / minColumnWidth);
	if (columnCount < 1) {
		columnCount = 1;
	} else if (columnCount > 4) {
		columnCount = 4;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 4));
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 2));
	if (ImGui::BeginTable("AIM_TypeFilters", columnCount, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
		for (int i = 0; i < static_cast<int>(_types.size()); i++) {
			ImGui::TableNextColumn();
			const char* label = TR(TYPE_KEYS[i], _types[i].first.c_str());
			if (ImGui::Checkbox(label, &_types[i].second)) {
				_cached = false;
			}
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleVar(2);

	ImGui::Spacing();

	// Render the list of items
	IMEWidgets::TextFilter(TR("aim_item_filter", "Item Name"), _itemFilter);
	MaybeOpenGamepadKeyboardForLastItem(
		_itemFilter.InputBuf,
		[](std::string text) {
			SetFilterText(_itemFilter, text);
		});

	{
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (ctx && ctx->NavInputSource == ImGuiInputSource_Gamepad &&
		    (ImGui::IsItemFocused() || ImGui::IsItemActive())) {
			ImGui::TextDisabled("%s", TR("aim_gamepad_keyboard_hint", "Press A to open the on-screen keyboard."));
		}
	}

	// Use a child window to limit the size of the item list
	ImGui::BeginChild("AIM_Items", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.7f), true);

	for (int i = 0; i < static_cast<int>(_items.size()); i++) {
		// Filter
		if (_itemFilter.PassFilter(_items[i].first.data())) {
			ImGui::PushID(i);
			if (ImGui::Selectable(_items[i].first.c_str(), _selectedItem == _items[i].second)) {
				_selectedItem = _items[i].second;
			}

			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("%08X", _items[i].second->GetFormID());
				ImGui::EndTooltip();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	// Show selected item info and spawn button
	if (_selectedItem != nullptr) {
		ImGui::Text("%s", TR("aim_selected_item", "Selected Item:"));
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", _selectedItem->GetName());
		if (ImGui::Button(TR("aim_spawn_button", "Spawn"), ImVec2(ImGui::GetContentRegionAvail().x * 0.2f, 0))) {
			if (RE::PlayerCharacter::GetSingleton() != nullptr) {
				// Parse amount safely
				int amount = 1;
				try {
					if (!_amountText.empty()) {
						amount = std::stoi(_amountText);
					}
				} catch (...) {
					amount = 1;
				}

				if (amount <= 0) {
					amount = 1;
				}

				switch (_selectedItem->GetFormType()) {
				case RE::FormType::NPC:
					spawnNPCAtPlayer(_selectedItem, amount);
					break;
				case RE::FormType::Spell:
					learnSpellForPlayer(_selectedItem);
					break;
				case RE::FormType::Shout:
					learnShoutForPlayer(_selectedItem);
					break;
				case RE::FormType::WordOfPower:
					unlockWordForPlayer(_selectedItem);
					break;
				default:
					giveItemToPlayer(_selectedItem, amount);
					break;
				}
			}
		}
		ImGui::SameLine();
		IMEWidgets::InputText(TR("aim_amount", "Amount"), &_amountText, ImGuiInputTextFlags_CharsDecimal);
		MaybeOpenGamepadKeyboardForLastItem(
			_amountText,
			[](std::string text) {
				_amountText = DigitsOnly(std::move(text));
			},
			ScreenKeyboardBridge::RequestOptions{
				true,
				false
			});
	}

	if (!_cached) {
		cache();
	}

	// Use consistent padding and alignment
	ImGui::PopStyleVar(2);
}
