// Консольная симуляция для балансировки сложности.
// Сравнивает «наивную» игру (случайный отряд и случайная раздача предметов) с «умной»
// (отряд под босса и разумное распределение снаряжения), перебирая всех 8 боссов.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include "core/ConsoleUtf8.h"
#include "core/Run.h"

using namespace dc;

// Классы симулятора: 0 Воин, 1 Маг, 2 Жрица, 3 Вор, 4 Лучник, 5 Паладин, 6 Берсерк, 7 Друид,
// 8 Бард, 9 Сейдмард, дальше три отдельные стойки Монаха.
static constexpr int kBaseClasses = static_cast<int>(Role::Monk);  // 10 обычных ролей до Монаха
static constexpr int kClassCount = kBaseClasses + 3;               // 13 классов с учётом стоек

static Hero classHero(int cls, const std::vector<Hero>& roster) {
    Hero h = roster[cls < kBaseClasses ? cls : kBaseClasses];  // все стойки берут одного Монаха из heroRoster()
    if (cls >= kBaseClasses) setHeroVariant(h, cls - kBaseClasses);
    return h;
}

static std::string className(int cls, const std::vector<Hero>& roster) {
    switch (cls - kBaseClasses) {
        case 0: return "Монах(ветер)";
        case 1: return "Монах(молния)";
        case 2: return "Монах(земля)";
        default: return cls < kBaseClasses ? roster[cls].name : "?";
    }
}

static std::vector<int> smartTeam(int boss) {
    // Составы под каждого босса; теперь представлены все 13 классов,
    // включая три стойки Монаха, Вора и Барда.
    // Для Слизня и Некроманта сохранён бонус Лучника против призывов.
    switch (boss) {
        case BossSlime:    return {0, 2, 4, 8};   // Воин Жрица Лучник Бард
        case BossDevourer: return {0, 1, 5, 7};   // Воин Маг Паладин Друид
        case BossKnight:   return {0, 2, 11, 9};  // Воин Жрица Монах(молния) Сейдмард: магия против брони
        case BossArchmage: return {0, 1, 4, 10};  // Воин Маг Лучник Монах(ветер): отражение по площади
        case BossDruid:    return {0, 1, 3, 9};   // Воин Маг Вор Сейдмард
        case BossTroll:    return {0, 2, 12, 7};  // Воин Жрица Монах(земля) Друид
        case BossNecro:    return {0, 1, 4, 7};   // Воин Маг Лучник Друид
        default:           return {0, 1, 2, 7};   // дракон: Воин Маг Жрица Друид
    }
}

// Насколько герою полезен предмет (веса по ролям).
static double weight(const Hero& h, const Item& it) {
    const Role r = h.role;
    switch (it.kind) {
        case ItemKind::Weapon:
            switch (r) {
                case Role::Mage: case Role::Berserker: case Role::Rogue: return 3;
                case Role::Archer: return 2.8;
                case Role::Seidmard: return 2.5;
                case Role::Warrior: return 2;
                case Role::Paladin: return 1.5;
                case Role::Cleric: return 0.6;
                case Role::Monk: return 0;    // не носит оружие
                default: return 0.4;
            }
        case ItemKind::Armor:
            if (r == Role::Monk) return 0;    // не носит броню
            if (r == Role::Paladin || r == Role::Warrior) return 3;
            return r == Role::Berserker ? 1.5 : 1;
        case ItemKind::Relic:
            switch (it.relic) {
                case RelicEffect::Vitality: return r == Role::Mage ? 3 : isHealer(r) ? 2.5 : r == Role::Archer ? 2 : 1.5;
                case RelicEffect::Haste:    return (isHealer(r) || r == Role::Monk) ? 2 : 1;  // Монаху усиливает и Молнию, и Ветер
                case RelicEffect::Keen:
                    return (r == Role::Rogue) ? 3 : (r == Role::Mage || r == Role::Archer || r == Role::Berserker) ? 2.5
                           : r == Role::Warrior ? 1.5 : r == Role::Seidmard ? 2 : 0.8;
                case RelicEffect::Vampire:
                    return (r == Role::Berserker || r == Role::Warrior) ? 3 : r == Role::Paladin ? 2.5 : r == Role::Rogue ? 2 : 1;
            }
            return 1;
        default: return 0;
    }
}

