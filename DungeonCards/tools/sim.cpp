// Консольная симуляция для балансировки сложности.
// Сравнивает «наивную» игру (случайный отряд и случайная раздача предметов) с «умной»
// (отряд под босса и разумное распределение снаряжения), перебирая всех 8 боссов.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include "core/Run.h"

using namespace dc;

// индексы в heroRoster(): 0 Воин, 1 Маг, 2 Жрица, 3 Вор, 4 Лучник, 5 Паладин, 6 Берсерк, 7 Друид
static std::vector<int> smartTeam(int boss) {
    // найдено перебором всех 70 составов (режим search)
    switch (boss) {
        case BossSlime:    return {1, 2, 3, 5};  // Маг Жрица Вор Паладин
        case BossDevourer: return {2, 3, 5, 7};  // Жрица Вор Паладин Друид
        case BossKnight:   return {1, 2, 5, 7};  // Маг Жрица Паладин Друид
        case BossArchmage: return {0, 2, 5, 7};  // Воин Жрица Паладин Друид
        case BossDruid:    return {2, 3, 5, 6};  // Жрица Вор Паладин Берсерк
        case BossTroll:    return {0, 2, 5, 7};  // Воин Жрица Паладин Друид
        case BossNecro:    return {0, 2, 3, 5};  // Воин Жрица Вор Паладин
        default:           return {2, 5, 6, 7};  // дракон: Жрица Паладин Берсерк Друид
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
                case Role::Warrior: return 2;
                case Role::Paladin: return 1.5;
                case Role::Cleric: return 0.6;
                default: return 0.4;
            }
        case ItemKind::Armor:
            if (r == Role::Paladin || r == Role::Warrior) return 3;
            return r == Role::Berserker ? 1.5 : 1;
        case ItemKind::Relic:
            switch (it.relic) {
                case RelicEffect::Vitality: return r == Role::Mage ? 3 : isHealer(r) ? 2.5 : r == Role::Archer ? 2 : 1.5;
                case RelicEffect::Haste:    return isHealer(r) ? 2 : 1;
                case RelicEffect::Keen:
                    return (r == Role::Rogue) ? 3 : (r == Role::Mage || r == Role::Archer || r == Role::Berserker) ? 2.5
                           : r == Role::Warrior ? 1.5 : 0.8;
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

struct Result { bool win = false; int cleared = 0; bool reachedBoss = false; bool shopConnected = false; };

static Result playOne(int seed, int bossId, bool smartTeamPick, bool smartItems, int buildPolicy,
                      const std::vector<int>* fixedTeam = nullptr) {
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
    for (int left = 12; left > 0; --left) {
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
    if (fixedTeam) {
        for (int i : *fixedTeam) squad.push_back(roster[i]);
    } else if (smartTeamPick) {
        for (int i : smartTeam(bossId)) squad.push_back(roster[i]);
    } else {
        std::vector<int> ids = {0, 1, 2, 3, 4, 5, 6, 7};
        rng.shuffle(ids);
        for (int i = 0; i < 4; ++i) squad.push_back(roster[ids[i]]);
    }

    Run run(dg, 5000 + seed, squad);
    bool reached = false;
    std::set<Vec2i> lateShops;
    for (int guard = 0; !run.finished() && guard < 40000; ++guard) {
        if (run.phase() == RunPhase::BossIntro) reached = true;
        switch (run.phase()) {
            case RunPhase::RestChoice: {
                const Party& p = run.party();
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
            case RunPhase::Shop:
                shopping(run, smartItems, rng);
                if (smartItems) distribute(run, true, rng);
                run.leaveShop();
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
    return {run.phase() == RunPhase::Victory, run.roomsCleared(), reached, shopConn};
}

// Перебор всех 70 составов отряда для каждого босса (умная раздача предметов).
static void searchTeams(int games, int policy) {
    auto roster = heroRoster();
    for (int boss = 0; boss < kBossCount; ++boss) {
        double best = -1, worst = 101, sum = 0;
        std::vector<int> bestT, worstT;
        int n = 0;
        for (int a = 0; a < 8; ++a) for (int b = a + 1; b < 8; ++b) for (int c = b + 1; c < 8; ++c) for (int d = c + 1; d < 8; ++d) {
            std::vector<int> team = {a, b, c, d};
            int w = 0, p = 0;
            for (int g = 0; g < games; ++g) {
                Result r = playOne(g * 8 + boss, boss, false, true, policy, &team);
                if (r.cleared < 0) continue;
                ++p;
                if (r.win) ++w;
            }
            double pct = 100.0 * w / std::max(1, p);
            sum += pct; ++n;
            if (pct > best) { best = pct; bestT = team; }
            if (pct < worst) { worst = pct; worstT = team; }
        }
        auto name = [&](const std::vector<int>& t) { std::string s; for (int i : t) s += roster[i].name + " "; return s; };
        std::printf("%-18s лучший %5.1f%% [%s] худший %5.1f%% [%s] средний %5.1f%%\n", bossInfo(boss).name.c_str(),
                    best, name(bestT).c_str(), worst, name(worstT).c_str(), sum / n);
    }
}

int main(int argc, char** argv) {
    int games = argc > 1 ? std::atoi(argv[1]) : 1600;
    int policy = argc > 2 ? std::atoi(argv[2]) : 0;
    if (argc > 3) { searchTeams(games, policy); return 0; }
    const char* policyNames[3] = {"случайная расстановка комнат", "жадная (только сложные)", "осторожная (лёгкие + отдых)"};
    std::printf("Расстановка: %s, партий на ячейку: %d\n\n", policyNames[policy], games);

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
    for (int c = 0; c < 4; ++c)
        for (int g = 0; g < games; ++g) {
            int boss = g % kBossCount;
            Result r = playOne(g, boss, cfgs[c].team, cfgs[c].items, policy);
            if (r.cleared < 0) continue;  // босс не удалось разместить
            ++played[c][boss];
            if (r.win) ++wins[c][boss];
            if (r.reachedBoss) ++reach[c];
            ++shopPlayed[c];
            if (r.shopConnected) ++shopSeen[c];
        }
    for (int c = 0; c < 4; ++c) {
        int w = 0, p = 0;
        for (int b = 0; b < kBossCount; ++b) { w += wins[c][b]; p += played[c][b]; }
        std::printf("%s  победы: %5.1f%%  дошли до босса: %5.1f%%  торговец подключён: %4.1f%%\n", cfgs[c].name, 100.0 * w / std::max(1, p), 100.0 * reach[c] / std::max(1, p), 100.0 * shopSeen[c] / std::max(1, shopPlayed[c]));
    }
    std::printf("\nПо боссам (наивно / умно):\n");
    for (int b = 0; b < kBossCount; ++b)
        std::printf("  %-18s %5.1f%% / %5.1f%%\n", bossInfo(b).name.c_str(),
                    100.0 * wins[0][b] / std::max(1, played[0][b]), 100.0 * wins[3][b] / std::max(1, played[3][b]));
}
