#include "Run.h"
#include "Util.h"

namespace dc {

Run::Run(const Dungeon& dungeon, uint32_t seed, std::vector<Hero> heroes, RunOptions options)
    : dungeon_(dungeon),
      party_(heroes.empty() ? Party() : Party(std::move(heroes))),
      rng_(seed),
      pos_(dungeon.startPos()),
      meta_(options.meta),
      artifacts_(std::max(0, options.artifacts)) {
    say("Отряд входит в подземелье. WASD - идти, I - инвентарь.");
    for (const auto& h : party_.heroes) squad_.push_back(h.role);
    died_.assign(party_.heroes.size(), false);

    // Наследие: стартовые монеты и зелья («Дар предков»: 1 или 2 зелья).
    party_.coins = meta_.startCoins();
    const int gifts = meta_.provisions();
    for (int i = 0; i < std::min(2, gifts); ++i) party_.bag.push_back(makePotion(1));
    if (gifts > 0) say(fmt("Дар предков: в рюкзаке %d зелья.", std::min(2, gifts)));
    updateLosses();
}

Run::Run(const Dungeon& dungeon, uint32_t seed, Party party, int floor, RunOptions options)
    : dungeon_(dungeon), party_(std::move(party)), rng_(seed), pos_(dungeon.startPos()),
      meta_(options.meta), artifacts_(std::max(0, options.artifacts)), floor_(std::max(1, floor)) {
    for (const auto& h : party_.heroes) squad_.push_back(h.role);
    died_.assign(party_.heroes.size(), false);
    if (floor_ == 1) {
        party_.coins += meta_.startCoins();
        const int gifts = meta_.provisions();
        for (int i = 0; i < std::min(2, gifts); ++i) party_.bag.push_back(makePotion(1));
    }
    say(fmt("Этаж %d. Отряд входит в подземелье.", floor_));
    updateLosses();
}

// Фиксируем каждого, кто хоть раз пал (воскрешённый остаётся в списке потерь).
void Run::updateLosses() {
    for (size_t i = 0; i < party_.heroes.size() && i < died_.size(); ++i)
        if (!party_.heroes[i].alive()) died_[i] = true;
}

int Run::casualties() const {
    int n = 0;
    for (bool d : died_) n += d ? 1 : 0;
    return n;
}

RunSummary Run::summary() const {
    RunSummary s;
    s.victory = phase_ == RunPhase::Victory;
    s.bossId = fightBoss_;
    s.floor = floor_;
    s.squad = squad_;
    s.casualties = casualties();
    s.aliveAtEnd = party_.aliveCount();
    s.bossRewarded = bossRewardedRecorded_;
    return s;
}

void Run::say(const std::string& s) {
    log_.push_back(s);
    if (log_.size() > 400) log_.erase(log_.begin(), log_.begin() + 100);
}

// ------------------------------------------------------------- движение

bool Run::move(Dir d) {
    if (phase_ != RunPhase::Exploring || combat_) return false;
    if (!dungeon_.linked(pos_, d)) return false;
    pos_ = pos_ + dirDelta(d);
    enterRoom(pos_);
    return true;
}

void Run::enterRoom(Vec2i p) {
    Room* r = dungeon_.roomAt(p);
    if (!r) return;
    if (r->card.type == RoomType::Event) {  // Событие: встреча с NPC (одноразовое)
        if (r->eventUsed) {
            return;  // NPC одноразовый: повторный вход без нового знакомства.
        }
        r->eventUsed = true;
        r->visited = true;
        makeEventChoices();
        say(fmt("Событие: %s.", eventResult_.title.c_str()));
        phase_ = RunPhase::EventChoice;
        return;
    }
    if (r->card.type == RoomType::Shop) {  // к торговцу можно возвращаться
        if (!r->visited) {
            r->visited = true;
            stocks_[p] = makeShopStock(rng_, 5, meta_.shopDiscountPct());
            say("Торговец! Покупай вещи за монеты.");
        } else {
            say("Снова у торговца.");
        }
        phase_ = RunPhase::Shop;
        return;
    }
    if (r->card.type == RoomType::Forge) {  // в кузницу можно возвращаться
        if (!r->visited) {
            r->visited = true;
            say("Кузница! Выбери предмет в рюкзаке для улучшения.");
        } else {
            say("Снова у кузницы.");
        }
        phase_ = RunPhase::ForgeChoice;
        return;
    }
    if (r->visited) return;
    r->visited = true;
    const int d = r->card.difficulty;
    say(fmt("Вход: %s (сложность %d)", roomTypeName(r->card.type), d));

    switch (r->card.type) {
        case RoomType::Combat:
            startFight(makeEnemies(d, rng_, meta_.coinBonusPct()), d, -1);
            break;
        case RoomType::Treasure:
            if (d >= 2) {
                say("Сокровище охраняют!");
                startFight(makeEnemies(d - 1, rng_, meta_.coinBonusPct()), d, -1);
            } else {
                ++cleared_;
                giveLoot(d);
            }
            break;
        case RoomType::Trap:
            resolveTrap(d);
            break;
        case RoomType::Rest:
            phase_ = RunPhase::RestChoice;
            freeReviveUsed_ = false;
            say("Привал! Лечение группы или усиление двух героев?");
            break;
        case RoomType::Boss:
            pendingBoss_ = r->card.bossId >= 0 ? r->card.bossId : 0;
            phase_ = RunPhase::BossIntro;
            say(fmt("Логово босса: %s. Подготовь отряд!", bossInfo(pendingBoss_).name.c_str()));
            break;
        
        default:
            break;
    }
}
namespace {

std::vector<int> livingIndexes(const Party& p) {
    std::vector<int> out;
    for (size_t i = 0; i < p.heroes.size(); ++i)
        if (p.heroes[i].alive()) out.push_back(static_cast<int>(i));
    return out;
}

Item cursedRelic(Rng& rng, ItemKind kind, Role role, int penalty) {
    Item it = rollLootOfKind(rng, kind, 3);   // всегда 3 звезды
    it.name = std::string("Кость ") + roleTitle(role) + " (проклятая)";
    if (kind == ItemKind::Weapon) it.eventPenaltyAtk = penalty;
    else if (kind == ItemKind::Armor) it.eventPenaltyDef = penalty;
    return it;
}

}  // namespace

const std::vector<EventChoice>& Run::eventChoices() const { return eventChoices_; }

int Run::takePlacementBonus() {
    const int v = pendingPlacementBonus_;
    pendingPlacementBonus_ = 0;
    return v;
}

void Run::makeEventChoices() {
    eventChoices_.clear();
    eventResult_ = EventResult{};

    static const EventKind kinds[7] = {
        EventKind::LostHero, EventKind::BloodyAltar, EventKind::SkeletonRemains, EventKind::MimicChest,
        EventKind::TalkingRaven, EventKind::CursedBook, EventKind::MonsterChef,
    };
    const EventKind kind = kinds[rng_.range(0, 6)];
    eventResult_.kind = kind;

    auto add = [&](int param, const std::string& name, const std::string& desc) {
        EventChoice c;
        c.id = static_cast<int>(eventChoices_.size()) + 1;
        c.param = param;
        c.name = name;
        c.desc = desc;
        eventChoices_.push_back(c);
    };
    const std::vector<int> alive = livingIndexes(party_);

    switch (kind) {
        case EventKind::LostHero:
            eventResult_.title = "Потерявшийся герой";
            eventResult_.story = "У тропы сидит воин, отставший от своей группы. Он готов занять место любого из ваших.";
            for (int idx : alive)
                add(idx, "Заменить: " + party_.heroes[idx].name, "Найденный герой возьмёт его вещи и баффы.");
            if (alive.size() < 2) add(-1, "Оставить отряд", "Никого не менять.");
            add(-1, "Отказаться", "Пройти мимо, не меняя состав.");
            break;

        case EventKind::BloodyAltar:
            eventResult_.title = "Кровавый алтарь";
            eventResult_.story = "Камень требует крови. Живые герои заплатят частью здоровья.";
            add(0, "Пожертвовать кровью", "Каждый живой теряет до 15% ЗДР, но получает +2 АТК и +1 ЗАЩ.");
            add(1, "Забрать монеты с алтаря", "+15 монет без усиления.");
            break;

        case EventKind::SkeletonRemains:
            eventResult_.title = "Скелеты героев";
            eventResult_.story = "В нише лежат останки воинов. Их вещи сильны, но прокляты.";
            add(0, "Взять оружие", "Оружие 3 тира под героя, но -2 АТК. Снимается в кузнице за 5 монет.");
            add(1, "Взять доспех", "Броня 3 тира, но -2 ЗАЩ. Снимается в кузнице за 5 монет.");
            add(2, "Не трогать", "Оставить кости в покое.");
            break;

        case EventKind::MimicChest:
            eventResult_.title = "Сундук-мимик";
            eventResult_.story = "Ящик вдруг вздохнул и открыл зубастую пасть. Вор может усыпить его.";
            if (party_.findAlive(Role::Rogue))
                add(0, "Вор обезвреживает", "Без боя: добыча 3 тира и +20 монет.");
            add(1, "Открыть силой", "Бой с мимиком 3 звезды, добыча 3 тира.");
            add(2, "Обойти стороной", "Ни боя, ни добычи.");
            break;

        case EventKind::TalkingRaven:
            eventResult_.title = "Говорящий ворон";
            eventResult_.story = "Ворон каркает: «Покорми меня — подскажу, где поставить ещё одну комнату».";
            add(0, "Покормить ворона", "+1 размещение комнаты на следующем этаже.");
            add(1, "Уйти", "+10 монет мелочи с ветки.");
            break;

        case EventKind::CursedBook:
            eventResult_.title = "Проклятая книга";
            eventResult_.story = "Книга перелистывается сама. Знания даются легко, но память стирается.";
            add(0, "Прочесть вслух", "Всем живым +3 АТК и -2 ЗАЩ до конца забега.");
            add(1, "Забрать с собой", "+1 артефакт, но -10 монет.");
            add(2, "Закрыть и уйти", "Ничего не менять.");
            break;

        case EventKind::MonsterChef: {
            eventResult_.title = "Повар из монстров";
            eventResult_.story = "Повар помешивает похлёбку: «Вы слишком целы! Я обижусь!»";
            const int totalMax = party_.totalMaxHp();
            const int hpPct = totalMax > 0 ? party_.totalHp() * 100 / totalMax : 100;
            if (hpPct < 80) {
                add(0, "Съесть похлёбку", "Все живые лечатся на 30% макс. ЗДР.");
                add(1, "Отказаться", "Повар вздохнёт, но не тронет.");
            } else {
                add(0, "Попробовать всё равно", "Обиженный повар заденет всех: -8% ЗДР.");
                add(1, "Похвалить повара", "Спокойно уйти, ничего не теряя.");
            }
            break;
        }
    }
}

bool Run::takeEventChoice(int choiceIdx) {
    if (phase_ != RunPhase::EventChoice || choiceIdx < 0 || choiceIdx >= static_cast<int>(eventChoices_.size()))
        return false;
    const EventChoice c = eventChoices_[choiceIdx];
    const std::vector<int> alive = livingIndexes(party_);

    auto healAll = [&](int pct) {
        for (Hero& h : party_.heroes)
            if (h.alive()) h.hp = std::min(h.maxHp, h.hp + h.maxHp * pct / 100);
    };
    auto hurtAll = [&](int pct) {
        for (Hero& h : party_.heroes)
            if (h.alive()) h.hp = std::max(1, h.hp - std::max(1, h.maxHp * pct / 100));
    };
    auto buffAll = [&](int atk, int def) {
        for (size_t i = 0; i < party_.heroes.size(); ++i)
            if (party_.heroes[i].alive()) party_.buff(static_cast<int>(i), atk, def);
    };

    switch (eventResult_.kind) {
        case EventKind::LostHero: {
            if (c.param < 0 || c.param >= static_cast<int>(party_.heroes.size()) || !party_.heroes[c.param].alive()) {
                say("Событие: отряд остался как есть.");
                break;
            }
            const std::string oldName = party_.heroes[c.param].name;
            const std::vector<Hero> roster = heroRoster();
            Hero found = roster[rng_.range(0, static_cast<int>(roster.size()) - 1)];
            found.name = "Найденный " + found.name;
            found.baseHp += (floor_ - 1) * 4;
            found.baseAtk += (floor_ - 1);
            found.baseDef += (floor_ - 1) / 2;
            found.weapon = party_.heroes[c.param].weapon;
            found.armor = party_.heroes[c.param].armor;
            found.relic = party_.heroes[c.param].relic;
            found.buffAtk = party_.heroes[c.param].buffAtk;
            found.buffDef = party_.heroes[c.param].buffDef;
            found.hp = found.maxHp = found.baseHp;
            found.recalc();
            party_.heroes[c.param] = found;
            if (c.param < static_cast<int>(squad_.size())) squad_[c.param] = found.role;
            if (c.param < static_cast<int>(died_.size())) died_[c.param] = false;
            say(fmt("Событие: %s занял место %s.", found.name.c_str(), oldName.c_str()));
            break;
        }

        case EventKind::BloodyAltar:
            if (c.param == 0) {
                for (Hero& h : party_.heroes)
                    if (h.alive()) h.hp = std::max(1, h.hp - h.maxHp * 15 / 100);
                buffAll(2, 1);
                say("Событие: алтарь забрал кровь, отряд стал сильнее (+2 АТК, +1 ЗАЩ).");
            } else {
                party_.coins += 15;
                say("Событие: монеты с алтаря: +15.");
            }
            break;

        case EventKind::SkeletonRemains:
            if (c.param == 0 || c.param == 1) {
                if (alive.empty()) {
                    say("Событие: некому взять вещь.");
                    break;
                }
                const Role role = party_.heroes[alive[rng_.range(0, static_cast<int>(alive.size()) - 1)]].role;
                const Item it = cursedRelic(rng_, c.param == 0 ? ItemKind::Weapon : ItemKind::Armor, role, -2);
                party_.bag.push_back(it);
                newLoot_ = true;
                say(fmt("Событие: найдена вещь: %s.", it.name.c_str()));
            } else {
                say("Событие: кости оставлены в покое.");
            }
            break;

        case EventKind::MimicChest:
            if (c.param == 0) {
                giveLoot(3);
                party_.coins += 20;
                say("Событие: Вор усыпил мимика. Добыча 3 тира и +20 монет.");
            } else if (c.param == 1) {
                Enemy chest;
                chest.name = "Сундук-мимик";
                chest.maxHp = chest.hp = 58;
                chest.atk = 14;
                chest.def = 4;
                chest.spd = 3;
                chest.coins = 22;
                chest.size = 100;
                chest.slot = 1;
                say("Событие: мимик нападает!");
                startFight({chest}, 3, -1);
            } else {
                say("Событие: сундук остался стоять.");
            }
            break;

        case EventKind::TalkingRaven:
            if (c.param == 0) {
                pendingPlacementBonus_ += 1;
                say("Событие: ворон обещает +1 комнату на следующем этаже.");
            } else {
                party_.coins += 10;
                say("Событие: мелочь с ветки: +10 монет.");
            }
            break;

        case EventKind::CursedBook:
            if (c.param == 0) {
                buffAll(3, -2);
                say("Событие: знания дались ценой памяти: +3 АТК, -2 ЗАЩ.");
            } else if (c.param == 1) {
                grantArtifacts(1);
                party_.coins = std::max(0, party_.coins - 10);
                say("Событие: книга забрана: +1 артефакт, -10 монет.");
            } else {
                say("Событие: книга закрыта.");
            }
            break;

        case EventKind::MonsterChef: {
            const int totalMax = party_.totalMaxHp();
            const int hpPct = totalMax > 0 ? party_.totalHp() * 100 / totalMax : 100;
            if (hpPct < 80) {
                if (c.param == 0) {
                    healAll(30);
                    say("Событие: повар накормил отряд похлёбкой (+30% ЗДР).");
                } else {
                    say("Событие: повар ушёл готовить для себя.");
                }
            } else if (c.param == 0) {
                hurtAll(8);
                say("Событие: повар обиделся и шлёпнул всех ложкой (-8% ЗДР).");
            } else {
                say("Событие: повар обрадовался похвале.");
            }
            break;
        }
    }

    eventChoices_.clear();
    eventResult_ = EventResult{};
    if (phase_ == RunPhase::EventChoice) phase_ = RunPhase::Exploring;
    return true;
}

bool Run::inForgeRoom() const {
    const Room* r = dungeon_.roomAt(pos_);
    return r && r->card.type == RoomType::Forge;
}

bool Run::openForge() {
    if (phase_ != RunPhase::Exploring || combat_ || !inForgeRoom()) return false;
    phase_ = RunPhase::ForgeChoice;
    return true;
}

// ------------------------------------------------------------------ бой

void Run::startFight(std::vector<Enemy> enemies, int lootTier, int bossId) {
    pendingTier_ = lootTier;
    fightBoss_ = bossId;
    ++fightId_;
    combat_ = std::make_unique<Combat>(party_, std::move(enemies), rng_, bossId, floor_);
    combat_->setDamageReduction(meta_.damageReducePct());
    phase_ = RunPhase::Fighting;
}

void Run::beginBossFight() {
    if (phase_ != RunPhase::BossIntro) return;
    say("Бой с боссом начался!");
    startFight({makeBoss(pendingBoss_)}, 4, pendingBoss_);
}

void Run::step() {
    if (phase_ != RunPhase::Fighting || !combat_) return;
    combat_->step(log_);
    updateLosses();
    int gained = 0;  // монеты с убитых врагов
    for (const auto& a : combat_->events())
        for (const auto& h : a.hits)
            if (!h.targetHero && !h.heal) gained += h.coins;
    if (gained > 0 && hasLivingArcher()) gained += gained * 25 / 100;
    if (gained > 0) {
        party_.coins += gained;
        say(fmt("Монеты: +%d (всего %d)", gained, party_.coins));
    }
    if (combat_->finished()) {
        party_.clearCombatBuffs();
        if (combat_->heroesWon()) {
            onFightWon();
        } else {
            phase_ = RunPhase::Defeat;
            say("Отряд повержен. Поход окончен.");
        }
    }
}

void Run::closeCombat() {
    if (combat_ && phase_ == RunPhase::Exploring) combat_.reset();
}

void Run::onFightWon() {
    ++cleared_;
    // combat_ остаётся живым, чтобы интерфейс показал последний удар; закроется через closeCombat().
    if (fightBoss_ >= 0) {
        say(fmt("%s повержен! Выбери награду.", bossInfo(fightBoss_).name.c_str()));
        bossReward_.clear();
        bossRewardClaimed_ = false;
        for (int i = 0; i < 3; ++i) {
            Item it;
            for (int attempt = 0; attempt < 20; ++attempt) {
                it = rollLoot(rng_, 4);
                bool dup = false;
                for (const auto& o : bossReward_)
                    if (o.name == it.name && o.kind == it.kind && o.power == it.power && o.relic == it.relic) dup = true;
                if (!dup) break;
            }
            bossReward_.push_back(it);
        }
        phase_ = RunPhase::BossReward;
        return;
    }
    say("Враги разбиты.");
    giveLoot(pendingTier_);
    phase_ = RunPhase::Exploring;
}

bool Run::claimBossRewardItem(int index) {
    if (phase_ != RunPhase::BossReward || bossRewardClaimed_) return false;
    if (index < 0 || index >= static_cast<int>(bossReward_.size())) return false;
    party_.bag.push_back(bossReward_[index]);
    bossRewardClaimed_ = true;
    say(fmt("Награда за босса: %s.", bossReward_[index].name.c_str()));
    return true;
}

bool Run::claimBossRewardArtifacts() {
    if (phase_ != RunPhase::BossReward || bossRewardClaimed_) return false;
    artifacts_ += 3;
    bossRewardClaimed_ = true;
    say("Награда за босса: +3 артефакта.");
    return true;
}

void Run::finishRunAfterBoss(bool victory) {
    if (phase_ != RunPhase::BossReward || !bossRewardClaimed_) return;
    phase_ = victory ? RunPhase::Victory : RunPhase::Exploring;
    if (victory) say("Забег завершён победой.");
}

// ------------------------------------------------------------- события

void Run::giveLoot(int tier) {
    Item it = rollLoot(rng_, tier);
    party_.bag.push_back(it);
    loot_.push_back(it);
    newLoot_ = true;
    say(fmt("Добыча ур.%d: %s (%s)", tier, it.name.c_str(), itemBonusText(it).c_str()));
}

void Run::resolveTrap(int d) {
    // Ловушка - это риск с наградой: добыча на тир выше (тир 4 остаётся за боссом)
    // плюс монеты. Любой отряд может заметить её и снизить урон; живой Вор замечает
    // всегда и обезвреживает полностью.
    const bool rogue = party_.findAlive(Role::Rogue) != nullptr;
    const bool noticed = rogue || rng_.range(1, 100) <= kTrapNoticePct;
    int pct = noticed ? 50 : 100;
    if (rogue) {
        pct = 0;
        say("Вор обезвредил ловушку: урона нет.");
    } else if (noticed) {
        say(fmt("Ловушка замечена: урон снижен до %d%%.", pct));
    } else {
        say("Ловушка сработала!");
    }

    // Урон получают два РАЗНЫХ героя (один герой дважды - никогда).
    if (pct > 0) {
        std::vector<int> alive;
        for (int i = 0; i < static_cast<int>(party_.heroes.size()); ++i)
            if (party_.heroes[i].alive()) alive.push_back(i);
        rng_.shuffle(alive);
        const int dmg = std::max(1, d * 5 * pct / 100);
        for (size_t k = 0; k < alive.size() && k < 2; ++k) {
            Hero& h = party_.heroes[alive[k]];
            h.hp -= dmg;
            say(fmt("%s: -%d%s", h.name.c_str(), dmg, h.alive() ? "" : " - пал"));
        }
    }
    updateLosses();
    if (!party_.anyAlive()) {
        phase_ = RunPhase::Defeat;
        say("Отряд погиб в ловушке.");
        return;
    }
    const int tier = std::min(3, d + 1);         // тир 4 по-прежнему только с босса
    const int coins = d * kTrapCoinPerDifficulty;
    grantCoins(coins);
    ++cleared_;
    giveLoot(tier);
    say(fmt("Ловушка: добыча ур.%d, +%d монет.", tier, coins));
}

void Run::restHeal() {
    if (phase_ != RunPhase::RestChoice) return;
    party_.healPercent(restHealPct());
    ++cleared_;
    say(fmt("Привал: отряд восстановил %d%% здоровья.", restHealPct()));
    phase_ = RunPhase::Exploring;
}

// Гарантированное воскрешение: платим только за недостающий до 100% шанс.
// Привал при этом не заканчивается - можно выбрать лечение или усиление.
bool Run::restRevive(int heroIndex) {
    if (phase_ != RunPhase::RestChoice) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(party_.heroes.size())) return false;
    Hero& h = party_.heroes[heroIndex];
    const int cost = reviveCoinCost();
    if (h.alive() || party_.coins < cost) return false;
    party_.coins -= cost;
    if (heroIndex < static_cast<int>(died_.size())) died_[heroIndex] = true;
    h.hp = std::max(1, h.maxHp * kRevivePct / 100);
    h.away = false;
    say(fmt("Привал: %s воскрешён за %d монет (%d%% здоровья).", h.name.c_str(), cost, kRevivePct));
    return true;
}

