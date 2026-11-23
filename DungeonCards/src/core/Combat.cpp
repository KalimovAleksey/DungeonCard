#include "Combat.h"
#include <algorithm>
#include "Assets.h"
#include "Assets.h"
#include "Util.h"

namespace dc {

namespace {
constexpr int kRuneStep = 2;    // сколько ЗАЩ снимает одна руна
constexpr int kRuneMax = 6;     // предел снятия ЗАЩ с одной цели за бой
int minDmg(int atk) { return std::max(1, atk / 4); }
// Урон с учётом защиты; даже против брони проходит не меньше 25% АТК.
int calcDmg(int atk, int def) { return std::max(minDmg(atk), atk - def); }
}  // namespace

// ---------------------------------------------------------------- враги

std::vector<Enemy> makeEnemies(int d, Rng& rng, int coinBonusPct) {
    d = std::max(1, std::min(3, d));
    static const char* names[3][3] = {
        {"Гоблин", "Крыса", "Скелет"},
        {"Орк", "Гуль", "Бандит"},
        {"Огр", "Рыцарь смерти", "Демон"},
    };
    struct T { int count, hp, atk, def, spd, coinMin, coinMax; };
    static const T t[3] = {{2, 16, 6, 1, 3, 4, 7}, {3, 26, 9, 1, 4, 7, 11}, {3, 40, 12, 2, 4, 11, 16}};
    const T& s = t[d - 1];
    static const char* keys[3][3] = {
        {"goblin", "rat", "skeleton"},
        {"orc", "ghoul", "bandit"},
        {"ogre", "death_knight", "demon"},
    };
    const int pick = rng.range(0, 2);
    std::string base = names[d - 1][pick];
    std::vector<Enemy> out;
    for (int i = 0; i < s.count; ++i) {
        Enemy e;
        e.name = base + " " + std::to_string(i + 1);
        e.maxHp = e.hp = s.hp + rng.range(-2, 2);
        e.atk = s.atk;
        e.def = s.def;
        e.spd = s.spd;
        e.slot = i;
        e.coins = rng.range(s.coinMin, s.coinMax) * (100 + std::max(0, coinBonusPct)) / 100;
        e.art = enemyArt(keys[d - 1][pick]);
        out.push_back(e);
    }
    return out;
}

Enemy makeBoss(int id) {
    const BossInfo& info = bossInfo(id);
    Enemy e;
    e.name = info.name;
    e.maxHp = e.hp = info.hp;
    e.atk = info.atk;
    e.def = info.def;
    e.spd = info.spd;
    e.bossId = id;
    e.boss = true;
    e.art = bossArt(id);
    e.size = 120;
    e.slot = (id == BossSlime || id == BossNecro) ? 1 : 0;
    if (id == BossDevourer) e.cd = 2;  // первый глоток не раньше 2-го раунда
    return e;
}

// ---------------------------------------------------------------- боевка

EnemyScale enemyScaleForFloor(int floor) {
    const int step = std::max(0, std::max(1, floor) - 1);
    return {100 + 30 * step, 100 + 30 * step, step, 100 + 20 * step};
}

void scaleEnemy(Enemy& e, const EnemyScale& scale) {
    const int newMax = std::max(1, e.maxHp * scale.hpPct / 100);
    const int delta = newMax - e.maxHp;
    e.maxHp = newMax;
    // Текущее HP сдвигается на ту же дельту: свежие враги остаются полными,
    // а уже раненые (например, в тестах) не «залечиваются» масштабированием.
    if (delta != 0) e.hp = std::max(1, e.hp + delta);
    e.atk = std::max(1, e.atk * scale.atkPct / 100);
    e.def += scale.defBonus;
    if (e.coins > 0) e.coins = std::max(1, e.coins * scale.coinPct / 100);
}

Combat::Combat(Party& party, std::vector<Enemy> enemies, Rng& rng, int bossId, int floor)
    : party_(party), enemies_(std::move(enemies)), rng_(rng), bossId_(bossId), scale_(enemyScaleForFloor(floor)),
      arcaneCharge_(party.heroes.size(), 0) {
    for (auto& e : enemies_) scaleEnemy(e, scale_);
    enemies_.reserve(16);  // ссылки на врагов не должны «ломаться» при призыве/делении
    slotCount_ = std::max<int>(1, static_cast<int>(enemies_.size()));
    if (bossId == BossSlime || bossId == BossNecro) slotCount_ = 4;
}

bool Combat::finished() const {
    bool enemiesAlive = std::any_of(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.alive(); });
    return !enemiesAlive || ableCount() == 0;
}
bool Combat::heroesWon() const {
    bool enemiesAlive = std::any_of(enemies_.begin(), enemies_.end(), [](const Enemy& e) { return e.alive(); });
    return !enemiesAlive && party_.anyAlive();
}

