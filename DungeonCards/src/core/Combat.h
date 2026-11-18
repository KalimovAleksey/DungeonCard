#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "Boss.h"
#include "Party.h"
#include "Rng.h"

namespace dc {

struct Enemy {
    std::string name;
    int hp = 1, maxHp = 1, atk = 1, def = 0, spd = 1;
    int bossId = -1;     // >= 0: босс или его порождение
    bool boss = false;   // главный босс
    bool minion = false; // призванный скелет
    int gen = 0;         // поколение слизня (0 - исходный)
    int slot = 0;        // место на арене (для отрисовки)
    int size = 72;       // размер спрайта
    int cd = 0;          // служебный счётчик способности
    int coins = 0;       // монеты, которые выпадают при смерти
    std::string art;     // ключ картинки (assets/<art>)
    bool alive() const { return hp > 0; }
};

std::vector<Enemy> makeEnemies(int difficulty, Rng& rng, int coinBonusPct = 0);  // группа врагов по сложности 1..3
Enemy makeBoss(int bossId);

// Описание попадания/лечения: по нему интерфейс рисует числа и вспышки.
struct CombatHit {
    bool targetHero = false;
    int idx = 0;
    int amount = 0;
    bool heal = false;
    bool crit = false;
    bool killed = false;
    int coins = 0;       // монеты, выпавшие с убитого врага
};

// Одно действие в бою: кто, какого рода и по кому.
struct CombatAction {
    enum Kind { Melee, Ranged, Heal, Potion, Effect, Shout };
    Kind kind = Melee;
    bool actorHero = true;
    int actor = -1;  // -1: нет актёра (зелье, регенерация)
    std::vector<CombatHit> hits;
    std::string text;  // для Shout: надпись над актёром
};

// Автобой: один вызов step() = одно действие одного участника.
class Combat {
public:
    Combat(Party& party, std::vector<Enemy> enemies, Rng& rng, int bossId = -1);

    void step(std::vector<std::string>& log);
    bool finished() const;
    bool heroesWon() const;
    const std::vector<Enemy>& enemies() const { return enemies_; }
    int round() const { return round_; }
    int slotCount() const { return slotCount_; }
    void setDamageReduction(int pct) { dmgReducePct_ = std::max(0, std::min(60, pct)); }  // «Закалённые доспехи»
    int bossId() const { return bossId_; }
    // События последнего вызова step() (зелья, регенерация, само действие, реакции).
    const std::vector<CombatAction>& events() const { return events_; }
    // Короткая подсказка о текущем состоянии боя с боссом (пустая для обычных боёв).
    std::string status() const;

private:
    struct Actor { bool hero; int idx; int spd; };

    void startRound(std::vector<std::string>& log);
    void heroAct(int i, std::vector<std::string>& log);
    void heroActInner(int i, std::vector<std::string>& log);
    void enemyAct(int i, std::vector<std::string>& log);

    int healHero(int idx, int amount);
    void flushStolen(std::vector<std::string>& log);
    int mostWoundedHero() const;
    int pickWeightedHero();
    int pickWeakestHero() const;
    int ableCount() const;
    bool anyMinionAlive() const;
    int findBoss(int bossId) const;
    int freeSlot() const;

    int heroDamage(const Hero& h, const Enemy& e, int atkValue, bool magic, bool ranged) const;
    int hurtHero(int ei, int target, int dmg, CombatAction& act, std::vector<std::string>& log);
    void aoeHeroes(int ei, int dmg, bool magic, CombatAction& act, std::vector<std::string>& log);
    void shout(int ei, const std::string& text);
    void afterHeroAction(const std::vector<int>& killed, std::vector<std::string>& log);
    void onEnemyDeath(int ei, std::vector<std::string>& log);
    void releaseSwallowed(std::vector<std::string>& log);
    void summonSkeletons(int ei, std::vector<std::string>& log);

    Party& party_;
    std::vector<Enemy> enemies_;
    Rng& rng_;
    std::vector<Actor> queue_;
    std::vector<CombatAction> events_;
    size_t qi_ = 0;
    int round_ = 0;
    int bossId_ = -1;
    int slotCount_ = 1;
    int dmgReducePct_ = 0;

    // механики боссов
    int swallowed_ = -1;       // индекс проглоченного героя
    int swallowLeft_ = 0;      // сколько урона по Пожирателю ещё нужно
    bool releaseNow_ = false;
    int stolenAccum_ = 0;      // украденное друидом лечение (за текущее действие)
    int stolenTarget_ = -1;
};

}  // namespace dc