// Доплата артефактами до 100%: дороже по редкости валюты, зато здоровья больше.
bool Run::restReviveArtifact(int heroIndex) {
    if (phase_ != RunPhase::RestChoice) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(party_.heroes.size())) return false;
    Hero& h = party_.heroes[heroIndex];
    const int cost = reviveArtifactCost();
    if (h.alive() || artifacts_ < cost) return false;
    artifacts_ -= cost;
    if (heroIndex < static_cast<int>(died_.size())) died_[heroIndex] = true;
    h.hp = std::max(1, h.maxHp * kReviveArtifactPct / 100);
    h.away = false;
    say(fmt("Привал: %s воскрешён за %d артефакта (%d%% здоровья).", h.name.c_str(), cost, kReviveArtifactPct));
    return true;
}

void Run::restBuff(const std::vector<int>& idx) {
    if (phase_ != RunPhase::RestChoice) return;
    std::vector<bool> done(party_.heroes.size(), false);
    std::string names;
    int applied = 0;
    for (int i : idx) {
        if (applied >= kBuffTargets) break;
        if (i < 0 || i >= static_cast<int>(party_.heroes.size()) || done[i] || !party_.heroes[i].alive()) continue;
        party_.buff(i, kBuffAtk, kBuffDef);
        done[i] = true;
        names += (applied ? ", " : "") + party_.heroes[i].name;
        ++applied;
    }
    if (!applied) return;  // ничего не выбрано - остаёмся на привале
    ++cleared_;
    say(fmt("Привал: усилены %s (+%d АТК, +%d ЗАЩ).", names.c_str(), kBuffAtk, kBuffDef));
    phase_ = RunPhase::Exploring;
}