int Combat::ableCount() const {
    int n = 0;
    for (auto& h : party_.heroes) n += h.inFight() ? 1 : 0;
    return n;
}
bool Combat::anyMinionAlive() const {
    for (auto& e : enemies_)
        if (e.minion && e.alive()) return true;
    return false;
}
int Combat::findBoss(int id) const {
    for (int i = 0; i < static_cast<int>(enemies_.size()); ++i)
        if (enemies_[i].boss && enemies_[i].bossId == id && enemies_[i].alive()) return i;
    return -1;
}
int Combat::freeSlot() const {
    for (int s = 0; s < slotCount_; ++s) {
        bool used = false;
        for (auto& e : enemies_)
            if (e.alive() && e.slot == s) used = true;
        if (!used) return s;
    }
    return -1;
}

// Лечение героя. Если жив Друид-Похититель, половина лечения уходит ему.
int Combat::healHero(int idx, int amount) {
    Hero& h = party_.heroes[idx];
    int give = amount, stolen = 0;
    int di = findBoss(BossDruid);
    if (di >= 0) {
        stolen = amount / 2;
        give = amount - stolen;
    }
    int gained = std::max(0, std::min(h.maxHp - h.hp, give));
    h.hp += gained;
    if (stolen > 0) {
        Enemy& d = enemies_[di];
        int g = std::max(0, std::min(d.maxHp - d.hp, stolen));
        d.hp += g;
        stolenAccum_ += g;
        stolenTarget_ = di;
    }
    return gained;
}

void Combat::flushStolen(std::vector<std::string>& log) {
    if (stolenAccum_ > 0 && stolenTarget_ >= 0) {
        CombatAction a;
        a.kind = CombatAction::Effect;
        a.actor = -1;
        a.hits.push_back({false, stolenTarget_, stolenAccum_, true, false, false});
        events_.push_back(a);
        log.push_back(fmt("%s крадёт лечение: +%d", enemies_[stolenTarget_].name.c_str(), stolenAccum_));
    }
    stolenAccum_ = 0;
    stolenTarget_ = -1;
}

// Самый раненый (по доле здоровья) боеспособный герой или -1.
int Combat::mostWoundedHero() const {
    int best = -1;
    for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j) {
        const Hero& o = party_.heroes[j];
        if (!o.inFight()) continue;
        if (best < 0 || o.hp * party_.heroes[best].maxHp < party_.heroes[best].hp * o.maxHp) best = j;
    }
    return best;
}

int Combat::pickWeakestHero() const {
    int best = -1;
    for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j) {
        const Hero& o = party_.heroes[j];
        if (!o.inFight()) continue;
        if (best < 0 || o.hp < party_.heroes[best].hp) best = j;
    }
    return best;
}

// Случайная цель; танки привлекают втрое больше ударов.
int Combat::pickWeightedHero() {
    int total = 0;
    for (auto& h : party_.heroes)
        if (h.inFight()) total += isTank(h.role) ? 3 : 1;
    if (total == 0) return -1;
    int roll = rng_.range(1, total);
    for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j) {
        const Hero& h = party_.heroes[j];
        if (!h.inFight()) continue;
        roll -= isTank(h.role) ? 3 : 1;
        if (roll <= 0) return j;
    }
    return -1;
}

void Combat::shout(int ei, const std::string& text) {
    CombatAction a;
    a.kind = CombatAction::Shout;
    a.actorHero = false;
    a.actor = ei;
    a.text = text;
    events_.push_back(a);
}

