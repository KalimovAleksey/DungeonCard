#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Combat.h"
#include "Dungeon.h"
#include "Meta.h"
#include "Party.h"
#include "Profile.h"
#include "Rng.h"

namespace dc {

// Exploring: игрок ходит по комнатам (WASD) и управляет инвентарём.
// Fighting: автобой. RestChoice: привал, ждём решения. BossIntro: показана карточка босса.
// Shop: отряд у торговца (покупка за монеты).
enum class RunPhase { Exploring, Fighting, RestChoice, BossIntro, Shop, ForgeChoice, BossReward, Victory, Defeat };

// Что приносит в забег постоянный профиль: улучшения «Наследия» и доступные артефакты.
// Улучшения героев (ЗДР/АТК/ЗАЩ) применяются к героям ДО создания Run (Meta::applyToHero).
struct RunOptions {
    Meta meta;
    int artifacts = 0;
};

class Run {
public:
    static constexpr int kRestHealPct = kBaseRestHealPct;  // лечение на привале (без улучшений)
    static constexpr int kBuffAtk = 3;       // бафф на привале
    static constexpr int kBuffDef = 1;
    static constexpr int kBuffTargets = 2;
    static constexpr int kReviveCost = kBaseReviveCost;    // полная цена воскрешения за монеты (без улучшений)
    static constexpr int kReviveArtifactCost = 2;          // полная цена воскрешения за артефакты
    static constexpr int kReviveArtifactPct = 75;          // здоровье после воскрешения за артефакт
    static constexpr int kRevivePct = 50;                   // здоровье после воскрешения за монеты
    static constexpr int kFreeRevivePct = 30;               // здоровье после удачной бесплатной попытки
    static constexpr int kTrapNoticePct = 25;               // шанс заметить ловушку без вора
    static constexpr int kTrapCoinPerDifficulty = 10;       // монеты за ловушку за единицу сложности
    static constexpr int kShopRestockCost = 50;             // обновление ассортимента у торговца

    Run(const Dungeon& dungeon, uint32_t seed, std::vector<Hero> heroes = {}, RunOptions options = {});
    Run(const Dungeon& dungeon, uint32_t seed, Party party, int floor, RunOptions options = {});
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;

    // --- движение ---
    bool move(Dir d);          // шаг в соседнюю комнату через дверь; true, если отряд переместился
    // --- бой ---
    void step();               // одно действие автобоя
    void beginBossFight();     // из BossIntro: начать бой с боссом
    void closeCombat();        // убрать показ закончившегося боя
    // --- награда за босса ---
    const std::vector<Item>& bossRewardOptions() const { return bossReward_; }
    bool bossRewardClaimed() const { return bossRewardClaimed_; }
    bool bossRewardRecorded() const { return bossRewardedRecorded_; }
    void markBossRewardRecorded() { bossRewardedRecorded_ = true; }
    bool claimBossRewardItem(int index);      // выбрать 1 из 3 предметов 4 тира
    bool claimBossRewardArtifacts();          // выбрать +3 артефакта
    void grantArtifacts(int n) { artifacts_ += std::max(0, n); }
    void finishRunAfterBoss(bool victory);    // закончить забег после награды
    // --- привал ---
    void restHeal();
    void restBuff(const std::vector<int>& heroIndexes);
    bool canRestRevive() const;
    int reviveChancePct() const;                  // базовый шанс: 25% (50% при живом жреце)
    bool canRestReviveFree() const;               // доступна ли бесплатная попытка (один раз за привал)
    bool freeReviveUsed() const { return freeReviveUsed_; }
    bool restReviveFree(int heroIndex);           // бесплатная попытка по выбранному герою; привал продолжается
    int restHealPct() const;
    bool hasLivingArcher() const;
    bool hasLivingBard() const;
    bool hasLivingSeidmard() const;           // скидка 20% в кузнице, пока герой жив
    bool restRevive(int heroIndex);               // доплата монетами до 100%; привал продолжается
    bool restReviveArtifact(int heroIndex);       // доплата артефактами до 100%; здоровья больше
    int fullReviveCoinCost() const { return meta_.reviveCoinCost(); }
    int reviveCoinCost() const;                   // доплата монетами за недостающий шанс
    int reviveArtifactCost() const;               // доплата артефактами за недостающий шанс
    int artifacts() const { return artifacts_; }    // артефакты, оставшиеся у игрока
    const Meta& meta() const { return meta_; }
    int casualties() const;                          // сколько героев погибало за забег
    RunSummary summary() const;                      // итог для профиля (достижения)
    // --- торговец ---
    bool inShopRoom() const;                          // отряд стоит в комнате торговца
    bool openShop();    
    int effectivePrice(const ShopOffer& offer) const;                              // открыть торговлю (если стоим у торговца)
    bool buy(int offerIndex);                         // купить лот: монеты -> предмет в рюкзак
    bool sellItem(int bagIndex);                      // продать предмет из рюкзака за 50% стоимости
    bool restockShop();                               // обновить ассортимент за 50 монет
    int shopRestockCost() const { return kShopRestockCost; }
    void leaveShop();
    bool forgeUpgrade(int bagIndex);
    bool inForgeRoom() const;   // отряд стоит в комнате кузницы
    bool openForge();  
    int forgePrice(const Item& it) const;
    void leaveForge();
    const std::vector<ShopOffer>* shopStock() const;  // лоты текущего торговца или nullptr
    void grantCoins(int n) { party_.coins += n; }     // для тестов и отладки
    Party& partyMutable() { return party_; }
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
    int floor() const { return floor_; }

private:
    void say(const std::string& s);
    void enterRoom(Vec2i p);
    void startFight(std::vector<Enemy> enemies, int lootTier, int bossId);
    void onFightWon();
    void resolveTrap(int difficulty);
    void giveLoot(int tier);
    void updateLosses();
    int floor_ = 1;

    Dungeon dungeon_;
    Party party_;
    Rng rng_;
    Vec2i pos_;
    Meta meta_;
    int artifacts_ = 0;
    std::vector<Role> squad_;
    std::vector<bool> died_;
    RunPhase phase_ = RunPhase::Exploring;
    std::unique_ptr<Combat> combat_;
    int pendingTier_ = 0;
    int pendingBoss_ = -1;
    int fightBoss_ = -1;
    int fightId_ = 0;
    bool newLoot_ = false;
    std::vector<std::string> log_;
    std::vector<Item> loot_;
    std::vector<Item> bossReward_;
    bool bossRewardClaimed_ = false;
    bool bossRewardedRecorded_ = false;
    bool freeReviveUsed_ = false;  // на привале разрешена только одна бесплатная попытка
    int cleared_ = 0;
    std::map<Vec2i, std::vector<ShopOffer>> stocks_;  // лоты торговцев (по комнатам)
};

}  // namespace dc
