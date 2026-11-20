#include "combat.h"
#include "rng.h"
#include <string.h>
#include <stdio.h>
#include "survival.h"


static const char* enemyNames[] = {
    "Ghoul", "Guard", "Cave Dweller", "Crow Mauler", "Night Lurcher"
};

static const char* partNames[] = {
    "Head", "L.Arm", "R.Arm", "L.Leg", "R.Leg", "Torso"
};

static void makeEnemy(Enemy* e) {
    int type = rngNext(5);
    e->name = enemyNames[type];
    
    switch (type) {
        case 0: // Ghoul - weak but bleeds you
            e->hp = e->maxHp = 40; e->attack = 8; e->speed = 5; e->mindDamage = 0; break;
        case 1: // Guard - tanky
            e->hp = e->maxHp = 80; e->attack = 12; e->speed = 3; e->mindDamage = 0; break;
        case 2: // Cave Dweller - fast
            e->hp = e->maxHp = 50; e->attack = 10; e->speed = 8; e->mindDamage = 5; break;
        case 3: // Crow Mauler - scary
            e->hp = e->maxHp = 100; e->attack = 15; e->speed = 6; e->mindDamage = 15; break;
        case 4: // Night Lurcher - nightmare fuel
            e->hp = e->maxHp = 60; e->attack = 14; e->speed = 7; e->mindDamage = 20; break;
    }
    
    for (int i = 0; i < 6; i++) e->parts[i] = true;
    e->bleeding = false;
}

void combatStart(Combat* c) {
    c->playerHp = 50;      // F&H: you're fragile
    c->playerMaxHp = 50;
    makeEnemy(&c->enemy);
    c->combatOver = false;
    c->playerWon = false;
    c->turnCount = 0;
    c->weaponBroke = false;
    snprintf(c->lastAction, sizeof(c->lastAction), "A %s appears!", c->enemy.name);
}

const char* combatPartName(BodyPart p) {
    return partNames[p];
}

const char* combatAttack(Combat* c, BodyPart part, ItemType weapon) {
    static char buf[64];
    c->weaponBroke = false;
    if (c->combatOver) return "combat is over";
    if (!c->enemy.parts[part]) return "that part is gone!";
    
    int baseDmg = 5;
    
    // Weapon bonuses (durability is tracked in loot.c)
    if (weapon == ITEM_MEAT_CLEAVER) baseDmg = 15;
    else if (weapon == ITEM_BONE_SAW) baseDmg = 8;
    
    // Head = 1.5x damage, harder to hit (simplified)
    if (part == PART_HEAD) baseDmg = (baseDmg * 3) / 2;
    
    // Random 80-120% damage
    int dmg = baseDmg + rngNext(2 * (baseDmg/5) + 1) - baseDmg/5;
    if (dmg < 1) dmg = 1;
    
    c->enemy.hp -= dmg;
    c->turnCount++;

    // Every landed blow wears the weapon; skull hits wear it double.
    if (weapon != ITEM_NONE)
        c->weaponBroke = lootWearWeapon(weapon, part == PART_HEAD ? 2 : 1);
    
    // Check for part destruction (simplified: 25% chance on non-torso hits)
    bool partGone = false;
    if (part != PART_TORSO && c->enemy.hp > 0 && rngChance(25)) {
        c->enemy.parts[part] = false;
        partGone = true;
        if (part == PART_LEG_L || part == PART_LEG_R) c->enemy.speed -= 3;
        if (part == PART_ARM_L || part == PART_ARM_R) c->enemy.attack = (c->enemy.attack * 3) / 4;
    }
    
    if (c->enemy.hp <= 0) {
        c->combatOver = true;
        c->playerWon = true;
        snprintf(buf, sizeof(buf), "Killed the %s!", c->enemy.name);
        lootAddItem(ITEM_DRIED_MEAT); // Victory loot!
        return buf;
    }
    
    if (partGone) {
        snprintf(buf, sizeof(buf), "Hit %s! %s destroyed!", 
            combatPartName(part), combatPartName(part));
    } else {
        snprintf(buf, sizeof(buf), "Hit %s for %d dmg", 
            combatPartName(part), dmg);
    }
    return buf;
}

const char* combatEnemyTurn(Combat* c, bool playerHasLuckyCoin) {
    static char buf[64];
    if (c->combatOver) return "combat is over";
    
    // Bleeding tick
    if (c->enemy.bleeding) {
        c->enemy.hp -= 5;
        if (c->enemy.hp <= 0) {
            c->combatOver = true;
            c->playerWon = true;
            return "Enemy bled out!";
        }
    }
    
    // Coin flip dodge
    bool lucky = playerHasLuckyCoin;
    bool dodge = coinFlip(lucky); // heads = you get hit, tails = dodge
    
    if (dodge) {
        snprintf(buf, sizeof(buf), "%s attacks... you dodge!", c->enemy.name);
        return buf;
    }
    
    // You got hit
    int dmg = c->enemy.attack + rngNext(5) - 2;
    if (dmg < 1) dmg = 1;
    c->playerHp -= dmg;
    
    // Mind damage from scary enemies
    int mindLoss = c->enemy.mindDamage + rngNext(6);
    survivalDrain(0, mindLoss);
    
    // 20% chance to start bleeding
    bool newBleed = !c->enemy.bleeding && rngChance(20);
    if (newBleed) c->enemy.bleeding = true;
    
    if (c->playerHp <= 0) {
        c->combatOver = true;
        c->playerWon = false;
        return "You died...";
    }
    
    snprintf(buf, sizeof(buf), "Hit for %d! Mind -%d%s", 
        dmg, mindLoss, newBleed ? " BLEEDING!" : "");
    return buf;
}

bool combatIsOver(const Combat* c) {
    return c->combatOver;
}