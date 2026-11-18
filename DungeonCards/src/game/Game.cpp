#include "game/Game.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include "core/Util.h"

namespace dc {
namespace {

constexpr int kScreenW = 1280;
constexpr int kScreenH = 720;
constexpr int kCell = 80;
constexpr int kGridX = 20;
constexpr int kGridY = 20;
constexpr int kArenaW = 720;
constexpr int kArenaH = 560;
constexpr int kPanelX = 780;
constexpr int kPanelW = 470;
constexpr int kCardW = 160;
constexpr int kCardH = 100;
constexpr int kCardY = 600;

// Темп боя: 0.7 с на одно действие (вдвое медленнее прежнего). Передвижение - по нажатию WASD.
constexpr float kCombatStep = 0.70f;
constexpr float kSpeeds[] = {0.5f, 1.f, 2.f, 4.f, 10.f};
constexpr float kMoveCooldown = 0.16f;

constexpr float kLungeDur = 0.45f;
constexpr float kProjDur = 0.40f;
constexpr float kFloatLife = 1.2f;
constexpr float kFlashDur = 0.40f;

// экран выбора героев
constexpr int kSelCardW = 300;
constexpr int kSelCardH = 252;
constexpr int kSelX = 25;
constexpr int kSelY = 90;
constexpr int kSelGap = 10;

const Color kBg{20, 21, 28, 255};
const Color kPanel{30, 32, 42, 255};
const Color kGridLine{48, 50, 62, 255};
const Color kEmpty{26, 28, 36, 255};
const Color kText{235, 235, 240, 255};
const Color kDim{150, 152, 165, 255};
const Color kDoor{235, 225, 190, 255};
const Color kGood{110, 210, 130, 255};
const Color kBad{235, 100, 100, 255};
const Color kAccent{250, 210, 90, 255};
const Color kDamage{255, 70, 70, 255};
const Color kHeal{80, 235, 120, 255};
const Color kCoin{255, 215, 60, 255};

Color roomColor(RoomType t) {
    switch (t) {
        case RoomType::Start:    return {90, 110, 150, 255};
        case RoomType::Combat:   return {170, 70, 70, 255};
        case RoomType::Treasure: return {200, 165, 60, 255};
        case RoomType::Trap:     return {185, 105, 45, 255};
        case RoomType::Rest:     return {70, 150, 90, 255};
        case RoomType::Boss:     return {135, 55, 165, 255};
        case RoomType::Shop:     return {55, 160, 175, 255};
    }
    return kDim;
}

Color roleColor(Role r) {
    switch (r) {
        case Role::Warrior:   return {205, 140, 90, 255};
        case Role::Mage:      return {100, 140, 235, 255};
        case Role::Cleric:    return {235, 235, 235, 255};
        case Role::Rogue:     return {110, 200, 170, 255};
        case Role::Archer:    return {170, 210, 90, 255};
        case Role::Paladin:   return {225, 185, 70, 255};
        case Role::Berserker: return {215, 85, 65, 255};
        case Role::Druid:     return {80, 185, 115, 255};
    }
    return kDim;
}

Color bossColor(int id) {
    switch (id) {
        case BossSlime:    return {90, 200, 110, 255};
        case BossDevourer: return {170, 100, 70, 255};
        case BossKnight:   return {170, 175, 195, 255};
        case BossArchmage: return {160, 115, 235, 255};
        case BossDruid:    return {100, 160, 70, 255};
        case BossTroll:    return {120, 140, 95, 255};
        case BossNecro:    return {125, 125, 160, 255};
        case BossDragon:   return {225, 95, 45, 255};
    }
    return {135, 55, 165, 255};
}

Color enemyColor(const Enemy& e) {
    if (e.minion) return {200, 200, 210, 255};
    if (e.bossId >= 0) return bossColor(e.bossId);
    return {170, 70, 70, 255};
}

Color itemColor(ItemKind k) {
    switch (k) {
        case ItemKind::Weapon: return {225, 115, 95, 255};
        case ItemKind::Armor:  return {100, 155, 235, 255};
        case ItemKind::Relic:  return {185, 125, 235, 255};
        case ItemKind::Potion: return {110, 210, 130, 255};
    }
    return kDim;
}

std::string rewardText(const Card& c) {
    switch (c.type) {
        case RoomType::Boss: return "Награда: победа в забеге!";
        case RoomType::Rest: return "Награда: лечение или бафф";
        case RoomType::Shop: return "Тратишь монеты, выпавшие с врагов";
        case RoomType::Start: return "";
        default: return fmt("Награда: добыча ур. %d", c.difficulty);
    }
}

// Первая буква имени (UTF-8: кириллица занимает 2 байта).
std::string firstGlyph(const std::string& s) {
    if (s.empty()) return "";
    unsigned char c = static_cast<unsigned char>(s[0]);
    size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return s.substr(0, n);
}

struct R { int x, y, w, h; };
bool inR(Vector2 m, R r) { return m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h; }
bool inRect(Vector2 m, int x, int y, int w, int h) { return inR(m, {x, y, w, h}); }

// кнопки меню
R menuBtn(int i, int y0 = 300) { return {490, y0 + i * 72, 300, 56}; }

// экран достижений: два столбца по 9 строк; экран «Наследие»: два столбца карточек
R achRow(int i) { return {40 + (i / 9) * 620, 130 + (i % 9) * 58, 600, 54}; }
R legacyCard(int i) { return {40 + (i / 6) * 620, 124 + (i % 6) * 86, 600, 80}; }
R legacyBuyBtn(int i) {
    R c = legacyCard(i);
    return {c.x + c.w - 150, c.y + 14, 138, 52};
}
const R kBackBtn{440, 660, 400, 44};
R resBtn(int i) { return {440, 190 + i * 58, 400, 48}; }

// геометрия окна инвентаря
// рюкзак - сетка ячеек 5 x 6, у каждого героя 3 ячейки снаряжения
constexpr int kBagCols = 5;
constexpr int kBagCells = 30;
const ItemKind kSlotKinds[3] = {ItemKind::Weapon, ItemKind::Armor, ItemKind::Relic};
R bagCell(int i) { return {kGridX + 12 + (i % kBagCols) * 68, kGridY + 78 + (i / kBagCols) * 68, 62, 62}; }
R heroPanel(int i) { return {kGridX + 360, kGridY + 50 + i * 122, 350, 116}; }
R slotRect(int i, int k) {
    R p = heroPanel(i);
    return {p.x + 8 + k * 54, p.y + 62, 50, 50};
}

// цвет рамки по уровню предмета
Color tierColor(int tier) {
    switch (tier) {
        case 1: return {150, 152, 165, 255};
        case 2: return {110, 210, 130, 255};
        case 3: return {100, 155, 235, 255};
        default: return {200, 130, 245, 255};
    }
}

const char* itemGlyph(ItemKind k) {
    switch (k) {
        case ItemKind::Weapon: return "О";
        case ItemKind::Armor:  return "Б";
        case ItemKind::Relic:  return "Р";
        case ItemKind::Potion: return "З";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

Game::Game() {
    loadSettings();
    // Папка с картинками: рядом с игрой или на уровень-два выше (удобно при запуске из IDE).
    for (const char* dir : {"assets", "../assets", "../../assets"}) {
        if (DirectoryExists(dir)) {
            assetDir_ = dir;
            break;
        }
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(settings_.width, settings_.height, "Dungeon Cards - prototype");
    SetExitKey(KEY_NULL);  // Esc открывает меню, а не закрывает игру
    SetTargetFPS(60);

    // Интерфейс рисуется в «логическом» кадре 1280x720 и масштабируется под окно.
    target_ = LoadRenderTexture(kScreenW, kScreenH);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
    buildResolutionList();
    applyDisplay();

    // Шрифт с кириллицей: assets/font.ttf или системный шрифт Windows.
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0x410; c <= 0x44F; ++c) cps.push_back(c);
    cps.push_back(0x401);
    cps.push_back(0x451);
    const std::string bundled = assetDir_ + "/font.ttf";
    const char* candidates[] = {bundled.c_str(), "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
    for (const char* path : candidates) {
        if (FileExists(path)) {
            font_ = LoadFontEx(path, 32, cps.data(), static_cast<int>(cps.size()));
            ownFont_ = font_.texture.id != 0;
            if (ownFont_) break;
        }
    }
    if (!ownFont_) font_ = GetFontDefault();
    SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);

    loadProfile();
    refreshRoster();
    for (int i = 0; i < kPartySize && i < kMaxRoster; ++i) chosen_[i] = true;
    rollRun();
    screen_ = Screen::MainMenu;
}

Game::~Game() {
    if (ownFont_) UnloadFont(font_);
    for (auto& kv : tex_)
        if (kv.second.id != 0) UnloadTexture(kv.second);
    UnloadRenderTexture(target_);
    CloseWindow();
}

void Game::run() {
    while (!WindowShouldClose() && !quit_) {
        update(GetFrameTime());

        BeginTextureMode(target_);
        ClearBackground(kBg);
        draw();
        EndTextureMode();

        BeginDrawing();
        ClearBackground(Color{0, 0, 0, 255});
        float sc, ox, oy;
        viewTransform(sc, ox, oy);
        DrawTexturePro(target_.texture, Rectangle{0.f, 0.f, static_cast<float>(kScreenW), -static_cast<float>(kScreenH)},
                       Rectangle{ox, oy, kScreenW * sc, kScreenH * sc}, Vector2{0.f, 0.f}, 0.f, Color{255, 255, 255, 255});
        EndDrawing();
    }
}

// ----------------------------------------------------- окно и настройки

void Game::loadSettings() {
    std::ifstream f("settings.cfg");
    int w = 0, h = 0, fs = 0;
    if (f >> w >> h >> fs) {
        if (w >= 640 && h >= 360 && w <= 7680 && h <= 4320) {
            settings_.width = w;
            settings_.height = h;
        }
        settings_.fullscreen = fs != 0;
    }
}

void Game::saveSettings() const {
    std::ofstream f("settings.cfg");
    f << settings_.width << " " << settings_.height << " " << (settings_.fullscreen ? 1 : 0) << "\n";
}

// Доступные разрешения: стандартные, не больше разрешения монитора.
void Game::buildResolutionList() {
    const Resolution all[] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}};
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    resolutions_.clear();
    for (const auto& r : all)
        if (resolutions_.empty() || mw <= 0 || (r.w <= mw && r.h <= mh)) resolutions_.push_back(r);
    bool found = false;
    for (const auto& r : resolutions_) found |= (r.w == settings_.width && r.h == settings_.height);
    if (!found) {
        settings_.width = resolutions_[0].w;
        settings_.height = resolutions_[0].h;
    }
}

void Game::applyDisplay() {
    const bool fs = IsWindowFullscreen();
    if (settings_.fullscreen) {
        if (!fs) ToggleFullscreen();
    } else {
        if (fs) ToggleFullscreen();
        SetWindowSize(settings_.width, settings_.height);
        const int mon = GetCurrentMonitor();
        const int px = (GetMonitorWidth(mon) - settings_.width) / 2;
        const int py = (GetMonitorHeight(mon) - settings_.height) / 2;
        SetWindowPosition(std::max(0, px), std::max(30, py));
    }
    saveSettings();
}

// Масштаб и смещение, с которыми логический кадр 1280x720 вписан в окно (с чёрными полосами).
void Game::viewTransform(float& sc, float& ox, float& oy) const {
    const float sw = static_cast<float>(GetScreenWidth()), sh = static_cast<float>(GetScreenHeight());
    sc = std::min(sw / kScreenW, sh / kScreenH);
    if (sc <= 0.f) sc = 1.f;
    ox = (sw - kScreenW * sc) / 2.f;
    oy = (sh - kScreenH * sc) / 2.f;
}

Vector2 Game::mouse() const {
    const Vector2 m = GetMousePosition();
    float sc, ox, oy;
    viewTransform(sc, ox, oy);
    return {(m.x - ox) / sc, (m.y - oy) / sc};
}

// ------------------------------------------------------------------ профиль

void Game::loadProfile() { profile_ = Profile::loadFile(profilePath_); }
void Game::saveProfile() const { profile_.saveFile(profilePath_); }

// Герои на экране выбора и в забеге уже с улучшениями Наследия (применяются ровно один раз).
void Game::refreshRoster() {
    roster_ = heroRoster();
    for (auto& h : roster_) profile_.meta.applyToHero(h);
}

// Итог забега: за победу над боссом - артефакты, плюс награды за новые достижения.
void Game::recordRunResult() {
    if (runRecorded_ || !run_) return;
    runRecorded_ = true;
    lastReward_ = profile_.recordRun(run_->summary());
    saveProfile();
}

int Game::chosenCount() const {
    int n = 0;
    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i) n += chosen_[i] ? 1 : 0;
    return n;
}

// Новый забег: seed определяет колоду и три предложенных босса. Босс выбирается на экране
// отряда - под него можно подобрать состав.
void Game::rollRun() {
    refreshRoster();  // вдруг в «Наследии» что-то купили
    std::random_device rd;
    nextSeed_ = rd();
    Rng tmp(nextSeed_);
    std::vector<int> ids;
    for (int i = 0; i < kBossCount; ++i) ids.push_back(i);
    tmp.shuffle(ids);
    for (int i = 0; i < 3; ++i) offered_[i] = ids[i];
    bossPick_ = 0;
    bossId_ = offered_[0];
    showBossCard_ = false;
}

void Game::newGame(uint32_t seed) {
    rng_.reseed(seed);
    deck_ = Deck(rng_, bossId_);
    dungeon_ = Dungeon();
    dungeon_.placeShop(rng_);  // торговец появляется сам: случайное место ближе к краю карты
    run_.reset();
    for (auto& s : hand_) s.has = false;
    for (auto& s : hand_) refill(s);
    placementsLeft_ = kMaxPlacements + profile_.meta.extraPlacements();   // «Мастер-строитель»
    discardsLeft_ = kMaxDiscards + profile_.meta.extraDiscards();         // «Запасная колода»
    selected_ = 0;
    screen_ = Screen::Build;
    resultText_.clear();
    resultWin_ = false;
    stepTimer_ = endTimer_ = closeTimer_ = moveCd_ = 0.f;
    speedIdx_ = 1;
    paused_ = false;
    inRest_ = false;
    invOpen_ = false;
    invSel_ = -1;
    clearAnims();
    setMessage("Выбери карту, поверни (R) и поставь рядом с дверью.", false);
}

void Game::refill(Slot& s) {
    if (deck_.empty()) {
        s.has = false;
        return;
    }
    s.card = deck_.draw();
    s.has = true;
}

void Game::setMessage(const std::string& m, bool bad) {
    message_ = m;
    messageBad_ = bad;
}

void Game::startRun() {
    std::vector<Hero> squad;
    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i)
        if (chosen_[i]) squad.push_back(roster_[i]);
    RunOptions options;
    options.meta = profile_.meta;
    options.artifacts = profile_.artifacts;
    run_ = std::make_unique<Run>(dungeon_, static_cast<uint32_t>(rng_.range(1, 1 << 30)), squad, options);
    runRecorded_ = false;
    lastReward_ = RunReward{};
    screen_ = Screen::Run;
    stepTimer_ = endTimer_ = closeTimer_ = moveCd_ = 0.f;
    inRest_ = false;
    invOpen_ = false;
    invSel_ = -1;
    lastFightId_ = 0;
    const Vec2i p = run_->partyPos();
    markerPos_ = {kGridX + p.x * kCell + kCell / 2.f, kGridY + p.y * kCell + kCell / 2.f + 22.f};
    clearAnims();
}

