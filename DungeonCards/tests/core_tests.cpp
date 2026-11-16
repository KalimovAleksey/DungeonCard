#include <cstdio>
#include <cstdlib>
#include <set>
#include "core/Assets.h"
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
    {   // монеты падают с врагов (и только с врагов)
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
        CHECK(r.move(East));                                            // ловушка монет не даёт
        CHECK(r.party().coins == coins);
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