// ------------------------------------------------------------ инвентарь

bool Run::canManage() const {
    return (phase_ == RunPhase::Exploring && !combat_) || phase_ == RunPhase::BossIntro || phase_ == RunPhase::Shop || phase_ == RunPhase::ForgeChoice || phase_ == RunPhase::BossReward;
}

// ------------------------------------------------------------- торговец

bool Run::inShopRoom() const { return stocks_.count(pos_) > 0; }

const std::vector<ShopOffer>* Run::shopStock() const {
    auto it = stocks_.find(pos_);
    return it == stocks_.end() ? nullptr : &it->second;
}

bool Run::openShop() {
    if (phase_ != RunPhase::Exploring || combat_ || !inShopRoom()) return false;
    phase_ = RunPhase::Shop;
    return true;
}
int Run::effectivePrice(const ShopOffer& offer) const {
    return hasLivingBard() ? offer.price * 85 / 100 : offer.price;
}

bool Run::buy(int idx) {
    if (phase_ != RunPhase::Shop) return false;
    auto it = stocks_.find(pos_);
    if (it == stocks_.end() || idx < 0 || idx >= static_cast<int>(it->second.size())) return false;
    ShopOffer& o = it->second[idx];
    const int price = effectivePrice(o);
    if (o.sold || party_.coins < price) return false;
    party_.coins -= price;
    o.sold = true;
    party_.bag.push_back(o.item);
    say(fmt("Куплено: %s за %d", o.item.name.c_str(), price));
    return true;
}