void Game::failBuild(const std::string& why) {
    screen_ = Screen::Result;
    resultWin_ = false;
    resultText_ = why;
}

bool Game::hasAnyMove() const {
    for (const auto& s : hand_) {
        if (!s.has) continue;
        if (s.card.type != RoomType::Boss && discardsLeft_ > 0) return true;
        Card c = s.card;
        for (int rot = 0; rot < 4; ++rot, c.rotate())
            for (int y = 0; y < Dungeon::Height; ++y)
                for (int x = 0; x < Dungeon::Width; ++x)
                    if (dungeon_.check(c, {x, y}) == Dungeon::PlaceResult::Ok) return true;
    }
    return false;
}

void Game::afterBuildAction() {
    if (placementsLeft_ <= 0) {
        if (dungeon_.hasBoss()) startRun();
        else failBuild("Лимит комнат исчерпан, а босс так и не размещён.");
        return;
    }
    if (!hasAnyMove()) {
        if (dungeon_.hasBoss()) startRun();
        else failBuild("Больше некуда ставить карты, а босс не размещён.");
    }
}

// ------------------------------------------------------------------- update

void Game::update(float dt) {
    // Esc: пауза во время игры, назад из меню/настроек
    if (IsKeyPressed(KEY_ESCAPE)) {
        switch (screen_) {
            case Screen::Settings: screen_ = settingsBack_; break;
            case Screen::Achievements:
            case Screen::Legacy:   screen_ = Screen::MainMenu; break;
            case Screen::Pause:    screen_ = prevScreen_; break;
            case Screen::MainMenu: break;
            default:
                prevScreen_ = screen_;
                screen_ = Screen::Pause;
                break;
        }
        return;
    }

    // N - вернуться к выбору героев (новая партия, новые боссы на выбор)
    if (IsKeyPressed(KEY_N) && (screen_ == Screen::Build || screen_ == Screen::Run || screen_ == Screen::Result)) {
        run_.reset();
        clearAnims();
        screen_ = Screen::Select;
        rollRun();
        setMessage("Выбери четырёх героев и босса для похода.", false);
        return;
    }
    switch (screen_) {
        case Screen::MainMenu: updateMainMenu(); break;
        case Screen::Select: updateSelect(); break;
        case Screen::Build: updateBuild(); break;
        case Screen::Run: updateRun(dt); break;
        case Screen::Pause: updatePause(); break;
        case Screen::Settings: updateSettings(); break;
        case Screen::Achievements: updateAchievements(); break;
        case Screen::Legacy: updateLegacy(); break;
        case Screen::Result: break;
    }
}

// ------------------------------------------------------------------ меню

void Game::updateMainMenu() {
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int pick = IsKeyPressed(KEY_ENTER) ? 0 : -1;
    for (int i = 0; i < 5; ++i)
        if (click && inR(m, menuBtn(i))) pick = i;
    if (pick == 0) {
        run_.reset();
        clearAnims();
        rollRun();
        setMessage("Выбери четырёх героев и босса для похода.", false);
        screen_ = Screen::Select;
    } else if (pick == 1) {
        screen_ = Screen::Achievements;
    } else if (pick == 2) {
        legacyMsg_.clear();
        screen_ = Screen::Legacy;
    } else if (pick == 3) {
        settingsBack_ = Screen::MainMenu;
        screen_ = Screen::Settings;
    } else if (pick == 4) {
        quit_ = true;
    }
}

// Достижения: просмотр списка (награда выдаётся автоматически при выполнении условия).
void Game::updateAchievements() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inR(mouse(), kBackBtn)) screen_ = Screen::MainMenu;
}

// «Наследие»: покупка постоянных улучшений за артефакты.
void Game::updateLegacy() {
    const Vector2 m = mouse();
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (inR(m, kBackBtn)) {
        screen_ = Screen::MainMenu;
        return;
    }
    for (int u = 0; u < kUpgradeCount; ++u) {
        if (!inR(m, legacyBuyBtn(u))) continue;
        const UpgradeInfo& info = upgradeInfo(u);
        if (profile_.meta.maxed(u)) {
            legacyMsg_ = info.name + ": уже максимальный уровень.";
        } else if (profile_.artifacts < profile_.meta.nextCost(u)) {
            legacyMsg_ = fmt("Не хватает артефактов: нужно %d, есть %d.", profile_.meta.nextCost(u), profile_.artifacts);
        } else if (profile_.buyUpgrade(u)) {
            saveProfile();
            legacyMsg_ = fmt("Улучшено: %s (ур. %d). Действует в следующих забегах.", info.name.c_str(), profile_.meta.level[u]);
        }
        return;
    }
}

void Game::updatePause() {
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int pick = -1;
    for (int i = 0; i < 4; ++i)
        if (click && inR(m, menuBtn(i, 250))) pick = i;
    if (pick == 0) {
        screen_ = prevScreen_;
    } else if (pick == 1) {
        settingsBack_ = Screen::Pause;
        screen_ = Screen::Settings;
    } else if (pick == 2) {
        run_.reset();
        clearAnims();
        screen_ = Screen::MainMenu;
    } else if (pick == 3) {
        quit_ = true;
    }
}

void Game::updateSettings() {
    const Vector2 m = mouse();
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    const int n = static_cast<int>(resolutions_.size());
    for (int i = 0; i < n; ++i) {
        if (!inR(m, resBtn(i))) continue;
        settings_.width = resolutions_[i].w;
        settings_.height = resolutions_[i].h;
        settings_.fullscreen = false;  // выбор разрешения возвращает оконный режим
        applyDisplay();
        return;
    }
    if (inRect(m, 440, 190 + n * 58 + 14, 400, 48)) {
        settings_.fullscreen = !settings_.fullscreen;
        applyDisplay();
    } else if (inRect(m, 440, 190 + n * 58 + 84, 400, 48)) {
        screen_ = settingsBack_;
    }
}

void Game::updateSelect() {
    if (IsKeyPressed(KEY_B)) showBossCard_ = !showBossCard_;
    if (showBossCard_) {
        if (IsKeyPressed(KEY_ENTER)) showBossCard_ = false;
        return;
    }
    // босс из трёх: Q / W / E или клик по плитке (повторный клик - подробности)
    if (IsKeyPressed(KEY_Q)) bossPick_ = 0;
    if (IsKeyPressed(KEY_W)) bossPick_ = 1;
    if (IsKeyPressed(KEY_E)) bossPick_ = 2;

    const int keys[kMaxRoster] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT};
    Vector2 m = mouse();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i) {
        int x = kSelX + (i % 4) * (kSelCardW + kSelGap);
        int y = kSelY + (i / 4) * (kSelCardH + kSelGap);
        bool toggle = IsKeyPressed(keys[i]) || (click && inRect(m, x, y, kSelCardW, kSelCardH));
        if (!toggle) continue;
        if (chosen_[i]) {
            chosen_[i] = false;
            setMessage("", false);
        } else if (chosenCount() < kPartySize) {
            chosen_[i] = true;
            setMessage("", false);
        } else {
            setMessage("Уже выбрано 4 героя. Сними выбор с кого-нибудь.", true);
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (click && inRect(m, 520 + i * 246, 630, 238, 62)) {
            if (bossPick_ == i) showBossCard_ = true;
            bossPick_ = i;
        }
    }
    bossId_ = offered_[bossPick_];
    if (IsKeyPressed(KEY_ENTER)) {
        if (chosenCount() == kPartySize) newGame(nextSeed_);
        else setMessage(fmt("Нужно выбрать ровно %d героя.", kPartySize), true);
    }
}

bool Game::cellAtMouse(Vec2i& out) const {
    Vector2 m = mouse();
    int mx = static_cast<int>(m.x) - kGridX;
    int my = static_cast<int>(m.y) - kGridY;
    if (mx < 0 || my < 0) return false;
    out = {mx / kCell, my / kCell};
    return dungeon_.inBounds(out);
}

void Game::updateBuild() {
    if (IsKeyPressed(KEY_ONE)) selected_ = 0;
    if (IsKeyPressed(KEY_TWO)) selected_ = 1;
    if (IsKeyPressed(KEY_THREE)) selected_ = 2;
    if (IsKeyPressed(KEY_FOUR)) selected_ = 3;

    Vector2 m = mouse();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    for (int i = 0; i < kHand; ++i) {
        int x = kGridX + i * (kCardW + 8);
        if (click && inRect(m, x, kCardY, kCardW, kCardH)) {
            selected_ = i;
            return;
        }
    }

    Slot& s = hand_[selected_];

    if (s.has && (IsKeyPressed(KEY_R) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || GetMouseWheelMove() != 0.f))
        s.card.rotate();

    if (IsKeyPressed(KEY_D) && s.has) {
        if (s.card.type == RoomType::Boss) {
            setMessage("Босса сбросить нельзя.", true);
        } else if (discardsLeft_ <= 0) {
            setMessage("Сбросов не осталось.", true);
        } else {
            --discardsLeft_;
            refill(s);
            setMessage("Карта сброшена.", false);
            afterBuildAction();
        }
        return;
    }

    if (IsKeyPressed(KEY_ENTER)) {
        if (dungeon_.hasBoss()) startRun();
        else setMessage("Сначала нужно разместить карту босса.", true);
        return;
    }

    Vec2i cell;
    if (click && s.has && cellAtMouse(cell)) {
        switch (dungeon_.check(s.card, cell)) {
            case Dungeon::PlaceResult::Ok:
                dungeon_.place(s.card, cell);
                --placementsLeft_;
                refill(s);
                setMessage(dungeon_.hasBoss() ? "Босс на месте! Enter - начать поход." : "Комната размещена.", false);
                afterBuildAction();
                break;
            case Dungeon::PlaceResult::Occupied:
                setMessage("Клетка занята.", true);
                break;
            case Dungeon::PlaceResult::NoConnection:
                setMessage("Нужна стыковка: дверь к двери соседней комнаты.", true);
                break;
            case Dungeon::PlaceResult::ShopOnly:
                setMessage("Торговец ещё не подключён: комната должна стыковаться и с подземельем, и с дверью торговца.", true);
                break;
            default:
                break;
        }
    }
}

// ------------------------------------------------------------------- поход

void Game::updateRun(float dt) {
    if (IsKeyPressed(KEY_SPACE)) paused_ = !paused_;
    if (IsKeyPressed(KEY_UP) && speedIdx_ < 4) ++speedIdx_;
    if (IsKeyPressed(KEY_DOWN) && speedIdx_ > 0) --speedIdx_;

    const float animDt = paused_ ? 0.f : dt * kSpeeds[speedIdx_];
    if (run_->fightId() != lastFightId_) {
        lastFightId_ = run_->fightId();
        clearAnims();
    }
    updateAnims(animDt);

    // маркер отряда плавно скользит в текущую комнату
    {
        const Vec2i p = run_->partyPos();
        Vector2 target{kGridX + p.x * kCell + kCell / 2.f, kGridY + p.y * kCell + kCell / 2.f + 22.f};
        float dx = target.x - markerPos_.x, dy = target.y - markerPos_.y;
        float len = std::sqrt(dx * dx + dy * dy), step = 700.f * dt;
        if (len <= step) markerPos_ = target;
        else markerPos_ = {markerPos_.x + dx / len * step, markerPos_.y + dy / len * step};
    }

    // привал: ждём решения игрока, время стоит
    if (run_->phase() == RunPhase::RestChoice) {
        if (!inRest_) {
            inRest_ = true;
            restStage_ = 0;
            restMsg_.clear();
            std::fill(std::begin(restPick_), std::end(restPick_), false);
        }
        updateRestChoice();
        return;
    }
    inRest_ = false;

    // инвентарь: I / Tab (вне боя и на карточке босса)
    if ((IsKeyPressed(KEY_I) || IsKeyPressed(KEY_TAB)) && run_->canManage()) {
        invOpen_ = !invOpen_;
        invSel_ = -1;
        invMsg_.clear();
    }
    if (!run_->canManage()) invOpen_ = false;

    // после боя: показ закрывается сам, а найденная добыча сразу открывает инвентарь
    if (run_->phase() == RunPhase::Exploring && run_->combat() && !run_->finished()) {
        closeTimer_ += animDt;
        if (closeTimer_ > 1.0f) {
            run_->closeCombat();
            closeTimer_ = 0.f;
        }
    } else {
        closeTimer_ = 0.f;
    }
    if (run_->phase() == RunPhase::Exploring && !run_->combat() && run_->hasNewLoot()) {
        run_->ackLoot();
        invOpen_ = true;
        invSel_ = -1;
        invMsg_ = "Найдена добыча! Раздай предметы героям.";
    }

    switch (run_->phase()) {
        case RunPhase::BossIntro:
            if (invOpen_) {
                updateInventory();
            } else if (IsKeyPressed(KEY_ENTER)) {
                run_->beginBossFight();
                stepTimer_ = 0.f;
            }
            break;
        case RunPhase::Exploring:
            if (invOpen_) updateInventory();
            else if (!run_->combat()) updateExplore(dt);
            break;
        case RunPhase::Shop:
            if (invOpen_) updateInventory();
            else updateShop();
            break;
        case RunPhase::Fighting:
            if (!paused_) {
                stepTimer_ += animDt;
                while (run_->phase() == RunPhase::Fighting && stepTimer_ >= kCombatStep) {
                    stepTimer_ -= kCombatStep;
                    run_->step();
                    onRunStep();
                }
            }
            break;
        default:
            break;
    }

    if (run_->finished()) {
        recordRunResult();  // артефакты и достижения засчитываются сразу, даже если выйти раньше показа итога
        endTimer_ += dt;  // небольшая пауза, чтобы увидеть последний удар
        if (endTimer_ > 1.6f) {
            screen_ = Screen::Result;
            resultWin_ = run_->phase() == RunPhase::Victory;
            resultText_.clear();
        }
    }
}

