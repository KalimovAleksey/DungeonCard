#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Combat.h"
#include "Dungeon.h"
#include "Party.h"
#include "Rng.h"

namespace dc {

// Exploring: игрок ходит по комнатам (WASD) и управляет инвентарём.
// Fighting: автобой. RestChoice: привал, ждём решения. BossIntro: показана карточка босса.
// Shop: отряд у торговца (покупка за монеты).
enum class RunPhase { Exploring, Fighting, RestChoice, BossIntro, Shop, Victory, Defeat };

class Run {
public:
    static constexpr int kRestHealPct = 40;  // лечение на привале
    static constexpr int kBuffAtk = 3;       // бафф на привале
    static constexpr int kBuffDef = 1;
    static constexpr int kBuffTargets = 2;

    static constexpr int kReviveChancePct = 25;  // шанс успеха
    static constexpr int kReviveChanceClericPct = 50; // шанс успеха с жрицей
    static constexpr int kReviveHpPct = 30;      // доля макс. HP при успехе

    bool canRestRevive() const;  // есть ли павший герой
    void restRevive(); 
    int reviveChancePct() const; 

    Run(const Dungeon& dungeon, uint32_t seed, std::vector<Hero> heroes = {});
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    // --- движение ---
    bool move(Dir d);          // шаг в соседнюю комнату через дверь; true, если отряд переместился
    // --- бой ---
    void step();               // одно действие автобоя
    void beginBossFight();     // из BossIntro: начать бой с боссом
    void closeCombat();        // убрать показ закончившегося боя
    // --- привал ---
    void restHeal();
    void restBuff(const std::vector<int>& heroIndexes);
    // --- торговец ---
    bool inShopRoom() const;                          // отряд стоит в комнате торговца
    bool openShop();                                  // открыть торговлю (если стоим у торговца)
    bool buy(int offerIndex);                         // купить лот: монеты -> предмет в рюкзак
    void leaveShop();
    const std::vector<ShopOffer>* shopStock() const;  // лоты текущего торговца или nullptr
    void grantCoins(int n) { party_.coins += n; }     // для тестов и отладки
    // --- инвентарь (доступен вне боя, у торговца и на карточке босса) ---
    bool canManage() const;
    bool equipItem(int bagIndex, int heroIndex);
    bool unequipItem(int heroIndex, ItemKind slot);
    bool usePotion(int bagIndex, int heroIndex);
    bool hasNewLoot() const { return newLoot_; }
    void ackLoot() { newLoot_ = false; }

    RunPhase phase() const { return phase_; }
    bool finished() const { return phase_ == RunPhase::Victory || phase_ == RunPhase::Defeat; }
    Vec2i partyPos() const { return pos_; }
    const Dungeon& dungeon() const { return dungeon_; }
    const Party& party() const { return party_; }
    // Бой остаётся доступным, пока его не закроют (чтобы показать последний удар).
    const Combat* combat() const { return combat_.get(); }
    int fightId() const { return fightId_; }
    int pendingBossId() const { return pendingBoss_; }
    const std::vector<std::string>& log() const { return log_; }
    const std::vector<Item>& loot() const { return loot_; }
    int roomsCleared() const { return cleared_; }

private:
    void say(const std::string& s);
    void enterRoom(Vec2i p);
    void startFight(std::vector<Enemy> enemies, int lootTier, int bossId);
    void onFightWon();
    void resolveTrap(int difficulty);
    void giveLoot(int tier);
    // 25% или 50%, если в отряде есть живая Жрица

    Dungeon dungeon_;
    Party party_;
    Rng rng_;
    Vec2i pos_;
    RunPhase phase_ = RunPhase::Exploring;
    std::unique_ptr<Combat> combat_;
    int pendingTier_ = 0;
    int pendingBoss_ = -1;
    int fightBoss_ = -1;
    int fightId_ = 0;
    bool newLoot_ = false;
    std::vector<std::string> log_;
    std::vector<Item> loot_;
    int cleared_ = 0;
    std::map<Vec2i, std::vector<ShopOffer>> stocks_;  // лоты торговцев (по комнатам)
};

}  // namespace dc