bool Run::sellItem(int bagIndex) {
    if (!canManage() || !inShopRoom()) return false;
    if (bagIndex < 0 || bagIndex >= static_cast<int>(party_.bag.size())) return false;
    const Item item = party_.bag[bagIndex];
    const int price = itemSellPrice(item);
    party_.bag.erase(party_.bag.begin() + bagIndex);
    party_.coins += price;
    say(fmt("Продано: %s за %d", item.name.c_str(), price));
    return true;
}

bool Run::restockShop() {
    if (phase_ != RunPhase::Shop || !inShopRoom() || party_.coins < kShopRestockCost) return false;
    party_.coins -= kShopRestockCost;
    stocks_[pos_] = makeShopStock(rng_, 5, meta_.shopDiscountPct());
    say(fmt("Торговец обновил ассортимент за %d монет.", kShopRestockCost));
    return true;
}

void Run::leaveShop() {
    if (phase_ == RunPhase::Shop) phase_ = RunPhase::Exploring;
}

bool Run::equipItem(int bagIndex, int heroIndex) { return canManage() && party_.equipFromBag(bagIndex, heroIndex); }
bool Run::unequipItem(int heroIndex, ItemKind slot) { return canManage() && party_.unequip(heroIndex, slot); }
bool Run::usePotion(int bagIndex, int heroIndex) { return canManage() && party_.usePotion(bagIndex, heroIndex); }