// Ручное управление группой: W/A/S/D - шаг в соседнюю комнату через дверь.
void Game::updateExplore(float dt) {
    if (IsKeyPressed(KEY_T) && run_->openShop()) {  // вернуться к торговле, не выходя из комнаты
        shopMsg_.clear();
        return;
    }
    moveCd_ = std::max(0.f, moveCd_ - dt);
    if (moveCd_ > 0.f) return;
    Dir d;
    if (IsKeyDown(KEY_W)) d = North;
    else if (IsKeyDown(KEY_S)) d = South;
    else if (IsKeyDown(KEY_A)) d = West;
    else if (IsKeyDown(KEY_D)) d = East;
    else return;
    if (run_->move(d)) {
        moveCd_ = kMoveCooldown;
        stepTimer_ = 0.f;
    }
}

void Game::updateRestChoice() {
    Vector2 m = mouse();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const auto& hs = run_->party().heroes;

    if (restStage_ == 0) {
        const int by = kGridY + 130, bw = 224, bh = 220;
        bool anyDead = false;
        for (const auto& h : hs) anyDead |= !h.alive();
        if (IsKeyPressed(KEY_ONE) || (click && inRect(m, kGridX + 14, by, bw, bh))) {
            run_->restHeal();
            stepTimer_ = 0.f;
            restMsg_.clear();
        } else if (IsKeyPressed(KEY_TWO) || (click && inRect(m, kGridX + 250, by, bw, bh))) {
            restStage_ = 1;
            restMsg_.clear();
            std::fill(std::begin(restPick_), std::end(restPick_), false);
        } else if (IsKeyPressed(KEY_THREE) || (click && inRect(m, kGridX + 486, by, bw, bh))) {
            const bool canCoins = run_->party().coins >= run_->reviveCoinCost();
            const bool canArtifact = run_->artifacts() >= Run::kReviveArtifactCost;
            if (!anyDead) restMsg_ = "Павших героев нет.";
            else if (!canCoins && !canArtifact)
                restMsg_ = fmt("Нужно %d монет или %d артефакт. Есть: %d монет, %d артефактов.", run_->reviveCoinCost(),
                               Run::kReviveArtifactCost, run_->party().coins, run_->artifacts());
            else {
                restStage_ = 2;
                restMsg_.clear();
            }
        }
        return;
    }

    // стадия 2: выбор павшего героя для воскрешения (платная услуга, привал продолжается)
    if (restStage_ == 2) {
        std::vector<int> dead;
        for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i)
            if (!hs[i].alive()) dead.push_back(i);
        if (IsKeyPressed(KEY_B) || dead.empty()) {
            restStage_ = 0;
            return;
        }
        const int rk[4] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
        for (int k = 0; k < static_cast<int>(dead.size()); ++k) {
            const int rx = kGridX + 40, ry = kGridY + 100 + k * 84;
            const bool onCoin = click && inRect(m, rx + 400, ry + 14, 112, 44);
            const bool onArtifact = click && inRect(m, rx + 520, ry + 14, 112, 44);
            const bool rowPick = (k < 4 && IsKeyPressed(rk[k])) || (click && !onCoin && !onArtifact && inRect(m, rx, ry, 640, 72));
            bool useArtifact = onArtifact, useCoins = onCoin;
            if (rowPick) {  // клик по строке / клавиша: сначала монеты, если не хватает - артефакт
                if (run_->party().coins >= run_->reviveCoinCost()) useCoins = true;
                else useArtifact = true;
            }
            if (!useCoins && !useArtifact) continue;
            bool ok;
            if (useArtifact) {
                ok = run_->restReviveArtifact(dead[k]);
                if (ok) {  // артефакты - постоянная валюта: трата сохраняется сразу
                    profile_.artifacts = run_->artifacts();
                    saveProfile();
                }
            } else {
                ok = run_->restRevive(dead[k]);
            }
            if (ok)
                restMsg_ = fmt("%s воскрешён: %d%% здоровья.", hs[dead[k]].name.c_str(),
                               useArtifact ? Run::kReviveArtifactPct : Run::kRevivePct);
            else
                restMsg_ = useArtifact ? "Нет артефактов." : "Не хватает монет.";
            restStage_ = 0;
            return;
        }
        return;
    }

    const int req = std::min(Run::kBuffTargets, run_->party().aliveCount());
    const int keys[4] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
    int picked = 0;
    for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i) picked += restPick_[i] ? 1 : 0;

    for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i) {
        if (!hs[i].alive()) continue;
        bool toggle = (i < 4 && IsKeyPressed(keys[i])) ||
                      (click && inRect(m, kGridX + 40, kGridY + 100 + i * 84, 640, 72));
        if (!toggle) continue;
        if (restPick_[i]) {
            restPick_[i] = false;
            --picked;
        } else if (picked < req) {
            restPick_[i] = true;
            ++picked;
        }
    }
    if (IsKeyPressed(KEY_B)) {
        restStage_ = 0;
        return;
    }
    if (IsKeyPressed(KEY_ENTER) && picked == req) {
        std::vector<int> idx;
        for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i)
            if (restPick_[i]) idx.push_back(i);
        run_->restBuff(idx);
        stepTimer_ = 0.f;
    }
}

// Торговец: клик по лоту - купить, Enter - уйти. Монеты падают с врагов.
void Game::updateShop() {
    const Vector2 m = mouse();
    if (IsKeyPressed(KEY_ENTER)) {
        run_->leaveShop();
        shopMsg_.clear();
        return;
    }
    const auto* stock = run_->shopStock();
    if (!stock || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    for (int i = 0; i < static_cast<int>(stock->size()); ++i) {
        if (!inRect(m, kGridX + 20, kGridY + 88 + i * 76, 680, 68)) continue;
        const ShopOffer& o = (*stock)[i];
        if (o.sold) {
            shopMsg_ = "Этот лот уже продан.";
        } else if (run_->party().coins < o.price) {
            shopMsg_ = fmt("Не хватает монет: нужно %d, есть %d.", o.price, run_->party().coins);
        } else if (run_->buy(i)) {
            shopMsg_ = "Куплено: " + o.item.name + ". Надеть - в инвентаре (I).";
        }
        return;
    }
}

// Инвентарь: выбрать предмет в рюкзаке, затем кликнуть слот героя (оружие/броня/реликвия)
// или портрет героя (для зелья). Клик по надетому предмету снимает его.
void Game::updateInventory() {
    const Party& p = run_->party();
    Vector2 m = mouse();
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) invSel_ = -1;
    if (invSel_ >= static_cast<int>(p.bag.size())) invSel_ = -1;
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    for (int i = 0; i < static_cast<int>(p.bag.size()) && i < kBagCells; ++i) {
        if (inR(m, bagCell(i))) {
            invSel_ = (invSel_ == i) ? -1 : i;
            invMsg_.clear();
            return;
        }
    }
    for (int h = 0; h < static_cast<int>(p.heroes.size()) && h < kMaxRoster; ++h) {
        for (int k = 0; k < 3; ++k) {
            if (!inR(m, slotRect(h, k))) continue;
            if (invSel_ >= 0) {
                const Item it = p.bag[invSel_];
                if (it.kind != kSlotKinds[k]) {
                    invMsg_ = fmt("Это %s - ему нужен другой слот.", itemKindName(it.kind));
                } else if (run_->equipItem(invSel_, h)) {
                    invSel_ = -1;
                    invMsg_ = "Надето.";
                } else {
                    invMsg_ = "Павший герой не может носить снаряжение.";
                }
            } else if (p.heroes[h].slot(kSlotKinds[k])) {
                run_->unequipItem(h, kSlotKinds[k]);
                invMsg_ = "Снято в рюкзак.";
            }
            return;
        }
        if (invSel_ >= 0 && inR(m, heroPanel(h)) && p.bag[invSel_].kind == ItemKind::Potion) {
            if (run_->usePotion(invSel_, h)) {
                invSel_ = -1;
                invMsg_ = "Зелье выпито.";
            } else {
                invMsg_ = "Зелье не нужно: герой здоров или пал.";
            }
            return;
        }
    }
}

// --------------------------------------------------------- боевая анимация

Vector2 Game::heroCenter(int i) const { return {kGridX + 150.f, kGridY + 110.f + i * 120.f}; }

Vector2 Game::enemyCenter(int slot, int n) const {
    const float spacing = n <= 3 ? 140.f : 120.f;
    return {kGridX + 560.f, kGridY + 295.f + (static_cast<float>(slot) - (n - 1) / 2.f) * spacing};
}

Vector2 Game::lungeOffset(bool hero, int idx) const {
    if (!lunge_.active || lunge_.hero != hero || lunge_.idx != idx) return {0, 0};
    float p = std::min(1.f, lunge_.t / kLungeDur);
    float k = 46.f * std::sin(3.14159265f * p);
    return {lunge_.dir.x * k, lunge_.dir.y * k};
}

void Game::clearAnims() {
    lunge_.active = false;
    projs_.clear();
    floaters_.clear();
    for (auto& f : heroFlash_) f = Flash{};
    for (auto& f : enemyFlash_) f = Flash{};
}

// Вызывается после каждого шага боя: запускаем анимацию его действий.
void Game::onRunStep() { spawnActions(); }

void Game::spawnActions() {
    const Combat* c = run_->combat();
    if (!c || c->events().empty()) return;
    const int slots = c->slotCount();
    const auto& heroes = run_->party().heroes;
    const auto& enemies = c->enemies();

    auto posOf = [&](bool hero, int idx) {
        return hero ? heroCenter(idx) : enemyCenter(enemies[idx].slot, slots);
    };
    auto halfSize = [&](bool hero, int idx) { return hero ? 32.f : enemies[idx].size / 2.f; };

    for (const CombatAction& a : c->events()) {
        Vector2 from = a.actor >= 0 ? posOf(a.actorHero, a.actor) : Vector2{0, 0};

        // реплика босса: «Делится!», «Проглатывает!» и т.п.
        if (a.kind == CombatAction::Shout) {
            Floater f;
            f.pos = {from.x, from.y - halfSize(a.actorHero, a.actor) - 26.f};
            f.text = a.text;
            f.col = kAccent;
            f.size = 24.f;
            f.fired = true;
            f.delay = 0.f;
            floaters_.push_back(f);
            continue;
        }

        Vector2 cen{0, 0};
        for (const auto& h : a.hits) {
            Vector2 p = posOf(h.targetHero, h.idx);
            cen.x += p.x;
            cen.y += p.y;
        }
        if (!a.hits.empty()) {
            cen.x /= a.hits.size();
            cen.y /= a.hits.size();
        }

        float delay = 0.f;
        Color projCol = kHeal;
        switch (a.kind) {
            case CombatAction::Melee: {
                delay = 0.22f;
                float dx = cen.x - from.x, dy = cen.y - from.y;
                float len = std::sqrt(dx * dx + dy * dy);
                lunge_.active = true;
                lunge_.hero = a.actorHero;
                lunge_.idx = a.actor;
                lunge_.t = 0.f;
                lunge_.dir = len > 1.f ? Vector2{dx / len, dy / len} : Vector2{0, 0};
                break;
            }
            case CombatAction::Ranged:
                delay = kProjDur;
                projCol = a.actorHero ? roleColor(heroes[a.actor].role) : kDamage;
                break;
            case CombatAction::Heal:
                delay = 0.3f;
                break;
            case CombatAction::Effect:
                delay = 0.25f;
                break;
            default:  // Potion
                delay = 0.f;
                break;
        }

        for (size_t hi = 0; hi < a.hits.size(); ++hi) {
            const auto& h = a.hits[hi];
            // цепное лечение: каждая следующая цель получает порцию чуть позже предыдущей
            const float stagger = (a.kind == CombatAction::Heal && a.hits.size() > 1) ? 0.16f * static_cast<float>(hi) : 0.f;
            const float d = delay + stagger;
            Vector2 to = posOf(h.targetHero, h.idx);
            if (a.kind == CombatAction::Ranged || (a.kind == CombatAction::Heal && a.actor >= 0)) {
                Proj p;
                p.from = from;
                p.to = to;
                p.col = projCol;
                p.t = -stagger;  // отрицательное время - снаряд ещё не вылетел
                projs_.push_back(p);
            }
            Floater f;
            float jitter = static_cast<float>(static_cast<int>(uiRng_() % 21) - 10);
            f.pos = {to.x + jitter, to.y - halfSize(h.targetHero, h.idx) - 4.f};
            f.text = h.heal ? fmt("+%d", h.amount) : fmt("-%d%s", h.amount, h.crit ? "!" : "");
            f.col = h.heal ? kHeal : kDamage;
            f.size = h.crit ? 32.f : 22.f;
            f.delay = d;
            f.hero = h.targetHero;
            f.idx = h.idx;
            f.heal = h.heal;
            floaters_.push_back(f);

            if (!h.targetHero && !h.heal && h.coins > 0) {  // с врага выпали монеты
                Floater cf;
                cf.pos = {to.x, to.y - halfSize(false, h.idx) + 20.f};
                cf.text = fmt("+%d мон.", h.coins);
                cf.col = kCoin;
                cf.size = 18.f;
                cf.delay = d;
                cf.fired = true;
                floaters_.push_back(cf);
            }
        }
    }
}

