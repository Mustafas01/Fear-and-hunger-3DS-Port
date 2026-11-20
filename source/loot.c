#include "loot.h"
#include "rng.h"
#include <string.h>
#include <stdio.h>
#include "survival.h"


// CHANGE THIS TO true LATER FOR TRUE F&H DIFFICULTY
// false = guaranteed loot every time (easy mode for testing)
// true  = 50% chance to find nothing, then weighted roll for item
#define BRUTAL_MODE false

static LootTableEntry lootTable[MAX_LOOT_TABLE];
static int lootTableSize;
static int inventory[256];  // count per ItemType
static int weaponDur[256];  // durability of the copy in use, per weapon type
static char lastItemName[64];
static int totalWeight;

static const char* itemNames[] = {
    "Nothing",
    "Moldy Bread",
    "Dried Meat", 
    "Green Herb",
    "Blue Vial",
    "Cloth Fragment",
    "Lucky Coin",
    "Meat Cleaver",
    "Empty Scroll",
    "Soul Stone",
    "Bone Saw"
};

void lootInit(void) {
    // Clear inventory
    memset(inventory, 0, sizeof(inventory));
    memset(weaponDur, 0, sizeof(weaponDur));
    strcpy(lastItemName, "");
    
    // Setup loot table with weights
    // Higher weight = more common. Total weight = 100 for easy math.
    lootTable[0] = (LootTableEntry){ITEM_MOLDY_BREAD, 25, itemNames[1]};
    lootTable[1] = (LootTableEntry){ITEM_DRIED_MEAT, 20, itemNames[2]};
    lootTable[2] = (LootTableEntry){ITEM_GREEN_HERB, 15, itemNames[3]};
    lootTable[3] = (LootTableEntry){ITEM_BLUE_VIAL, 10, itemNames[4]};
    lootTable[4] = (LootTableEntry){ITEM_CLOTH_FRAGMENT, 15, itemNames[5]};
    lootTable[5] = (LootTableEntry){ITEM_LUCKY_COIN, 5, itemNames[6]};
    lootTable[6] = (LootTableEntry){ITEM_MEAT_CLEAVER, 5, itemNames[7]};
    lootTable[7] = (LootTableEntry){ITEM_EMPTY_SCROLL, 3, itemNames[8]};
    lootTable[8] = (LootTableEntry){ITEM_SOUL_STONE, 2, itemNames[9]};
    lootTable[9] = (LootTableEntry){ITEM_BONE_SAW, 5, itemNames[10]};
    
    lootTableSize = 10;
    
    // Calculate total weight
    totalWeight = 0;
    for (int i = 0; i < lootTableSize; i++) {
        totalWeight += lootTable[i].weight;
    }
}

bool lootContainer(void) {
    // First coin flip: do we find anything?
    if (BRUTAL_MODE) {
        if (!coinFlip(false)) {
            strcpy(lastItemName, "nothing...");
            return false;
        }
    }
    
    // Second roll: what do we get? (weighted)
    int roll = rngNext(totalWeight);
    int current = 0;
    
    for (int i = 0; i < lootTableSize; i++) {
        current += lootTable[i].weight;
        if (roll < current) {
            // Found this item
            strcpy(lastItemName, lootTable[i].name);
            return lootAddItem(lootTable[i].itemId);
        }
    }
    
    // Fallback (should never happen)
    strcpy(lastItemName, "nothing");
    return false;
}

const char* lootGetLastItemName(void) {
    return lastItemName;
}

int lootGetItemCount(ItemType item) {
    if (item < 0 || item >= 256) return 0;
    return inventory[item];
}

bool lootAddItem(ItemType item) {
    if (item < 0 || item >= 256) return false;
    if (inventory[item] >= 99) return false;  // cap at 99
    inventory[item]++;
    // First copy of a weapon: it starts at full durability.
    if (inventory[item] == 1 && lootIsWeapon(item))
        weaponDur[item] = lootGetWeaponMaxDurability(item);
    return true;
}
const char* lootGetItemName(ItemType item) {
    if (item < 0 || item > ITEM_BONE_SAW) return "???";
    return itemNames[item];
}

int lootGetInventoryList(ItemType* items, int* counts, int maxOut) {
    int n = 0;
    for (int i = 1; i <= ITEM_BONE_SAW && n < maxOut; i++) {
        if (inventory[i] > 0) {
            items[n] = (ItemType)i;
            counts[n] = inventory[i];
            n++;
        }
    }
    return n;
}

const char* lootUseItem(ItemType item) {
    if (item < 0 || item > ITEM_BONE_SAW) return "???";
    if (inventory[item] <= 0) return "none left";

    switch (item) {
        case ITEM_MOLDY_BREAD:
            survivalEat(15);
            survivalDrain(0, 5);   // eating mold hurts the soul
            inventory[item]--;
            return "+15 food, -5 mind";
        case ITEM_DRIED_MEAT:
            survivalEat(25);
            inventory[item]--;
            return "+25 food";
        case ITEM_GREEN_HERB:
            survivalSoothe(10);
            inventory[item]--;
            return "+10 mind";
        case ITEM_BLUE_VIAL:
            survivalSoothe(30);
            inventory[item]--;
            return "+30 mind";
        case ITEM_LUCKY_COIN:
            return "passive: lucky flips";
        case ITEM_MEAT_CLEAVER:
        case ITEM_BONE_SAW: {
            static char msg[40];
            snprintf(msg, sizeof(msg), "weapon: %d/%d durability",
                weaponDur[item], lootGetWeaponMaxDurability(item));
            return msg;
        }
        default:
            return "can't use yet";
    }
}
// ---------------------------------------------------------------
// Weapons & durability
// ---------------------------------------------------------------

