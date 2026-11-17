#include "Run.h"
#include "Util.h"

namespace dc {

Run::Run(const Dungeon& dungeon, uint32_t seed, std::vector<Hero> heroes)
    : dungeon_(dungeon),
      party_(heroes.empty() ? Party() : Party(std::move(heroes))),
      rng_(seed),
      pos_(dungeon.startPos()) {
    say("Отряд входит в подземелье. WASD - идти, I - инвентарь.");
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
    if (r->card.type == RoomType::Shop) {  // к торговцу можно возвращаться
        if (!r->visited) {
            r->visited = true;
            stocks_[p] = makeShopStock(rng_, 5);
            say("Торговец! Покупай вещи за монеты.");
        } else {
            say("Снова у торговца.");
        }
        phase_ = RunPhase::Shop;
        return;
    }
    if (r->visited) return;
    r->visited = true;
    const int d = r->card.difficulty;
    say(fmt("Вход: %s (сложность %d)", roomTypeName(r->card.type), d));

    switch (r->card.type) {
        case RoomType::Combat:
            startFight(makeEnemies(d, rng_), d, -1);
            break;
        case RoomType::Treasure:
            if (d >= 2) {
                say("Сокровище охраняют!");
                startFight(makeEnemies(d - 1, rng_), d, -1);
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

// ------------------------------------------------------------------ бой

void Run::startFight(std::vector<Enemy> enemies, int lootTier, int bossId) {
    pendingTier_ = lootTier;
    fightBoss_ = bossId;
    ++fightId_;
    combat_ = std::make_unique<Combat>(party_, std::move(enemies), rng_, bossId);
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
    int gained = 0;  // монеты с убитых врагов
    for (const auto& a : combat_->events())
        for (const auto& h : a.hits)
            if (!h.targetHero && !h.heal) gained += h.coins;
            
    // --- НОВОЕ: Пассивка Лучника ---
    if (gained > 0 && hasLivingArcher()) {
        int bonus = gained * kArcherCoinBonusPct / 100;
        gained += bonus;
    }
    // -------------------------------
    if (gained > 0) {
        party_.coins += gained;
        say(fmt("Монеты: +%d (всего %d)", gained, party_.coins));
    }
    if (combat_->finished()) {
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
        say(fmt("%s повержен! Победа!", bossInfo(fightBoss_).name.c_str()));
        phase_ = RunPhase::Victory;
        return;
    }
    say("Враги разбиты.");
    giveLoot(pendingTier_);
    phase_ = RunPhase::Exploring;
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
    if (party_.findAlive(Role::Rogue) && rng_.chance(0.5)) {
        say("Вор обезвреживает ловушку!");
    } else {
        say("Ловушка сработала!");
        for (int k = 0; k < 2; ++k) {
            std::vector<Hero*> alive;
            for (auto& h : party_.heroes)
                if (h.alive()) alive.push_back(&h);
            if (alive.empty()) break;
            Hero* h = alive[rng_.range(0, static_cast<int>(alive.size()) - 1)];
            h->hp -= d * 5;
            say(fmt("%s: -%d%s", h->name.c_str(), d * 5, h->alive() ? "" : " - пал"));
        }
    }
    if (!party_.anyAlive()) {
        phase_ = RunPhase::Defeat;
        say("Отряд погиб в ловушке.");
        return;
    }
    ++cleared_;
    giveLoot(d);
}

void Run::restHeal() {
    if (phase_ != RunPhase::RestChoice) return;
    const int pct = restHealPct();
    party_.healPercent(pct);
    ++cleared_;
    say(fmt("Привал: отряд восстановил %d%% здоровья.", pct));
    phase_ = RunPhase::Exploring;
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
    return (phase_ == RunPhase::Exploring && !combat_) || phase_ == RunPhase::BossIntro || phase_ == RunPhase::Shop;
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

bool Run::buy(int idx) {
    if (phase_ != RunPhase::Shop) return false;
    auto it = stocks_.find(pos_);
    if (it == stocks_.end() || idx < 0 || idx >= static_cast<int>(it->second.size())) return false;
    ShopOffer& o = it->second[idx];
    if (o.sold || party_.coins < o.price) return false;
    party_.coins -= o.price;
    o.sold = true;
    party_.bag.push_back(o.item);
    say(fmt("Куплено: %s за %d", o.item.name.c_str(), o.price));
    return true;
}

void Run::leaveShop() {
    if (phase_ == RunPhase::Shop) phase_ = RunPhase::Exploring;
}

bool Run::equipItem(int bagIndex, int heroIndex) { return canManage() && party_.equipFromBag(bagIndex, heroIndex); }
bool Run::unequipItem(int heroIndex, ItemKind slot) { return canManage() && party_.unequip(heroIndex, slot); }
bool Run::usePotion(int bagIndex, int heroIndex) { return canManage() && party_.usePotion(bagIndex, heroIndex); }

bool Run::canRestRevive() const {
    for (const auto& h : party_.heroes)
        if (!h.alive()) return true;
    return false;
}
int Run::reviveChancePct() const { // <-- ДОЛЖНО БЫТЬ ТАК
    for (const auto& h : party_.heroes) {
        if (h.alive() && h.role == Role::Cleric) {
            return kReviveChanceClericPct;
        }
    }
    return kReviveChancePct;
}
int Run::restHealPct() const {
    for (const auto& h : party_.heroes) {
        if (h.alive() && h.role == Role::Paladin) {
            return kRestHealPaladinPct;
        }
    }
    return kRestHealPct;
} // <--- Не забудьте закрыть метод!
bool Run::hasLivingArcher() const {
    for (const auto& h : party_.heroes) {
        if (h.alive() && h.role == Role::Archer) {
            return true;
        }
    }
    return false;
}

void Run::restRevive() {
    if (phase_ != RunPhase::RestChoice) return;

    std::vector<int> dead;
    for (int i = 0; i < (int)party_.heroes.size(); ++i)
        if (!party_.heroes[i].alive()) dead.push_back(i);
    if (dead.empty()) return;

    ++cleared_;
    const int pick = dead[rng_.range(0, (int)dead.size() - 1)];
    Hero& h = party_.heroes[pick];

    if (rng_.range(1, 100) <= reviveChancePct()) {
        h.hp = std::max(1, h.maxHp * kReviveHpPct / 100);
        h.away = false;
        say(fmt("Привал: чудо! %s возвращается в строй (%d%% ЗДР).", h.name.c_str(), kReviveHpPct));
    } else {
        say(fmt("Привал: попытка воскресить %s не удалась.", h.name.c_str()));
    }
    phase_ = RunPhase::Exploring;
}

}  // namespace dc