void Combat::startRound(std::vector<std::string>& log) {
    ++round_;
    queue_.clear();
    qi_ = 0;

    // перезарядка Пожирателя
    int dev = findBoss(BossDevourer);
    if (dev >= 0 && enemies_[dev].cd > 0) --enemies_[dev].cd;

    // пищеварение: проглоченный герой теряет здоровье
    if (swallowed_ >= 0) {
        Hero& h = party_.heroes[swallowed_];
        h.hp -= 3;
        CombatAction a;
        a.kind = CombatAction::Effect;
        a.hits.push_back({true, swallowed_, 3, false, false, !h.alive()});
        events_.push_back(a);
        log.push_back(fmt("%s переваривается: -3", h.name.c_str()));
        if (!h.alive()) {
            log.push_back(fmt("%s погиб в пасти", h.name.c_str()));
            h.away = false;
            swallowed_ = -1;
            if (dev >= 0) enemies_[dev].cd = 3;
        }
    }

    // регенерация тролля
    for (int i = 0; i < static_cast<int>(enemies_.size()); ++i) {
        Enemy& e = enemies_[i];
        if (!e.alive() || e.bossId != BossTroll) continue;
        int gained = std::min(e.maxHp - e.hp, std::max(1, 7 * scale_.hpPct / 100));
        if (gained > 0) {
            e.hp += gained;
            CombatAction a;
            a.kind = CombatAction::Effect;
            a.hits.push_back({false, i, gained, true, false, false});
            events_.push_back(a);
            log.push_back(fmt("%s регенерирует: +%d", e.name.c_str(), gained));
        }
    }

    for (int i = 0; i < static_cast<int>(party_.heroes.size()); ++i)
        if (party_.heroes[i].inFight()) queue_.push_back({true, i, party_.heroes[i].spd});
    for (int i = 0; i < static_cast<int>(enemies_.size()); ++i)
        if (enemies_[i].alive()) queue_.push_back({false, i, enemies_[i].spd});
    std::stable_sort(queue_.begin(), queue_.end(), [](const Actor& a, const Actor& b) { return a.spd > b.spd; });
    log.push_back(fmt("-- Раунд %d --", round_));

    // Авто-использование зелий из рюкзака на тяжело раненных героях.
    for (int i = 0; i < static_cast<int>(party_.heroes.size()); ++i) {
        Hero& h = party_.heroes[i];
        if (!h.inFight() || party_.potionCount() == 0) continue;
        if (h.hp * 100 < h.maxHp * 35) {
            int power = 0;
            if (!party_.takeBestPotion(power)) break;
            int gained = healHero(i, power);
            log.push_back(fmt("%s пьёт зелье (+%d)", h.name.c_str(), gained));
            CombatAction a;
            a.kind = CombatAction::Potion;
            a.actor = -1;
            a.hits.push_back({true, i, gained, true, false, false});
            events_.push_back(a);
        }
    }
    flushStolen(log);
}

void Combat::step(std::vector<std::string>& log) {
    events_.clear();
    if (finished()) return;
    for (;;) {
        if (qi_ >= queue_.size()) startRound(log);
        if (finished()) break;  // пищеварение могло убить последнего бойца
        if (qi_ >= queue_.size()) continue;
        Actor a = queue_[qi_++];
        if (a.hero) {
            if (!party_.heroes[a.idx].inFight()) continue;
            heroAct(a.idx, log);
        } else {
            if (!enemies_[a.idx].alive()) continue;
            enemyAct(a.idx, log);
        }
        break;
    }
    if (finished()) {
        for (auto& h : party_.heroes) h.away = false;  // пленники освобождаются
        swallowed_ = -1;
    }
}

// ----------------------------------------------------------------- герои

int Combat::heroDamage(const Hero& h, const Enemy& e, int atkValue, bool magic, bool ranged) const {
    // Руны Сейдмарда снимают часть ЗАЩ для ВСЕХ героев, обычные и магические атаки.
    const int def = std::max(0, e.def - e.defShred);
    int eff = def;
    if (magic) eff = (e.bossId == BossKnight) ? 0 : def / 2;  // магия: полпрозрачная защита, у рыцаря - вся
    int dmg = calcDmg(atkValue, eff);
    if (ranged && e.bossId == BossDragon) dmg = dmg * 13 / 10;  // дракон уязвим к дальнему бою
    if (e.bossId == BossNecro && e.boss && anyMinionAlive()) dmg = std::max(1, dmg / 4);  // барьер костей
    (void)h;
    return dmg;
}

