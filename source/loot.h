#ifndef LOOT_H
#define LOOT_H

#include <stdbool.h>


#define MAX_INVENTORY_SLOTS 20
#define MAX_LOOT_TABLE 16

typedef enum {
    ITEM_NONE = 0,
    ITEM_MOLDY_BREAD,
    ITEM_DRIED_MEAT,
    ITEM_GREEN_HERB,
    ITEM_BLUE_VIAL,
    ITEM_CLOTH_FRAGMENT,
    ITEM_LUCKY_COIN,
    ITEM_MEAT_CLEAVER,
    ITEM_EMPTY_SCROLL,
    ITEM_SOUL_STONE,
    ITEM_BONE_SAW
} ItemType;

typedef struct {
    ItemType itemId;
    int weight;  // higher = more common
    const char* name;
} LootTableEntry;

void lootInit(void);

// Call this when player opens a container. Returns true if loot found.
bool lootContainer(void);

// Returns the name of the last item found (for display)
const char* lootGetLastItemName(void);

// Returns count of specific item in inventory
int lootGetItemCount(ItemType item);

// Adds item to inventory. Returns true if successful (inventory not full)
bool lootAddItem(ItemType item);

// Pure logic self-test
bool lootSelfTest(void);
// --- Inventory (Step 4) ---

// Fills arrays with item types you own and their counts.
// Returns number of distinct items (0 = empty inventory).
int lootGetInventoryList(ItemType* items, int* counts, int maxOut);

// Uses one of the item: applies its effect and removes it.
// Returns a short message describing what happened.
const char* lootUseItem(ItemType item);

// Display name for any item type.
const char* lootGetItemName(ItemType item);

// --- Weapons & durability ---
// Inventory is count-based, so durability belongs to the copy you are
// currently using. When it breaks, one copy is removed and the next
// copy (if any) starts fresh at full durability.

// True for items that can be wielded in combat.
bool lootIsWeapon(ItemType item);

// Full durability for a weapon type (0 = not a weapon).
int lootGetWeaponMaxDurability(ItemType item);

// Durability left on the copy in use (0 if you own none).
int lootGetWeaponDurability(ItemType item);

// Wears the weapon down by `amount`. Returns true if it broke
// (one copy removed from inventory).
bool lootWearWeapon(ItemType item, int amount);

// Best weapon you own (highest damage first), or ITEM_NONE for fists.
ItemType lootGetBestWeapon(void);

// Next owned weapon after `current`, cycling through fists (ITEM_NONE).
ItemType lootGetNextWeapon(ItemType current);
#endif