static int itemValue(const Item& it) { return it.kind == ItemKind::Relic ? it.tier * 2 : it.power; }

static void distribute(Run& run, bool smart, Rng& rng) {
    // Run отдаёт party() только для чтения - используем публичный API снаряжения.
    const Party& p = run.party();
    if (smart) {
        for (size_t i = 0; i < p.heroes.size(); ++i) {  // снимаем вещи с павших
            if (p.heroes[i].alive()) continue;
            run.unequipItem(static_cast<int>(i), ItemKind::Weapon);
            run.unequipItem(static_cast<int>(i), ItemKind::Armor);
            run.unequipItem(static_cast<int>(i), ItemKind::Relic);
        }
        for (int iter = 0; iter < 40; ++iter) {
            int bestBag = -1, bestHero = -1;
            double best = 0;
            for (int b = 0; b < static_cast<int>(p.bag.size()); ++b) {
                const Item& it = p.bag[b];
                if (it.kind == ItemKind::Potion) continue;
                for (int h = 0; h < static_cast<int>(p.heroes.size()); ++h) {
                    const Hero& hero = p.heroes[h];
                    if (!hero.alive()) continue;
                    const auto& cur = hero.slot(it.kind);
                    double gain = (itemValue(it) - (cur ? itemValue(*cur) : 0)) * weight(hero, it);
                    if (gain > best) { best = gain; bestBag = b; bestHero = h; }
                }
            }
            if (bestBag < 0) break;
            run.equipItem(bestBag, bestHero);
        }
    } else {
        // наивно: каждый предмет - случайному живому герою (в занятый слот тоже, со сменой)
        int n = static_cast<int>(p.bag.size());
        for (int k = 0; k < n; ++k) {
            for (int b = 0; b < static_cast<int>(p.bag.size()); ++b) {
                if (p.bag[b].kind == ItemKind::Potion) continue;
                std::vector<int> alive;
                for (int h = 0; h < static_cast<int>(p.heroes.size()); ++h) if (p.heroes[h].alive()) alive.push_back(h);
                if (alive.empty()) return;
                const Hero& target = p.heroes[alive[rng.range(0, (int)alive.size() - 1)]];
                int hi = static_cast<int>(&target - &p.heroes[0]);
                // не меняем занятый слот на предмет, если слот уже занят (иначе вечный обмен)
                if (target.slot(p.bag[b].kind)) continue;
                run.equipItem(b, hi);
                break;
            }
        }
    }
}

// Покупки у торговца. Умный игрок берёт то, что даёт больше пользы на монету; наивный - случайное доступное.
static void shopping(Run& run, bool smart, Rng& rng) {
    for (int iter = 0; iter < 20; ++iter) {
        const auto* stock = run.shopStock();
        if (!stock) return;
        const Party& p = run.party();
        int best = -1;
        double bestScore = 0;
        for (int i = 0; i < static_cast<int>(stock->size()); ++i) {
            const ShopOffer& o = (*stock)[i];
            if (o.sold || o.price > p.coins) continue;
            double score;
            if (smart) {
                double gain = 0;
                if (o.item.kind == ItemKind::Potion) {
                    gain = o.item.tier * 2.0;
                } else {
                    for (const auto& h : p.heroes) {
                        if (!h.alive()) continue;
                        const auto& cur = h.slot(o.item.kind);
                        gain = std::max(gain, (itemValue(o.item) - (cur ? itemValue(*cur) : 0)) * weight(h, o.item));
                    }
                }
                score = gain / o.price;
            } else {
                score = 0.01 + rng.range(1, 100) / 100.0;
            }
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) break;
        run.buy(best);
    }
}