void Game::updateAnims(float dt) {
    if (lunge_.active) {
        lunge_.t += dt;
        if (lunge_.t >= kLungeDur) lunge_.active = false;
    }
    for (auto& p : projs_) p.t += dt;
    projs_.erase(std::remove_if(projs_.begin(), projs_.end(), [](const Proj& p) { return p.t >= kProjDur; }),
                 projs_.end());

    for (auto& f : floaters_) {
        f.age += dt;
        if (!f.fired && f.age >= f.delay) {
            f.fired = true;  // момент попадания - вспышка на цели
            int fi = f.idx;
            if (fi < 0) fi = 0;
            if (fi > kMaxRoster - 1) fi = kMaxRoster - 1;
            Flash& fl = (f.hero ? heroFlash_ : enemyFlash_)[fi];
            fl.t = kFlashDur;
            fl.heal = f.heal;
        }
    }
    floaters_.erase(std::remove_if(floaters_.begin(), floaters_.end(),
                                   [](const Floater& f) { return f.age > f.delay + kFloatLife; }),
                    floaters_.end());

    for (auto& f : heroFlash_) f.t = std::max(0.f, f.t - dt);
    for (auto& f : enemyFlash_) f.t = std::max(0.f, f.t - dt);
}

// ------------------------------------------------------------------ картинки

// Ленивая загрузка assets/<rel>. nullptr - файла нет, нужно рисовать шаблон.
const Texture2D* Game::tex(const std::string& rel) const {
    auto it = tex_.find(rel);
    if (it == tex_.end()) {
        Texture2D t{};
        std::string base = rel;  // имя из перечня оканчивается на .png; допускается и .jpg с тем же именем
        if (base.size() > 4 && base.compare(base.size() - 4, 4, ".png") == 0) base.resize(base.size() - 4);
        for (const char* ext : {".png", ".jpg"}) {
            const std::string path = assetDir_ + "/" + base + ext;
            if (!FileExists(path.c_str())) continue;
            t = LoadTexture(path.c_str());
            if (t.id != 0) {
                SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
        it = tex_.emplace(rel, t).first;
    }
    return it->second.id != 0 ? &it->second : nullptr;
}

// Картинка вписывается в прямоугольник с сохранением пропорций (по центру).
bool Game::drawArt(const std::string& rel, float x, float y, float w, float h, Color tint) const {
    const Texture2D* t = tex(rel);
    if (!t || t->width <= 0 || t->height <= 0) return false;
    const float sc = std::min(w / t->width, h / t->height);
    const float dw = t->width * sc, dh = t->height * sc;
    DrawTexturePro(*t, Rectangle{0.f, 0.f, static_cast<float>(t->width), static_cast<float>(t->height)},
                   Rectangle{x + (w - dw) / 2.f, y + (h - dh) / 2.f, dw, dh}, Vector2{0.f, 0.f}, 0.f, tint);
    return true;
}

// Фон растягивается на весь прямоугольник.
void Game::drawBackground(const std::string& rel, float x, float y, float w, float h) const {
    const Texture2D* t = tex(rel);
    if (!t || t->width <= 0 || t->height <= 0) return;
    DrawTexturePro(*t, Rectangle{0.f, 0.f, static_cast<float>(t->width), static_cast<float>(t->height)},
                   Rectangle{x, y, w, h}, Vector2{0.f, 0.f}, 0.f, Color{255, 255, 255, 255});
}

// Квадратная иконка: картинка (или запасная) либо цветной шаблон с буквой.
void Game::drawIconBox(const std::string& art, const std::string& art2, Color fallback, const std::string& glyph,
                       float x, float y, float size, Color tint) const {
    if (drawArt(art, x, y, size, size, tint)) return;
    if (!art2.empty() && drawArt(art2, x, y, size, size, tint)) return;
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(size), static_cast<int>(size), fallback);
    drawTextCentered(glyph, x + size / 2.f, y + size * 0.5f - size * 0.27f, size * 0.5f, {20, 20, 28, 255});
}

void Game::drawItemIcon(const Item& it, float x, float y, float size) const {
    drawIconBox(itemArt(it), "", itemColor(it.kind), itemGlyph(it.kind), x, y, size);
}

void Game::drawCoin(float cx, float cy, float radius) const {
    if (drawArt("ui/coin.png", cx - radius, cy - radius, radius * 2, radius * 2)) return;
    DrawCircle(static_cast<int>(cx), static_cast<int>(cy), radius, kCoin);
}

// Звезда-фигура (без шрифтов: в шрифте нет символа ★). Рисуем обе обмотки треугольников,
// чтобы заливка была видна при любом отсечении граней.
void Game::drawStar(float cx, float cy, float r, Color col) const {
    Vector2 v[10];
    for (int i = 0; i < 10; ++i) {
        const float ang = -1.5707963f + static_cast<float>(i) * 0.62831853f;
        const float rad = (i % 2 == 0) ? r : r * 0.42f;
        v[i] = {cx + std::cos(ang) * rad, cy + std::sin(ang) * rad};
    }
    const Vector2 c{cx, cy};
    for (int i = 0; i < 10; ++i) {
        DrawTriangle(c, v[i], v[(i + 1) % 10], col);
        DrawTriangle(c, v[(i + 1) % 10], v[i], col);
    }
}

void Game::drawStars(float x, float y, int count, float r) const {
    const float step = r * 2.15f;
    for (int i = 0; i < count; ++i) {
        const float cx = x + r + static_cast<float>(i) * step;
        drawStar(cx, y, r + 1.6f, Color{12, 12, 18, 255});  // тёмный контур для читаемости на любом фоне
        drawStar(cx, y, r, Color{255, 222, 70, 255});
    }
}

// Ячейка рюкзака: фон, иконка предмета, рамка цвета уровня, звёзды уровня.
void Game::drawBagCell(int x, int y, int size, const Item* it, bool selected, bool hover) const {
    if (!drawArt("ui/slot.png", static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), static_cast<float>(size)))
        DrawRectangle(x, y, size, size, selected ? Color{58, 60, 84, 255} : hover ? Color{44, 46, 60, 255} : Color{34, 36, 46, 255});
    if (it) {
        drawItemIcon(*it, x + 6.f, y + 6.f, size - 12.f);
        DrawRectangle(x + 1, y + size - 19, size - 2, 18, {0, 0, 0, 150});  // тёмная плашка под звёздами
        drawStars(x + 4.f, y + size - 10.f, it->tier, 6.5f);
    }
    DrawRectangleLines(x, y, size, size, selected ? kAccent : hover ? Color{200, 200, 215, 255} : (it ? tierColor(it->tier) : kGridLine));
    if (selected) DrawRectangleLines(x + 1, y + 1, size - 2, size - 2, kAccent);
}

// Ячейка снаряжения героя: надетый предмет или значок пустого слота.
void Game::drawEquipSlot(int x, int y, int size, ItemKind kind, const std::optional<Item>& item, bool fits, bool hover) const {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fs = static_cast<float>(size);
    if (!drawArt("ui/slot.png", fx, fy, fs, fs)) DrawRectangle(x, y, size, size, hover ? Color{44, 46, 60, 255} : Color{36, 38, 50, 255});
    if (item) {
        drawItemIcon(*item, fx + 5.f, fy + 5.f, fs - 10.f);
        DrawRectangle(x + 1, y + size - 15, size - 2, 14, {0, 0, 0, 150});
        drawStars(fx + 3.f, fy + fs - 8.f, item->tier, 5.f);
    } else {
        const char* key = kind == ItemKind::Weapon ? "ui/slot_weapon.png" : kind == ItemKind::Armor ? "ui/slot_armor.png" : "ui/slot_relic.png";
        if (!drawArt(key, fx + 5.f, fy + 5.f, fs - 10.f, fs - 10.f)) {
            drawTextCentered(kind == ItemKind::Weapon ? "Оруж." : kind == ItemKind::Armor ? "Броня" : "Релик.", fx + fs / 2.f, fy + fs / 2.f - 7.f, 12, kDim);
        }
    }
    DrawRectangleLines(x, y, size, size, fits ? kGood : hover ? kAccent : (item ? tierColor(item->tier) : kGridLine));
    if (fits) DrawRectangleLines(x + 1, y + 1, size - 2, size - 2, kGood);
}

// Всплывающая подсказка у курсора.
void Game::drawItemTooltip(const Item& it, Vector2 at) const {
    const std::string l1 = it.name;
    const std::string l2 = fmt("%s, ур.%d", itemKindName(it.kind), it.tier);
    const std::string l3 = itemBonusText(it);
    const float w = std::max({textWidth(l1, 16), textWidth(l2, 13) + 8.f + it.tier * 15.f, textWidth(l3, 14)}) + 20.f;
    const float h = 66.f;
    float x = at.x + 16.f, y = at.y + 16.f;
    if (x + w > kGridX + kArenaW) x = at.x - w - 8.f;
    if (y + h > kGridY + kArenaH) y = at.y - h - 8.f;
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), {14, 15, 20, 245});
    DrawRectangleLines(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), tierColor(it.tier));
    drawText(l1, x + 10.f, y + 6.f, 16, tierColor(it.tier));
    drawText(l2, x + 10.f, y + 28.f, 13, kDim);
    drawStars(x + 10.f + textWidth(l2, 13) + 8.f, y + 35.f, it.tier, 7.f);
    drawText(l3, x + 10.f, y + 46.f, 14, kAccent);
}

// --------------------------------------------------------------------- text

float Game::textWidth(const std::string& s, float size) const {
    return MeasureTextEx(font_, s.c_str(), size, 1.f).x;
}

void Game::drawText(const std::string& s, float x, float y, float size, Color c) const {
    DrawTextEx(font_, s.c_str(), {x, y}, size, 1.f, c);
}

void Game::drawTextCentered(const std::string& s, float cx, float y, float size, Color c) const {
    drawText(s, cx - textWidth(s, size) / 2.f, y, size, c);
}