int Run::restHealPct() const {
    return party_.findAlive(Role::Paladin) ? 40 : meta_.restHealPct();
}
bool Run::hasLivingArcher() const { return party_.findAlive(Role::Archer) != nullptr; }
bool Run::hasLivingBard() const { return party_.findAlive(Role::Bard) != nullptr; }
bool Run::hasLivingSeidmard() const { return party_.findAlive(Role::Seidmard) != nullptr; }
bool Run::canRestRevive() const {
    for (const auto& h : party_.heroes)
        if (!h.alive()) return true;
    return false;
}

int Run::reviveChancePct() const { return party_.findAlive(Role::Cleric) ? 50 : 25; }

bool Run::canRestReviveFree() const {
    return phase_ == RunPhase::RestChoice && !freeReviveUsed_ && canRestRevive();
}

// Бесплатная попытка воскресить выбранного героя. Шанс известен заранее,
// но попытка одна за привал: после неё привал продолжается и можно доплатить.
bool Run::restReviveFree(int heroIndex) {
    if (!canRestReviveFree()) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(party_.heroes.size())) return false;
    Hero& h = party_.heroes[heroIndex];
    if (h.alive()) return false;

    freeReviveUsed_ = true;
    const int chance = reviveChancePct();
    if (rng_.range(1, 100) <= chance) {
        if (heroIndex < static_cast<int>(died_.size())) died_[heroIndex] = true;
        h.hp = std::max(1, h.maxHp * kFreeRevivePct / 100);
        h.away = false;
        say(fmt("Привал: %s возвращается в строй (%d%% здоровья, шанс %d%%).",
                h.name.c_str(), kFreeRevivePct, chance));
        return true;
    }
    say(fmt("Привал: попытка воскресить %s не удалась (шанс %d%%).", h.name.c_str(), chance));
    return false;
}