static Dir dirTo(Vec2i a, Vec2i b) {
    if (b.x > a.x) return East;
    if (b.x < a.x) return West;
    return b.y > a.y ? South : North;
}

// printf считает ширину %Ns в байтах, а не в символах - кириллица разъезжает таблицу.
static std::string padUtf8(const std::string& s, int width) {
    int chars = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++chars;   // не продолжающий байт
    std::string out = s;
    for (int i = chars; i < width; ++i) out += ' ';
    return out;
}

struct Result {
    bool win = false;
    int cleared = 0;
    bool reachedBoss = false;
    bool shopConnected = false;
    std::vector<int> roles;      // индексы классов симулятора (включая стойки Монаха) в порядке отряда
    std::vector<bool> survived;  // выжил ли каждый из них к концу забега
};

static Result playOne(int seed, int bossId, bool smartTeamPick, bool smartItems, int buildPolicy,
                      const std::vector<int>* fixedTeam = nullptr, const Meta* meta = nullptr) {
    Rng rng(1000 + seed);
    Deck deck(rng, bossId);
    Dungeon dg;
    dg.placeShop(rng);  // торговец у края, как в игре
    std::array<Card, 4> hand{};
    std::array<bool, 4> has{};
    auto refill = [&](int i) { has[i] = !deck.empty(); if (has[i]) hand[i] = deck.draw(); };
    for (int i = 0; i < 4; ++i) refill(i);

    auto score = [&](const Card& c, int left) {
        if (c.type == RoomType::Boss) return left <= 3 ? 1000 : -1000;
        switch (buildPolicy) {
            case 1: return c.difficulty * 10 + (c.type == RoomType::Rest ? -50 : 0);
            case 2: return (c.type == RoomType::Rest ? 50 : 0) - c.difficulty * 10;
            default: return 0;
        }
    };
    for (int left = 12 + (meta ? meta->extraPlacements() : 0); left > 0; --left) {
        std::vector<int> order;
        for (int i = 0; i < 4; ++i) if (has[i]) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return score(hand[a], left) > score(hand[b], left); });
        bool placed = false;
        for (int i : order) {
            struct Opt { Card c; Vec2i p; };
            std::vector<Opt> opts;
            Card c = hand[i];
            for (int rot = 0; rot < 4; ++rot, c.rotate())
                for (int y = 0; y < Dungeon::Height; ++y)
                    for (int x = 0; x < Dungeon::Width; ++x)
                        if (dg.check(c, {x, y}) == Dungeon::PlaceResult::Ok) opts.push_back({c, {x, y}});
            if (opts.empty()) continue;
            auto& o = opts[rng.range(0, (int)opts.size() - 1)];
            dg.place(o.c, o.p);
            refill(i);
            placed = true;
            break;
        }
        if (!placed) break;
    }
    if (!dg.hasBoss()) return {false, -1};
    const bool shopConn = dg.connected(dg.shopPos());

    auto roster = heroRoster();
    std::vector<Hero> squad;
    std::vector<int> squadClass;
    if (fixedTeam) {
        for (int i : *fixedTeam) { squad.push_back(classHero(i, roster)); squadClass.push_back(i); }
    } else if (smartTeamPick) {
        for (int i : smartTeam(bossId)) { squad.push_back(classHero(i, roster)); squadClass.push_back(i); }
    } else {
        std::vector<int> ids;
        for (int i = 0; i < kClassCount; ++i) ids.push_back(i);
        rng.shuffle(ids);
        for (int i = 0; i < 4 && i < static_cast<int>(ids.size()); ++i) {
            squad.push_back(classHero(ids[i], roster));
            squadClass.push_back(ids[i]);
        }
    }

    RunOptions options;
    if (meta) {
        options.meta = *meta;
        for (auto& h : squad) meta->applyToHero(h);
    }
    Run run(dg, 5000 + seed, squad, options);
    bool reached = false;
    std::set<Vec2i> lateShops;
    for (int guard = 0; !run.finished() && guard < 40000; ++guard) {
        if (run.phase() == RunPhase::BossIntro) reached = true;
        switch (run.phase()) {
            case RunPhase::RestChoice: {
                const Party& p = run.party();
                if (smartItems) {  // умный игрок тратит накопленные монеты на воскрешение сильнейших павших героев
                    for (int tries = 0; tries < 4; ++tries) {
                        int dead = -1;
                        for (int i = 0; i < (int)p.heroes.size(); ++i)
                            if (!p.heroes[i].alive() &&
                                (dead < 0 || weight(p.heroes[i], Item{"", ItemKind::Weapon, 1, 1}) > weight(p.heroes[dead], Item{"", ItemKind::Weapon, 1, 1})))
                                dead = i;
                        if (dead < 0 || p.coins < run.reviveCoinCost()) break;
                        run.restRevive(dead);
                    }
                }
                if (p.totalHp() * 100 < p.totalMaxHp() * 70) {
                    run.restHeal();
                } else {
                    std::vector<int> idx;
                    for (size_t i = 0; i < p.heroes.size(); ++i) if (p.heroes[i].alive()) idx.push_back((int)i);
                    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return weight(p.heroes[a], Item{"", ItemKind::Weapon, 1, 1}) > weight(p.heroes[b], Item{"", ItemKind::Weapon, 1, 1}); });
                    run.restBuff(idx);
                }
                break;
            }
            case RunPhase::BossIntro: {
                if (smartItems) {
                    distribute(run, true, rng);
                    // перед боссом тратим зелья на раненых
                    const Party& p = run.party();
                    for (int tries = 0; tries < 10; ++tries) {
                        int pb = -1, wh = -1;
                        for (int b = 0; b < (int)p.bag.size(); ++b) if (p.bag[b].kind == ItemKind::Potion) pb = b;
                        for (int h = 0; h < (int)p.heroes.size(); ++h)
                            if (p.heroes[h].alive() && p.heroes[h].hp * 100 < p.heroes[h].maxHp * 60 &&
                                (wh < 0 || p.heroes[h].hp < p.heroes[wh].hp)) wh = h;
                        if (pb < 0 || wh < 0) break;
                        run.usePotion(pb, wh);
                    }
                }
                run.beginBossFight();
                break;
            }
            case RunPhase::Fighting:
                run.step();
                break;
            case RunPhase::BossReward:
                run.claimBossRewardArtifacts();
                run.finishRunAfterBoss(true);
                break;
            case RunPhase::Shop:
                shopping(run, smartItems, rng);
                if (smartItems) distribute(run, true, rng);
                run.leaveShop();
                break;
            case RunPhase::ForgeChoice:
                if (smartItems) {
                    const Party& p = run.party();
                    for (int b = 0; b < (int)p.bag.size(); ++b) {
                        if (p.bag[b].kind == ItemKind::Potion || p.bag[b].tier >= 4) continue;
                        if (p.coins >= run.forgePrice(p.bag[b])) { run.forgeUpgrade(b); break; }
                    }
                    distribute(run, true, rng);
                }
                run.leaveForge();
                break;
            case RunPhase::EventChoice:
                // NPC всегда даёт минимум 2 варианта; первый — основной/common.
                if (!run.takeEventChoice(0)) run.takeEventChoice(1);
                if (smartItems) distribute(run, true, rng);
                break;
            case RunPhase::Exploring: {
                run.closeCombat();
                distribute(run, smartItems, rng);
                const Dungeon& d = run.dungeon();
                auto passable = [](const Room& r) { return !(r.card.type == RoomType::Boss && !r.visited); };
                std::vector<Vec2i> path;
                if (smartItems) {
                    // умный игрок откладывает торговца на конец, когда накоплены монеты
                    path = d.pathToNearest(run.partyPos(), [](const Room& r) { return !r.visited && r.card.type != RoomType::Boss && r.card.type != RoomType::Shop; }, passable);
                    if (path.empty() && run.party().coins >= 12) {
                        if (run.inShopRoom() && !lateShops.count(run.partyPos())) {
                            lateShops.insert(run.partyPos());
                            run.openShop();
                            break;
                        }
                        path = d.pathToNearest(run.partyPos(), [&](const Room& r) { return r.card.type == RoomType::Shop && !lateShops.count(r.pos); }, passable);
                        if (!path.empty() && path.size() == 1) lateShops.insert(path.back());
                    }
                } else {
                    path = d.pathToNearest(run.partyPos(), [](const Room& r) { return !r.visited && r.card.type != RoomType::Boss; }, passable);
                }
                if (path.empty())
                    path = d.pathToNearest(run.partyPos(), [](const Room& r) { return !r.visited && r.card.type == RoomType::Boss; });
                if (path.empty()) { guard = 40000; break; }
                run.move(dirTo(run.partyPos(), path.front()));
                break;
            }
            default:
                break;
        }
    }
    Result out;
    out.win = run.phase() == RunPhase::Victory;
    out.cleared = run.roomsCleared();
    out.reachedBoss = reached;
    out.shopConnected = shopConn;
    for (size_t i = 0; i < squad.size() && i < squadClass.size(); ++i) {
        out.roles.push_back(squadClass[i]);
        out.survived.push_back(i < run.party().heroes.size() && run.party().heroes[i].alive());
    }
    return out;
}