void Combat::heroAct(int i, std::vector<std::string>& log) {
    heroActInner(i, log);
    flushStolen(log);
}

void Combat::heroActInner(int i, std::vector<std::string>& log) {
    Hero& h = party_.heroes[i];

    // Аура Берсерка действует на весь отряд, если он выбран в команде.
    const bool berserkerInParty = std::any_of(party_.heroes.begin(), party_.heroes.end(),
                                              [](const Hero& hero) { return hero.role == Role::Berserker; });
    const int auraVamp = berserkerInParty ? kBerserkerAuraVampPct : 0;

    // Каждый герой сначала бьёт, а уже потом применяет своё умение.
    std::vector<int> order;
    for (int e = 0; e < static_cast<int>(enemies_.size()); ++e)
        if (enemies_[e].alive()) order.push_back(e);
    if (order.empty()) return;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return enemies_[a].hp < enemies_[b].hp; });

    int totalDmg = 0;
    const int critChance = std::min(75, (h.role == Role::Rogue ? 25 : 0) + h.critPct);

    auto hitInto = [&](CombatAction& out, std::vector<int>& killed, int ei, int atkValue, bool magic, bool ranged) {
        Enemy& e = enemies_[ei];
        int dmg = heroDamage(h, e, atkValue, magic, ranged);
        bool crit = critChance > 0 && rng_.range(1, 100) <= critChance;
        if (crit) dmg *= 2;
        e.hp -= dmg;
        totalDmg += dmg;
        const bool dead = !e.alive();
        out.hits.push_back({false, ei, dmg, false, crit, dead, dead ? e.coins : 0});
        log.push_back(fmt("%s > %s: -%d%s%s", h.name.c_str(), e.name.c_str(), dmg, crit ? " (крит!)" : "",
                          dead ? " - повержен" : ""));
        if (e.bossId == BossDevourer && e.boss && swallowed_ >= 0) {
            swallowLeft_ -= dmg;
            if (swallowLeft_ <= 0) releaseNow_ = true;
        }
        if (dead) killed.push_back(ei);
    };
    auto aliveTargets = [&]() {
        std::vector<int> out;
        for (int e = 0; e < static_cast<int>(enemies_.size()); ++e)
            if (enemies_[e].alive()) out.push_back(e);
        std::sort(out.begin(), out.end(), [&](int a, int b) { return enemies_[a].hp < enemies_[b].hp; });
        return out;
    };

    // --- атака ---
    CombatAction act;
    act.actorHero = true;
    act.actor = i;
    std::vector<int> killed;
    switch (h.role) {
        case Role::Mage:
            act.kind = CombatAction::Ranged;
            hitInto(act, killed, order[0], h.atk, true, true);
            break;
        case Role::Archer:
            act.kind = CombatAction::Ranged;
            for (size_t k = 0; k < order.size() && k < 2; ++k)
                hitInto(act, killed, order[k], order.size() == 1 ? h.atk : h.atk * 8 / 10, false, true);
            break;
        default:
            act.kind = CombatAction::Melee;
            hitInto(act, killed, order[0], h.atk, false, false);
            break;
    }
    events_.push_back(act);
    afterHeroAction(killed, log);

    // --- способности после атаки ---
    std::vector<int> rageKilled;
    if (h.role == Role::Berserker && h.hp * 2 < h.maxHp) {
        // Ярость: дополнительный удар, когда здоровье ниже половины.
        auto rest = aliveTargets();
        if (!rest.empty()) {
            CombatAction ra;
            ra.kind = CombatAction::Melee;
            ra.actorHero = true;
            ra.actor = i;
            hitInto(ra, rageKilled, rest[0], h.atk / 2, false, false);
            events_.push_back(ra);
            log.push_back(fmt("%s в ярости: дополнительный удар!", h.name.c_str()));
        }
    }
    if (!rageKilled.empty()) afterHeroAction(rageKilled, log);

    // Вампиризм: реликвии героя плюс аура Берсерка для всей группы.
    const int stealPct = h.lifestealPct + auraVamp;
    if (stealPct > 0 && totalDmg > 0 && h.inFight()) {
        int want = std::max(1, totalDmg * stealPct / 100);
        int gained = healHero(i, want);
        if (gained > 0) {
            CombatAction ha;
            ha.kind = CombatAction::Effect;
            ha.actorHero = true;
            ha.actor = i;
            ha.hits.push_back({true, i, gained, true, false, false});
            events_.push_back(ha);
            log.push_back(fmt("%s: вампиризм +%d", h.name.c_str(), gained));
        }
    }

    // Маг: после каждого 3-го удара накопленная магия выливается в арканный взрыв
    // по всем живым врагам. Счётчик живёт внутри боя и не переносится между схватками.
    if (h.role == Role::Mage && i < static_cast<int>(arcaneCharge_.size())) {
        ++arcaneCharge_[i];
        if (arcaneCharge_[i] >= 3) {
            arcaneCharge_[i] = 0;
            auto targets = aliveTargets();
            if (!targets.empty()) {
                std::vector<int> burstKilled;
                CombatAction ea;
                ea.kind = CombatAction::Ranged;
                ea.actorHero = true;
                ea.actor = i;
                for (int ei : targets)
                    hitInto(ea, burstKilled, ei, h.atk * 6 / 10, true, true);
                events_.push_back(ea);
                CombatAction sa;
                sa.kind = CombatAction::Shout;
                sa.actorHero = true;
                sa.actor = i;
                sa.text = "Арканный взрыв!";
                events_.push_back(sa);
                log.push_back(fmt("%s: арканный взрыв по %d целям!", h.name.c_str(), static_cast<int>(targets.size())));
                afterHeroAction(burstKilled, log);
            }
        }
    }

    // Сейдмард: снимает до 6 ЗАЩ с самой защищённой цели (по 2 за применение).
    // Когда снимать больше нечего, наносит дополнительный магический удар.
    if (h.role == Role::Seidmard) {
        int best = -1, bestDef = 0;
        for (int ei = 0; ei < static_cast<int>(enemies_.size()); ++ei) {
            const Enemy& e = enemies_[ei];
            if (!e.alive() || e.defShred >= kRuneMax) continue;  // лимит рун исчерпан - пора бить
            const int d = std::max(0, e.def - e.defShred);
            if (d > bestDef) { bestDef = d; best = ei; }
        }
        if (best >= 0) {
            Enemy& target = enemies_[best];
            target.defShred = std::min(kRuneMax, target.defShred + kRuneStep);
            CombatAction sa;
            sa.kind = CombatAction::Shout;
            sa.actorHero = false;
            sa.actor = best;
            sa.text = "Руна: -2 ЗАЩ";
            events_.push_back(sa);
            log.push_back(fmt("%s: руна ослабляет броню, ЗАЩ теперь %d", target.name.c_str(),
                              std::max(0, target.def - target.defShred)));
        } else {
            int target = -1, mostArmored = -1;
            for (int ei = 0; ei < static_cast<int>(enemies_.size()); ++ei) {
                const Enemy& e = enemies_[ei];
                if (!e.alive()) continue;
                if (target < 0 || e.def > mostArmored) { target = ei; mostArmored = e.def; }
            }
            if (target >= 0) {
                std::vector<int> runeKilled;
                CombatAction ra;
                ra.kind = CombatAction::Ranged;
                ra.actorHero = true;
                ra.actor = i;
                hitInto(ra, runeKilled, target, h.atk / 2, true, true);
                events_.push_back(ra);
                log.push_back(fmt("%s: руна исчерпана - дополнительный магический удар!", h.name.c_str()));
                afterHeroAction(runeKilled, log);
            }
        }
    }

    if (h.role == Role::Cleric) {
        int a = mostWoundedHero();
        if (a >= 0 && party_.heroes[a].hp * 100 < party_.heroes[a].maxHp * 60) {
            int gained = healHero(a, h.atk + 3);
            CombatAction ha;
            ha.kind = CombatAction::Heal;
            ha.actorHero = true;
            ha.actor = i;
            ha.hits.push_back({true, a, gained, true, false, false});
            events_.push_back(ha);
            log.push_back(fmt("%s лечит %s (+%d)", h.name.c_str(), party_.heroes[a].name.c_str(), gained));
        }
    }
    if (h.role == Role::Druid) {
        bool anyHurt = false;
        for (auto& o : party_.heroes)
            if (o.inFight() && o.hp * 100 < o.maxHp * 85) anyHurt = true;
        if (anyHurt) {
            // Усиленное цепное лечение: 6/5/4/3... по всем живым союзникам,
            // к каждой порции добавляется половина защиты друида.
            std::vector<int> targets;
            for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j)
                if (party_.heroes[j].inFight()) targets.push_back(j);
            rng_.shuffle(targets);
            CombatAction ha;
            ha.kind = CombatAction::Heal;
            ha.actorHero = true;
            ha.actor = i;
            const int bonus = h.def / 2;
            for (size_t k = 0; k < targets.size(); ++k) {
                const int amount = std::max(1, 6 - static_cast<int>(k)) + bonus;
                const int gained = healHero(targets[k], amount);
                if (gained > 0) ha.hits.push_back({true, targets[k], gained, true, false, false});
            }
            events_.push_back(ha);
            log.push_back(fmt("%s: цепное лечение (%d целей)", h.name.c_str(), static_cast<int>(ha.hits.size())));
        }
    }
    if (h.role == Role::Paladin) {
        int a = mostWoundedHero();
        if (a >= 0 && party_.heroes[a].hp < party_.heroes[a].maxHp) {
            int gained = healHero(a, 2);
            if (gained > 0) {
                CombatAction ha;
                ha.kind = CombatAction::Heal;
                ha.actorHero = true;
                ha.actor = i;
                ha.hits.push_back({true, a, gained, true, false, false});
                events_.push_back(ha);
                log.push_back(fmt("%s лечит %s (+%d)", h.name.c_str(), party_.heroes[a].name.c_str(), gained));
            }
        }
    }
    if (h.role == Role::Bard) {
        int best = -1;
        for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j) {
            if (j == i || !party_.heroes[j].inFight()) continue;
            if (best < 0 || party_.heroes[j].atk > party_.heroes[best].atk) best = j;
        }
        if (best >= 0) {
            const int bonus = std::max(1, h.atk / 2);
            party_.heroes[best].buffAtkCombat += bonus;
            party_.heroes[best].recalc();
            CombatAction ha;
            ha.kind = CombatAction::Effect;
            ha.actorHero = true;
            ha.actor = i;
            ha.hits.push_back({true, best, bonus, true, false, false});
            events_.push_back(ha);
            log.push_back(fmt("%s вдохновляет %s: +%d АТК!", h.name.c_str(), party_.heroes[best].name.c_str(), bonus));
        }
    }
    if (h.role == Role::Warrior) {
        CombatAction ha;
        ha.kind = CombatAction::Shout;
        ha.actorHero = true;
        ha.actor = i;
        ha.text = "Провокация!";
        events_.push_back(ha);
        log.push_back(fmt("%s: Провокация!", h.name.c_str()));
    }
}