std::vector<std::string> Game::wrap(const std::string& s, float maxW, float size) const {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string word, line;
    while (iss >> word) {
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(t, size) > maxW) {
            out.push_back(line);
            line = word;
        } else {
            line = t;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

void Game::drawBar(int x, int y, int w, int h, float ratio, Color fill) const {
    ratio = std::max(0.f, std::min(1.f, ratio));
    DrawRectangle(x, y, w, h, {50, 50, 60, 255});
    DrawRectangle(x, y, static_cast<int>(w * ratio), h, fill);
}

// ------------------------------------------------------------------- drawing

void Game::drawRoom(int px, int py, int cell, const Card& c, bool labels, float alpha, bool dim) const {
    const int pad = cell / 8;
    const int dw = cell / 5;
    const int mid = cell / 2;
    Color col = roomColor(c.type);
    Color door = kDoor;
    if (dim) {
        col = {static_cast<unsigned char>(col.r * 6 / 10), static_cast<unsigned char>(col.g * 6 / 10),
               static_cast<unsigned char>(col.b * 6 / 10), 255};
        door = {static_cast<unsigned char>(door.r * 6 / 10), static_cast<unsigned char>(door.g * 6 / 10),
                static_cast<unsigned char>(door.b * 6 / 10), 255};
    }
    if (alpha < 1.f) {
        col = Fade(col, alpha);
        door = Fade(door, alpha);
    }
    const std::string art = roomArt(c.type);
    const bool hasArt = tex(art) != nullptr;
    if (hasArt) {
        Color tint = dim ? Color{150, 150, 150, 255} : Color{255, 255, 255, 255};
        if (alpha < 1.f) tint = Fade(tint, alpha);
        drawArt(art, static_cast<float>(px + pad), static_cast<float>(py + pad), static_cast<float>(cell - 2 * pad),
                static_cast<float>(cell - 2 * pad), tint);
        if (labels) DrawRectangle(px + pad, py + cell / 2 - 18, cell - 2 * pad, 38, {0, 0, 0, 120});  // плашка под подпись
    } else {
        DrawRectangle(px + pad, py + pad, cell - 2 * pad, cell - 2 * pad, col);
    }
    if (hasDoor(c.doors, North)) DrawRectangle(px + mid - dw / 2, py, dw, pad + 2, door);
    if (hasDoor(c.doors, South)) DrawRectangle(px + mid - dw / 2, py + cell - pad - 2, dw, pad + 2, door);
    if (hasDoor(c.doors, West)) DrawRectangle(px, py + mid - dw / 2, pad + 2, dw, door);
    if (hasDoor(c.doors, East)) DrawRectangle(px + cell - pad - 2, py + mid - dw / 2, pad + 2, dw, door);
    if (labels) {
        const float nameSize = textWidth(roomTypeName(c.type), 16) > cell - 16.f ? 13.f : 16.f;
        drawTextCentered(roomTypeName(c.type), px + cell / 2.f, py + cell / 2.f - 16, nameSize, kText);
        if (c.difficulty > 0) drawTextCentered(std::string(c.difficulty, '*'), px + cell / 2.f, py + cell / 2.f + 2, 18, kText);
    }
}

void Game::drawGrid(bool buildMode) {
    for (int y = 0; y < Dungeon::Height; ++y)
        for (int x = 0; x < Dungeon::Width; ++x) {
            int px = kGridX + x * kCell, py = kGridY + y * kCell;
            DrawRectangle(px, py, kCell, kCell, kEmpty);
            DrawRectangleLines(px, py, kCell, kCell, kGridLine);
        }

    const Slot& sel = hand_[selected_];
    if (buildMode && sel.has) {
        for (int y = 0; y < Dungeon::Height; ++y)
            for (int x = 0; x < Dungeon::Width; ++x)
                if (dungeon_.check(sel.card, {x, y}) == Dungeon::PlaceResult::Ok)
                    DrawRectangle(kGridX + x * kCell + kCell / 2 - 3, kGridY + y * kCell + kCell / 2 - 3, 6, 6, kGood);
    }

    const Dungeon& dg = (!buildMode && run_) ? run_->dungeon() : dungeon_;
    for (const auto& kv : dg.rooms()) {
        const Room& r = kv.second;
        bool dim = !buildMode && r.visited && r.card.type != RoomType::Start;
        const int rx = kGridX + r.pos.x * kCell, ry = kGridY + r.pos.y * kCell;
        drawRoom(rx, ry, kCell, r.card, true, 1.f, dim);
        if (r.card.type == RoomType::Shop && !dg.connected(r.pos)) {  // торговец ждёт подключения
            DrawRectangleLines(rx + 4, ry + 4, kCell - 8, kCell - 8, kBad);
            drawTextCentered("не подключён", rx + kCell / 2.f, ry + kCell - 20.f, 11, kBad);
        }
    }

    if (buildMode && sel.has) {
        Vec2i cell;
        if (cellAtMouse(cell) && !dungeon_.roomAt(cell)) {
            int px = kGridX + cell.x * kCell, py = kGridY + cell.y * kCell;
            bool ok = dungeon_.check(sel.card, cell) == Dungeon::PlaceResult::Ok;
            drawRoom(px, py, kCell, sel.card, false, 0.55f, false);
            DrawRectangleLines(px + 1, py + 1, kCell - 2, kCell - 2, ok ? kGood : kBad);
        }
    }
}

void Game::draw() {
    const bool menuBg = screen_ == Screen::MainMenu || screen_ == Screen::Settings || screen_ == Screen::Achievements ||
                        screen_ == Screen::Legacy;
    drawBackground(menuBg ? "backgrounds/menu.png" : "backgrounds/game.png", 0.f, 0.f, kScreenW, kScreenH);
    switch (screen_) {
        case Screen::MainMenu: drawMainMenu(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Achievements: drawAchievements(); break;
        case Screen::Legacy: drawLegacy(); break;
        case Screen::Pause:
            drawScreen(prevScreen_);
            drawPause();
            break;
        default: drawScreen(screen_); break;
    }
}

void Game::drawScreen(Screen sc) {
    switch (sc) {
        case Screen::Select: drawSelect(); break;
        case Screen::Build: drawBuild(); break;
        case Screen::Run: drawRunView(); break;
        case Screen::Result:
            if (run_) drawRunView();
            else drawBuild();
            drawResult();
            break;
        default: break;
    }
}

// ------------------------------------------------------------------ меню

void Game::drawButton(int x, int y, int w, int h, const std::string& label, bool selected, bool enabled) const {
    const bool hover = enabled && inRect(mouse(), x, y, w, h);
    DrawRectangle(x, y, w, h, !enabled ? Color{28, 29, 36, 255} : hover ? Color{52, 56, 74, 255} : Color{36, 38, 52, 255});
    DrawRectangleLines(x, y, w, h, selected ? kGood : hover ? kAccent : kGridLine);
    if (selected) DrawRectangleLines(x + 1, y + 1, w - 2, h - 2, kGood);
    drawTextCentered(label, x + w / 2.f, y + h / 2.f - 11.f, 22, enabled ? kText : kDim);
}

void Game::drawMainMenu() {
    drawTextCentered("DUNGEON CARDS", kScreenW / 2.f, 90.f, 60, kAccent);
    drawTextCentered("Строй подземелье из карт, веди отряд, победи босса", kScreenW / 2.f, 170.f, 20, kDim);

    // постоянная валюта: артефакты (не сбрасываются между забегами)
    drawArtifact(kScreenW - 220.f, 36.f, 14.f);
    drawText(fmt("Артефакты: %d", profile_.artifacts), kScreenW - 198.f, 24.f, 24, Color{190, 150, 255, 255});

    const std::string labels[5] = {"Играть", fmt("Достижения  %d/%d", profile_.unlockedCount(), static_cast<int>(achievements().size())),
                                   "Наследие", "Настройки", "Выход"};
    for (int i = 0; i < 5; ++i) {
        const R r = menuBtn(i);
        drawButton(r.x, r.y, r.w, r.h, labels[i], false, true);
    }
    drawTextCentered(fmt("Побед: %d из %d забегов.   Победа над боссом: +%d артефакта.", profile_.wins, profile_.runs, kBossArtifacts),
                     kScreenW / 2.f, 662.f, 15, kDim);
    drawTextCentered(fmt("Окно: %dx%d%s", GetScreenWidth(), GetScreenHeight(), IsWindowFullscreen() ? " (полный экран)" : ""),
                     kScreenW / 2.f, 686.f, 15, kDim);
}

// Артефакт: иконка ui/artifact.png или шаблон - фиолетовый ромб.
void Game::drawArtifact(float cx, float cy, float r) const {
    if (drawArt("ui/artifact.png", cx - r, cy - r, r * 2, r * 2)) return;
    const Vector2 top{cx, cy - r}, right{cx + r * 0.8f, cy}, bottom{cx, cy + r}, left{cx - r * 0.8f, cy};
    const Color body{170, 120, 245, 255}, edge{235, 215, 255, 255};
    DrawTriangle(top, left, right, body);       // обе обмотки: видно при любом отсечении граней
    DrawTriangle(top, right, left, body);
    DrawTriangle(bottom, right, left, body);
    DrawTriangle(bottom, left, right, body);
    DrawTriangle(top, Vector2{cx - r * 0.25f, cy - r * 0.15f}, Vector2{cx + r * 0.1f, cy - r * 0.35f}, edge);
    DrawTriangle(top, Vector2{cx + r * 0.1f, cy - r * 0.35f}, Vector2{cx - r * 0.25f, cy - r * 0.15f}, edge);
}

// ------------------------------------------------------------ достижения

void Game::drawAchievements() {
    const auto& list = achievements();
    drawTextCentered("ДОСТИЖЕНИЯ", kScreenW / 2.f, 24.f, 44, kAccent);
    drawTextCentered(fmt("Получено: %d из %d.   За каждое достижение: +%d артефакта (выдаётся автоматически).", profile_.unlockedCount(),
                         static_cast<int>(list.size()), kAchievementArtifacts), kScreenW / 2.f, 84.f, 17, kDim);
    drawArtifact(kScreenW - 220.f, 36.f, 14.f);
    drawText(fmt("Артефакты: %d", profile_.artifacts), kScreenW - 198.f, 24.f, 24, Color{190, 150, 255, 255});

    const Vector2 m = mouse();
    (void)m;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        const Achievement& a = list[i];
        const R r = achRow(i);
        const bool done = profile_.isUnlocked(a.id);
        DrawRectangle(r.x, r.y, r.w, r.h, done ? Color{30, 44, 36, 255} : kPanel);
        DrawRectangleLines(r.x, r.y, r.w, r.h, done ? kGood : kGridLine);
        // отметка выполнения: зелёный квадрат / пустая рамка
        DrawRectangle(r.x + 10, r.y + 13, 28, 28, done ? kGood : Color{40, 42, 54, 255});
        DrawRectangleLines(r.x + 10, r.y + 13, 28, 28, done ? kGood : kDim);
        if (done) drawTextCentered("+", r.x + 24.f, r.y + 14.f, 24, {20, 30, 24, 255});
        drawText(a.name, r.x + 52.f, r.y + 4.f, 18, done ? kText : kDim);
        float ty = r.y + 25.f;
        int lines = 0;
        for (const auto& l : wrap(a.desc, 460.f, 13)) {
            if (lines++ >= 2) break;
            drawText(l, r.x + 52.f, ty, 13, done ? kDim : Color{110, 112, 125, 255});
            ty += 14.f;
        }
        drawArtifact(r.x + r.w - 56.f, r.y + r.h / 2.f, 10.f);
        drawText(fmt("+%d", a.reward), r.x + r.w - 40.f, r.y + 14.f, 24, done ? Color{190, 150, 255, 255} : kDim);
    }
    drawButton(kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, "Назад", false, true);
}

// ---------------------------------------------------------------- наследие

void Game::drawLegacy() {
    drawTextCentered("НАСЛЕДИЕ", kScreenW / 2.f, 24.f, 44, kAccent);
    drawTextCentered("Постоянные улучшения за артефакты. Действуют во всех следующих забегах.", kScreenW / 2.f, 82.f, 17, kDim);
    drawArtifact(kScreenW - 220.f, 36.f, 14.f);
    drawText(fmt("Артефакты: %d", profile_.artifacts), kScreenW - 198.f, 24.f, 24, Color{190, 150, 255, 255});

    const Vector2 m = mouse();
    for (int u = 0; u < kUpgradeCount; ++u) {
        const UpgradeInfo& info = upgradeInfo(u);
        const int lv = profile_.meta.level[u];
        const bool maxed = profile_.meta.maxed(u);
        const int cost = profile_.meta.nextCost(u);
        const bool afford = !maxed && profile_.artifacts >= cost;
        const R c = legacyCard(u);
        DrawRectangle(c.x, c.y, c.w, c.h, kPanel);
        DrawRectangleLines(c.x, c.y, c.w, c.h, maxed ? kAccent : lv > 0 ? kGood : kGridLine);
        drawText(info.name, c.x + 12.f, c.y + 5.f, 20, maxed ? kAccent : kText);
        drawText(fmt("ур. %d/%d", lv, info.maxLevel), c.x + 330.f, c.y + 8.f, 16, kDim);
        drawText(info.desc, c.x + 12.f, c.y + 29.f, 13, kDim);
        std::string eff = "Сейчас: " + upgradeEffect(u, lv);
        if (!maxed) eff += "     Дальше: " + upgradeEffect(u, lv + 1);
        drawText(eff, c.x + 12.f, c.y + 47.f, 14, lv > 0 ? kGood : kDim);
        // полоска уровней
        for (int k = 0; k < info.maxLevel; ++k)
            DrawRectangle(c.x + 12 + k * 18, c.y + 68, 14, 6, k < lv ? kGood : Color{50, 52, 64, 255});

        const R b = legacyBuyBtn(u);
        const bool hover = afford && inR(m, b);
        DrawRectangle(b.x, b.y, b.w, b.h, maxed ? Color{40, 36, 22, 255} : !afford ? Color{28, 29, 36, 255} : hover ? Color{56, 60, 80, 255} : Color{38, 40, 56, 255});
        DrawRectangleLines(b.x, b.y, b.w, b.h, maxed ? kAccent : !afford ? kGridLine : hover ? kAccent : Color{190, 150, 255, 255});
        if (maxed) {
            drawTextCentered("МАКСИМУМ", b.x + b.w / 2.f, b.y + 16.f, 18, kAccent);
        } else {
            drawTextCentered("Улучшить", b.x + b.w / 2.f, b.y + 6.f, 16, afford ? kText : kDim);
            drawArtifact(b.x + b.w / 2.f - 16.f, b.y + 36.f, 9.f);
            drawText(fmt("%d", cost), b.x + b.w / 2.f - 2.f, b.y + 26.f, 20, afford ? Color{190, 150, 255, 255} : kDim);
        }
    }
    if (!legacyMsg_.empty()) drawTextCentered(legacyMsg_, kScreenW / 2.f, 636.f, 16, kAccent);
    drawButton(kBackBtn.x, kBackBtn.y + 6, kBackBtn.w, kBackBtn.h - 8, "Назад", false, true);
}

void Game::drawPause() {
    DrawRectangle(0, 0, kScreenW, kScreenH, {0, 0, 0, 170});
    drawTextCentered("ПАУЗА", kScreenW / 2.f, 150.f, 44, kAccent);
    const char* labels[4] = {"Продолжить", "Настройки", "В главное меню", "Выйти из игры"};
    for (int i = 0; i < 4; ++i) {
        const R r = menuBtn(i, 250);
        drawButton(r.x, r.y, r.w, r.h, labels[i], false, true);
    }
    drawTextCentered("Esc - продолжить", kScreenW / 2.f, 570.f, 15, kDim);
}

void Game::drawSettings() {
    drawTextCentered("НАСТРОЙКИ", kScreenW / 2.f, 60.f, 44, kAccent);
    drawTextCentered("Разрешение окна", kScreenW / 2.f, 154.f, 22, kText);
    const int n = static_cast<int>(resolutions_.size());
    for (int i = 0; i < n; ++i) {
        const R r = resBtn(i);
        const bool cur = !settings_.fullscreen && settings_.width == resolutions_[i].w && settings_.height == resolutions_[i].h;
        drawButton(r.x, r.y, r.w, r.h, fmt("%d x %d%s", resolutions_[i].w, resolutions_[i].h, cur ? "   (текущее)" : ""), cur, true);
    }
    drawButton(440, 190 + n * 58 + 14, 400, 48, settings_.fullscreen ? "Полный экран: ВКЛ" : "Полный экран: ВЫКЛ", settings_.fullscreen, true);
    drawButton(440, 190 + n * 58 + 84, 400, 48, "Назад", false, true);
    drawTextCentered(fmt("Текущее окно: %dx%d", GetScreenWidth(), GetScreenHeight()), kScreenW / 2.f, 628.f, 16, kDim);
    drawTextCentered("Интерфейс масштабируется под окно. Окно можно растягивать мышью. Esc - назад.", kScreenW / 2.f, 654.f, 15, kDim);
}

// ----------------------------------------------------------- карточка босса

void Game::drawBossCard(int x, int y, int w, int h, int bossId) const {
    const BossInfo& b = bossInfo(bossId);
    const Color bc = bossColor(bossId);
    DrawRectangle(x, y, w, h, {24, 25, 34, 255});
    DrawRectangleLines(x, y, w, h, bc);

    drawIconBox(bossArt(bossId), "", bc, firstGlyph(b.name), x + 16.f, y + 14.f, 72.f);
    drawText(b.name, x + 104.f, y + 10.f, 30, bc);
    drawText(b.title, x + 104.f, y + 46.f, 18, kDim);
    drawText(fmt("ЗДР %d    АТК %d    ЗАЩ %d    СКР %d", b.hp, b.atk, b.def, b.spd), x + 104.f, y + 70.f, 17, kText);

    float cy = static_cast<float>(y + 100);
    auto section = [&](const char* title, Color col, const std::vector<std::string>& items) {
        drawText(title, x + 16.f, cy, 17, col);
        cy += 24;
        for (const auto& item : items) {
            auto lines = wrap(item, static_cast<float>(w - 60), 15);
            for (size_t i = 0; i < lines.size(); ++i) {
                drawText((i == 0 ? "- " : "  ") + lines[i], x + 22.f, cy, 15, kText);
                cy += 18;
            }
        }
        cy += 8;
    };
    section("СПОСОБНОСТИ", kAccent, b.abilities);
    section("СИЛЬНЫЕ СТОРОНЫ", kBad, b.strengths);
    section("СЛАБЫЕ СТОРОНЫ", kGood, b.weaknesses);
}

void Game::drawBossIntro() {
    drawBossCard(kGridX, kGridY, kArenaW, kArenaH - 46, run_->pendingBossId());
    drawTextCentered("Enter - начать бой     I - инвентарь: подготовь отряд!", kGridX + kArenaW / 2.f,
                     kGridY + kArenaH - 36.f, 18, kAccent);
}

// ----------------------------------------------------------- выбор героев

void Game::drawSelect() {
    drawText("ВЫБОР ОТРЯДА", kSelX, 20, 32, kAccent);
    drawText("Выбери четырёх героев (клик или 1-8) и одного из трёх боссов (Q/W/E). Enter - начать. Состав решает исход боя!",
             kSelX, 60, 16, kDim);
    drawText(fmt("Выбрано: %d/%d", chosenCount(), kPartySize), 1100, 28, 22,
             chosenCount() == kPartySize ? kGood : kText);

    Vector2 m = mouse();
    const int maxVal[4] = {45, 12, 5, 8};
    const char* labels[4] = {"ЗДР", "АТК", "ЗАЩ", "СКР"};
    const Color barCols[4] = {{110, 210, 130, 255}, {235, 110, 80, 255}, {100, 150, 235, 255}, {240, 210, 90, 255}};

    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i) {
        const Hero& h = roster_[i];
        const int x = kSelX + (i % 4) * (kSelCardW + kSelGap);
        const int y = kSelY + (i / 4) * (kSelCardH + kSelGap);
        const bool hover = inRect(m, x, y, kSelCardW, kSelCardH) && !showBossCard_;
        const bool ch = chosen_[i];
        const Color rc = roleColor(h.role);

        DrawRectangle(x, y, kSelCardW, kSelCardH, hover ? Color{40, 42, 54, 255} : kPanel);
        DrawRectangle(x, y, 8, kSelCardH, rc);
        DrawRectangleLines(x, y, kSelCardW, kSelCardH, ch ? kGood : kGridLine);
        if (ch) {
            DrawRectangleLines(x + 1, y + 1, kSelCardW - 2, kSelCardH - 2, kGood);
            DrawRectangleLines(x + 2, y + 2, kSelCardW - 4, kSelCardH - 4, kGood);
        }

        drawIconBox(heroArt(h.role), "", rc, firstGlyph(h.name), x + 20.f, y + 14.f, 44.f);
        drawText(h.name, x + 74.f, y + 10.f, 26, kText);
        drawText(roleTitle(h.role), x + 74.f, y + 40.f, 16, kDim);
        drawText(fmt("[%d]", i + 1), x + kSelCardW - 34.f, y + 8.f, 14, kDim);

        const int vals[4] = {h.maxHp, h.atk, h.def, h.spd};
        for (int k = 0; k < 4; ++k) {
            const int ry = y + 72 + k * 24;
            drawText(labels[k], x + 20.f, static_cast<float>(ry), 15, kDim);
            drawBar(x + 70, ry + 5, 150, 10, static_cast<float>(vals[k]) / maxVal[k], barCols[k]);
            drawText(fmt("%d", vals[k]), x + 232.f, static_cast<float>(ry), 16, kText);
        }

        float ay = static_cast<float>(y + 176);
        int lines = 0;
        for (const auto& l : wrap(roleAbility(h.role), kSelCardW - 36.f, 15)) {
            if (lines++ >= 4) break;
            drawText(l, static_cast<float>(x + 20), ay, 15, kText);
            ay += 18;
        }
        if (ch) drawText("ВЫБРАН", x + kSelCardW - 92.f, y + 40.f, 16, kGood);
    }

    if (!message_.empty()) drawText(message_, kSelX, 640, 20, messageBad_ ? kBad : kGood);
    if (chosenCount() == kPartySize) drawText("Enter - начать строительство подземелья", kSelX, 672, 22, kAccent);
    else drawText(fmt("Нужно выбрать ещё: %d", kPartySize - chosenCount()), kSelX, 672, 20, kDim);

    // три предложенных босса: выбери одного (Q / W / E или клик)
    drawText("Финальный босс забега (Q/W/E, клик; B - подробности):", 520.f, 608.f, 14, kDim);
    const char* keyNames[3] = {"Q", "W", "E"};
    for (int i = 0; i < 3; ++i) {
        const int bx = 520 + i * 246, by = 630;
        const BossInfo& b = bossInfo(offered_[i]);
        const bool sel = bossPick_ == i;
        const bool hover = inRect(m, bx, by, 238, 62) && !showBossCard_;
        const Color bc = bossColor(offered_[i]);
        DrawRectangle(bx, by, 238, 62, hover ? Color{40, 42, 54, 255} : kPanel);
        DrawRectangleLines(bx, by, 238, 62, sel ? bc : kGridLine);
        if (sel) {
            DrawRectangleLines(bx + 1, by + 1, 236, 60, bc);
            DrawRectangleLines(bx + 2, by + 2, 234, 58, bc);
        }
        drawIconBox(bossArt(offered_[i]), "", bc, firstGlyph(b.name), bx + 8.f, by + 11.f, 40.f);
        drawText(b.name, bx + 56.f, by + 7.f, 17, sel ? bc : kText);
        drawText(b.title, bx + 56.f, by + 30.f, 13, kDim);
        drawText(sel ? "выбран" : "", bx + 56.f, by + 45.f, 12, kGood);
        drawText(keyNames[i], bx + 222.f, by + 4.f, 12, kDim);
    }

    if (showBossCard_) {
        DrawRectangle(0, 0, kScreenW, kScreenH, {0, 0, 0, 190});
        drawBossCard(190, 50, 900, 560, bossId_);
        drawTextCentered("B / Enter - закрыть", kScreenW / 2.f, 624.f, 18, kAccent);
    }
}

// ------------------------------------------------------------- строительство

void Game::drawBuild() {
    drawGrid(true);

    for (int i = 0; i < kHand; ++i) {
        int x = kGridX + i * (kCardW + 8);
        DrawRectangle(x, kCardY, kCardW, kCardH, kPanel);
        DrawRectangleLines(x, kCardY, kCardW, kCardH, i == selected_ ? kAccent : kGridLine);
        const Slot& s = hand_[i];
        drawText(fmt("[%d]", i + 1), x + 6.f, kCardY + 4.f, 14, kDim);
        if (!s.has) continue;
        drawRoom(x + 4, kCardY + 16, 80, s.card, false, 1.f, false);
        drawText(roomTypeName(s.card.type), x + 88.f, kCardY + 22.f, 18, kText);
        if (s.card.type == RoomType::Boss) drawText("финал", x + 88.f, kCardY + 46.f, 16, kAccent);
        else drawText(std::string(s.card.difficulty, '*'), x + 88.f, kCardY + 46.f, 20, kAccent);
    }

    DrawRectangle(kPanelX - 10, 10, kPanelW + 20, kScreenH - 20, kPanel);
    float y = 20;
    drawText("ПОСТРОЙ ПОДЗЕМЕЛЬЕ", kPanelX, y, 26, kAccent);
    y += 42;
    drawText(fmt("Карт в колоде: %d", deck_.remaining()), kPanelX, y, 18, kText);
    y += 24;
    drawText(fmt("Комнат можно поставить: %d", placementsLeft_), kPanelX, y, 18, kText);
    y += 24;
    drawText(fmt("Сбросов осталось: %d", discardsLeft_), kPanelX, y, 18, kText);
    y += 24;
    drawText("Босс забега: " + bossInfo(bossId_).name, kPanelX, y, 18, bossColor(bossId_));
    y += 34;

    const Slot& s = hand_[selected_];
    if (s.has) {
        std::string stars = (s.card.difficulty > 0 && s.card.type != RoomType::Boss) ? std::string(s.card.difficulty, '*') : "";
        drawText(fmt("Выбрано: %s %s", roomTypeName(s.card.type), stars.c_str()), kPanelX, y, 20, kText);
        y += 26;
        drawText(roomTypeHint(s.card.type), kPanelX, y, 16, kDim);
        y += 22;
        drawText(rewardText(s.card), kPanelX, y, 16, kDim);
        y += 30;
    }

    for (const auto& line : wrap(message_, kPanelW, 18)) {
        drawText(line, kPanelX, y, 18, messageBad_ ? kBad : kGood);
        y += 22;
    }
    y += 14;

    drawText("УПРАВЛЕНИЕ", kPanelX, y, 18, kAccent);
    y += 24;
    const char* keys[] = {"1-4 / клик - выбрать карту", "R / ПКМ / колесо - повернуть", "ЛКМ по сетке - поставить",
                          "D - сбросить карту", "Enter - начать поход (босс должен быть на месте)",
                          "N - к выбору героев (новая игра), Esc - меню"};
    for (const char* k : keys) {
        drawText(k, kPanelX, y, 16, kText);
        y += 20;
    }
    y += 14;

    drawText("КОМНАТЫ", kPanelX, y, 18, kAccent);
    y += 24;
    RoomType types[] = {RoomType::Combat, RoomType::Treasure, RoomType::Trap, RoomType::Rest, RoomType::Shop, RoomType::Boss};
    for (RoomType t : types) {
        DrawRectangle(kPanelX, static_cast<int>(y) + 2, 14, 14, roomColor(t));
        drawText(fmt("%s - %s", roomTypeName(t), roomTypeHint(t)), kPanelX + 22.f, y, 15, kText);
        y += 20;
    }
    y += 6;
    drawText("Звёзды = сложность. Сложнее комната - ценнее добыча.", kPanelX, y, 14, kDim);
    y += 20;
    for (const auto& l : wrap("Торговец уже стоит у края карты, но не подключён: поставь комнату, стыкующуюся и с подземельем, и с его дверью.", kPanelW, 14)) {
        drawText(l, kPanelX, y, 14, roomColor(RoomType::Shop));
        y += 17;
    }

    std::string squad = "Отряд: ";
    bool first = true;
    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i)
        if (chosen_[i]) {
            squad += (first ? "" : ", ") + roster_[i].name;
            first = false;
        }
    float sy = static_cast<float>(kScreenH - 38);
    for (const auto& l : wrap(squad, kPanelW, 15)) {
        drawText(l, kPanelX, sy, 15, kDim);
        sy += 18;
    }
}