// У боссов с призывами (скелеты Некроманта, деления Слизня) залп Лучника бьёт
// сразу двух врагов. В проценте побед это не всегда видно на малой выборке,
// поэтому при выборе состава для таких боссов добавляем Лучнику бонус.
static bool bossHasAdds(int boss) { return boss == BossNecro || boss == BossSlime; }
static constexpr double kArcherAddsBonus = 10.0;

// Перебор всех C(13,4) = 715 составов отряда для каждого босса (умная раздача предметов).
static void searchTeams(int games, int policy, int requireRole = -1) {
    auto roster = heroRoster();
    for (int boss = 0; boss < kBossCount; ++boss) {
        double best = -1, bestRaw = 0, worst = 101, sum = 0;
        std::vector<int> bestT, worstT;
        int n = 0;
        for (int a = 0; a < kClassCount; ++a)
            for (int b = a + 1; b < kClassCount; ++b)
                for (int c = b + 1; c < kClassCount; ++c)
                    for (int d = c + 1; d < kClassCount; ++d) {
                        std::vector<int> team = {a, b, c, d};
                        if (requireRole >= 0 && std::find(team.begin(), team.end(), requireRole) == team.end()) continue;
                        int w = 0, p = 0;
                        for (int g = 0; g < games; ++g) {
                            Result r = playOne(g * 8 + boss, boss, false, true, policy, &team);
                            if (r.cleared < 0) continue;
                            ++p;
                            if (r.win) ++w;
                        }
                        double pct = 100.0 * w / std::max(1, p);
                        bool archer = false;
                        for (int id : team) if (id == static_cast<int>(Role::Archer)) archer = true;
                        const double score = pct + (archer && bossHasAdds(boss) ? kArcherAddsBonus : 0.0);
                        sum += pct; ++n;
                        if (score > best) { best = score; bestRaw = pct; bestT = team; }
                        if (pct < worst) { worst = pct; worstT = team; }
                    }
        auto name = [&](const std::vector<int>& t) { std::string s; for (int i : t) s += className(i, roster) + " "; return s; };
        std::printf("%-18s лучший %5.1f%% (оценка %5.1f) [%s] худший %5.1f%% [%s] средний %5.1f%%\n", bossInfo(boss).name.c_str(),
                    bestRaw, best, name(bestT).c_str(), worst, name(worstT).c_str(), sum / n);
        std::string ids;
        for (size_t i = 0; i < bestT.size(); ++i) ids += (i ? ", " : "") + std::to_string(bestT[i]);
        std::printf("        case %d: return {%s};  // %s\n", boss, ids.c_str(), name(bestT).c_str());
    }
}