int lootGetWeaponMaxDurability(ItemType item) {
    switch (item) {
        case ITEM_MEAT_CLEAVER: return 12;  // hits hard, dulls fast
        case ITEM_BONE_SAW:     return 18;  // weaker but sturdier
        default:                return 0;
    }
}

bool lootIsWeapon(ItemType item) {
    return lootGetWeaponMaxDurability(item) > 0;
}

int lootGetWeaponDurability(ItemType item) {
    if (!lootIsWeapon(item) || inventory[item] <= 0) return 0;
    return weaponDur[item];
}

bool lootWearWeapon(ItemType item, int amount) {
    if (!lootIsWeapon(item) || inventory[item] <= 0) return false;
    weaponDur[item] -= amount;
    if (weaponDur[item] > 0) return false;

    // Broken: lose this copy, the next one (if any) is fresh.
    inventory[item]--;
    weaponDur[item] = inventory[item] > 0
        ? lootGetWeaponMaxDurability(item) : 0;
    return true;
}

// Ordered strongest first; fists (ITEM_NONE) are always available.
static const ItemType weaponOrder[] = {
    ITEM_MEAT_CLEAVER, ITEM_BONE_SAW, ITEM_NONE
};
#define WEAPON_ORDER_N (int)(sizeof(weaponOrder) / sizeof(weaponOrder[0]))

static bool ownsWeapon(ItemType w) {
    return w == ITEM_NONE || inventory[w] > 0;
}

ItemType lootGetBestWeapon(void) {
    for (int i = 0; i < WEAPON_ORDER_N; i++)
        if (ownsWeapon(weaponOrder[i])) return weaponOrder[i];
    return ITEM_NONE;
}

ItemType lootGetNextWeapon(ItemType current) {
    int start = 0;
    for (int i = 0; i < WEAPON_ORDER_N; i++)
        if (weaponOrder[i] == current) { start = i; break; }
    for (int k = 1; k <= WEAPON_ORDER_N; k++) {
        ItemType w = weaponOrder[(start + k) % WEAPON_ORDER_N];
        if (ownsWeapon(w)) return w;
    }
    return ITEM_NONE;
}

bool lootSelfTest(void) {
    lootInit();
    
    // Test 1: Inventory starts empty
    if (lootGetItemCount(ITEM_MOLDY_BREAD) != 0)
        return false;
    
    // Test 2: Add item works
    if (!lootAddItem(ITEM_BLUE_VIAL))
        return false;
    if (lootGetItemCount(ITEM_BLUE_VIAL) != 1)
        return false;
    
    // Test 3: Add multiple
    lootAddItem(ITEM_BLUE_VIAL);
    lootAddItem(ITEM_BLUE_VIAL);
    if (lootGetItemCount(ITEM_BLUE_VIAL) != 3)
        return false;
    
    // Test 4: Container always gives something in easy mode
    // (BRUTAL_MODE is false, so lootContainer should always return true)
    for (int i = 0; i < 10; i++) {
        if (!lootContainer())
            return false;
    }
    
    // Test 5: We should have gotten items
    int totalItems = 0;
    for (int i = 0; i < 256; i++) {
        totalItems += lootGetItemCount((ItemType)i);
    }
    if (totalItems < 5)  // We added 3 + at least 10 from containers
        return false;

    // Test 6: weapon durability
    lootInit();
    if (lootGetBestWeapon() != ITEM_NONE) return false;      // fists
    lootAddItem(ITEM_MEAT_CLEAVER);
    lootAddItem(ITEM_MEAT_CLEAVER);
    int max = lootGetWeaponMaxDurability(ITEM_MEAT_CLEAVER);
    if (lootGetWeaponDurability(ITEM_MEAT_CLEAVER) != max) return false;
    if (lootWearWeapon(ITEM_MEAT_CLEAVER, max - 1)) return false; // not yet
    if (!lootWearWeapon(ITEM_MEAT_CLEAVER, 1)) return false;      // breaks
    if (lootGetItemCount(ITEM_MEAT_CLEAVER) != 1) return false;   // spare left
    if (lootGetWeaponDurability(ITEM_MEAT_CLEAVER) != max) return false;
    if (!lootWearWeapon(ITEM_MEAT_CLEAVER, max)) return false;
    if (lootGetItemCount(ITEM_MEAT_CLEAVER) != 0) return false;
    if (lootGetBestWeapon() != ITEM_NONE) return false;

    // Leave a clean inventory so the self-test doesn't stock the player.
    lootInit();
    return true;
}