// ------------------------------------------------------------------- поход

void Game::drawSprite(Vector2 c, int size, Color col, const std::string& name, int hp, int maxHp, Flash fl,
                      Vector2 offset, const std::string& tag, const std::string& art, const std::string& art2) const {
    const bool alive = hp > 0;
    const bool captive = !tag.empty();
    const int x = static_cast<int>(c.x + offset.x) - size / 2;
    const int y = static_cast<int>(c.y + offset.y) - size / 2;
    Color body = alive ? col : Color{70, 70, 78, 255};
    if (captive) body = {70, 60, 55, 255};
    // картинка (если есть в assets/) заменяет цветной шаблон; павший и проглоченный - затемнены
    Color tint = alive ? Color{255, 255, 255, 255} : Color{90, 90, 100, 255};
    if (captive) tint = {120, 105, 100, 255};
    const bool hasArt = tex(art) || (!art2.empty() && tex(art2));
    if (hasArt) {
        drawIconBox(art, art2, body, "", static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), tint);
    } else {
        DrawRectangle(x, y, size, size, body);
        DrawRectangleLines(x, y, size, size, {240, 240, 245, 120});
        drawTextCentered(alive ? firstGlyph(name) : "x", x + size / 2.f, y + size * 0.5f - size * 0.27f,
                         size * 0.5f, {20, 20, 28, 255});
    }
    if (fl.t > 0.f) DrawRectangle(x, y, size, size, Fade(fl.heal ? kHeal : kDamage, fl.t / kFlashDur * (hasArt ? 0.45f : 0.75f)));
    if (captive) drawTextCentered(tag, c.x, static_cast<float>(y) - 18.f, 14, kAccent);

    // подпись и полоска здоровья остаются на месте, двигается только фигура
    const int ty = static_cast<int>(c.y) + size / 2 + 3;
    drawTextCentered(name, c.x, static_cast<float>(ty), 14, alive ? kText : kDim);
    const int bw = std::max(size, 72) + 12;
    drawBar(static_cast<int>(c.x) - bw / 2, ty + 20, bw, 7, maxHp ? static_cast<float>(hp) / maxHp : 0.f,
            alive ? kGood : kDim);
    drawTextCentered(fmt("%d/%d", std::max(0, hp), maxHp), c.x, static_cast<float>(ty + 29), 13, kDim);
}