// Реакции на удар героя: освобождение пленника Пожирателя, деление слизней.
void Combat::afterHeroAction(const std::vector<int>& killed, std::vector<std::string>& log) {
    for (int ei : killed) onEnemyDeath(ei, log);
    if (releaseNow_) {
        releaseNow_ = false;
        releaseSwallowed(log);
    }
}

void Combat::releaseSwallowed(std::vector<std::string>& log) {
    if (swallowed_ < 0) return;
    Hero& h = party_.heroes[swallowed_];
    h.away = false;
    log.push_back(fmt("%s выплёвывает %s!", "Пожиратель", h.name.c_str()));
    int dev = -1;
    for (int i = 0; i < static_cast<int>(enemies_.size()); ++i)
        if (enemies_[i].bossId == BossDevourer && enemies_[i].boss) dev = i;
    if (dev >= 0) {
        enemies_[dev].cd = 3;
        shout(dev, "Выплёвывает!");
    }
    swallowed_ = -1;
    swallowLeft_ = 0;
}

void Combat::onEnemyDeath(int ei, std::vector<std::string>& log) {
    const Enemy parent = enemies_[ei];  // копия: вектор может измениться
    if (parent.bossId == BossSlime && parent.gen < 2) {
        const int g = parent.gen + 1;
        auto make = [&](int slot) {
            Enemy c;
            c.name = g == 1 ? "Слизень" : "Слизнёнок";
            c.bossId = BossSlime;
            c.gen = g;
            c.maxHp = c.hp = g == 1 ? 157 : 79;
            c.atk = g == 1 ? 12 : 9;
            c.def = g == 1 ? 1 : 0;
            c.spd = 4 + g;
            c.size = g == 1 ? 90 : 64;
            c.slot = slot;
            c.art = g == 1 ? "bosses/slime_medium.png" : "bosses/slime_small.png";
            scaleEnemy(c, scale_);
            return c;
        };
        enemies_.push_back(make(parent.slot));
        int s2 = freeSlot();
        enemies_.push_back(make(s2 >= 0 ? s2 : parent.slot));
        shout(ei, "Делится!");
        log.push_back(fmt("%s делится на два!", parent.name.c_str()));
    }
}