// Вклад каждого улучшения «Наследия» на максимальном уровне (наивная и умная игра).
static void metaImpact(int games, int policy) {
    auto rate = [&](bool smart, const Meta* meta) {
        int w = 0, p = 0;
        for (int g = 0; g < games; ++g) {
            Result r = playOne(g, g % kBossCount, smart, smart, policy, nullptr, meta);
            if (r.cleared < 0) continue;
            ++p;
            if (r.win) ++w;
        }
        return 100.0 * w / std::max(1, p);
    };
    const double base0 = rate(false, nullptr), base1 = rate(true, nullptr);
    std::printf("Без Наследия: наивно %.1f%%, умно %.1f%%\n\n", base0, base1);
    for (int u = 0; u < kUpgradeCount; ++u) {
        Meta m;
        m.level[u] = upgradeInfo(u).maxLevel;
        std::printf("  %-22s (макс. ур.%d)  наивно %+5.1f  умно %+5.1f\n", upgradeInfo(u).name.c_str(), upgradeInfo(u).maxLevel,
                    rate(false, &m) - base0, rate(true, &m) - base1);
    }
}

int main(int argc, char** argv) {
    dc::enableUtf8Console();
    int games = argc > 1 ? std::atoi(argv[1]) : 3000;  // партий на ячейку
    int policy = argc > 2 ? std::atoi(argv[2]) : 0;
    if (argc > 3 && std::string(argv[3]) == "search")
    { searchTeams(games, policy, argc > 4 ? std::atoi(argv[4]) : -1); return 0; }
    if (argc > 3 && std::string(argv[3]) == "impact") { metaImpact(games, policy); return 0; }
    // «smart» — только умный отряд + умная раздача (без наивных режимов), быстрый прогон баланса
    const bool smartOnly = argc > 3 && std::string(argv[3]) == "smart";
    // argv[4]: прокачка «Наследия»: 0 - нет, 1 - половина уровней, 2 - максимум
    const int metaMode = argc > 4 ? std::atoi(argv[4]) : (argc > 3 && !smartOnly ? std::atoi(argv[3]) : 0);
    Meta metaCfg;
    for (int u = 0; u < kUpgradeCount; ++u)
        metaCfg.level[u] = metaMode == 0 ? 0 : metaMode == 1 ? (upgradeInfo(u).maxLevel + 1) / 2 : upgradeInfo(u).maxLevel;
    const Meta* metaPtr = metaMode == 0 ? nullptr : &metaCfg;
    const char* policyNames[3] = {"случайная расстановка комнат", "жадная (только сложные)", "осторожная (лёгкие + отдых)"};
    std::printf("Расстановка: %s, партий на ячейку: %d, прокачка Наследия: %s\n\n", policyNames[policy], games,
                metaMode == 0 ? "нет" : metaMode == 1 ? "половина" : "максимум");

    struct Cfg { const char* name; bool team, items; };
    const Cfg cfgs[4] = {
        {"наивно: случайный отряд, случайные предметы ", false, false},
        {"отряд под босса, предметы случайно          ", true, false},
        {"случайный отряд, умная раздача предметов    ", false, true},
        {"отряд под босса + умная раздача             ", true, true},
    };
    int wins[4][kBossCount] = {};
    int reach[4] = {};
    int shopSeen[4] = {};
    int shopPlayed[4] = {};
    int played[4][kBossCount] = {};
    int classPlayed[4][kClassCount] = {};
    int classWins[4][kClassCount] = {};
    int classSurvived[4][kClassCount] = {};
    for (int c = smartOnly ? 3 : 0; c < 4; ++c)
        for (int g = 0; g < games; ++g) {
            int boss = g % kBossCount;
            Result r = playOne(g, boss, cfgs[c].team, cfgs[c].items, policy, nullptr, metaPtr);
            if (r.cleared < 0) continue;  // босс не удалось разместить
            ++played[c][boss];
            if (r.win) ++wins[c][boss];
            if (r.reachedBoss) ++reach[c];
            ++shopPlayed[c];
            if (r.shopConnected) ++shopSeen[c];
            for (size_t i = 0; i < r.roles.size() && i < r.survived.size(); ++i) {
                const int cls = r.roles[i];
                if (cls < 0 || cls >= kClassCount) continue;
                ++classPlayed[c][cls];
                if (r.win) ++classWins[c][cls];
                if (r.survived[i]) ++classSurvived[c][cls];
            }
        }
    for (int c = smartOnly ? 3 : 0; c < 4; ++c) {
        int w = 0, p = 0;
        for (int b = 0; b < kBossCount; ++b) { w += wins[c][b]; p += played[c][b]; }
        std::printf("%s  победы: %5.1f%%  дошли до босса: %5.1f%%  торговец подключён: %4.1f%%\n", cfgs[c].name, 100.0 * w / std::max(1, p), 100.0 * reach[c] / std::max(1, p), 100.0 * shopSeen[c] / std::max(1, shopPlayed[c]));
    }
    if (smartOnly) {
        std::printf("\nПо боссам (умный отряд + умные предметы):\n");
        for (int b = 0; b < kBossCount; ++b)
            std::printf("  %-18s %5.1f%%\n", bossInfo(b).name.c_str(),
                        100.0 * wins[3][b] / std::max(1, played[3][b]));
    } else {
        std::printf("\nПо боссам (наивно / умно):\n");
        for (int b = 0; b < kBossCount; ++b)
            std::printf("  %-18s %5.1f%% / %5.1f%%\n", bossInfo(b).name.c_str(),
                        100.0 * wins[0][b] / std::max(1, played[0][b]), 100.0 * wins[3][b] / std::max(1, played[3][b]));
    }

    const auto roster = heroRoster();
    std::printf("\nПо классам (победы / выжил, %% от забегов с этим классом в отряде):\n");
    struct Row { int cls; double rate[4]; };
    std::vector<Row> rows;
    for (int cls = 0; cls < kClassCount; ++cls) {
        Row row{cls, {-1, -1, -1, -1}};
        for (int c = 0; c < 4; ++c)
            if (classPlayed[c][cls] > 0) row.rate[c] = 100.0 * classWins[c][cls] / classPlayed[c][cls];
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.rate[3] > b.rate[3]; });
    auto cell = [&](int c, int cls) {
        if (classPlayed[c][cls] == 0) return std::string("-");
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%4.1f%% / %4.1f%%",
                      100.0 * classWins[c][cls] / classPlayed[c][cls],
                      100.0 * classSurvived[c][cls] / classPlayed[c][cls]);
        return std::string(buf);
    };
    if (smartOnly) {
        std::printf("  %s\n", padUtf8("класс", 16).c_str());
        for (const auto& row : rows)
            std::printf("  %s  %s\n", padUtf8(className(row.cls, roster), 16).c_str(), cell(3, row.cls).c_str());
    } else {
        const char* heads[5] = {"класс", "наивная", "отряд под босса", "умные предметы", "всё умное"};
        for (int i = 0; i < 5; ++i) std::printf("  %s", padUtf8(heads[i], 16).c_str());
        std::printf("\n");
        for (const auto& row : rows) {
            std::printf("  %s", padUtf8(className(row.cls, roster), 16).c_str());
            for (int c = 0; c < 4; ++c) std::printf("  %s", padUtf8(cell(c, row.cls), 16).c_str());
            std::printf("\n");
        }
    }
}