void Game::drawArena() {
    const Combat* c = run_->combat();
    if (!c) return;
    const auto& heroes = run_->party().heroes;
    const auto& enemies = c->enemies();
    const int slots = c->slotCount();

    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, {22, 23, 32, 255});
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, kBad);
    drawText(fmt("БОЙ - раунд %d", std::max(1, c->round())), kGridX + 16.f, kGridY + 8.f, 22, kBad);
    const std::string st = c->status();
    if (!st.empty()) drawText(st, kGridX + 16.f, kGridY + 36.f, 16, kAccent);
    drawText("ОТРЯД", kGridX + 118.f, kGridY + 62.f, 13, kDim);
    drawText("ВРАГИ", kGridX + 536.f, kGridY + 62.f, 13, kDim);

    for (int i = 0; i < static_cast<int>(heroes.size()) && i < kMaxRoster; ++i) {
        const Hero& h = heroes[i];
        drawSprite(heroCenter(i), 64, roleColor(h.role), h.name, h.hp, h.maxHp, heroFlash_[i], lungeOffset(true, i),
                   h.away ? "В ПАСТИ" : "", heroArt(h.role), "");
    }
    for (int i = 0; i < static_cast<int>(enemies.size()) && i < kMaxRoster; ++i) {
        const Enemy& e = enemies[i];
        if (!e.alive()) {  // мёртвого не рисуем, если его место уже занял другой (деление, призыв)
            bool taken = false;
            for (const auto& o : enemies)
                if (o.alive() && o.slot == e.slot) taken = true;
            if (taken) continue;
        }
        drawSprite(enemyCenter(e.slot, slots), e.size, enemyColor(e), e.name, e.hp, e.maxHp, enemyFlash_[i],
                   lungeOffset(false, i), "", e.art, e.bossId >= 0 ? bossArt(e.bossId) : "");
    }

    // снаряды
    for (const Proj& p : projs_) {
        if (p.t < 0.f) continue;
        float u = std::min(1.f, p.t / kProjDur);
        float x = p.from.x + (p.to.x - p.from.x) * u;
        float y = p.from.y + (p.to.y - p.from.y) * u;
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 11.f, Fade(p.col, 0.35f));
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 6.f, p.col);
    }

    // всплывающие числа: урон красный, лечение зелёное; реплики боссов - жёлтые
    for (const Floater& f : floaters_) {
        if (f.age < f.delay) continue;
        float u = (f.age - f.delay) / kFloatLife;
        float alpha = std::max(0.f, 1.f - u * u);
        float y = f.pos.y - u * 45.f;
        drawTextCentered(f.text, f.pos.x + 1.f, y + 1.f, f.size, Fade({0, 0, 0, 255}, alpha));
        drawTextCentered(f.text, f.pos.x, y, f.size, Fade(f.col, alpha));
    }
}

void Game::drawRestOverlay() {
    const auto& hs = run_->party().heroes;
    const float cx = kGridX + kArenaW / 2.f;
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, {22, 26, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, kGood);
    Vector2 m = mouse();

    if (restStage_ == 0) {
        drawTextCentered("ПРИВАЛ", cx, kGridY + 22.f, 36, kGood);
        drawTextCentered("Отряд отдыхает. Как использовать время?", cx, kGridY + 72.f, 18, kDim);
        drawCoin(kGridX + 590.f, kGridY + 36.f, 10.f);
        drawText(fmt("Монеты: %d", run_->party().coins), kGridX + 606.f, kGridY + 24.f, 20, kCoin);
        drawArtifact(kGridX + 590.f, kGridY + 66.f, 10.f);
        drawText(fmt("Артефакты: %d", run_->artifacts()), kGridX + 606.f, kGridY + 54.f, 20, Color{190, 150, 255, 255});

        bool anyDead = false;
        for (const auto& h : hs) anyDead |= !h.alive();
        const bool canRevive = anyDead && (run_->party().coins >= run_->reviveCoinCost() || run_->artifacts() >= Run::kReviveArtifactCost);

        const int bw = 224, bh = 220, by = kGridY + 130;
        const char* titles[3] = {"[1] Лечение", "[2] Усиление", "[3] Воскрешение"};
        const std::string descs[3] = {
            fmt("Все живые герои восстанавливают %d%% максимального здоровья.", run_->restHealPct()),
            fmt("Выбери двух героев: каждый получит +%d АТК и +%d ЗАЩ до конца похода.", Run::kBuffAtk, Run::kBuffDef),
            fmt("Вернуть павшего героя: за %d монет (%d%% ЗДР) или за %d артефакт (%d%% ЗДР). Можно повторять.", run_->reviveCoinCost(),
                Run::kRevivePct, Run::kReviveArtifactCost, Run::kReviveArtifactPct)};
        const Color titleCols[3] = {kHeal, kAccent, kCoin};
        for (int k = 0; k < 3; ++k) {
            const int bx = kGridX + 14 + k * 236;
            const bool enabled = k < 2 || canRevive;
            const bool hover = inRect(m, bx, by, bw, bh);
            DrawRectangle(bx, by, bw, bh, hover ? Color{40, 48, 56, 255} : kPanel);
            DrawRectangleLines(bx, by, bw, bh, hover ? (enabled ? kAccent : kBad) : kGridLine);
            drawText(titles[k], static_cast<float>(bx + 14), static_cast<float>(by + 14), 22, enabled ? titleCols[k] : kDim);
            float ty = static_cast<float>(by + 56);
            for (const auto& l : wrap(descs[k], bw - 28.f, 16)) {
                drawText(l, static_cast<float>(bx + 14), ty, 16, enabled ? kText : kDim);
                ty += 21;
            }
            if (k == 2) {
                if (!anyDead) drawText("Павших героев нет", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kDim);
                else if (!canRevive) drawText("Не хватает монет и артефактов", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kBad);
                else drawText("Доступно", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kGood);
            }
        }
        if (!restMsg_.empty()) drawText(restMsg_, kGridX + 16.f, kGridY + 358.f, 16, kAccent);

        drawText("Состояние отряда", static_cast<float>(kGridX + 30), static_cast<float>(kGridY + 384), 16, kAccent);
        for (int i = 0; i < static_cast<int>(hs.size()) && i < 4; ++i) {
            const Hero& h = hs[i];
            const int hx = kGridX + 30 + i * 172;
            drawText(h.name, static_cast<float>(hx), static_cast<float>(kGridY + 410), 16, h.alive() ? kText : kDim);
            drawBar(hx, kGridY + 434, 150, 9, h.maxHp ? static_cast<float>(h.hp) / h.maxHp : 0.f, kGood);
            drawText(h.alive() ? fmt("%d/%d", h.hp, h.maxHp) : std::string("пал"), static_cast<float>(hx), static_cast<float>(kGridY + 448), 14,
                     h.alive() ? kDim : kBad);
        }
        return;
    }

    // стадия 2: кого воскресить (оплата монетами или артефактом)
    if (restStage_ == 2) {
        drawTextCentered("КОГО ВОСКРЕСИТЬ?", cx, kGridY + 20.f, 34, kCoin);
        drawTextCentered(fmt("За монеты: %d (ЗДР %d%%).  За артефакт: %d (ЗДР %d%%).  У вас: %d мон., %d арт.", run_->reviveCoinCost(),
                             Run::kRevivePct, Run::kReviveArtifactCost, Run::kReviveArtifactPct, run_->party().coins, run_->artifacts()),
                         cx, kGridY + 66.f, 16, kCoin);
        int row = 0;
        for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i) {
            const Hero& h = hs[i];
            if (h.alive()) continue;
            const int rx = kGridX + 40, ry = kGridY + 100 + row * 84;
            const bool canCoins = run_->party().coins >= run_->reviveCoinCost();
            const bool canArt = run_->artifacts() >= Run::kReviveArtifactCost;
            DrawRectangle(rx, ry, 640, 72, kPanel);
            DrawRectangleLines(rx, ry, 640, 72, kGridLine);
            drawIconBox(heroArt(h.role), "", Color{70, 70, 78, 255}, firstGlyph(h.name), rx + 12.f, ry + 12.f, 48.f, Color{140, 140, 150, 255});
            drawText(fmt("[%d] %s", row + 1, h.name.c_str()), static_cast<float>(rx + 74), static_cast<float>(ry + 8), 22, kText);
            drawText(fmt("ЗДР 0/%d  ->  %d/%d (монеты) или %d/%d (артефакт)", h.maxHp, std::max(1, h.maxHp * Run::kRevivePct / 100), h.maxHp,
                         std::max(1, h.maxHp * Run::kReviveArtifactPct / 100), h.maxHp),
                     static_cast<float>(rx + 74), static_cast<float>(ry + 40), 14, kDim);
            // кнопки оплаты
            const Vector2 mm = m;
            const R bc{rx + 400, ry + 14, 112, 44}, ba{rx + 520, ry + 14, 112, 44};
            for (int b2 = 0; b2 < 2; ++b2) {
                const R& br = b2 == 0 ? bc : ba;
                const bool can = b2 == 0 ? canCoins : canArt;
                const bool hov = can && inR(mm, br);
                DrawRectangle(br.x, br.y, br.w, br.h, !can ? Color{28, 29, 36, 255} : hov ? Color{56, 60, 80, 255} : Color{38, 40, 56, 255});
                DrawRectangleLines(br.x, br.y, br.w, br.h, !can ? kGridLine : hov ? kAccent : (b2 == 0 ? kCoin : Color{190, 150, 255, 255}));
                if (b2 == 0) drawCoin(br.x + 22.f, br.y + br.h / 2.f, 10.f);
                else drawArtifact(br.x + 22.f, br.y + br.h / 2.f, 10.f);
                drawText(fmt("%d", b2 == 0 ? run_->reviveCoinCost() : Run::kReviveArtifactCost), br.x + 40.f, br.y + 12.f, 22,
                         can ? kText : kDim);
            }
            ++row;
        }
        drawTextCentered("Кнопка оплаты или клик по герою (сначала монеты)    B - назад", cx, kGridY + kArenaH - 36.f, 17, kDim);
        return;
    }

    const int req = std::min(Run::kBuffTargets, run_->party().aliveCount());
    int picked = 0;
    for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i) picked += restPick_[i] ? 1 : 0;
    drawTextCentered("КОГО УСИЛИТЬ?", cx, kGridY + 20.f, 34, kAccent);
    drawTextCentered(fmt("Выбрано героев: %d из %d", picked, req), cx, kGridY + 66.f, 18, picked == req ? kGood : kDim);

    for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i) {
        const Hero& h = hs[i];
        const int rx = kGridX + 40, ry = kGridY + 100 + i * 84;
        const bool hover = h.alive() && inRect(m, rx, ry, 640, 72);
        DrawRectangle(rx, ry, 640, 72, hover ? Color{40, 42, 54, 255} : kPanel);
        DrawRectangleLines(rx, ry, 640, 72, restPick_[i] ? kGood : kGridLine);
        if (restPick_[i]) DrawRectangleLines(rx + 1, ry + 1, 638, 70, kGood);
        const Color rc = h.alive() ? roleColor(h.role) : Color{70, 70, 78, 255};
        drawIconBox(heroArt(h.role), "", rc, firstGlyph(h.name), rx + 12.f, ry + 12.f, 48.f,
                    h.alive() ? Color{255, 255, 255, 255} : Color{110, 110, 120, 255});
        drawText(fmt("[%d] %s", i + 1, h.name.c_str()), static_cast<float>(rx + 74), static_cast<float>(ry + 10), 22,
                 h.alive() ? kText : kDim);
        if (!h.alive()) {
            drawText("пал - усилить нельзя", static_cast<float>(rx + 74), static_cast<float>(ry + 40), 16, kDim);
            continue;
        }
        drawText(fmt("АТК %d -> %d    ЗАЩ %d -> %d    ЗДР %d/%d", h.atk, h.atk + Run::kBuffAtk, h.def,
                     h.def + Run::kBuffDef, h.hp, h.maxHp),
                 static_cast<float>(rx + 74), static_cast<float>(ry + 40), 16, restPick_[i] ? kGood : kDim);
    }
    drawTextCentered("Enter - подтвердить    B - назад", cx, kGridY + kArenaH - 36.f, 18, picked == req ? kAccent : kDim);
}