// Доплата = полная цена * недостающий шанс / 100. Артефакты округляем вверх:
// 2 артефакта за 100% -> 2 за 75% (без жреца) и 1 за 50% (со жрецом).
int Run::reviveCoinCost() const {
    return fullReviveCoinCost() * (100 - reviveChancePct()) / 100;
}
int Run::reviveArtifactCost() const {
    const int missing = 100 - reviveChancePct();
    return (kReviveArtifactCost * missing + 99) / 100;
}
int Run::forgePrice(const Item& it) const {
    if (it.tier >= 4 || it.kind == ItemKind::Potion) return 0;
    Item n = it;
    ++n.tier;
    const int base = itemPrice(n) * 60 / 100;
    return hasLivingSeidmard() ? base * 80 / 100 : base;  // руны Сейдмарда: скидка кузницы 20%
}
bool Run::forgeUpgrade(int i) {
    if (phase_!=RunPhase::ForgeChoice || i<0 || i>=(int)party_.bag.size()) return false;
    const Item old=party_.bag[i]; if(old.tier>=4 || old.kind==ItemKind::Potion) return false;
    const int cost=forgePrice(old); if(party_.coins<cost) return false;
    party_.coins-=cost; party_.bag[i]=upgradeItem(old); say(fmt("Кузница: %s улучшен за %d монет.",old.name.c_str(),cost)); return true;
}
void Run::leaveForge() { if(phase_==RunPhase::ForgeChoice) phase_=RunPhase::Exploring; }

}  // namespace dc
