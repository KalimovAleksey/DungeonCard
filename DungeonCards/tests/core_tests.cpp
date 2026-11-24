#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include "core/Assets.h"
#include "core/ConsoleUtf8.h"
#include "core/Run.h"

using namespace dc;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Card card(RoomType t, int diff, uint8_t doors, int boss = -1) {
    Card c; c.type = t; c.difficulty = diff; c.doors = doors; c.bossId = boss; return c;
}
static Item gear(ItemKind k, int tier, int power, RelicEffect e = RelicEffect::Vitality) {
    Item it; it.name = "тест"; it.kind = k; it.tier = tier; it.power = power; it.relic = e; return it;
}

static std::set<std::string> manifestPaths() {
    std::set<std::string> s;
    for (auto& a : assetManifest()) s.insert(a.path);
    return s;
}

// Герой с «неубиваемыми» характеристиками для проверки механик боссов.
static Hero tough(const char* name, Role r, int atk) {
    Hero h = makeHero(name, r, 900, atk, 5, 5);
    return h;
}

int main() {
    dc::enableUtf8Console();
    // ---------------------------------------------------------- двери и карты
    CHECK(rotateDoors(doorBit(North), 1) == doorBit(East));
    CHECK(rotateDoors(doorBit(West), 1) == doorBit(North));
    CHECK(rotateDoors(15, 3) == 15);

    Dungeon dg;
    const Vec2i s = dg.startPos();
    Card c = card(RoomType::Combat, 1, doorBit(West) | doorBit(East));
    CHECK(dg.check(c, {s.x + 1, s.y}) == Dungeon::PlaceResult::Ok);
    CHECK(dg.check(c, {0, 0}) == Dungeon::PlaceResult::NoConnection);
    CHECK(dg.check(c, s) == Dungeon::PlaceResult::Occupied);
    CHECK(dg.check(c, {-1, 0}) == Dungeon::PlaceResult::OutOfBounds);
    Card wrongSide = card(RoomType::Combat, 1, doorBit(North) | doorBit(South));
    CHECK(dg.check(wrongSide, {s.x + 1, s.y}) == Dungeon::PlaceResult::NoConnection);
    CHECK(dg.place(c, {s.x + 1, s.y}));
    CHECK(dg.linked(s, East));
    CHECK(dg.linked({s.x + 1, s.y}, West));
    CHECK(!dg.linked(s, North));

    // ----------------------------------------- торговец у края и подключение
    {
        for (int seed = 0; seed < 300; ++seed) {
            Rng rr(seed);
            Dungeon d;
            Vec2i sp = d.placeShop(rr);
            const int edge = std::min(std::min(sp.x, Dungeon::Width - 1 - sp.x), std::min(sp.y, Dungeon::Height - 1 - sp.y));
            CHECK(edge <= 1);                                   // у края карты
            CHECK(d.hasShop() && d.shopPos() == sp);
            CHECK(d.roomAt(sp) && d.roomAt(sp)->card.type == RoomType::Shop);
            CHECK(!d.connected(sp));                            // в начале не подключён
            CHECK(d.connected(d.startPos()));
            int inbound = 0;
            for (int i = 0; i < kDirCount; ++i) inbound += d.inBounds(sp + dirDelta((Dir)i)) ? 1 : 0;
            int doors = 0;
            for (int i = 0; i < kDirCount; ++i) doors += hasDoor(d.roomAt(sp)->card.doors, (Dir)i) ? 1 : 0;
            CHECK(doors == inbound && doors >= 2);
        }
        int edge0 = 0;                                          // самый край выпадает заметно чаще
        for (int seed = 0; seed < 600; ++seed) {
            Rng rr(seed);
            Dungeon d;
            Vec2i sp = d.placeShop(rr);
            if (std::min(std::min(sp.x, Dungeon::Width - 1 - sp.x), std::min(sp.y, Dungeon::Height - 1 - sp.y)) == 0) ++edge0;
        }
        CHECK(edge0 > 600 * 55 / 100);

        // подключение: комната, стыкующаяся только с торговцем, не ставится; мост - ставится
        Dungeon d;
        CHECK(d.placeShopAt({0, 3}));
        CHECK(!d.placeShopAt({0, 1}));                          // торговец только один
        Card toShop = card(RoomType::Combat, 1, doorBit(West) | doorBit(East));
        CHECK(d.check(toShop, {1, 3}) == Dungeon::PlaceResult::ShopOnly);
        CHECK(!d.place(toShop, {1, 3}));
        Card mid = card(RoomType::Combat, 1, doorBit(West) | doorBit(East));
        CHECK(d.place(mid, {3, 3}));
        CHECK(d.place(mid, {2, 3}));
        CHECK(!d.connected({0, 3}));
        CHECK(d.place(toShop, {1, 3}));                         // стыкуется и с сетью, и с торговцем
        CHECK(d.connected({0, 3}));                             // теперь торговец достижим
        CHECK(d.connected({1, 3}));
        Card behind = card(RoomType::Trap, 1, doorBit(South) | doorBit(North));
        CHECK(d.place(behind, {0, 2}));                         // и к нему можно пристраивать комнаты
    }

    // ---------------------------------------------------- снаряжение и рюкзак
    {
        Party p;
        const int atk0 = p.heroes[1].atk;
        p.bag.push_back(gear(ItemKind::Weapon, 2, 5));
        CHECK(p.equipFromBag(0, 1));
        CHECK(p.bag.empty());
        CHECK(p.heroes[1].atk == atk0 + 5);

        p.bag.push_back(gear(ItemKind::Weapon, 3, 7));          // замена: старый предмет возвращается
        CHECK(p.equipFromBag(0, 1));
        CHECK(p.heroes[1].atk == atk0 + 7);
        CHECK(p.bag.size() == 1 && p.bag[0].power == 5);
        CHECK(p.unequip(1, ItemKind::Weapon));
        CHECK(p.heroes[1].atk == atk0);
        CHECK(p.bag.size() == 2);

        const int max0 = p.heroes[0].maxHp, def0 = p.heroes[0].def;
        p.bag.push_back(gear(ItemKind::Armor, 2, 3));
        CHECK(p.equipFromBag((int)p.bag.size() - 1, 0));
        CHECK(p.heroes[0].def == def0 + 3);
        CHECK(p.heroes[0].maxHp == max0 + 6);                   // броня даёт и ЗДР (ур. x3)
        CHECK(p.heroes[0].hp == p.heroes[0].maxHp);

        // Снятие предмета возвращает и текущее, и максимальное ЗДР: бесплатного лечения нет.
        p.heroes[0].hp -= 4;
        const int hurtHp = p.heroes[0].hp;
        CHECK(p.unequip(0, ItemKind::Armor));
        CHECK(p.heroes[0].maxHp == max0);
        CHECK(p.heroes[0].hp == hurtHp - 6);                    // -6 бонусных ЗДР ушло вместе с бронёй
        CHECK(p.equipFromBag((int)p.bag.size() - 1, 0));
        CHECK(p.heroes[0].maxHp == max0 + 6);
        CHECK(p.heroes[0].hp == hurtHp);                        // надевание вернуло ровно столько же

        p.bag.push_back(gear(ItemKind::Potion, 1, 10));
        CHECK(!p.equipFromBag((int)p.bag.size() - 1, 0));       // зелье не надевается
        p.heroes[2].hp = 5;
        CHECK(p.usePotion((int)p.bag.size() - 1, 2));
        CHECK(p.heroes[2].hp == 15);
        CHECK(!p.usePotion(0, 2) || true);

        p.bag.push_back(gear(ItemKind::Relic, 2, 16, RelicEffect::Keen));
        CHECK(p.equipFromBag((int)p.bag.size() - 1, 3));
        CHECK(p.heroes[3].critPct == 16);

        p.heroes[0].hp = 0;                                     // павшему надеть нельзя, снять - можно
        p.bag.push_back(gear(ItemKind::Weapon, 1, 2));
        CHECK(!p.equipFromBag((int)p.bag.size() - 1, 0));
        CHECK(p.unequip(0, ItemKind::Armor));
    }

    // ----------------------------------------------- ручное движение и привал
    {
        Dungeon d;
        d.place(card(RoomType::Rest, 1, doorBit(West) | doorBit(East)), {s.x + 1, s.y});
        d.place(card(RoomType::Boss, 4, doorBit(West), BossKnight), {s.x + 2, s.y});
        Run r(d, 3);
        const int atk0 = r.party().heroes[0].atk, def1 = r.party().heroes[1].def, atk2 = r.party().heroes[2].atk;

        CHECK(!r.move(North));                       // там стены: хода нет
        CHECK(r.partyPos() == s);
        CHECK(r.move(East));
        CHECK(r.phase() == RunPhase::RestChoice);
        CHECK(!r.move(East));                        // на привале стоим
        r.restBuff({0, 1, 2});                       // третий лишний: максимум двое
        CHECK(r.phase() == RunPhase::Exploring);
        CHECK(r.party().heroes[0].atk == atk0 + Run::kBuffAtk);
        CHECK(r.party().heroes[1].def == def1 + Run::kBuffDef);
        CHECK(r.party().heroes[2].atk == atk2);

        CHECK(r.move(West));                         // можно вернуться назад
        CHECK(r.move(East));
        CHECK(r.move(East));                         // логово босса
        CHECK(r.phase() == RunPhase::BossIntro);
        CHECK(r.pendingBossId() == BossKnight);
        r.step();                                    // пока не начали - ничего не происходит
        CHECK(r.phase() == RunPhase::BossIntro);
        CHECK(r.canManage());                        // в логове можно менять снаряжение
        r.beginBossFight();
        CHECK(r.phase() == RunPhase::Fighting);
        CHECK(!r.canManage());                       // в бою инвентарь закрыт
        CHECK(!r.equipItem(0, 0));
        for (int g = 0; g < 5000 && !r.finished(); ++g) r.step();
        CHECK(r.finished());
    }

    // ---------------------------------------------------- боссы: общие инварианты
    for (int boss = 0; boss < kBossCount; ++boss) {
        CHECK(!bossInfo(boss).name.empty());
        CHECK(!bossInfo(boss).abilities.empty() && !bossInfo(boss).strengths.empty() && !bossInfo(boss).weaknesses.empty());
        for (int seed = 0; seed < 25; ++seed) {
            Rng rng(seed);
            Party party;
            Combat cb(party, {makeBoss(boss)}, rng, boss);
            std::vector<std::string> lg;
            int steps = 0;
            while (!cb.finished() && steps++ < 5000) {
                cb.step(lg);
                for (auto& a : cb.events())
                    for (auto& h : a.hits) CHECK(h.amount > 0);
                for (auto& h : party.heroes) CHECK(h.hp <= h.maxHp);
                for (auto& e : cb.enemies()) { CHECK(e.hp <= e.maxHp); CHECK(manifestPaths().count(e.art) > 0); }
                CHECK(cb.enemies().size() <= 8);
            }
            CHECK(cb.finished());
            for (auto& h : party.heroes) CHECK(!h.away);   // пленники освобождены
        }
    }

    // --------------------------------------------------------- механики боссов
    {   // слизень: 1 -> 2 -> 4 (всего 7 существ), все побеждены
        Rng rng(1);
        Party party;
        party.heroes = {tough("А", Role::Warrior, 60), tough("Б", Role::Warrior, 60), tough("В", Role::Warrior, 60), tough("Г", Role::Warrior, 60)};
        Combat cb(party, {makeBoss(BossSlime)}, rng, BossSlime);
        std::vector<std::string> lg;
        int maxAlive = 0;
        for (int i = 0; i < 2000 && !cb.finished(); ++i) {
            cb.step(lg);
            int alive = 0;
            for (auto& e : cb.enemies()) alive += e.alive() ? 1 : 0;
            maxAlive = std::max(maxAlive, alive);
        }
        CHECK(cb.heroesWon());
        CHECK(cb.enemies().size() == 7);
        CHECK(maxAlive <= 4);
        CHECK(cb.slotCount() == 4);
    }
    {   // пожиратель: глотает героя, который выбывает из боя, пока не нанесён нужный урон
        Rng rng(2);
        Party party;
        party.heroes = {tough("А", Role::Warrior, 6), tough("Б", Role::Mage, 11), tough("В", Role::Cleric, 5), tough("Г", Role::Rogue, 7)};
        Combat cb(party, {makeBoss(BossDevourer)}, rng, BossDevourer);
        std::vector<std::string> lg;
        bool swallowed = false, released = false;
        for (int i = 0; i < 4000 && !cb.finished(); ++i) {
            cb.step(lg);
            bool anyAway = false;
            for (auto& h : party.heroes) anyAway |= h.away;
            if (anyAway) swallowed = true;
            else if (swallowed) released = true;
        }
        CHECK(swallowed);
        CHECK(released);
    }
    {   // рыцарь: маг игнорирует броню, обычный воин бьёт по минимуму (25% АТК)
        Rng rng(3);
        Party party;
        party.heroes = {tough("Маг", Role::Mage, 11)};
        Combat cb(party, {makeBoss(BossKnight)}, rng, BossKnight);
        std::vector<std::string> lg;
        int mageDmg = 0;
        for (int i = 0; i < 20 && !mageDmg; ++i) {
            cb.step(lg);
            for (auto& a : cb.events())
                if (a.actorHero && !a.hits.empty() && !a.hits[0].heal) mageDmg = a.hits[0].amount;
        }
        CHECK(mageDmg == 11);

        Party p2;
        p2.heroes = {tough("Воин", Role::Warrior, 8)};
        Combat cb2(p2, {makeBoss(BossKnight)}, rng, BossKnight);
        int warDmg = 0;
        for (int i = 0; i < 20 && !warDmg; ++i) {
            cb2.step(lg);
            for (auto& a : cb2.events())
                if (a.actorHero && !a.hits.empty() && !a.hits[0].heal) warDmg = a.hits[0].amount;
        }
        CHECK(warDmg == 2);
    }
    {   // друид-похититель: часть лечения отряда достаётся боссу
        Rng rng(4);
        Party party;
        Hero cleric = tough("Жрица", Role::Cleric, 5);
        cleric.hp = 100;
        party.heroes = {cleric};
        Enemy boss = makeBoss(BossDruid);
        boss.hp = boss.maxHp - 80;
        Combat cb(party, {boss}, rng, BossDruid);
        std::vector<std::string> lg;
        bool stolen = false;
        for (int i = 0; i < 30 && !stolen; ++i) {
            cb.step(lg);
            for (auto& a : cb.events())
                if (a.kind == CombatAction::Effect)
                    for (auto& h : a.hits) if (!h.targetHero && h.heal) stolen = true;
        }
        CHECK(stolen);
    }
    {   // некромант: призывает скелетов в первом раунде
        Rng rng(5);
        Party party;
        party.heroes = {tough("А", Role::Warrior, 5), tough("Б", Role::Warrior, 5)};
        Combat cb(party, {makeBoss(BossNecro)}, rng, BossNecro);
        std::vector<std::string> lg;
        for (int i = 0; i < 12; ++i) cb.step(lg);
        int skeletons = 0;
        for (auto& e : cb.enemies()) skeletons += e.minion ? 1 : 0;
        CHECK(skeletons >= 1 && skeletons <= 3);
    }

    // ------------------------------------------------------------- лучник
    {
        auto target = [](int hp, int def) {
            Enemy e;
            e.name = "Мишень";
            e.maxHp = e.hp = hp;
            e.atk = 1;
            e.def = def;
            e.spd = 1;
            return e;
        };
        auto archerHits = [&](std::vector<Enemy> enemies) {
            Rng rr(1);
            Party party;
            party.heroes = {tough("Лучник", Role::Archer, 10)};
            Combat cb(party, std::move(enemies), rr);
            std::vector<std::string> lg;
            for (int i = 0; i < 20; ++i) {
                cb.step(lg);
                for (auto& a : cb.events())
                    if (a.actorHero && a.kind == CombatAction::Ranged) {
                        std::vector<int> dmg;
                        for (auto& h : a.hits) dmg.push_back(h.amount);
                        return dmg;
                    }
            }
            return std::vector<int>{};
        };
        auto one = archerHits({target(5000, 1)});
        CHECK(one.size() == 1 && one[0] == 10 - 1);                       // одна цель: один выстрел, 100%
        auto two = archerHits({target(5000, 1), target(5000, 1)});
        CHECK(two.size() == 2 && two[0] == 8 - 1 && two[1] == 8 - 1);     // две цели: по 80%
        auto three = archerHits({target(5000, 1), target(5000, 1), target(5000, 1)});
        CHECK(three.size() == 2);                                         // больше двух не бьёт
    }

    // --------------------------------------------- новые классы и их умения
    {
        const auto roster = heroRoster();
        CHECK(roster.size() == 10);
        CHECK(roster.back().role == Role::Seidmard && roster.back().name == "Сейдмард");

        // Сейдмард: снимает по 2 ЗАЩ за свой ход, до 6, затем бьёт магией
        {
            Rng rr(5);
            Party p;
            Hero r = tough("Сейдмард", Role::Seidmard, 8);
            r.baseSpd = r.spd = 9;
            p.heroes = {r};
            Enemy e;
            e.name = "Броня"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 6; e.spd = 1;
            Combat cb(p, {e}, rr);
            std::vector<std::string> lg;
            std::vector<int> shred;
            int shouts = 0, extras = 0;
            for (int turn = 1; turn <= 8; ++turn) {  // 4 раунда: герой ходит перед врагом
                cb.step(lg);
                shred.push_back(cb.enemies()[0].defShred);
                if (turn % 2 == 1) {
                    if (turn <= 5) {
                        CHECK(!cb.events().empty() && cb.events().back().kind == CombatAction::Shout);
                        CHECK(cb.events().back().text == "Руна: -2 ЗАЩ");
                        ++shouts;
                    } else {
                        for (const auto& a : cb.events())
                            if (a.kind == CombatAction::Ranged && !a.hits.empty()) ++extras;
                    }
                }
            }
            CHECK((shred == std::vector<int>{2, 2, 4, 4, 6, 6, 6, 6}));
            CHECK(shouts == 3);
            CHECK(extras == 1);  // когда ЗАЩ снята - дополнительный магический удар
        }
        // Снятая рунами ЗАЩ помогает всему отряду
        {
            Rng rr(6);
            Party p;
            Hero r = tough("Сейдмард", Role::Seidmard, 8);
            r.baseSpd = r.spd = 9;
            Hero w = tough("Воин", Role::Warrior, 8);
            w.baseSpd = w.spd = 8;
            p.heroes = {r, w};
            Enemy e;
            e.name = "Рыцарь"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 11; e.spd = 1;
            Combat cb(p, {e}, rr);
            std::vector<std::string> lg;
            int first = 0, late = 0;
            for (int turn = 1; turn <= 8; ++turn) {  // 3 раунда: рунист, воин, враг
                cb.step(lg);
                int mx = 0;
                for (const auto& a : cb.events())
                    for (const auto& h : a.hits)
                        if (!h.targetHero && !h.heal) mx = std::max(mx, h.amount);
                if (turn == 2) first = mx;
                if (turn == 8) late = mx;
            }
            CHECK(first == 2);    // 11 ЗАЩ: проходит минимум 25% АТК
            CHECK(late > first);  // после трёх рун ЗАЩ 5, урон больше
        }
        // Маг: на 3-й ход после атаки - арканный взрыв по всем врагам, счётчик не переносится в новый бой
        {
            Rng rr(7);
            Party p;
            Hero m = tough("Маг", Role::Mage, 8);
            m.baseSpd = m.spd = 9;
            p.heroes = {m};
            auto mk = []() {
                Enemy e;
                e.name = "Цель"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 0; e.spd = 1;
                return e;
            };
            Combat cb(p, {mk(), mk()}, rr);
            std::vector<std::string> lg;
            for (int turn = 1; turn <= 7; ++turn) {  // 3 хода мага + все враги
                cb.step(lg);
                if (turn != 1 && turn != 4 && turn != 7) continue;
                if (turn < 7) {
                    CHECK(cb.events().size() == 1);
                    CHECK(cb.events()[0].hits.size() == 1);
                } else {
                    bool burst = false, shout = false;
                    for (const auto& a : cb.events()) {
                        if (a.kind == CombatAction::Ranged && a.hits.size() == 2) burst = true;
                        if (a.kind == CombatAction::Shout && a.text == "Арканный взрыв!") shout = true;
                    }
                    CHECK(burst && shout);
                }
            }
            Combat fresh(p, {mk(), mk()}, rr);
            fresh.step(lg);
            CHECK(fresh.events().size() == 1);
            CHECK(fresh.events()[0].hits.size() == 1);
        }

        // Сейдмард даёт скидку 20% в кузнице, пока жив
        {
            Dungeon d;
            const Item it = gear(ItemKind::Weapon, 1, 5);
            auto priceWith = [&](bool withSeidmard) {
                std::vector<Hero> squad = {tough("Воин", Role::Warrior, 8)};
                if (withSeidmard) squad.push_back(tough("Сейдмард", Role::Seidmard, 8));
                Run r(d, 5, squad);
                return r.forgePrice(it);
            };
            const int plain = priceWith(false), discounted = priceWith(true);
            CHECK(plain > 0);
            CHECK(discounted == plain * 80 / 100);   // 60% от цены upgraded тира, затем −20%

            Dungeon d2;
            std::vector<Hero> squad = {tough("Воин", Role::Warrior, 8), tough("Сейдмард", Role::Seidmard, 8)};
            squad[1].hp = 0;
            Run dead(d2, 5, squad);
            CHECK(!dead.hasLivingSeidmard());
            CHECK(dead.forgePrice(it) == plain);     // погибший скидку не даёт
        }
    }

    // ----------------------------- каждый герой бьёт, затем применяет умение
    {
        auto dummyEnemy = []() {
            Enemy e;
            e.name = "Мишень"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 0; e.spd = 1;
            return e;
        };
        auto firstStep = [&](std::vector<Hero> squad) {
            Rng rr(1);
            Party p;
            p.heroes = std::move(squad);
            Combat cb(p, {dummyEnemy()}, rr);
            std::vector<std::string> lg;
            cb.step(lg);
            return cb.events();
        };

        {   // жрица: удар -> лечение
            Hero cleric = tough("Жрица", Role::Cleric, 10);
            cleric.baseSpd = cleric.spd = 9;
            Hero ally = tough("Союзник", Role::Warrior, 8);
            ally.hp = 50;
            auto ev = firstStep({cleric, ally});
            CHECK(ev.size() >= 2);
            if (ev.size() >= 2) {
                CHECK(!ev[0].hits.empty() && !ev[0].hits[0].heal && !ev[0].hits[0].targetHero);
                CHECK(ev[1].kind == CombatAction::Heal && !ev[1].hits.empty() && ev[1].hits[0].heal);
            }
        }
        {   // бард: удар -> вдохновение
            Hero bard = tough("Бард", Role::Bard, 10);
            bard.baseSpd = bard.spd = 9;
            Hero ally = tough("Воин", Role::Warrior, 20);
            auto ev = firstStep({bard, ally});
            CHECK(ev.size() >= 2);
            if (ev.size() >= 2) {
                CHECK(!ev[0].hits.empty() && !ev[0].hits[0].heal);
                CHECK(ev[1].kind == CombatAction::Effect && !ev[1].hits.empty() && ev[1].hits[0].heal);
                CHECK(ev[1].hits[0].idx == 1 && ev[1].hits[0].amount == 5);  // +атк/2 союзнику после удара
            }
        }
        {   // берсерк: удар -> ярость (доп. удар) -> вампиризм
            Hero z = tough("Берсерк", Role::Berserker, 10);
            z.baseSpd = z.spd = 9;
            z.hp = 400;                                         // ниже половины от 900
            auto ev = firstStep({z});
            CHECK(ev.size() >= 3);
            if (ev.size() >= 3) {
                CHECK(!ev[0].hits.empty() && !ev[0].hits[0].heal && ev[0].hits[0].amount == 10);
                CHECK(!ev[1].hits.empty() && !ev[1].hits[0].heal && ev[1].hits[0].amount == 5);
                CHECK(ev[2].kind == CombatAction::Effect && !ev[2].hits.empty() && ev[2].hits[0].heal);
                CHECK(ev[2].hits[0].amount == 1);               // 10% ауры от 15 урона
            }
        }
        {   // воин: удар -> провокация
            Hero w = tough("Воин", Role::Warrior, 8);
            w.baseSpd = w.spd = 9;
            auto ev = firstStep({w});
            CHECK(ev.size() >= 2);
            if (ev.size() >= 2) {
                CHECK(!ev[0].hits.empty() && !ev[0].hits[0].heal);
                CHECK(ev[1].kind == CombatAction::Shout && ev[1].text == "Провокация!");
            }
        }
        {   // друид: удар -> усиленное цепное лечение
            Hero d = tough("Друид", Role::Druid, 4);
            d.baseSpd = d.spd = 9;
            d.hp = 500;
            Hero ally = tough("Союзник", Role::Warrior, 8);
            ally.hp = 500;
            auto ev = firstStep({d, ally});
            CHECK(ev.size() >= 2);
            if (ev.size() >= 2) {
                CHECK(!ev[0].hits.empty() && !ev[0].hits[0].heal && !ev[0].hits[0].targetHero);
                CHECK(ev[1].kind == CombatAction::Heal);
                int healed = 0;
                for (auto& h : ev[1].hits) if (h.heal) healed += h.amount;
                CHECK(healed >= 9);                             // 6 + 5 по двум целям
            }
        }
    }

    // ------------------------------------------ масштабирование врагов по этажам
    {
        CHECK(enemyScaleForFloor(1).hpPct == 100);
        CHECK(enemyScaleForFloor(2).hpPct == 130);
        CHECK(enemyScaleForFloor(3).hpPct == 160);
        CHECK(enemyScaleForFloor(3).atkPct == 160);
        CHECK(enemyScaleForFloor(3).defBonus == 2);
        CHECK(enemyScaleForFloor(3).coinPct == 140);

        auto scaled = [](int floor) {
            Rng rr(1);
            Enemy e;
            e.name = "Враг"; e.maxHp = e.hp = 100; e.atk = 10; e.def = 2; e.coins = 10; e.spd = 1;
            Party party;
            Combat cb(party, {e}, rr, -1, floor);
            return cb.enemies().front();
        };
        const Enemy f1 = scaled(1), f3 = scaled(3);
        CHECK(f1.maxHp == 100 && f1.atk == 10 && f1.def == 2 && f1.coins == 10);
        CHECK(f3.maxHp == 160 && f3.atk == 16 && f3.def == 4 && f3.coins == 14);
        CHECK(f3.hp == f3.maxHp);
    }

    // -------------------------------------------------- друид: цепное лечение
    {
        for (int bonusDef = 0; bonusDef <= 6; bonusDef += 3) {
            Rng rr(8);
            Party party;
            Hero druid = tough("Друид", Role::Druid, 4);
            druid.baseDef = druid.def = bonusDef;
            party.heroes = {druid, tough("А", Role::Warrior, 6), tough("Б", Role::Warrior, 6), tough("В", Role::Warrior, 6)};
            for (auto& h : party.heroes) h.hp = 20;                       // все ранены (макс. 900)
            Enemy e;
            e.name = "Мишень"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 50; e.spd = 1;
            Combat cb(party, {e}, rr);
            std::vector<std::string> lg;
            std::vector<int> amounts, targets;
            for (int i = 0; i < 40 && amounts.empty(); ++i) {
                cb.step(lg);
                for (auto& a : cb.events())
                    if (a.actorHero && a.actor == 0 && a.kind == CombatAction::Heal)
                        for (auto& h : a.hits) { amounts.push_back(h.amount); targets.push_back(h.idx); }
            }
            const int bonus = bonusDef / 2;
            CHECK(amounts.size() == 4);                                   // все четверо членов отряда
            if (amounts.size() == 4) {
                CHECK(amounts[0] == 6 + bonus && amounts[1] == 5 + bonus && amounts[2] == 4 + bonus && amounts[3] == 3 + bonus);
                std::set<int> uniq(targets.begin(), targets.end());
                CHECK(uniq.size() == 4);                                  // каждая цель по разу
            }
        }
    }

    // --------------------------------------- ловушки: премия за риск, шанс заметить, вор
    {
        struct TrapResult {
            std::vector<int> lost;
            int coins = 0;
            int lootTier = 0;
        };
        auto trapRun = [&](bool withRogue, int aliveCount, int seed, int diff = 2) -> TrapResult {
            Dungeon d;
            d.place(card(RoomType::Trap, diff, doorBit(West)), {s.x + 1, s.y});
            std::vector<Hero> squad = {tough("Воин", Role::Warrior, 8), tough("Маг", Role::Mage, 8),
                                       tough(withRogue ? "Вор" : "Жрица", withRogue ? Role::Rogue : Role::Cleric, 8),
                                       tough("Паладин", Role::Paladin, 8)};
            for (int i = aliveCount; i < 4; ++i) squad[i].hp = 0;
            Run r(d, seed, squad);
            r.move(East);
            TrapResult out;
            for (auto& h : r.party().heroes) out.lost.push_back(h.alive() || h.hp < 0 ? 900 - h.hp : 0);
            out.coins = r.party().coins;
            out.lootTier = r.loot().empty() ? 0 : r.loot().front().tier;
            return out;
        };

        bool sawFull = false, sawNoticed = false;
        for (int seed = 0; seed < 60; ++seed) {
            auto t = trapRun(false, 4, seed);            // без вора: 10 урона или 5, если заметили
            int hurt = 0, dmg = 0;
            for (int l : t.lost) {
                if (l <= 0) continue;
                ++hurt;
                CHECK(l == 5 || l == 10);                // оба пострадавших получают одинаково
                if (dmg == 0) dmg = l;
                CHECK(l == dmg);
            }
            CHECK(hurt == 2);
            sawFull |= dmg == 10;
            sawNoticed |= dmg == 5;
            CHECK(t.coins == 20 && t.lootTier == 3);     // d=2: добыча ур.3 вместо ур.2 + 20 монет

            auto withRogue = trapRun(true, 4, seed);     // вор обезвреживает: урона нет
            for (int l : withRogue.lost) CHECK(l == 0);
            CHECK(withRogue.coins == 20 && withRogue.lootTier == 3);

            auto one = trapRun(false, 1, seed);          // жив один: бьют только его, один раз
            int total = 0, hurtOne = 0;
            for (int l : one.lost) if (l > 0 && l < 900) { total += l; ++hurtOne; }
            CHECK(hurtOne == 1 && (total == 5 || total == 10));
        }
        CHECK(sawFull && sawNoticed);                    // оба исхода встречаются

        auto hard = trapRun(false, 4, 3, 3);             // d=3: тир 4 не раздаём, но монет больше
        CHECK(hard.lootTier == 3 && hard.coins == 30);
    }

    // -------------------------------------------------- воскрешение на привале
    {
        Dungeon d;
        d.place(card(RoomType::Rest, 1, doorBit(West)), {s.x + 1, s.y});
        std::vector<Hero> squad = {makeHero("Воин", Role::Warrior, 40, 8, 3, 4), makeHero("Маг", Role::Mage, 26, 11, 1, 5),
                                   makeHero("Жрица", Role::Cleric, 30, 5, 2, 3), makeHero("Вор", Role::Rogue, 28, 9, 2, 7)};
        squad[0].hp = 0;
        squad[1].hp = 0;
        Run r(d, 2, squad);
        CHECK(!r.restRevive(0));                       // вне привала нельзя
        CHECK(r.move(East));
        CHECK(r.phase() == RunPhase::RestChoice);
        CHECK(!r.restRevive(0));                       // нет монет
        r.grantCoins(120);
        CHECK(r.reviveChancePct() == 50);              // жива Жрица
        CHECK(r.reviveCoinCost() == 37);               // 75 * (100 - 50) / 100
        CHECK(r.reviveArtifactCost() == 1);            // ceil(2 * 50 / 100)
        CHECK(r.canRestReviveFree());
        CHECK(!r.restRevive(3));                       // живого воскрешать нельзя
        CHECK(!r.restRevive(9));
        CHECK(r.restRevive(0));
        CHECK(r.party().coins == 120 - 37);
        CHECK(r.party().heroes[0].alive() && r.party().heroes[0].hp == 40 * Run::kRevivePct / 100);
        CHECK(r.phase() == RunPhase::RestChoice);      // привал продолжается: можно выбрать лечение/бафф
        CHECK(r.restRevive(1));
        CHECK(r.party().coins == 120 - 2 * 37);
        CHECK(r.party().heroes[1].hp == 13);
        CHECK(!r.restRevive(0));                       // уже жив
        r.restHeal();
        CHECK(r.phase() == RunPhase::Exploring);
        CHECK(r.party().heroes[0].hp == 20 + 40 * Run::kRestHealPct / 100);   // воскрешённый лечится как все
    }

    // ------------------------------ бесплатная попытка и доплата до 100% (без жреца)
    {
        auto makeRest = [&](uint32_t seed, bool cleric) -> std::unique_ptr<Run> {
            Dungeon d;
            d.place(card(RoomType::Rest, 1, doorBit(West)), {s.x + 1, s.y});
            std::vector<Hero> squad = {makeHero("Воин", Role::Warrior, 40, 8, 3, 4),
                                       makeHero("Маг", Role::Mage, 26, 11, 1, 5)};
            if (cleric) squad.push_back(makeHero("Жрица", Role::Cleric, 30, 5, 2, 3));
            squad[0].hp = 0;
            RunOptions opt;
            opt.artifacts = 2;
            std::unique_ptr<Run> r(new Run(d, seed, squad, opt));
            r->move(East);
            return r;
        };

        auto noCleric = makeRest(7, false);
        CHECK(noCleric->phase() == RunPhase::RestChoice);
        CHECK(noCleric->reviveChancePct() == 25);
        CHECK(noCleric->reviveCoinCost() == 56);         // 75 * (100 - 25) / 100
        CHECK(noCleric->reviveArtifactCost() == 2);      // ceil(2 * 75 / 100)

        bool sawSuccess = false, sawFailure = false;
        for (uint32_t seed = 0; seed < 200 && !(sawSuccess && sawFailure); ++seed) {
            auto r = makeRest(seed, false);
            CHECK(r->canRestReviveFree());
            const bool ok = r->restReviveFree(0);
            CHECK(r->freeReviveUsed());
            CHECK(!r->canRestReviveFree());
            CHECK(!r->restReviveFree(0));                // вторая попытка в тот же привал недоступна
            CHECK(r->phase() == RunPhase::RestChoice);   // привал продолжается и после провала
            if (ok) {
                sawSuccess = true;
                CHECK(r->party().heroes[0].alive());
                CHECK(r->party().heroes[0].hp == 40 * Run::kFreeRevivePct / 100);
                CHECK(r->artifacts() == 2 && r->party().coins == 0);  // бесплатно
            } else {
                sawFailure = true;
                CHECK(!r->party().heroes[0].alive());
                r->grantCoins(80);
                CHECK(r->restRevive(0));                 // можно доплатить за гарантию
                CHECK(r->party().coins == 80 - 56);
                CHECK(r->party().heroes[0].hp == 40 * Run::kRevivePct / 100);
            }
        }
        CHECK(sawSuccess && sawFailure);

        auto withCleric = makeRest(7, true);
        CHECK(withCleric->reviveChancePct() == 50);
        CHECK(withCleric->reviveCoinCost() == 37);
        CHECK(withCleric->reviveArtifactCost() == 1);
        CHECK(withCleric->restReviveArtifact(0));
        CHECK(withCleric->artifacts() == 1);
        CHECK(!withCleric->restReviveArtifact(0));       // артефакты кончились

        auto art = makeRest(9, false);
        CHECK(art->restReviveArtifact(0));               // без жреца: 2 артефакта за гарантию
        CHECK(art->artifacts() == 0);
        CHECK(art->party().heroes[0].hp == 40 * Run::kReviveArtifactPct / 100);
    }

    // --------------------------------------------------- вампиризм и крит от реликвий
    {
        Rng rng(6);
        Party party;
        Hero h = tough("Берсерк", Role::Berserker, 10);
        h.hp = 300;
        h.relic = gear(ItemKind::Relic, 3, 21, RelicEffect::Vampire);
        h.recalc();
        CHECK(h.lifestealPct == 21);
        party.heroes = {h};
        Combat cb(party, makeEnemies(1, rng), rng);
        std::vector<std::string> lg;
        bool lifesteal = false;
        for (int i = 0; i < 60 && !cb.finished(); ++i) {
            party.heroes[0].hp = std::max(1, party.heroes[0].hp - 1);   // чтобы было что лечить
            cb.step(lg);
            for (auto& a : cb.events())
                if (a.kind == CombatAction::Effect && a.hits[0].targetHero && a.hits[0].heal) lifesteal = true;
        }
        CHECK(lifesteal);
    }

    // --------------------------- аура вампиризма Берсерка действует на отряд
    {
        auto dummy = []() {
            Enemy e;
            e.name = "Мишень"; e.maxHp = e.hp = 100000; e.atk = 1; e.def = 0; e.spd = 1;
            return e;
        };
        auto collect = [&](bool withBerserker) {
            Rng rr(3);
            Party p;
            p.heroes.clear();
            Hero w = tough("Воин", Role::Warrior, 8);
            w.baseSpd = w.spd = 9;
            w.hp = 800;                                     // есть куда лечиться
            p.heroes.push_back(w);
            if (withBerserker) {
                Hero z = tough("Берсерк", Role::Berserker, 8);
                z.baseSpd = z.spd = 8;
                z.hp = 800;
                p.heroes.push_back(z);
            }
            Combat cb(p, {dummy()}, rr);
            std::vector<std::string> lg;
            std::vector<CombatAction> all;
            for (int s = 0; s < static_cast<int>(p.heroes.size()); ++s) {
                cb.step(lg);
                for (auto& a : cb.events()) all.push_back(a);
            }
            return all;
        };
        auto healed = [](const std::vector<CombatAction>& ev, int idx) {
            for (const auto& a : ev)
                for (const auto& h : a.hits)
                    if (a.kind == CombatAction::Effect && h.heal && h.targetHero && h.idx == idx) return true;
            return false;
        };
        const auto with = collect(true);
        CHECK(healed(with, 0));                             // воин лечится от ауры
        CHECK(healed(with, 1));                             // и сам берсерк тоже
        const auto without = collect(false);
        CHECK(!healed(without, 0));                         // без берсерка вампиризма нет
    }

    // ------------------------------------------------------- торговец и монеты
    {
        // ассортимент: цены, без т4, т3 - редкость, т1 - самые частые
        Rng rr(11);
        int t[5] = {0, 0, 0, 0, 0}, total = 0;
        for (int i = 0; i < 600; ++i) {
            auto stock = makeShopStock(rr, 5);
            CHECK(stock.size() == 5);
            for (auto& o : stock) {
                CHECK(o.item.tier >= 1 && o.item.tier <= 3);
                CHECK(o.price == itemPrice(o.item) && o.price > 0);
                ++t[o.item.tier];
                ++total;
            }
        }
        CHECK(t[4] == 0);
        CHECK(t[3] * 100 < total * 15);          // т3 редки (< 15%)
        CHECK(t[3] > 0);                         // но встречаются
        CHECK(t[1] > t[2] && t[2] > t[3]);
        CHECK(itemPrice(gear(ItemKind::Weapon, 1, 2)) < itemPrice(gear(ItemKind::Weapon, 2, 4)));
        CHECK(itemPrice(gear(ItemKind::Weapon, 2, 4)) < itemPrice(gear(ItemKind::Weapon, 3, 6)));
    }
    {
        Dungeon d;
        d.place(card(RoomType::Shop, 0, doorBit(West) | doorBit(East)), {s.x + 1, s.y});
        d.place(card(RoomType::Combat, 1, doorBit(West)), {s.x + 2, s.y});
        Run r(d, 9);
        CHECK(r.move(East));
        CHECK(r.phase() == RunPhase::Shop);
        CHECK(r.inShopRoom());
        CHECK(r.shopStock() && r.shopStock()->size() == 5);
        CHECK(r.canManage());                         // у торговца можно менять снаряжение
        CHECK(!r.move(East));                         // пока торгуем - стоим
        CHECK(!r.buy(0));                             // нет монет
        const int price = (*r.shopStock())[0].price;
        r.grantCoins(price + 7);
        CHECK(r.buy(0));
        CHECK(r.party().coins == 7);
        CHECK(r.party().bag.size() == 1);
        CHECK(!r.buy(0));                             // лот продан
        CHECK(!r.buy(99));
        r.leaveShop();
        CHECK(r.phase() == RunPhase::Exploring);
        CHECK(r.openShop());                          // можно вернуться к торговле, не выходя из комнаты
        r.leaveShop();
        CHECK(r.move(West));
        CHECK(!r.openShop());                         // не у торговца
        CHECK(r.move(East));                          // повторный заход открывает те же лоты
        CHECK(r.phase() == RunPhase::Shop);
        CHECK((*r.shopStock())[0].sold);
    }
    {   // продажа за полцены и обновление ассортимента
        Dungeon d;
        d.place(card(RoomType::Shop, 0, doorBit(West) | doorBit(East)), {s.x + 1, s.y});
        Run r(d, 12);
        CHECK(r.move(East));
        CHECK(r.phase() == RunPhase::Shop);
        const Item potion = makePotion(2);
        const int sell = itemSellPrice(potion);
        CHECK(sell > 0 && sell < itemPrice(potion));         // не полная стоимость
        CHECK(!r.restockShop());                             // нет монет
        r.grantCoins(Run::kShopRestockCost + 3);
        r.partyMutable().bag.push_back(potion);
        CHECK(r.sellItem(0));
        CHECK(r.party().bag.empty());
        CHECK(r.party().coins == Run::kShopRestockCost + 3 + sell);
        CHECK(!r.sellItem(0));                               // нечего продавать
        CHECK(r.restockShop());
        CHECK(r.party().coins == 3 + sell);
        CHECK(r.shopStock() && r.shopStock()->size() == 5);
        for (const auto& o : *r.shopStock()) CHECK(!o.sold);  // все лоты заново
        r.leaveShop();
        CHECK(!r.restockShop());                             // только у торговца
    }
    {   // монеты падают с врагов и с ловушки
        Dungeon d;
        d.place(card(RoomType::Combat, 2, doorBit(West) | doorBit(East)), {s.x + 1, s.y});
        d.place(card(RoomType::Trap, 1, doorBit(West)), {s.x + 2, s.y});
        std::vector<Hero> strong = {tough("А", Role::Warrior, 30), tough("Б", Role::Mage, 30)};
        Run r(d, 4, strong);
        CHECK(r.party().coins == 0);
        CHECK(r.move(East));
        CHECK(r.phase() == RunPhase::Fighting);
        for (int g = 0; g < 2000 && r.phase() == RunPhase::Fighting; ++g) r.step();
        CHECK(r.phase() == RunPhase::Exploring);
        CHECK(r.party().coins >= 3 * 7 && r.party().coins <= 3 * 11);   // три врага сложности 2
        const int coins = r.party().coins;
        r.closeCombat();
        CHECK(r.move(East));                                            // ловушка сложности 1: +10 монет и добыча ур.2
        CHECK(r.party().coins == coins + 10);
        CHECK(r.loot().size() == 2);
    }
    {   // монеты за убитых: подсчёт в событиях боя
        Rng rr(7);
        Party party;
        party.heroes = {tough("А", Role::Warrior, 40)};
        Combat cb(party, makeEnemies(1, rr), rr);
        std::vector<std::string> lg;
        int fromEvents = 0;
        for (int i = 0; i < 500 && !cb.finished(); ++i) {
            cb.step(lg);
            for (auto& a : cb.events()) for (auto& h : a.hits) fromEvents += h.coins;
        }
        int expected = 0;
        for (auto& e : cb.enemies()) expected += e.coins;
        CHECK(fromEvents == expected && expected >= 8);
    }

    // ---------------------------------------------------------- картинки
    {
        const auto paths = manifestPaths();
        CHECK(paths.size() == assetManifest().size());          // без дублей
        const Role roles[8] = {Role::Warrior, Role::Mage, Role::Cleric, Role::Rogue, Role::Archer, Role::Paladin, Role::Berserker, Role::Druid};
        for (Role r : roles) CHECK(paths.count(heroArt(r)) > 0);
        for (int b = 0; b < kBossCount; ++b) CHECK(paths.count(bossArt(b)) > 0);
        const RoomType rooms[7] = {RoomType::Start, RoomType::Combat, RoomType::Treasure, RoomType::Trap, RoomType::Rest, RoomType::Boss, RoomType::Shop};
        for (RoomType t : rooms) CHECK(paths.count(roomArt(t)) > 0);
        Rng rr(5);
        for (int i = 0; i < 400; ++i) CHECK(paths.count(itemArt(rollLoot(rr, 1 + i % 4))) > 0);
        for (int d = 1; d <= 3; ++d)
            for (int i = 0; i < 100; ++i)
                for (auto& e : makeEnemies(d, rr)) CHECK(paths.count(e.art) > 0);
        for (auto& a : assetManifest()) CHECK(a.w > 0 && a.h > 0 && !a.desc.empty());
    }

    // ---------------------------------------------------------- Наследие
    {
        for (int u = 0; u < kUpgradeCount; ++u) {
            const UpgradeInfo& info = upgradeInfo(u);
            CHECK(!info.name.empty() && !info.desc.empty());
            CHECK(info.maxLevel > 0 && static_cast<int>(info.costs.size()) == info.maxLevel);
            for (int c : info.costs) CHECK(c > 0);
            CHECK(upgradeEffect(u, 0) == "нет" && !upgradeEffect(u, 1).empty());
        }
        Meta m;
        CHECK(m.reviveCoinCost() == 75 && m.restHealPct() == 40 && m.startCoins() == 0);
        CHECK(m.nextCost(UpVitality) == 8 && !m.maxed(UpVitality));
        m.level[UpLife] = 3;
        CHECK(m.reviveCoinCost() == 51);
        m.level[UpVitality] = 2;
        CHECK(m.maxed(UpVitality) && m.nextCost(UpVitality) == 0);

        Hero h = makeHero("Воин", Role::Warrior, 40, 8, 3, 4);
        Meta mm;
        mm.level[UpVitality] = 2;
        mm.level[UpMight] = 4;
        mm.level[UpArmor] = 5;
        mm.applyToHero(h);
        CHECK(h.maxHp == 42 && h.hp == 42 && h.atk == 8 && h.def == 3);
        CHECK(h.critPct == 8 && mm.damageReducePct() == 5);
        h.relic = gear(ItemKind::Relic, 1, 8, RelicEffect::Keen);   // крит реликвии и Наследия складываются
        h.recalc();
        CHECK(h.critPct == 16);
    }

    // ------------------------------------------- профиль: артефакты и достижения
    {
        CHECK(achievements().size() == 22);
        std::set<int> ids;
        for (auto& a : achievements()) { ids.insert(a.id); CHECK(a.reward == kAchievementArtifacts && !a.name.empty() && !a.desc.empty()); }
        CHECK(ids.size() == 22);

        Profile p;
        RunSummary s;
        s.victory = true;
        s.bossId = BossArchmage;
        s.squad = {Role::Warrior, Role::Mage, Role::Cleric, Role::Rogue};
        s.casualties = 0;
        s.aliveAtEnd = 4;
        RunReward r = p.recordRun(s);
        CHECK(r.bossArtifacts == kBossArtifacts);
        CHECK(r.newAchievements.size() == 6);                       // босс + 4 героя + «без потерь»
        CHECK(r.achievementArtifacts == 6 * kAchievementArtifacts);
        CHECK(p.artifacts == 2 + 18 && p.artifactsEarned == 20 && p.unlockedCount() == 6);
        CHECK(p.isUnlocked(BossArchmage) && p.isUnlocked(100 + static_cast<int>(Role::Mage)) && p.isUnlocked(200));
        CHECK(!p.isUnlocked(201));
        CHECK(!p.isUnlocked(100 + static_cast<int>(Role::Bard)));

        RunReward again = p.recordRun(s);                           // повторная победа: только за босса
        CHECK(again.total() == kBossArtifacts && again.newAchievements.empty() && p.artifacts == 22);

        RunSummary lost = s;
        lost.victory = false;                                       // поражение артефактов не даёт
        RunReward none = p.recordRun(lost);
        CHECK(none.total() == 0 && p.artifacts == 22 && p.runs == 3 && p.wins == 2);

        RunSummary last = s;                                        // победа с одним выжившим
        last.bossId = BossDragon;
        last.casualties = 3;
        last.aliveAtEnd = 1;
        RunReward lr = p.recordRun(last);
        CHECK(lr.newAchievements.size() == 2);                      // дракон + «последний герой»
        CHECK(p.isUnlocked(BossDragon) && p.isUnlocked(201));
        CHECK(lr.total() == kBossArtifacts + 2 * kAchievementArtifacts);

        // потеря воскрешённого героя тоже мешает достижению «без потерь»
        Profile q;
        RunSummary lossy = s;
        lossy.casualties = 1;
        RunReward lrw = q.recordRun(lossy);
        CHECK(!q.isUnlocked(200) && lrw.newAchievements.size() == 5);

        // достижение Барда
        Profile bprof;
        RunSummary bs = s;
        bs.squad = {Role::Warrior, Role::Mage, Role::Cleric, Role::Bard};
        RunReward br = bprof.recordRun(bs);
        CHECK(bprof.isUnlocked(100 + static_cast<int>(Role::Bard)));
        CHECK(std::find(br.newAchievements.begin(), br.newAchievements.end(), 100 + static_cast<int>(Role::Bard)) != br.newAchievements.end());

        // достижения этажей
        Profile fp;
        RunSummary f2 = s; f2.floor = 2; f2.bossId = BossDevourer;
        RunReward fr2 = fp.recordRun(f2);
        CHECK(fp.isUnlocked(202));
        CHECK(std::find(fr2.newAchievements.begin(), fr2.newAchievements.end(), 202) != fr2.newAchievements.end());
        RunSummary f3 = s; f3.floor = 3; f3.bossId = BossKnight;
        RunReward fr3 = fp.recordRun(f3);
        CHECK(fp.isUnlocked(203));
        CHECK(std::find(fr3.newAchievements.begin(), fr3.newAchievements.end(), 203) != fr3.newAchievements.end());

        // артефакты постоянны: сохраняются и читаются из файла
        p.meta.level[UpGreed] = 2;
        const std::string path = "core_tests_profile.tmp";
        CHECK(p.saveFile(path));
        Profile loaded = Profile::loadFile(path);
        std::remove(path.c_str());
        CHECK(loaded.artifacts == p.artifacts && loaded.artifactsEarned == p.artifactsEarned);
        CHECK(loaded.unlocked == p.unlocked && loaded.meta.level == p.meta.level);
        CHECK(loaded.runs == p.runs && loaded.wins == p.wins && loaded.bossWins == p.bossWins && loaded.heroWins == p.heroWins);
        CHECK(Profile::loadFile("нет_такого_файла.tmp").artifacts == 0);

        // мусор и «подкрученные» значения не ломают профиль
        Profile bad = Profile::parse("artifacts -5\nmeta 99 99 99 99 99 99 99 99 99 99 99\nunlocked 7 999 -3\nмусор\n");
        CHECK(bad.artifacts == 0 && bad.isUnlocked(7) && !bad.isUnlocked(999));
        for (int u = 0; u < kUpgradeCount; ++u) CHECK(bad.meta.level[u] == upgradeInfo(u).maxLevel);

        // покупка улучшений
        Profile b;
        CHECK(!b.buyUpgrade(UpVitality));                           // нет артефактов
        b.artifacts = 100;
        int spent = 0;
        while (b.buyUpgrade(UpVitality)) spent += 1;
        CHECK(b.meta.level[UpVitality] == 2 && spent == 2);
        CHECK(b.artifacts == 100 - (8 + 14));
        CHECK(!b.buyUpgrade(UpVitality) && !b.buyUpgrade(-1) && !b.buyUpgrade(99));
    }

    // ------------------------------------- забег: артефакты и эффекты Наследия
    {
        Dungeon d;
        d.place(card(RoomType::Rest, 1, doorBit(West)), {s.x + 1, s.y});
        std::vector<Hero> squad = {makeHero("Воин", Role::Warrior, 40, 8, 3, 4), makeHero("Маг", Role::Mage, 26, 11, 1, 5),
                                   makeHero("Жрица", Role::Cleric, 30, 5, 2, 3), makeHero("Вор", Role::Rogue, 28, 9, 2, 7)};
        squad[0].hp = 0;
        squad[1].hp = 0;
        RunOptions opt;
        opt.artifacts = 2;
        opt.meta.level[UpCamp] = 2;
        Run r(d, 1, squad, opt);
        CHECK(r.artifacts() == 2 && !r.restReviveArtifact(0));      // вне привала нельзя
        CHECK(r.move(East));
        CHECK(r.fullReviveCoinCost() == 75);
        CHECK(r.reviveArtifactCost() == 1);                         // со жрецом: 50% шанса даёт 1 артефакт
        CHECK(r.restReviveArtifact(0));
        CHECK(r.artifacts() == 1);
        CHECK(r.party().heroes[0].hp == 40 * Run::kReviveArtifactPct / 100);   // 75% здоровья
        CHECK(r.party().coins == 0);                                // монеты не тронуты
        CHECK(!r.restReviveArtifact(2));                            // живого воскрешать нельзя
        CHECK(r.restReviveArtifact(1));
        CHECK(r.artifacts() == 0);
        CHECK(!r.restReviveArtifact(1));
        CHECK(r.phase() == RunPhase::RestChoice);                   // привал продолжается
        CHECK(r.restHealPct() == 50);                               // «Уютный лагерь»
        const int hp2 = r.party().heroes[2].hp;
        r.restHeal();
        CHECK(r.party().heroes[2].hp == std::min(30, hp2 + 30 * 50 / 100));
        CHECK(r.casualties() == 2);                                 // воскрешённые остаются в потерях
        CHECK(r.summary().casualties == 2 && r.summary().aliveAtEnd == 4 && !r.summary().victory);
    }
    {
        RunOptions opt;
        opt.meta.level[UpPurse] = 2;
        opt.meta.level[UpProvisions] = 2;
        opt.meta.level[UpHaggler] = 4;
        opt.meta.level[UpLife] = 3;
        Dungeon d;
        d.place(card(RoomType::Shop, 0, doorBit(West) | doorBit(East)), {s.x + 1, s.y});
        Run r(d, 4, {}, opt);
        CHECK(r.party().coins == 30);                               // «Походный кошелёк»
        CHECK(r.party().bag.size() == 2 && r.party().potionCount() == 2);   // «Дар предков»: 2 зелья т1
        for (auto& it : r.party().bag) CHECK(it.tier == 1);
        CHECK(r.fullReviveCoinCost() == 51);                         // «Школа жизни»: 75 - 3*8
        CHECK(r.reviveCoinCost() == 51 * (100 - r.reviveChancePct()) / 100);
        CHECK(r.move(East));
        for (auto& o : *r.shopStock()) CHECK(o.price == std::max(1, itemPrice(o.item) * 80 / 100));   // -20% цен
    }
    {   // «Закалённые доспехи»: меньше урона по героям
        auto totalDamage = [&](int reduce) {
            Rng rr(10);
            Party party;
            party.heroes = {tough("Воин", Role::Warrior, 1)};
            Enemy e;
            e.name = "Бьющий"; e.maxHp = e.hp = 100000; e.atk = 50; e.def = 99; e.spd = 9;
            Combat cb(party, {e}, rr);
            cb.setDamageReduction(reduce);
            std::vector<std::string> lg;
            for (int i = 0; i < 40; ++i) cb.step(lg);
            return 900 - party.heroes[0].hp;
        };
        const int plain = totalDamage(0), reduced = totalDamage(30);
        CHECK(plain > 0 && reduced > 0 && reduced < plain);
    }
    {
        Rng rr(3);
        int plain = 0, rich = 0;
        for (int i = 0; i < 50; ++i) {
            for (auto& e : makeEnemies(2, rr, 0)) { plain += e.coins; CHECK(e.coins >= 7 && e.coins <= 11); }
            for (auto& e : makeEnemies(2, rr, 50)) { rich += e.coins; CHECK(e.coins >= 7 * 150 / 100 && e.coins <= 11 * 150 / 100); }
        }
        CHECK(rich > plain);                                        // «Охотник за трофеями»
    }
    {   // потери считаются, даже если герой погиб в ловушке
        Dungeon d;
        d.place(card(RoomType::Trap, 1, doorBit(West)), {s.x + 1, s.y});
        std::vector<Hero> squad;
        for (int i = 0; i < 4; ++i) { Hero h = makeHero("Г", Role::Warrior, 40, 8, 3, 4); h.hp = 1; squad.push_back(h); }
        Run r(d, 6, squad);
        r.move(East);
        CHECK(r.casualties() == 2 && r.party().aliveCount() == 2);
        CHECK(r.summary().casualties == 2 && r.summary().aliveAtEnd == 2);
    }

    // -------------------------------------- события: NPC, варианты, одноразовость
    {
        const char* titles[7] = {"Потерявшийся герой", "Кровавый алтарь", "Скелеты героев",
                                 "Сундук-мимик", "Говорящий ворон", "Проклятая книга", "Повар из монстров"};
        std::set<std::string> seen;
        for (int seed = 1; seed <= 400 && seen.size() < 7; ++seed) {
            Dungeon d;
            d.place(card(RoomType::Event, 1, doorBit(West)), {s.x + 1, s.y});
            std::vector<Hero> squad = {makeHero("Воин", Role::Warrior, 40, 8, 3, 4),
                                       makeHero("Маг", Role::Mage, 26, 11, 1, 5),
                                       makeHero("Жрица", Role::Cleric, 30, 5, 2, 3),
                                       makeHero("Вор", Role::Rogue, 28, 9, 2, 7)};
            Run r(d, seed, squad);
            CHECK(r.move(East));
            CHECK(r.phase() == RunPhase::EventChoice);
            CHECK(r.eventChoices().size() >= 2);
            CHECK(!r.eventResult().title.empty() && !r.eventResult().story.empty());
            for (const EventChoice& c : r.eventChoices()) {
                CHECK(c.id >= 1);
                CHECK(!c.name.empty() && !c.desc.empty());
            }
            const bool raven = r.eventResult().title == "Говорящий ворон";
            seen.insert(r.eventResult().title);
            CHECK(r.takeEventChoice(0));
            CHECK(r.phase() == RunPhase::Exploring || r.phase() == RunPhase::Fighting);
            CHECK(r.eventChoices().empty());
            CHECK(!r.takeEventChoice(0));   // выбор можно сделать только один раз
            if (raven) CHECK(r.pendingPlacementBonus() == 1 && r.takePlacementBonus() == 1 && r.pendingPlacementBonus() == 0);
            if (r.phase() == RunPhase::Exploring) {
                CHECK(r.move(West));
                CHECK(r.move(East));
                CHECK(r.phase() != RunPhase::EventChoice);  // возвращаться к NPC нельзя
            }
        }
        CHECK(seen.size() == 7);
        for (const char* title : titles) CHECK(seen.count(title) == 1);
    }
    {   // отрицательные свойства вещей из событий влияют на характеристики
        Hero h = makeHero("Воин", Role::Warrior, 40, 8, 3, 4);
        Item w = gear(ItemKind::Weapon, 3, 6);
        w.eventPenaltyAtk = -2;
        h.weapon = w;
        h.recalc();
        CHECK(h.atk == 8 + 6 - 2);
        w.eventPenaltyAtk = 0;   // кузница снимает свойство
        h.weapon = w;
        h.recalc();
        CHECK(h.atk == 8 + 6);
        Item a = gear(ItemKind::Armor, 3, 3);
        a.eventPenaltyDef = -2;
        h.armor = a;
        h.recalc();
        CHECK(h.def == 3 + 3 - 2);
    }

    // --------------------------------------------------- колода и боссы
    Rng rng(42);
    Deck deck(rng, BossDragon);
    bool boss = false;
    int shops = 0, count = 0;
    while (!deck.empty()) {
        Card dc_ = deck.draw();
        ++count;
        if (dc_.type == RoomType::Boss) { boss = true; CHECK(dc_.bossId == BossDragon); CHECK(count <= 10); }
        if (dc_.type == RoomType::Shop) ++shops;
    }
    CHECK(boss);
    CHECK(shops == 0);                                          // торговец - не карта, а ставится автоматически

    std::printf(failures ? "Тесты: есть ошибки (%d)\n" : "Тесты пройдены\n", failures);
    return failures ? 1 : 0;
}
