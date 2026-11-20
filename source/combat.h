#pragma once
#include <stdbool.h>
#include "loot.h"

typedef enum {
    PART_HEAD,
    PART_ARM_L,
    PART_ARM_R,
    PART_LEG_L,
    PART_LEG_R,
    PART_TORSO
} BodyPart;

typedef struct {
    const char* name;
    int hp;
    int maxHp;
    int attack;
    int speed;
    bool parts[6];        // head, armL, armR, legL, legR, torso
    bool bleeding;
    int mindDamage;       // how much mind this enemy drains per hit
} Enemy;

typedef struct {
    int playerHp;
    int playerMaxHp;
    Enemy enemy;
    bool combatOver;
    bool playerWon;
    int turnCount;
    char lastAction[64];
    bool weaponBroke;     // set by combatAttack when the weapon breaks
} Combat;

// Start combat with a random enemy.
void combatStart(Combat* c);

// Player attacks a body part. Returns message.
// weapon = ITEM_NONE means bare fists. Each hit wears the weapon
// (head hits wear it twice as fast); check c->weaponBroke afterwards.
const char* combatAttack(Combat* c, BodyPart part, ItemType weapon);

// Enemy takes its turn (with coin flip dodge). Returns message.
const char* combatEnemyTurn(Combat* c, bool playerHasLuckyCoin);

// Check if combat ended.
bool combatIsOver(const Combat* c);

// Get a body part name for display.
const char* combatPartName(BodyPart p);