// ----------------------------------------------------------------- враги

int Combat::hurtHero(int ei, int target, int dmg, CombatAction& act, std::vector<std::string>& log) {
    Hero& h = party_.heroes[target];
    if (dmgReducePct_ > 0) {
        // снижение на pct% с вероятностным округлением: в среднем ровно pct%, без «лишнего» очка с каждого удара
        int cut = dmg * dmgReducePct_ / 100;
        if (rng_.range(0, 99) < dmg * dmgReducePct_ % 100) ++cut;
        dmg = std::max(1, dmg - cut);
    }
    h.hp -= dmg;
    act.hits.push_back({true, target, dmg, false, false, !h.alive()});
    log.push_back(fmt("%s > %s: -%d%s", enemies_[ei].name.c_str(), h.name.c_str(), dmg, h.alive() ? "" : " - пал"));
    return dmg;
}

void Combat::aoeHeroes(int ei, int dmg, bool magic, CombatAction& act, std::vector<std::string>& log) {
    for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j) {
        const Hero& h = party_.heroes[j];
        if (!h.inFight()) continue;
        hurtHero(ei, j, calcDmg(dmg, magic ? h.def / 2 : h.def), act, log);
    }
}

void Combat::summonSkeletons(int ei, std::vector<std::string>& log) {
    int made = 0;
    for (int k = 0; k < 2; ++k) {
        int slot = freeSlot();
        if (slot < 0) break;
        int revive = -1;
        for (int i = 0; i < static_cast<int>(enemies_.size()); ++i)
            if (enemies_[i].minion && !enemies_[i].alive()) revive = i;
        Enemy s;
        s.name = "Скелет";
        s.bossId = BossNecro;
        s.minion = true;
        s.maxHp = s.hp = 32;
        s.atk = 10;
        s.def = 1;
        s.spd = 4;
        s.size = 64;
        s.slot = slot;
        s.art = enemyArt("skeleton_minion");
        scaleEnemy(s, scale_);
        if (revive >= 0) enemies_[revive] = s;
        else enemies_.push_back(s);
        ++made;
    }
    if (made) {
        shout(ei, "Призыв!");
        log.push_back(fmt("Некромант призывает скелетов: %d", made));
    }
}