// Окно инвентаря: слева рюкзак - сетка ячеек, справа герои с тремя ячейками снаряжения.
void Game::drawInventory() {
    const Party& p = run_->party();
    const Vector2 m = mouse();
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, tex("backgrounds/arena.png") ? Color{12, 13, 18, 170} : Color{22, 24, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, kAccent);
    drawText("ИНВЕНТАРЬ", kGridX + 16.f, kGridY + 10.f, 26, kAccent);
    drawText("[I] закрыть", kGridX + 600.f, kGridY + 16.f, 15, kDim);
    drawText(fmt("Рюкзак (%d/%d)", static_cast<int>(p.bag.size()), kBagCells), kGridX + 12.f, kGridY + 54.f, 17, kText);

    const Item* selItem = (invSel_ >= 0 && invSel_ < static_cast<int>(p.bag.size())) ? &p.bag[invSel_] : nullptr;
    const Item* hoverItem = nullptr;

    // --- рюкзак: сетка ячеек ---
    for (int i = 0; i < kBagCells; ++i) {
        const R r = bagCell(i);
        const Item* it = i < static_cast<int>(p.bag.size()) ? &p.bag[i] : nullptr;
        const bool hover = inR(m, r);
        if (hover && it) hoverItem = it;
        drawBagCell(r.x, r.y, r.w, it, it && i == invSel_, hover && it);
    }

    // --- герои ---
    for (int h = 0; h < static_cast<int>(p.heroes.size()) && h < kMaxRoster; ++h) {
        const Hero& hero = p.heroes[h];
        const R pr = heroPanel(h);
        const bool potionTarget = selItem && selItem->kind == ItemKind::Potion && hero.alive() && hero.hp < hero.maxHp;
        DrawRectangle(pr.x, pr.y, pr.w, pr.h, kPanel);
        DrawRectangleLines(pr.x, pr.y, pr.w, pr.h, potionTarget ? kGood : kGridLine);
        drawIconBox(heroArt(hero.role), "", hero.alive() ? roleColor(hero.role) : Color{70, 70, 78, 255}, firstGlyph(hero.name),
                    pr.x + 8.f, pr.y + 5.f, 52.f, hero.alive() ? Color{255, 255, 255, 255} : Color{110, 110, 120, 255});
        drawText(hero.name + (hero.alive() ? "" : "  (пал)"), pr.x + 70.f, pr.y + 5.f, 18, hero.alive() ? kText : kDim);
        std::string extra;
        if (hero.critPct > 0) extra += fmt("  КРИТ +%d%%", hero.critPct);
        if (hero.lifestealPct > 0) extra += fmt("  ВАМП %d%%", hero.lifestealPct);
        drawText(fmt("АТК %d  ЗАЩ %d  СКР %d%s", hero.atk, hero.def, hero.spd, extra.c_str()), pr.x + 70.f, pr.y + 28.f, 13, kDim);
        drawBar(pr.x + 70, pr.y + 47, 190, 8, hero.maxHp ? static_cast<float>(hero.hp) / hero.maxHp : 0.f, kGood);
        drawText(fmt("%d/%d", std::max(0, hero.hp), hero.maxHp), pr.x + 268.f, pr.y + 42.f, 13, kDim);

        float by = pr.y + 66.f;
        for (int k = 0; k < 3; ++k) {
            const R sr = slotRect(h, k);
            const auto& item = hero.slot(kSlotKinds[k]);
            const bool fits = selItem && selItem->kind == kSlotKinds[k] && hero.alive();
            const bool hover = inR(m, sr);
            if (hover && item) hoverItem = &*item;
            drawEquipSlot(sr.x, sr.y, sr.w, kSlotKinds[k], item, fits, hover);
            // краткая сводка бонусов справа от ячеек
            if (item) drawText(itemBonusText(*item), pr.x + 184.f, by, 12, tierColor(item->tier));
            else drawText("-", pr.x + 184.f, by, 12, kDim);
            by += 15.f;
        }
    }

    // --- описание выбранного предмета, сообщение, подсказка ---
    float ty = kGridY + 492.f;
    if (selItem) {
        for (const auto& l : wrap(fmt("%s - %s ур.%d: %s", selItem->name.c_str(), itemKindName(selItem->kind), selItem->tier,
                                      itemBonusText(*selItem).c_str()), 335.f, 14)) {
            drawText(l, kGridX + 12.f, ty, 14, kAccent);
            ty += 17;
        }
    }
    if (!invMsg_.empty()) drawText(invMsg_, kGridX + 12.f, kGridY + 526.f, 14, kGood);
    drawText("Ячейка -> слот героя: надеть. Надетое: клик - снять. ПКМ - отмена.", kGridX + 12.f, kGridY + 542.f, 11, kDim);

    if (hoverItem) drawItemTooltip(*hoverItem, m);
}

// Лавка торговца: ячейки-лоты с иконками и ценами в монетах.
void Game::drawShop() {
    const auto* stock = run_->shopStock();
    const Vector2 m = mouse();
    const Color teal = roomColor(RoomType::Shop);
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, tex("backgrounds/arena.png") ? Color{12, 16, 18, 170} : Color{22, 27, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, teal);
    drawText("ТОРГОВЕЦ", kGridX + 20.f, kGridY + 12.f, 34, teal);
    drawCoin(kGridX + 556.f, kGridY + 30.f, 11.f);
    drawText(fmt("Монеты: %d", run_->party().coins), kGridX + 574.f, kGridY + 18.f, 22, kCoin);
    drawText("Товары: т1, т2 и редко т3. Клик по лоту - купить.", kGridX + 22.f, kGridY + 58.f, 15, kDim);

    const Item* hoverItem = nullptr;
    if (stock) {
        for (int i = 0; i < static_cast<int>(stock->size()); ++i) {
            const ShopOffer& o = (*stock)[i];
            const R r{kGridX + 20, kGridY + 88 + i * 76, 680, 68};
            const bool afford = run_->party().coins >= o.price;
            const bool hover = !o.sold && inR(m, r);
            if (hover) hoverItem = &o.item;
            DrawRectangle(r.x, r.y, r.w, r.h, o.sold ? Color{26, 28, 34, 255} : hover ? Color{42, 48, 58, 255} : kPanel);
            DrawRectangleLines(r.x, r.y, r.w, r.h, hover ? (afford ? kGood : kBad) : kGridLine);
            drawBagCell(r.x + 6, r.y + 3, 62, o.sold ? nullptr : &o.item, false, false);
            drawText(o.item.name, r.x + 82.f, r.y + 8.f, 21, o.sold ? kDim : kText);
            drawText(fmt("%s, ур.%d: %s", itemKindName(o.item.kind), o.item.tier, itemBonusText(o.item).c_str()),
                     r.x + 82.f, r.y + 38.f, 15, kDim);
            if (o.sold) {
                drawText("ПРОДАНО", r.x + r.w - 110.f, r.y + 22.f, 20, kDim);
            } else {
                drawCoin(r.x + r.w - 118.f, r.y + 34.f, 10.f);
                drawText(fmt("%d", o.price), r.x + r.w - 100.f, r.y + 20.f, 26, afford ? kGood : kBad);
            }
        }
    }
    if (!shopMsg_.empty()) drawText(shopMsg_, kGridX + 22.f, kGridY + 478.f, 16, kAccent);
    drawTextCentered("Enter - уйти     I - инвентарь (надеть покупки)", kGridX + kArenaW / 2.f, kGridY + kArenaH - 34.f, 18, kAccent);
    if (hoverItem) drawItemTooltip(*hoverItem, m);
}

void Game::drawRunView() {
    drawGrid(false);
    if (!run_) return;
    const Run& r = *run_;
    const bool exploring = r.phase() == RunPhase::Exploring && !r.combat() && !invOpen_;

    // маркер отряда и подсказки WASD у доступных дверей
    DrawCircle(static_cast<int>(markerPos_.x), static_cast<int>(markerPos_.y), 9, kAccent);
    if (exploring) {
        const Vec2i p = r.partyPos();
        const char* keyNames[4] = {"W", "D", "S", "A"};  // N, E, S, W
        for (int i = 0; i < kDirCount; ++i) {
            Dir d = static_cast<Dir>(i);
            if (!r.dungeon().linked(p, d)) continue;
            const Vec2i dv = dirDelta(d);
            const float hx = kGridX + p.x * kCell + kCell / 2.f + dv.x * (kCell / 2.f - 10.f);
            const float hy = kGridY + p.y * kCell + kCell / 2.f + dv.y * (kCell / 2.f - 10.f);
            DrawRectangle(static_cast<int>(hx) - 9, static_cast<int>(hy) - 9, 18, 18, {20, 20, 28, 220});
            drawTextCentered(keyNames[i], hx, hy - 8.f, 15, kAccent);
        }
    }

    // добыча под сеткой
    drawText(fmt("Рюкзак: %d предм.   Зелий: %d   Монеты: %d   [I] - инвентарь", static_cast<int>(r.party().bag.size()),
                 r.party().potionCount(), r.party().coins), kGridX, kCardY - 14.f, 16, r.hasNewLoot() ? kAccent : kDim);
    if (exploring && r.inShopRoom()) drawText("Торговец рядом: T - торговать", kGridX, kCardY - 34.f, 16, kCoin);
    float lx = kGridX, ly = kCardY + 10.f;
    int shown = 0;
    drawText("Недавняя добыча:", lx, ly, 14, kDim);
    ly += 20;
    for (int i = static_cast<int>(r.loot().size()) - 1; i >= 0 && shown < 7; --i, ++shown) {
        const Item& it = r.loot()[i];
        std::string t = fmt("%s [%d]", it.name.c_str(), it.tier);
        float w = textWidth(t, 15) + 14;
        if (lx + w > kGridX + 720) {
            lx = kGridX;
            ly += 20;
        }
        drawText(t, lx, ly, 15, itemColor(it.kind));
        lx += w;
    }

    // окна поверх карты
    if (r.combat()) drawArena();
    if (r.phase() == RunPhase::RestChoice) drawRestOverlay();
    else if (invOpen_) drawInventory();
    else if (r.phase() == RunPhase::BossIntro) drawBossIntro();
    else if (r.phase() == RunPhase::Shop) drawShop();

    // панель справа
    DrawRectangle(kPanelX - 10, 10, kPanelW + 20, kScreenH - 20, kPanel);
    float y = 20;
    drawText("ПОХОД ОТРЯДА", kPanelX, y, 26, kAccent);
    if (r.phase() == RunPhase::Fighting)
        drawText(paused_ ? "ПАУЗА" : fmt("скорость боя x%.1f", kSpeeds[speedIdx_]), kPanelX + 290.f, y + 6.f, 16, kDim);
    y += 40;

    for (const Hero& h : r.party().heroes) {
        drawText(h.name, kPanelX, y, 17, h.alive() ? kText : kDim);
        drawText(fmt("АТК %d  ЗАЩ %d  СКР %d  ЗДР %d/%d", h.atk, h.def, h.spd, std::max(0, h.hp), h.maxHp), kPanelX + 100.f,
                 y + 1.f, 15, kDim);
        drawBar(kPanelX, static_cast<int>(y) + 22, 300, 8, h.maxHp ? static_cast<float>(h.hp) / h.maxHp : 0.f, kGood);
        y += 40;
    }
    y += 2;
    drawCoin(kPanelX + 8.f, y + 11.f, 8.f);
    drawText(fmt("Монеты: %d", r.party().coins), kPanelX + 22.f, y + 1.f, 18, kCoin);
    drawArtifact(kPanelX + 190.f, y + 11.f, 8.f);
    drawText(fmt("Артефакты: %d", r.artifacts()), kPanelX + 204.f, y + 1.f, 18, Color{190, 150, 255, 255});
    y += 30;

    // лог: самые новые записи внизу
    const float logTop = y;
    const float logBottom = kScreenH - 56.f;
    std::vector<std::string> lines;
    const auto& log = r.log();
    for (int i = static_cast<int>(log.size()) - 1; i >= 0 && lines.size() < 40; --i) {
        auto w = wrap(log[i], kPanelW, 15);
        for (int k = static_cast<int>(w.size()) - 1; k >= 0; --k) lines.push_back(w[k]);
    }
    int maxLines = static_cast<int>((logBottom - logTop) / 18);
    if (static_cast<int>(lines.size()) > maxLines) lines.resize(std::max(0, maxLines));
    float ly2 = logBottom - 18;
    for (const auto& l : lines) {
        drawText(l, kPanelX, ly2, 15, kText);
        ly2 -= 18;
    }

    const char* hint = r.phase() == RunPhase::Fighting ? "Пробел - пауза   Вверх/Вниз - скорость боя   Esc - меню"
                                                       : "WASD - идти   I - инвентарь   T - торговец   Esc - меню";
    drawText(hint, kPanelX, kScreenH - 40.f, 15, kDim);
}

void Game::drawResult() {
    DrawRectangle(0, 0, kScreenW, kScreenH, {0, 0, 0, 150});

    // строки награды: артефакты за босса и новые достижения
    std::vector<std::pair<std::string, Color>> reward;
    if (run_ && resultWin_) {
        const Color art{190, 150, 255, 255};
        reward.push_back({fmt("Артефакты за победу над боссом: +%d", lastReward_.bossArtifacts), art});
        const auto& all = achievements();
        for (int id : lastReward_.newAchievements)
            for (const auto& a : all)
                if (a.id == id) reward.push_back({fmt("Достижение: %s (+%d)", a.name.c_str(), a.reward), kAccent});
        reward.push_back({fmt("Всего артефактов: %d", profile_.artifacts), art});
    }

    const int shown = static_cast<int>(std::min<size_t>(reward.size(), 9));
    const int w = 620, h = 330 + (run_ && resultWin_ ? 22 + shown * 26 : 0);
    const int x = (kScreenW - w) / 2, y = std::max(10, (kScreenH - h) / 2);
    DrawRectangle(x, y, w, h, kPanel);
    DrawRectangleLines(x, y, w, h, resultWin_ ? kGood : kBad);
    drawTextCentered(resultWin_ ? "ПОБЕДА!" : "ПОРАЖЕНИЕ", kScreenW / 2.f, y + 20.f, 40, resultWin_ ? kGood : kBad);
    drawTextCentered((resultWin_ ? "Побеждён: " : "Босс забега: ") + bossInfo(bossId_).name, kScreenW / 2.f, y + 70.f, 18,
                     bossColor(bossId_));

    float ty = y + 108.f;
    if (run_) {
        drawTextCentered(fmt("Комнат пройдено: %d", run_->roomsCleared()), kScreenW / 2.f, ty, 22, kText);
        ty += 30;
        drawTextCentered(fmt("Предметов добыто: %d", static_cast<int>(run_->loot().size())), kScreenW / 2.f, ty, 22, kText);
        ty += 30;
        drawTextCentered(fmt("Героев выжило: %d из %d    Потерь за забег: %d", run_->party().aliveCount(),
                             static_cast<int>(run_->party().heroes.size()), run_->casualties()), kScreenW / 2.f, ty, 22, kText);
        ty += 30;
        drawTextCentered(fmt("Монет в кармане: %d", run_->party().coins), kScreenW / 2.f, ty, 22, kCoin);
        ty += 36;
        for (int i = 0; i < shown; ++i) {
            drawTextCentered(reward[i].first, kScreenW / 2.f, ty, 19, reward[i].second);
            ty += 26;
        }
        if (!resultWin_) drawTextCentered("Артефакты дают только победы над боссами.", kScreenW / 2.f, ty, 16, kDim);
    } else {
        for (const auto& l : wrap(resultText_, w - 40.f, 20)) {
            drawTextCentered(l, kScreenW / 2.f, ty, 20, kText);
            ty += 26;
        }
    }
    drawTextCentered("N - новая игра (выбор героев)     Esc - меню", kScreenW / 2.f, y + h - 40.f, 18, kAccent);
}

}  // namespace dc
