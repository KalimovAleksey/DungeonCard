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
    s.squad = squad_;
    s.casualties = casualties();
    s.aliveAtEnd = party_.aliveCount();
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
            say("Привал! Лечение группы или усиление двух героев?");
            break;
        case RoomType::Boss:
            pendingBoss_ = r->card.bossId >= 0 ? r->card.bossId : 0;
            phase_ = RunPhase::BossIntro;
            say(fmt("Логово босса: %s. Подготовь отряд!", bossInfo(pendingBoss_).name.c_str()));
            break;
        case RoomType::Forge:
            phase_ = RunPhase::ForgeChoice;
            say("Кузница! Выбери предмет в рюкзаке для улучшения.");
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
    if (floor_ > 1) {
        const int pct = 100 + (floor_ - 1) * 15;
        for (auto& e : enemies) { e.hp = e.maxHp = std::max(1, e.maxHp * pct / 100); e.atk = std::max(1, e.atk * pct / 100); }
    }
    combat_ = std::make_unique<Combat>(party_, std::move(enemies), rng_, bossId);
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
bool Run::restRevivePaid(int heroIndex) {
    if (phase_ != RunPhase::RestChoice) return false;
    if (heroIndex < 0 || heroIndex >= (int)party_.heroes.size()) return false;
    Hero& h = party_.heroes[heroIndex];
    if (h.alive()) return false;
    const int cost = revivePaidCost(heroIndex);
    if (party_.coins < cost) return false;
    party_.coins -= cost;
    if (heroIndex < (int)died_.size()) died_[heroIndex] = true;
    h.hp = std::max(1, h.maxHp * kRevivePct / 100);
    h.away = false;
    say(fmt("Привал: %s воскрешён за %d монет (100%% шанс, %d%% ЗДР).",
            h.name.c_str(), cost, kRevivePct));
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
int Run::reviveBaseChancePct() const {
    return party_.findAlive(Role::Cleric) ? 50 : 25;
}
int Run::revivePaidCost(int heroIndex) const {
    if (heroIndex < 0 || heroIndex >= (int)party_.heroes.size()) return reviveCoinCost();
    if (party_.heroes[heroIndex].alive()) return 0;
    const int full = reviveCoinCost();
    const int chance = reviveBaseChancePct();
    return full * (100 - chance) / 100;   // 100-75=75 или 100-50=50
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
    return (phase_ == RunPhase::Exploring && !combat_) || phase_ == RunPhase::BossIntro || phase_ == RunPhase::Shop || phase_ == RunPhase::ForgeChoice;
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
    const int price = hasLivingBard() ? o.price * 85 / 100 : o.price;
    if (o.sold || party_.coins < price) return false;
    party_.coins -= price;
    o.sold = true;
    party_.bag.push_back(o.item);
    say(fmt("Куплено: %s за %d", o.item.name.c_str(), price));
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
bool Run::canRestRevive() const { for (const auto& h : party_.heroes) if (!h.alive()) return true; return false; }
int Run::reviveCoinCost() const {
    return meta_.reviveCoinCost();
}
void Run::restReviveRandom() {
    if (phase_ != RunPhase::RestChoice) return;
    std::vector<int> dead;
    for (int i = 0; i < (int)party_.heroes.size(); ++i)
        if (!party_.heroes[i].alive()) dead.push_back(i);
    if (dead.empty()) return;
    int i = dead[rng_.range(0, (int)dead.size()-1)];
    Hero& h = party_.heroes[i];
    if (rng_.range(1,100) <= reviveBaseChancePct()) {
        h.hp = std::max(1, h.maxHp * 30 / 100);
        h.away = false;
        say(fmt("Привал: %s возвращается в строй (%d%% шанс, 30%% ЗДР).",
                h.name.c_str(), reviveBaseChancePct()));
    } else {
        say(fmt("Привал: попытка воскресить %s не удалась.", h.name.c_str()));
    }
    ++cleared_;
    phase_ = RunPhase::Exploring;
}
int Run::forgePrice(const Item& it) const { if (it.tier>=4 || it.kind==ItemKind::Potion) return 0; Item n=it; ++n.tier; return itemPrice(n)*60/100; }
bool Run::forgeUpgrade(int i) {
    if (phase_!=RunPhase::ForgeChoice || i<0 || i>=(int)party_.bag.size()) return false;
    const Item old=party_.bag[i]; if(old.tier>=4 || old.kind==ItemKind::Potion) return false;
    const int cost=forgePrice(old); if(party_.coins<cost) return false;
    party_.coins-=cost; party_.bag[i]=upgradeItem(old); say(fmt("Кузница: %s улучшен за %d монет.",old.name.c_str(),cost)); return true;
}
void Run::leaveForge() { if(phase_==RunPhase::ForgeChoice) phase_=RunPhase::Exploring; }

}  // namespace dc