void Combat::enemyAct(int i, std::vector<std::string>& log) {
    CombatAction act;
    act.kind = CombatAction::Melee;
    act.actorHero = false;
    act.actor = i;

    const int id = enemies_[i].boss ? enemies_[i].bossId : -1;
    const int atk = enemies_[i].atk;

    // ------------------------------------------------------ особые ходы
    switch (id) {
        case BossDevourer: {
            if (swallowed_ < 0 && enemies_[i].cd <= 0 && ableCount() >= 2) {
                int best = -1;  // глотает самого сильного бойца
                for (int j = 0; j < static_cast<int>(party_.heroes.size()); ++j)
                    if (party_.heroes[j].inFight() && (best < 0 || party_.heroes[j].atk > party_.heroes[best].atk)) best = j;
                swallowed_ = best;
                swallowLeft_ = 55 * scale_.hpPct / 100;
                party_.heroes[best].away = true;
                shout(i, "Проглатывает!");
                log.push_back(fmt("%s проглатывает %s! Нужно нанести %d урона.", enemies_[i].name.c_str(),
                                  party_.heroes[best].name.c_str(), swallowLeft_));
                return;
            }
            break;
        }
        case BossArchmage: {
            if (round_ % 3 == 0) {
                log.push_back(enemies_[i].name + " вызывает огненный шторм!");
                shout(i, "Огненный шторм!");
                aoeHeroes(i, atk * 6 / 10, true, act, log);
                events_.push_back(act);
                return;
            }
            int t = pickWeakestHero();
            if (t < 0) return;
            hurtHero(i, t, calcDmg(atk, party_.heroes[t].def / 2), act, log);
            act.kind = CombatAction::Ranged;
            events_.push_back(act);
            return;
        }
        case BossTroll: {
            if (enemies_[i].cd == 0) {
                enemies_[i].cd = 1;
                shout(i, "Замах...");
                log.push_back(enemies_[i].name + " заносит кулак...");
                return;
            }
            enemies_[i].cd = 0;
            int t = pickWeightedHero();
            if (t < 0) return;
            shout(i, "СОКРУШАЮЩИЙ УДАР!");
            hurtHero(i, t, calcDmg(atk * 2, party_.heroes[t].def), act, log);
            events_.push_back(act);
            return;
        }
        case BossNecro: {
            int skeletons = 0;
            for (auto& e : enemies_) skeletons += (e.minion && e.alive()) ? 1 : 0;
            if (round_ % 3 == 1 && skeletons < 3) {
                summonSkeletons(i, log);
                return;
            }
            break;
        }
        case BossDragon: {
            bool rage = enemies_[i].hp * 2 < enemies_[i].maxHp;
            if (round_ % 3 == 0 || (rage && round_ % 2 == 0)) {
                log.push_back(enemies_[i].name + " дышит огнём!");
                shout(i, "Огненное дыхание!");
                aoeHeroes(i, atk * 7 / 10, true, act, log);
                events_.push_back(act);
                return;
            }
            break;
        }
        default:
            break;
    }

    // ------------------------------------------------------ обычная атака
    int t = pickWeightedHero();
    if (t < 0) return;
    int dmg = hurtHero(i, t, calcDmg(atk, party_.heroes[t].def), act, log);
    events_.push_back(act);

    // Друид лечит себя частью нанесённого урона.
    if (id == BossDruid) {
        Enemy& d = enemies_[i];
        int g = std::max(0, std::min(d.maxHp - d.hp, dmg * 40 / 100));
        if (g > 0) {
            d.hp += g;
            CombatAction ha;
            ha.kind = CombatAction::Effect;
            ha.hits.push_back({false, i, g, true, false, false});
            events_.push_back(ha);
            log.push_back(fmt("%s лечится: +%d", d.name.c_str(), g));
        }
    }
}

