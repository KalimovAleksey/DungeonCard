#include "Run.h"
#include "Util.h"

namespace dc {

Run::Run(const Dungeon& dungeon, uint32_t seed, Party party, int floor)
    : dungeon_(dungeon),
      party_(std::move(party)), // <-- забираем готовый отряд
      rng_(seed),
      pos_(dungeon.startPos()), 
      floor_(floor) 
{
    say(fmt("Этаж %d. Отряд входит в подземелье. WASD - идти, I - инвентарь.", floor_));
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
            stocks_[p] = makeShopStock(rng_, 5, meta_.shopDiscountPct());
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
            startFight(makeEnemies(d, rng_, floor_), d, -1);
            break;
        case RoomType::Treasure:
            if (d >= 2) {
                say("Сокровище охраняют!");
                startFight(makeEnemies(d - 1, rng_, floor_), d, -1);
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
    combat_->setDamageReduction(meta_.damageReducePct());
    phase_ = RunPhase::Fighting;
}

void Run::beginBossFight() {
    if (phase_ != RunPhase::BossIntro) return;
    say("Бой с боссом начался!");
    startFight({makeBoss(pendingBoss_, floor_)}, 4, pendingBoss_);
}

void Run::step() {
    if (phase_ != RunPhase::Fighting || !combat_) return;
    combat_->step(log_);
    updateLosses();
    int gained = 0;  // монеты с убитых врагов
    for (const auto& a : combat_->events())
        for (const auto& h : a.hits)
            if (!h.targetHero && !h.heal) gained += h.coins;
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
    // Урон получают два РАЗНЫХ героя (один герой дважды - никогда). Живой Вор всегда
    // замечает ловушку и снижает её урон до 50-60%.
    int pct = 100;
    if (party_.findAlive(Role::Rogue)) {
        pct = rng_.range(50, 60);
        say(fmt("Вор замечает ловушку: урон снижен до %d%%.", pct));
    } else {
        say("Ловушка сработала!");
    }
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
    updateLosses();
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
    party_.healPercent(restHealPct());
    ++cleared_;
    say(fmt("Привал: отряд восстановил %d%% здоровья.", restHealPct()));
    phase_ = RunPhase::Exploring;
}

// Воскрешение на привале: платная услуга, привал при этом не заканчивается.
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

// Воскрешение за артефакт: дороже по редкости валюты, зато здоровья больше.
bool Run::restReviveArtifact(int heroIndex) {
    if (phase_ != RunPhase::RestChoice) return false;
    if (heroIndex < 0 || heroIndex >= static_cast<int>(party_.heroes.size())) return false;
    Hero& h = party_.heroes[heroIndex];
    if (h.alive() || artifacts_ < kReviveArtifactCost) return false;
    artifacts_ -= kReviveArtifactCost;
    if (heroIndex < static_cast<int>(died_.size())) died_[heroIndex] = true;
    h.hp = std::max(1, h.maxHp * kReviveArtifactPct / 100);
    h.away = false;
    say(fmt("Привал: %s воскрешён за артефакт (%d%% здоровья).", h.name.c_str(), kReviveArtifactPct));
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

}  // namespace dc