// ---------------------------------------------------------------- статус

std::string Combat::status() const {
    if (bossId_ < 0) return "";
    switch (bossId_) {
        case BossSlime: {
            int n = 0;
            for (auto& e : enemies_) n += e.alive() ? 1 : 0;
            return fmt("Слизней в бою: %d", n);
        }
        case BossDevourer: {
            if (swallowed_ >= 0)
                return fmt("%s в пасти! Нанесите ещё %d урона", party_.heroes[swallowed_].name.c_str(), std::max(0, swallowLeft_));
            int dev = findBoss(BossDevourer);
            if (dev >= 0 && enemies_[dev].cd > 0) return fmt("Пожиратель проголодается через %d р.", enemies_[dev].cd);
            return "Пожиратель готов проглотить героя!";
        }
        case BossKnight: {
            int k = findBoss(BossKnight);
            if (k < 0) return "";
            const int def = std::max(0, enemies_[k].def - enemies_[k].defShred);
            return fmt("Броня ЗАЩ %d (руны: -%d): магия Мага её игнорирует", def, enemies_[k].defShred);
        }
        case BossArchmage: return "Метит в самого раненого героя";
        case BossDruid:    return "Друид крадёт 50% лечения отряда";
        case BossTroll: {
            int t = findBoss(BossTroll);
            if (t >= 0 && enemies_[t].cd == 1) return "Тролль занёс кулак: следующий удар сокрушителен!";
            return fmt("Тролль готовится к замаху (+%d ЗДР в раунд)", std::max(1, 7 * scale_.hpPct / 100));
        }
        case BossNecro:
            return anyMinionAlive() ? "Барьер костей: урон по некроманту -75%" : "Барьер пал - бей некроманта!";
        case BossDragon: {
            int d = findBoss(BossDragon);
            if (d >= 0 && enemies_[d].hp * 2 < enemies_[d].maxHp) return "Дракон в ярости: дыхание каждые 2 раунда!";
            return "Дыхание огнём каждые 3 раунда";
        }
    }
    return "";
}

}  // namespace dc
