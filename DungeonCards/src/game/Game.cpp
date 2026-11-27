#include "game/Game.h"
#include "rlgl.h"
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

// Темп боя: 0.7 с на одно действие. Передвижение - по нажатию WASD.
constexpr float kCombatStep = 0.70f;
constexpr float kSpeeds[] = {0.5f, 1.f, 2.f, 4.f, 10.f};
constexpr float kMoveCooldown = 0.16f;
constexpr float kDisplayFadeTime = 0.18f;  // затемнение/проявление при смене разрешения

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
        case RoomType::Trap:     return {10, 250, 85, 255};
        case RoomType::Rest:     return {70, 150, 90, 255};
        case RoomType::Boss:     return {135, 55, 165, 255};
        case RoomType::Shop:     return {55, 160, 175, 255};
        case RoomType::Forge:    return {200, 120, 60, 255};
        case RoomType::Event:    return {120, 180, 220, 255};
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
        case Role::Bard:      return {225, 130, 210, 255};
        case Role::Seidmard:  return {150, 115, 235, 255};
        case Role::Monk:      return {215, 200, 150, 255};
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

// кнопки меню (6 пунктов: Играть, Механики, Достижения, Наследие, Настройки, Выход)
R menuBtn(int i, int y0 = 262) { return {490, y0 + i * 62, 300, 52}; }

// экран достижений: два столбца, строки вниз (со скроллом); экран «Наследие»: город + карточки улучшений
R achRow(int i) { return {40 + (i % 2) * 620, 130 + (i / 2) * 58, 600, 54}; }
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
// Монах носит только реликвию: две остальные ячейки показаны закрытыми.
bool slotLocked(const Hero& h, ItemKind k) {
    return (h.role == Role::Monk && (k == ItemKind::Weapon || k == ItemKind::Armor)) ||
           (raceBansArmor(h.race) && k == ItemKind::Armor);  // Дракон тоже без брони
}
int restAtkBonus(const Hero& h) { return h.role == Role::Monk ? Run::kMonkBuffAtk : Run::kBuffAtk; }
int restDefBonus(const Hero& h) { return h.role == Role::Monk ? Run::kMonkBuffDef : Run::kBuffDef; }

// Кнопки вариантов (стоек) внутри карточки героя на экране выбора.
R variantRect(int cardX, int cardY, int k, int count) {
    constexpr int bw = 86, gap = 8;
    const int total = count * bw + (count - 1) * gap;
    return {cardX + (kSelCardW - total) / 2 + k * (bw + gap), cardY + 176, bw, 28};
}

// Экран рас: 6 карточек в 3 колонки по 2 ряда, герой выбирается по очереди.
R raceCard(int k) { return {190 + (k % 3) * 310, 154 + (k / 3) * 196, 300, 180}; }
const R kRaceBackBtn{170, 660, 420, 44};
const R kRaceNextBtn{690, 660, 420, 44};

R restockButton() { return {kGridX + 290, kGridY + 10, 260, 36}; }
R sellButton() { return {kGridX + 430, kGridY + 10, 290, 34}; }

// кузница: рюкзак чуть ниже, чем в инвентаре, чтобы оставить место под пояснения
R forgeCell(int i) { R r = bagCell(i); r.y += 20; return r; }
R forgeButton() { return {kGridX + 388, kGridY + 432, 304, 50}; }

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
    // Папка с картинками: рядом с игрой или на уровень-два выше.
    for (const char* dir : {"assets", "../assets", "../../assets"}) {
        if (DirectoryExists(dir)) {
            assetDir_ = dir;
            break;
        }
    }
    // Без FLAG_WINDOW_HIGHDPI: raylib держит screen == framebuffer без
    // дополнительного DPI-преобразования.
    // Fullscreen делаем без смены видеорежима монитора (безразличный окно на весь
    // экран): не дёргается окно и нет щелчка реле при запуске/смене разрешения.
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (settings_.fullscreen) flags |= FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST;
    SetConfigFlags(flags);
    InitWindow(settings_.width, settings_.height, "Dungeon Cards - prototype");
    SetExitKey(KEY_NULL);  // Esc открывает меню, а не закрывает игру
    SetTargetFPS(60);

    // Интерфейс рисуется в логических координатах 1280x720, но буфер создаётся
    // в реальном размере окна - так текст остаётся чётким на любом экране.
    buildResolutionList();
    applyDisplay();
    syncRenderTarget();

    // Шрифт с кириллицей: assets/font.ttf или системный шрифт Windows.
    // Атлас грузим с запасом, иначе крупный текст (заголовки) будет мыльным.
    const int mon0 = GetCurrentMonitor();
    const float monitorScale = std::min(GetMonitorWidth(mon0) / static_cast<float>(kScreenW),
                                        GetMonitorHeight(mon0) / static_cast<float>(kScreenH));
    const int fontBase = std::min(256, std::max(48, static_cast<int>(std::ceil(64.f * std::max(1.f, monitorScale)))));
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0x410; c <= 0x44F; ++c) cps.push_back(c);
    cps.push_back(0x401);
    cps.push_back(0x451);
    const std::string bundled = assetDir_ + "/font.ttf";
    const char* candidates[] = {bundled.c_str(), "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
    for (const char* path : candidates) {
        if (FileExists(path)) {
            font_ = LoadFontEx(path, fontBase, cps.data(), static_cast<int>(cps.size()));
            ownFont_ = font_.texture.id != 0;
            if (ownFont_) break;
        }
    }
    if (!ownFont_) font_ = GetFontDefault();
    SetTextureFilter(font_.texture, TEXTURE_FILTER_BILINEAR);

    roster_ = heroRoster();
    resetForNewRun();
    rollRun();

    // Вычисляем высоты сразу при старте, чтобы скроллбар работал с первого кадра
    const int totalRows = (static_cast<int>(roster_.size()) + 3) / 4;
    selContentH_ = static_cast<float>(totalRows * (kSelCardH + kSelGap));
    selViewH_ = 590.f - kSelY;

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
        syncRenderTarget();              // окно могли растянуть или переключить в fullscreen
        update(GetFrameTime());

        BeginTextureMode(target_);
        ClearBackground(kBg);
        rlPushMatrix();
        rlLoadIdentity();
        rlScalef(targetW_ / static_cast<float>(kScreenW), targetH_ / static_cast<float>(kScreenH), 1.f);
        draw();
        rlPopMatrix();
        EndTextureMode();

        BeginDrawing();
        ClearBackground(Color{0, 0, 0, 255});
        float sx, sy, ox, oy;
        viewTransform(sx, sy, ox, oy);
        DrawTexturePro(target_.texture, Rectangle{0.f, 0.f, static_cast<float>(targetW_), -static_cast<float>(targetH_)},
                       Rectangle{ox, oy, kScreenW * sx, kScreenH * sy},
                       Vector2{0.f, 0.f}, 0.f, Color{255, 255, 255, 255});
        if (displayFade_ > 0.f) {
            const int a = static_cast<int>(std::clamp(displayFade_, 0.f, 1.f) * 255.f);
            DrawRectangle(0, 0, GetRenderWidth(), GetRenderHeight(),
                          Color{0, 0, 0, static_cast<unsigned char>(a)});
        }
        EndDrawing();
    }
}

// ----------------------------------------------------- окно и настройки

void Game::loadSettings() {
    std::ifstream f("settings.cfg");
    int w = 0, h = 0, fs = 0, custom = 0;
    if (f >> w >> h >> fs) {
        if (w >= 640 && h >= 360 && w <= 7680 && h <= 4320) {
            settings_.width = w;
            settings_.height = h;
        }
        settings_.fullscreen = fs != 0;
        f >> custom;  // четвёртое поле появилось позже; старые cfg без него
        settings_.customResolution = custom != 0;
    }
}

void Game::saveSettings() const {
    std::ofstream f("settings.cfg");
    f << settings_.width << " " << settings_.height << " " << (settings_.fullscreen ? 1 : 0) << " "
      << (settings_.customResolution ? 1 : 0) << "\n";
}

void Game::buildResolutionList() {
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    resolutions_.clear();
    if (mw > 0 && mh > 0) resolutions_.push_back({mw, mh});  // нативный размер монитора - первый и основной
    const Resolution presets[] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}};
    for (const auto& r : presets) {
        if (mw > 0 && mh > 0 && (r.w > mw || r.h > mh)) continue;
        const bool dup = std::any_of(resolutions_.begin(), resolutions_.end(),
                                     [&](const Resolution& x) { return x.w == r.w && x.h == r.h; });
        if (!dup) resolutions_.push_back(r);
    }
    if (resolutions_.empty()) resolutions_.push_back({1280, 720});

    // Первый запуск (или старый cfg без явного выбора): сразу берём родное разрешение
    // монитора и полноэкранный режим, чтобы интерфейс не был мелким.
    if (!settings_.customResolution && mw > 0 && mh > 0 &&
        (settings_.width < mw || settings_.height < mh)) {
        settings_.width = mw;
        settings_.height = mh;
        settings_.fullscreen = true;
    }

    const bool found = std::any_of(resolutions_.begin(), resolutions_.end(),
                                   [&](const Resolution& r) { return r.w == settings_.width && r.h == settings_.height; });
    if (!found) {
        settings_.width = resolutions_[0].w;
        settings_.height = resolutions_[0].h;
    }
}

void Game::applyDisplay() {
    // Полноэкранный режим = безрамочное окно на весь монитор. Мы НЕ меняем
    // видеорежим монитора: поэтому нет щелчка/дёрганья при запуске и смене
    // разрешения. Разрешение меняет только внутренний буфер отрисовки.
    const int mon = GetCurrentMonitor();
    const int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    const Vector2 mp = GetMonitorPosition(mon);

    if (settings_.fullscreen) {
        SetWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
        SetWindowSize(mw, mh);
        SetWindowPosition(static_cast<int>(mp.x), static_cast<int>(mp.y));
    } else {
        ClearWindowState(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_TOPMOST);
        SetWindowSize(settings_.width, settings_.height);
        const int px = mp.x + (mw - settings_.width) / 2;
        const int py = mp.y + (mh - settings_.height) / 2;
        SetWindowPosition(std::max(static_cast<int>(mp.x), px),
                          std::max(static_cast<int>(mp.y), py));
    }
    saveSettings();
}

void Game::requestDisplayChange() {
    // Менять режим сразу нельзя: на полсекунды закрываем экран чёрным,
    // применяем изменение и только потом проявляем картинку.
    if (displayFadeDir_ != 0 || displayChangePending_) return;
    displayChangePending_ = true;
    displayFadeDir_ = +1;
}

void Game::syncRenderTarget() {
    const float rw = static_cast<float>(GetRenderWidth()), rh = static_cast<float>(GetRenderHeight());
    if (rw <= 0.f || rh <= 0.f) return;
    int tw, th;
    if (settings_.fullscreen) {
        // В fullscreen окно занимает весь монитор, а разрешение задаёт именно
        // внутренний буфер - смена идёт без видеорежима и без дёрганья окна.
        tw = std::max(1, settings_.width);
        th = std::max(1, settings_.height);
    } else {
        // В окне рисуем в физических пикселях: текст остаётся чётким.
        const float fit = std::min(rw / kScreenW, rh / kScreenH);
        tw = std::max(1, static_cast<int>(std::floor(kScreenW * fit + 0.5f)));
        th = std::max(1, static_cast<int>(std::floor(kScreenH * fit + 0.5f)));
    }
    if (tw == targetW_ && th == targetH_) return;
    if (target_.id != 0) UnloadRenderTexture(target_);
    target_ = LoadRenderTexture(tw, th);
    SetTextureFilter(target_.texture, TEXTURE_FILTER_BILINEAR);
    targetW_ = tw;
    targetH_ = th;
    targetScale_ = rw / static_cast<float>(kScreenW);
}

void Game::viewTransform(float& sx, float& sy, float& ox, float& oy) const {
    // Масштабируем буфер до framebuffer с сохранением пропорций. Размер самого
    // буфера при этом не важен - так смена разрешения не двигает картинку.
    const float rw = std::max(1.f, static_cast<float>(GetRenderWidth()));
    const float rh = std::max(1.f, static_cast<float>(GetRenderHeight()));
    const float fit = std::min(rw / static_cast<float>(kScreenW), rh / static_cast<float>(kScreenH));
    sx = fit;
    sy = fit;
    ox = (rw - kScreenW * fit) / 2.f;
    oy = (rh - kScreenH * fit) / 2.f;
}

void Game::beginScissor(int x, int y, int w, int h) const {
    const float sx = targetW_ / static_cast<float>(kScreenW);
    const float sy = targetH_ / static_cast<float>(kScreenH);
    BeginScissorMode(static_cast<int>(std::floor(x * sx + 0.5f)),
                     static_cast<int>(std::floor(y * sy + 0.5f)),
                     std::max(1, static_cast<int>(std::floor(w * sx + 0.5f))),
                     std::max(1, static_cast<int>(std::floor(h * sy + 0.5f))));
}

Vector2 Game::mouse() const {
    const Vector2 m = GetMousePosition();
    float sx, sy, ox, oy;
    viewTransform(sx, sy, ox, oy);
    return {(m.x - ox) / sx, (m.y - oy) / sy};
}

// ------------------------------------------------------------------ профиль

void Game::loadProfile() { profile_ = Profile::loadFile(profilePath_); }
void Game::saveProfile() const { profile_.saveFile(profilePath_); }

// Герои на экране выбора и в забеге уже с улучшениями Наследия (применяются ровно один раз).
void Game::refreshRoster() {
    roster_ = heroRoster();
    for (auto& h : roster_) profile_.meta.applyToHero(h);
    // Между этажами отряд берётся из Run, а карточки пересоздаются заново:
    // переносим выбранную стойку и расу, чтобы они не сбрасывались.
    if (run_) {
        const auto& keptHeroes = run_->party().heroes;
        for (auto& h : roster_) {
            for (const auto& kept : keptHeroes) {
                if (kept.role == h.role && kept.name == h.name) {
                    setHeroVariant(h, heroVariant(kept));
                    setHeroRace(h, static_cast<int>(kept.race));
                    break;
                }
            }
        }
    }
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

void Game::rollRun() {
    refreshRoster();  // вдруг в «Наследии» что-то купили
    // Один набор из шести рас на весь забег; на следующем этаже он не меняется.
    // Перед каждым новым забегом берём свежее случайное зерно, иначе uiRng_
    // с фиксированным стартовым значением выдавал бы одинаковый набор рас.
    if (currentFloor_ == 1) {
        std::vector<int> all(kRaceCount);
        for (int i = 0; i < kRaceCount; ++i) all[i] = i;
        std::random_device rd;
        std::mt19937 raceRng(rd());
        std::shuffle(all.begin(), all.end(), raceRng);
        for (int i = 0; i < 6; ++i) offeredRaces_[i] = all[i];
        std::fill(racePick_, racePick_ + kPartySize, -1);
        raceHero_ = 0;
    }
    std::random_device rd;
    nextSeed_ = rd();
    Rng tmp(nextSeed_);
    auto isDefeated = [&](int id) {
        return std::find(defeatedBosses_.begin(), defeatedBosses_.end(), id) != defeatedBosses_.end();
    };
    std::vector<int> ids;
    for (int i = 0; i < kBossCount; ++i)
        if (!isDefeated(i)) ids.push_back(i);
    if (ids.size() < 3) {
        // Все боссы пройдены - начинаем новый круг, чтобы выбор всегда был из трёх.
        defeatedBosses_.clear();
        ids.clear();
        for (int i = 0; i < kBossCount; ++i) ids.push_back(i);
    }
    tmp.shuffle(ids);
    for (int i = 0; i < 3; ++i) offered_[i] = ids[i];
    bossPick_ = 0;
    bossId_ = offered_[0];
    showBossCard_ = false;
}

void Game::resetForNewRun(uint32_t seed) {
    if (seed == 0) {
        seed = nextSeed_++; // или используйте текущий генератор сидов проекта
    }
    currentFloor_ = 1;
    defeatedBosses_.clear();
    for (int i = 0; i < kMaxRoster; ++i) chosen_[i] = (i < kPartySize);
    run_.reset();
    lastReward_ = RunReward{};
    runRecorded_ = false;
    clearAnims();

    // сброс состояния UI/боя
    stepTimer_ = endTimer_ = closeTimer_ = moveCd_ = 0.f;
    speedIdx_ = 1;
    paused_ = false;
    inRest_ = false;
    restStage_ = 0;
    restActionDone_ = false;
    std::fill(std::begin(restPick_), std::end(restPick_), false);
    invOpen_ = false;
    invSel_ = -1;
    invMsg_.clear();
    shopMsg_.clear();
    forgeMsg_.clear();
    showBossCard_ = false;
    message_.clear();
    messageBad_ = false;
}

void Game::newGame(uint32_t seed) {
    rng_.reseed(seed);

    currentFloor_ = 1;
    defeatedBosses_.clear();

    deck_ = Deck(rng_, bossId_);
    dungeon_ = Dungeon();
    dungeon_.placeShop(rng_);
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

void Game::nextFloor() {
    // Пересобираем подземелье
    dungeon_ = Dungeon();
    dungeon_.placeShop(rng_);

    // Обновляем колоду с новым боссом (bossId_ уже выбран в updateSelect)
    deck_ = Deck(rng_, bossId_);

    // Рука карт
    for (auto& s : hand_) s.has = false;
    for (auto& s : hand_) refill(s);

    // Сброс лимитов. Говорящий ворон даёт +1 размещение на этот этаж.
    placementsLeft_ = kMaxPlacements + (run_ ? run_->takePlacementBonus() : 0);
    discardsLeft_ = kMaxDiscards;
    selected_ = 0;

    screen_ = Screen::Build;
    setMessage(fmt("Этаж %d! Построй новые комнаты.", currentFloor_), false);
    clearAnims();
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
    Party squad;
    if (currentFloor_ == 1) {
        std::vector<Hero> heroes;
        for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i)
            if (chosen_[i]) heroes.push_back(roster_[i]);
        squad = Party(heroes);
    } else {
        if (run_) squad = run_->party(); // берём отряд с лутом и монетами
        else squad = Party();            // защита от вылета
    }

    run_ = std::make_unique<Run>(dungeon_, static_cast<uint32_t>(rng_.range(1, 1 << 30)), squad, currentFloor_);
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
    // Плавный переход между разрешениями: затемнение -> смена режима -> проявление.
    if (displayFadeDir_ > 0) {
        displayFade_ = std::min(1.f, displayFade_ + dt / kDisplayFadeTime);
        if (displayFade_ >= 1.f) {
            displayFadeDir_ = 0;
            if (displayChangePending_) {
                displayChangePending_ = false;
                applyDisplay();
                displayFadeDir_ = -1;
            }
        }
    } else if (displayFadeDir_ < 0) {
        displayFade_ = std::max(0.f, displayFade_ - dt / kDisplayFadeTime);
        if (displayFade_ <= 0.f) {
            displayFade_ = 0.f;
            displayFadeDir_ = 0;
        }
    }

    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_L)) {
        devOpen_ = !devOpen_;
        devMsg_ = devOpen_ ? "Режим разработчика включён." : "";
    }
    // Esc: пауза во время игры, назад из меню/настроек
    if (IsKeyPressed(KEY_ESCAPE)) {
        switch (screen_) {
            case Screen::Settings: screen_ = settingsBack_; break;
            case Screen::Achievements:
            case Screen::Legacy:
            case Screen::Guide:    screen_ = Screen::MainMenu; break;
            case Screen::Pause:    screen_ = prevScreen_; break;
            case Screen::RaceSelect: screen_ = Screen::Select; break;  // назад к выбору отряда
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
        resetForNewRun();
        rollRun();
        screen_ = Screen::Select;
        setMessage("Выбери четырёх героев и босса для похода.", false);
        return;
    }
    const bool devConsumed = updateDevPanel();
    if (!devConsumed) {
        switch (screen_) {
            case Screen::MainMenu: updateMainMenu(); break;
            case Screen::Select: updateSelect(); break;
            case Screen::RaceSelect: updateRaceSelect(); break;
            case Screen::Build: updateBuild(); break;
            case Screen::Run: updateRun(dt); break;
            case Screen::Pause: updatePause(); break;
            case Screen::Settings: updateSettings(); break;
            case Screen::Achievements: updateAchievements(); break;
            case Screen::Legacy: updateLegacy(); break;
            case Screen::Guide: updateGuide(); break;
            case Screen::Result: break;
        }
    }
}

// ------------------------------------------------------------------ меню

void Game::updateMainMenu() {
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int pick = IsKeyPressed(KEY_ENTER) ? 0 : -1;
    for (int i = 0; i < 6; ++i)
        if (click && inR(m, menuBtn(i))) pick = i;
    if (pick == 0) {
        resetForNewRun();
        rollRun();
        setMessage("Выбери четырёх героев и босса для похода.", false);
        screen_ = Screen::Select;
    } else if (pick == 1) {
        screen_ = Screen::Guide;
    } else if (pick == 2) {
        achScroll_ = 0.f;
        screen_ = Screen::Achievements;
    } else if (pick == 3) {
        legacyMsg_.clear();
        screen_ = Screen::Legacy;
    } else if (pick == 4) {
        settingsBack_ = Screen::MainMenu;
        screen_ = Screen::Settings;
    } else if (pick == 5) {
        quit_ = true;
    }
}

// Достижения: просмотр списка (награда выдаётся автоматически при выполнении условия).
void Game::updateAchievements() {
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.f) achScroll_ -= wheel * 40.f;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_PAGE_DOWN)) achScroll_ += 40.f;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_PAGE_UP)) achScroll_ -= 40.f;
    const int total = static_cast<int>(achievements().size());
    const int rows = (total + 1) / 2;
    const float contentH = 130.f + rows * 58.f + 20.f;
    const float maxScroll = std::max(0.f, contentH - (kScreenH - 70.f));
    achScroll_ = std::max(0.f, std::min(achScroll_, maxScroll));
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inR(mouse(), kBackBtn)) screen_ = Screen::MainMenu;
}

// «Наследие»: покупка постоянных улучшений за артефакты; фон города.
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

// Окно события рисуется поверх арены, поэтому кнопки считаются в её координатах.
static R eventChoiceBtn(int i) {
    return {kGridX + 40, kGridY + 150 + i * 62, kArenaW - 80, 56};
}

// --- Событие (комната: Событие): выбор среди вариантов реальных NPC
void Game::updateEventChoice() {
    if (run_->phase() != RunPhase::EventChoice || run_->eventChoices().empty()) return;
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const int n = std::min(static_cast<int>(run_->eventChoices().size()), 5);
    for (int i = 0; i < n; ++i) {
        if (IsKeyPressed(KEY_ONE + i) || (click && inR(m, eventChoiceBtn(i)))) {
            run_->takeEventChoice(i);
            return;
        }
    }
}

void Game::drawEventChoice() {
    const Run& r = *run_;
    const EventResult& info = r.eventResult();
    const Color tint = roomColor(RoomType::Event);
    const float cx = kGridX + kArenaW / 2.f;

    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, Color{10, 13, 20, 226});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, tint);
    drawTextCentered("СОБЫТИЕ", cx, kGridY + 16.f, 22, tint);
    drawTextCentered(info.title, cx, kGridY + 48.f, 24, kText);
    const auto story = wrap(info.story, kArenaW - 80.f, 15.f);
    float sy = kGridY + 84.f;
    for (const auto& line : story) {
        drawTextCentered(line, cx, sy, 15, kDim);
        sy += 18.f;
    }

    const auto& cs = r.eventChoices();
    const int n = std::min(static_cast<int>(cs.size()), 5);
    const Vector2 m = mouse();
    for (int i = 0; i < n; ++i) {
        const R b = eventChoiceBtn(i);
        const bool hover = inR(m, b);
        DrawRectangle(b.x, b.y, b.w, b.h, hover ? Color{52, 64, 84, 255} : Color{28, 34, 46, 255});
        DrawRectangleLines(b.x, b.y, b.w, b.h, hover ? tint : kGridLine);
        drawText(fmt("%d. %s", i + 1, cs[i].name.c_str()), b.x + 12.f, b.y + 6.f, 17, hover ? tint : kText);
        const auto dl = wrap(cs[i].desc, b.w - 24.f, 13.f);
        float dy = b.y + 28.f;
        for (size_t k = 0; k < dl.size() && k < 2; ++k, dy += 14.f)
            drawText(dl[k], b.x + 12.f, dy, 13, kDim);
    }
    drawTextCentered("Цифра 1-5 или клик - выбрать вариант", cx, kGridY + kArenaH - 26.f, 14, kDim);
}

void Game::updateGuide() {
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.f) guideScroll_ -= wheel * 26.f;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_PAGE_DOWN)) guideScroll_ += 30.f;
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_PAGE_UP))   guideScroll_ -= 30.f;
    const bool back = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_BACKSPACE);
    if (back || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inR(mouse(), kBackBtn))) {
        screen_ = Screen::MainMenu;
        guideScroll_ = 0.f;
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
        resetForNewRun();
        screen_ = Screen::MainMenu;
    } else if (pick == 3) {
        quit_ = true;
    }
}

void Game::updateSettings() {
    if (displayFadeDir_ != 0 || displayChangePending_) return;  // переход уже идёт
    const Vector2 m = mouse();
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    const int n = static_cast<int>(resolutions_.size());
    for (int i = 0; i < n; ++i) {
        if (!inR(m, resBtn(i))) continue;
        settings_.width = resolutions_[i].w;
        settings_.height = resolutions_[i].h;
        // Fullscreen сохраняем: смена режима пройдёт с затемнением.
        settings_.customResolution = true;
        requestDisplayChange();
        return;
    }
    if (inRect(m, 440, 190 + n * 58 + 14, 400, 48)) {
        settings_.fullscreen = !settings_.fullscreen;
        settings_.customResolution = true;
        requestDisplayChange();
    } else if (inRect(m, 440, 190 + n * 58 + 84, 400, 48)) {
        screen_ = settingsBack_;
    }
}

// Экран выбора расы: по одному герою за раз, дублиamong четырёх запрещены.
void Game::updateRaceSelect() {
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // Какая раса уже занята другим героем (или выбрана текущему).
    auto takenByOther = [&](int slot) {
        for (int h = 0; h < kPartySize; ++h)
            if (h != raceHero_ && racePick_[h] == slot) return true;
        return false;
    };

    // Выбор расы: клавиши 1..6 или клик по карточке.
    const int raceKeys[6] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX};
    for (int k = 0; k < 6; ++k) {
        const R r = raceCard(k);
        const bool pressed = IsKeyPressed(raceKeys[k]) || (click && inRect(m, r.x, r.y, r.w, r.h));
        if (!pressed) continue;
        if (takenByOther(k)) {
            setMessage("Эту расу уже выбрал другой герой.", true);
            continue;
        }
        // Повторный клик по своей расе снимает выбор.
        racePick_[raceHero_] = (racePick_[raceHero_] == k) ? -1 : k;
        setMessage("", false);
    }

    // Назад: к предыдущему герою или к выбору отряда.
    const bool back = IsKeyPressed(KEY_BACKSPACE) || (click && inRect(m, kRaceBackBtn.x, kRaceBackBtn.y,
                                                                      kRaceBackBtn.w, kRaceBackBtn.h));
    if (back) {
        if (raceHero_ > 0) {
            --raceHero_;
            setMessage("", false);
        } else {
            screen_ = Screen::Select;
            setMessage("Выбери четырёх героев и босса для похода.", false);
        }
        return;
    }

    // Далее: подтвердить для текущего героя, после четвёртого - начать поход.
    const bool next = IsKeyPressed(KEY_ENTER) || (click && inRect(m, kRaceNextBtn.x, kRaceNextBtn.y,
                                                                   kRaceNextBtn.w, kRaceNextBtn.h));
    if (!next) return;
    if (racePick_[raceHero_] < 0) {
        setMessage("Сначала выбери расу для этого героя.", true);
        return;
    }
    if (raceHero_ + 1 < kPartySize) {
        ++raceHero_;
        setMessage("", false);
        return;
    }

    // Применяем расы к выбранным героям (в порядке карточек отряда).
    int hero = 0;
    for (int i = 0; i < kMaxRoster && hero < kPartySize; ++i) {
        if (!chosen_[i]) continue;
        setHeroRace(roster_[i], offeredRaces_[racePick_[hero]]);
        ++hero;
    }
    newGame(nextSeed_);
}

void Game::updateSelect() {
    auto isDefeated = [&](int id) {
        return std::find(defeatedBosses_.begin(), defeatedBosses_.end(), id) != defeatedBosses_.end();
    };

    // босс из трёх: Q / W / E или клик по плитке (повторный клик - подробности)
    if (IsKeyPressed(KEY_Q) && !isDefeated(offered_[0])) bossPick_ = 0;
    if (IsKeyPressed(KEY_W) && !isDefeated(offered_[1])) bossPick_ = 1;
    if (IsKeyPressed(KEY_E) && !isDefeated(offered_[2])) bossPick_ = 2;

    if (IsKeyPressed(KEY_B)) showBossCard_ = !showBossCard_;
    if (showBossCard_) {
        if (IsKeyPressed(KEY_ENTER)) showBossCard_ = false;
        return;
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.f) {
        selScrollTarget_ -= wheel * 40.f;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_PAGE_DOWN)) selScrollTarget_ += 60.f;
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_PAGE_UP)) selScrollTarget_ -= 60.f;

    // Ограничиваем прокрутку
    float maxScroll = std::max(0.f, selContentH_ - selViewH_);
    selScrollTarget_ = std::max(0.f, std::min(selScrollTarget_, maxScroll));

    // Плавная интерполяция
    selScroll_ += (selScrollTarget_ - selScroll_) * 15.f * GetFrameTime();
    if (std::abs(selScroll_ - selScrollTarget_) < 0.5f) selScroll_ = selScrollTarget_;

    const int keys[kMaxRoster] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX,
                                  KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_MINUS};
    Vector2 m = mouse();
    bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (currentFloor_ == 1) {
        for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i) {
            int x = kSelX + (i % 4) * (kSelCardW + kSelGap);
            int y = static_cast<int>(kSelY + (i / 4) * (kSelCardH + kSelGap) - selScroll_);

            // Кнопки вариантов (пока только стойки Монаха) обрабатываем раньше,
            // чем клик по всей карточке, иначе выбор стойки снимал бы героя.
            bool variantClicked = false;
            const auto variants = heroVariants(roster_[i].role);
            for (int v = 0; v < static_cast<int>(variants.size()); ++v) {
                const R r = variantRect(x, y, v, static_cast<int>(variants.size()));
                if (!click || !inRect(m, r.x, r.y, r.w, r.h)) continue;
                setHeroVariant(roster_[i], variants[v].id);
                setMessage("", false);
                variantClicked = true;
                break;
            }
            if (variantClicked) continue;

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
    }
    for (int i = 0; i < 3; ++i) {
        if (click && inRect(m, 520 + i * 246, 630, 238, 62)) {
            if (isDefeated(offered_[i])) continue;
            if (bossPick_ == i) showBossCard_ = true;
            bossPick_ = i;
        }
    }
    bossId_ = offered_[bossPick_];
    if (IsKeyPressed(KEY_ENTER)) {
        if (chosenCount() == kPartySize) {
            if (currentFloor_ == 1) {
                std::fill(racePick_, racePick_ + kPartySize, -1);
                raceHero_ = 0;
                screen_ = Screen::RaceSelect;
                setMessage("Выбери расу для каждого героя.", false);
            } else {
                nextFloor();
            }
        } else {
            setMessage(fmt("Нужно выбрать ровно %d героя.", kPartySize), true);
        }
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

void Game::updateForge() {
    if (invSel_ >= static_cast<int>(run_->party().bag.size())) invSel_ = -1;
    const Vector2 m = mouse();
    auto& bag = run_->partyMutable().bag;

    // Улучшение - отдельное подтверждение: сначала выбор, потом кнопка/Enter.
    const auto tryUpgrade = [&]() {
        if (invSel_ < 0) { forgeMsg_ = "Сначала выбери предмет в рюкзаке."; return; }
        const Item& it = bag[invSel_];
        if (it.tier >= 4) { forgeMsg_ = "Уже максимальный уровень: улучшать нечего."; return; }
        if (it.kind == ItemKind::Potion) { forgeMsg_ = "Зелья в кузнице не улучшаются."; return; }
        const int price = run_->forgePrice(it);
        if (run_->party().coins < price) {
            forgeMsg_ = fmt("Не хватает монет: нужно %d, есть %d.", price, run_->party().coins);
            return;
        }
        if (run_->forgeUpgrade(invSel_))
            forgeMsg_ = fmt("Улучшено: %s теперь ур.%d.", bag[invSel_].name.c_str(), bag[invSel_].tier);
    };

    // Снятие отрицательных свойств уникальной вещи из событий (вместо улучшения).
    const auto tryRemovePenalty = [&]() {
        Item* target = nullptr;
        Hero* owner = nullptr;
        if (invSel_ >= 0 && invSel_ < static_cast<int>(bag.size())) {
            Item& it = bag[invSel_];
            if (it.eventPenaltyAtk != 0 || it.eventPenaltyDef != 0) target = &it;
        }
        if (!target) {
            for (Hero& h : run_->partyMutable().heroes) {
                for (std::optional<Item>* slot : {&h.weapon, &h.armor}) {
                    if (*slot && ((*slot)->eventPenaltyAtk != 0 || (*slot)->eventPenaltyDef != 0)) {
                        target = &**slot;
                        owner = &h;
                        break;
                    }
                }
                if (target) break;
            }
        }
        if (!target) { forgeMsg_ = "Нет отрицательных свойств. Выбери вещь в рюкзаке."; return; }
        if (run_->party().coins < 5) { forgeMsg_ = "Не хватает монет: 5."; return; }
        run_->partyMutable().coins -= 5;
        target->eventPenaltyAtk = 0;
        target->eventPenaltyDef = 0;
        if (owner) owner->recalc();
        forgeMsg_ = "Отрицательные свойства очищены (-5 монет).";
    };

    if (IsKeyPressed(KEY_E)) tryRemovePenalty();

    if (IsKeyPressed(KEY_ESCAPE) || (IsKeyPressed(KEY_ENTER) && invSel_ < 0)) {
        run_->leaveForge();
        forgeMsg_.clear();
        return;
    }
    if (IsKeyPressed(KEY_ENTER)) { tryUpgrade(); return; }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    if (inR(m, forgeButton())) { tryUpgrade(); return; }
    for (int i = 0; i < static_cast<int>(bag.size()) && i < kBagCells; ++i) {
        if (!inR(m, forgeCell(i))) continue;
        invSel_ = (invSel_ == i) ? -1 : i;
        forgeMsg_.clear();
        return;
    }
}

void Game::updateBossReward() {
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const auto& opts = run_->bossRewardOptions();
    const int baseY = kGridY + 170;
    if (!run_->bossRewardClaimed()) {
        for (int i = 0; i < static_cast<int>(opts.size()) && i < 3; ++i) {
            const R r{kGridX + 22 + i * 228, baseY, 214, 240};
            if (click && inR(m, r)) { run_->claimBossRewardItem(i); return; }
        }
        const R art{kGridX + 22, baseY + 260, 680, 54};
        if (click && inR(m, art)) {
            if (run_->claimBossRewardArtifacts()) { profile_.artifacts += 3; profile_.artifactsEarned += 3; saveProfile(); }
            return;
        }
        return;
    }
    const R cont{kGridX + 40, kGridY + 240, 300, 52};
    const R end{kGridX + 380, kGridY + 240, 300, 52};
    if (click && inR(m, cont)) {
        // Записываем победу над боссом/этажом, но не завершаем забег
        if (!run_->bossRewardRecorded()) {
            lastReward_ = profile_.recordFloorClear(run_->summary());
            run_->markBossRewardRecorded();
            saveProfile();
        }
        if (std::find(defeatedBosses_.begin(), defeatedBosses_.end(), bossId_) == defeatedBosses_.end()) defeatedBosses_.push_back(bossId_);
        run_->finishRunAfterBoss(false);
        ++currentFloor_;
        screen_ = Screen::Select;
        rollRun();
        setMessage(fmt("Этаж %d. Выбери следующего босса.", currentFloor_), false);
    } else if (click && inR(m, end)) {
        run_->finishRunAfterBoss(true);
    }
}

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
            restActionDone_ = false;
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
        case RunPhase::EventChoice:
            updateEventChoice();
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
        case RunPhase::ForgeChoice:
            if (invOpen_) updateInventory();
            else updateForge();
            break;
        case RunPhase::BossReward:
            if (invOpen_) updateInventory();
            else updateBossReward();
            break;
        default:
            break;
    }

    if (run_->finished()) {
        endTimer_ += dt;
        if (endTimer_ > 1.6f) {
            recordRunResult();
            resultWin_ = run_->phase() == RunPhase::Victory;
            screen_ = Screen::Result;
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
    if (IsKeyPressed(KEY_G) && run_->openForge()) {   // G - кузница (T занят торговцем)
        invSel_ = -1;
        forgeMsg_.clear();
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
            const bool canArtifact = run_->artifacts() >= run_->reviveArtifactCost();
            if (!anyDead) restMsg_ = "Павших героев нет.";
            else if (!run_->canRestReviveFree() && !canCoins && !canArtifact)
                restMsg_ = fmt("Бесплатная попытка использована. Нужно %d монет или %d артефакта. Есть: %d монет, %d артефактов.",
                               run_->reviveCoinCost(), run_->reviveArtifactCost(), run_->party().coins, run_->artifacts());
            else {
                restStage_ = 2;
                restMsg_.clear();
            }
        }
        return;
    }

    // стадия 2: выбор павшего героя и способа воскрешения (бесплатно, монеты, артефакты)
    if (restStage_ == 2) {
        std::vector<int> dead;
        for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i)
            if (!hs[i].alive()) dead.push_back(i);
        if (IsKeyPressed(KEY_B) || dead.empty()) {
            restStage_ = 0;
            return;
        }
        for (int k = 0; k < static_cast<int>(dead.size()); ++k) {
            const int heroIdx = dead[k];
            const int rx = kGridX + 40, ry = kGridY + 112 + k * 84;
            const bool onFree = click && inRect(m, rx + 302, ry + 14, 106, 44);
            const bool onCoin = click && inRect(m, rx + 416, ry + 14, 106, 44);
            const bool onArtifact = click && inRect(m, rx + 530, ry + 14, 106, 44);
            if (!onFree && !onCoin && !onArtifact) continue;

            // 0 - бесплатная попытка, 1 - монеты, 2 - артефакты
            const int way = onFree ? 0 : onCoin ? 1 : 2;
            bool ok = false;
            if (way == 0) {
                ok = run_->restReviveFree(heroIdx);
            } else if (way == 2) {
                ok = run_->restReviveArtifact(heroIdx);
                if (ok) {
                    profile_.artifacts = run_->artifacts();
                    saveProfile();
                }
            } else {
                ok = run_->restRevive(heroIdx);
            }

            if (ok) {
                const int pct = way == 0 ? Run::kFreeRevivePct : way == 2 ? Run::kReviveArtifactPct : Run::kRevivePct;
                restMsg_ = fmt("%s воскрешён: %d%% здоровья.", hs[heroIdx].name.c_str(), pct);
                restStage_ = 0;
            } else if (way == 0) {
                restMsg_ = fmt("Попытка не удалась (шанс %d%%). Можно доплатить за гарантию.", run_->reviveChancePct());
            } else {
                restMsg_ = way == 2 ? "Не хватает артефактов." : "Не хватает монет.";
            }
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
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inR(m, restockButton())) {
        if (run_->restockShop()) {
            shopMsg_ = fmt("Ассортимент обновлён: -%d монет.", Run::kShopRestockCost);
        } else {
            shopMsg_ = fmt("Не хватает монет: нужно %d, есть %d.", Run::kShopRestockCost, run_->party().coins);
        }
        return;
    }
    const auto* stock = run_->shopStock();
    if (!stock || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    for (int i = 0; i < static_cast<int>(stock->size()); ++i) {
        if (!inRect(m, kGridX + 20, kGridY + 88 + i * 76, 680, 68)) continue;
        const ShopOffer& o = (*stock)[i];
        if (o.sold) {
            shopMsg_ = "Этот лот уже продан.";
        } else if (run_->party().coins < run_->effectivePrice(o)) { 
            shopMsg_ = fmt("Не хватает монет: нужно %d, есть %d.", run_->effectivePrice(o), run_->party().coins); 
        } else if (run_->buy(i)) {
            shopMsg_ = "Куплено: " + o.item.name + ". Надеть - в инвентаре (I).";
        }
        return;
    }
}

// Панель разработчика stretched на правую informational панель: слева остаются
// сетка подземелья и руки карт, с которыми нужно работать в режиме постройки.
static R devRect() { return {kPanelX - 6, 10, kPanelW + 12, kScreenH - 20}; }

static const RoomType kDevRooms[9] = {
    RoomType::Start, RoomType::Combat, RoomType::Treasure, RoomType::Trap, RoomType::Rest,
    RoomType::Boss, RoomType::Shop, RoomType::Forge, RoomType::Event,
};

bool Game::updateDevPanel() {
    if (!devOpen_) return false;
    const R panel = devRect();
    const Vector2 m = mouse();
    const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (!click) return false;
    if (!inR(m, panel)) return false;

    // tier buttons
    for (int t = 1; t <= 4; ++t) {
        R r{panel.x + 12 + (t - 1) * 84, panel.y + 82, 76, 28};
        if (inR(m, r)) { devItemTier_ = t; devMsg_ = fmt("Тир предметов: %d", t); return true; }
    }

    // item buttons
    struct Btn { ItemKind kind; bool potion; };
    const Btn btns[] = {
        {ItemKind::Weapon, false},
        {ItemKind::Armor, false},
        {ItemKind::Relic, false},
        {ItemKind::Potion, true},
    };
    for (int i = 0; i < 4; ++i) {
        R r{panel.x + 12 + (i % 2) * 168, panel.y + 118 + (i / 2) * 34, 160, 30};
        if (!inR(m, r)) continue;
        if (!run_) { devMsg_ = "Нет активного забега."; return true; }
        Rng rr(static_cast<uint32_t>(GetTime() * 1000.0) + static_cast<uint32_t>(i * 101 + devItemTier_));
        Item it = btns[i].potion ? makePotion(devItemTier_) : rollLootOfKind(rr, btns[i].kind, devItemTier_);
        run_->partyMutable().bag.push_back(it);
        devMsg_ = "Добавлено: " + it.name;
        return true;
    }

    // Room buttons: только в режиме постройки заменяют тип выбранной карты в руке.
    // Во время забега новые карты не выбираются — забег уже начат.
    for (int t = 0; t < 9; ++t) {
        R r{panel.x + 12 + (t % 3) * 156, panel.y + 258 + (t / 3) * 32, 144, 28};
        if (!inR(m, r)) continue;
        if (screen_ != Screen::Build) {
            devMsg_ = "Выбор карт доступен только при размещении подземелья.";
            return true;
        }
        if (!hand_[selected_].has) { devMsg_ = "Выберите карту в руке."; return true; }
        hand_[selected_].card.type = kDevRooms[t];
        if (kDevRooms[t] == RoomType::Boss) hand_[selected_].card.difficulty = 4;
        else if (kDevRooms[t] == RoomType::Start) hand_[selected_].card.difficulty = 0;
        else if (hand_[selected_].card.difficulty < 1 || hand_[selected_].card.difficulty > 3)
            hand_[selected_].card.difficulty = 1;
        devMsg_ = fmt("Карта в руке: %s. Поставь её кликом на поле.", roomTypeName(kDevRooms[t]));
        return true;
    }

    // gold
    if (inR(m, {panel.x + 12, panel.y + 190, 160, 30})) {
        if (!run_) { devMsg_ = "Нет активного забега."; return true; }
        run_->grantCoins(50);
        devMsg_ = fmt("Золото: %d (+50)", run_->party().coins);
        return true;
    }
    if (inR(m, {panel.x + 180, panel.y + 190, 160, 30})) {
        if (!run_) { devMsg_ = "Нет активного забега."; return true; }
        run_->partyMutable().coins = std::max(0, run_->party().coins - 50);
        devMsg_ = fmt("Золото: %d (-50)", run_->party().coins);
        return true;
    }

    // heroes heal/kill
    if (!run_) { devMsg_ = "Нет активного забега."; return true; }
    auto& heroes = run_->partyMutable().heroes;
    for (int i = 0; i < static_cast<int>(heroes.size()) && i < kMaxRoster; ++i) {
        R heal{panel.x + 280, panel.y + 400 + i * 20, 64, 18};
        R kill{panel.x + 352, panel.y + 400 + i * 20, 64, 18};
        if (inR(m, heal)) {
            heroes[i].hp = heroes[i].maxHp;
            heroes[i].away = false;
            devMsg_ = heroes[i].name + ": вылечен.";
            return true;
        }
        if (inR(m, kill)) {
            heroes[i].hp = 0;
            devMsg_ = heroes[i].name + ": убит.";
            return true;
        }
    }
    return true;
}

void Game::drawDevPanel() {
    const R panel = devRect();
    DrawRectangle(panel.x, panel.y, panel.w, panel.h, Color{16, 18, 26, 248});
    DrawRectangleLines(panel.x, panel.y, panel.w, panel.h, Color{255, 120, 120, 255});
    drawText("КОНСОЛЬ РАЗРАБОТЧИКА", panel.x + 12.f, panel.y + 10.f, 20, Color{255, 140, 140, 255});
    drawText(fmt("Тир: %d", devItemTier_), panel.x + panel.w - 96.f, panel.y + 14.f, 15, kDim);
    DrawLine(panel.x + 12, panel.y + 54, panel.x + panel.w - 12, panel.y + 54, Color{90, 60, 70, 255});

    // компактные кнопки: обычный drawButton рисует 22-й шрифт и не помещается в 28 px
    const Vector2 m = mouse();
    auto smallBtn = [&](R r, const std::string& label, bool selected, bool enabled, int fs = 14) {
        const bool hover = enabled && inR(m, r);
        DrawRectangle(r.x, r.y, r.w, r.h, !enabled ? Color{28, 29, 36, 255}
                                                   : hover ? Color{56, 62, 84, 255} : Color{36, 38, 52, 255});
        DrawRectangleLines(r.x, r.y, r.w, r.h, selected ? kGood : hover ? kAccent : kGridLine);
        drawTextCentered(label, r.x + r.w / 2.f, r.y + (r.h - fs) / 2.f - 1.f, fs, enabled ? kText : kDim);
    };

    drawText("ПРЕДМЕТЫ", panel.x + 12.f, panel.y + 60.f, 14, kDim);
    for (int t = 1; t <= 4; ++t) {
        R r{panel.x + 12 + (t - 1) * 84, panel.y + 82, 76, 28};
        smallBtn(r, fmt("T%d", t), devItemTier_ == t, true, 15);
    }
    const char* labels[4] = {"Оружие", "Броня", "Реликвия", "Зелье"};
    for (int i = 0; i < 4; ++i) {
        R r{panel.x + 12 + (i % 2) * 168, panel.y + 118 + (i / 2) * 34, 160, 30};
        smallBtn(r, labels[i], false, run_ != nullptr, 15);
    }
    smallBtn({panel.x + 12, panel.y + 190, 160, 30}, "+50 золота", false, run_ != nullptr, 15);
    smallBtn({panel.x + 180, panel.y + 190, 160, 30}, "-50 золота", false, run_ != nullptr, 15);
    DrawLine(panel.x + 12, panel.y + 232, panel.x + panel.w - 12, panel.y + 232, Color{90, 60, 70, 255});

    drawText("КОМНАТЫ: заменить карту в руке", panel.x + 12.f, panel.y + 238.f, 14, kDim);
    const bool roomEnabled = screen_ == Screen::Build && hand_[selected_].has;
    for (int t = 0; t < 9; ++t) {
        R r{panel.x + 12 + (t % 3) * 156, panel.y + 258 + (t / 3) * 32, 144, 28};
        smallBtn(r, roomTypeName(kDevRooms[t]), false, roomEnabled, 14);
    }
    if (!roomEnabled)
        drawText("Доступно только при размещении подземелья.", panel.x + 12.f, panel.y + 358.f, 12, kDim);
    DrawLine(panel.x + 12, panel.y + 376, panel.x + panel.w - 12, panel.y + 376, Color{90, 60, 70, 255});

    drawText("ГЕРОИ: лечить / убить", panel.x + 12.f, panel.y + 382.f, 14, kDim);
    if (run_) {
        const auto& heroes = run_->party().heroes;
        for (int i = 0; i < static_cast<int>(heroes.size()) && i < kMaxRoster; ++i) {
            const auto& h = heroes[i];
            drawText(fmt("%-10s %3d/%3d", h.name.c_str(), std::max(0, h.hp), h.maxHp),
                     panel.x + 12.f, panel.y + 402.f + i * 20.f, 14, h.alive() ? kText : kBad);
            smallBtn({panel.x + 280, panel.y + 400 + i * 20, 64, 18}, "леч", false, true, 12);
            smallBtn({panel.x + 352, panel.y + 400 + i * 20, 64, 18}, "убить", false, true, 12);
        }
    } else {
        drawText("Открой забег для действий с героями.", panel.x + 12.f, panel.y + 402.f, 14, kDim);
    }
    drawText("Ctrl+L - закрыть панель", panel.x + 12.f, panel.y + panel.h - 48.f, 13, kDim);
    if (!devMsg_.empty()) drawText(devMsg_, panel.x + 12.f, panel.y + panel.h - 26.f, 14, Color{255, 200, 160, 255});
}

// Инвентарь: выбрать предмет в рюкзаке, затем кликнуть слот героя (оружие/броня/реликвия)
// или портрет героя (для зелья). Клик по надетому предмету снимает его.
void Game::updateInventory() {
    const Party& p = run_->party();
    Vector2 m = mouse();
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) invSel_ = -1;
    if (invSel_ >= static_cast<int>(p.bag.size())) invSel_ = -1;

    // У торговца можно продать выбранную вещь за половину стоимости.
    const bool canSell = run_->inShopRoom() && invSel_ >= 0;
    auto sellSelected = [&]() {
        if (!canSell) return;
        const int price = itemSellPrice(p.bag[invSel_]);
        if (run_->sellItem(invSel_)) {
            invMsg_ = fmt("Продано: +%d монет.", price);
            invSel_ = -1;
        }
    };
    if (canSell && IsKeyPressed(KEY_S)) sellSelected();
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    if (canSell && inR(m, sellButton())) {
        sellSelected();
        return;
    }

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
                } else if (slotLocked(p.heroes[h], kSlotKinds[k])) {
                    invMsg_ = "Монах не носит оружие и броню - только реликвию.";
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

        // реплика босса
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
            const float stagger = (a.kind == CombatAction::Heal && a.hits.size() > 1)
                                      ? 0.16f * static_cast<float>(hi) : 0.f;
            const float hitDelay = delay + stagger;
            Vector2 to = posOf(h.targetHero, h.idx);
            if (a.kind == CombatAction::Ranged || (a.kind == CombatAction::Heal && a.actor >= 0)) {
                Proj p;
                p.from = from;
                p.to = to;
                p.col = projCol;
                p.t = -stagger;
                projs_.push_back(p);
            }
            Floater f;
            float jitter = static_cast<float>(static_cast<int>(uiRng_() % 21) - 10);
            f.pos = {to.x + jitter, to.y - halfSize(h.targetHero, h.idx) - 4.f};
            f.text = h.heal ? fmt("+%d", h.amount) : fmt("-%d%s", h.amount, h.crit ? "!" : "");
            f.col = h.heal ? kHeal : kDamage;
            f.size = h.crit ? 32.f : 22.f;
            f.delay = hitDelay;
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
                cf.delay = 0.2f;
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

const Texture2D* Game::tex(const std::string& rel) const {
    auto it = tex_.find(rel);
    if (it == tex_.end()) {
        Texture2D t{};
        std::string base = rel;
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

bool Game::drawArt(const std::string& rel, float x, float y, float w, float h, Color tint) const {
    const Texture2D* t = tex(rel);
    if (!t || t->width <= 0 || t->height <= 0) return false;
    const float sc = std::min(w / t->width, h / t->height);
    const float dw = t->width * sc, dh = t->height * sc;
    DrawTexturePro(*t, Rectangle{0.f, 0.f, static_cast<float>(t->width), static_cast<float>(t->height)},
                   Rectangle{x + (w - dw) / 2.f, y + (h - dh) / 2.f, dw, dh}, Vector2{0.f, 0.f}, 0.f, tint);
    return true;
}

void Game::drawBackground(const std::string& rel, float x, float y, float w, float h) const {
    const Texture2D* t = tex(rel);
    if (!t || t->width <= 0 || t->height <= 0) return;
    DrawTexturePro(*t, Rectangle{0.f, 0.f, static_cast<float>(t->width), static_cast<float>(t->height)},
                   Rectangle{x, y, w, h}, Vector2{0.f, 0.f}, 0.f, Color{255, 255, 255, 255});
}

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

// Звезда-фигура (без шрифтов: в шрифте нет символа ★).
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
        drawStar(cx, y, r + 1.6f, Color{12, 12, 18, 255});  // тёмный контур для читаемости
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
void Game::drawEquipSlot(int x, int y, int size, ItemKind kind, const std::optional<Item>& item,
                         bool fits, bool hover, bool locked) const {
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
    if (locked) {
        DrawRectangle(x, y, size, size, {18, 18, 22, 200});
        drawTextCentered("ЗАКР.", fx + fs / 2.f, fy + fs / 2.f - 7.f, 11, kBad);
    }
    DrawRectangleLines(x, y, size, size,
                       locked ? kBad : fits ? kGood : hover ? kAccent : (item ? tierColor(item->tier) : kGridLine));
    if (fits && !locked) DrawRectangleLines(x + 1, y + 1, size - 2, size - 2, kGood);
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
        if (labels) DrawRectangle(px + pad, py + cell / 2 - 18, cell - 2 * pad, 38, {0, 0, 0, 120});
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
        // Сложность комнаты показываем золотыми звёздами, как уровень предмета.
        if (c.difficulty > 0 && c.type != RoomType::Boss) {
            const float starRadius = 5.5f;
            const float starStep = starRadius * 2.15f;
            const float totalWidth = starRadius * 2.f + (c.difficulty - 1) * starStep;
            drawStars(px + (cell - totalWidth) / 2.f, py + cell - 9.f, c.difficulty, starRadius);
        }
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
                        screen_ == Screen::Legacy || screen_ == Screen::Guide || screen_ == Screen::RaceSelect;
    drawBackground(menuBg ? "backgrounds/menu.png" : "backgrounds/game.png", 0.f, 0.f, kScreenW, kScreenH);
    switch (screen_) {
        case Screen::MainMenu: drawMainMenu(); break;
        case Screen::RaceSelect: drawRaceSelect(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Achievements: drawAchievements(); break;
        case Screen::Legacy: drawLegacy(); break;
        case Screen::Guide: drawGuide(); break;
        case Screen::Pause:
            drawScreen(prevScreen_);
            drawPause();
            break;
        default: drawScreen(screen_); break;
    }
    if (devOpen_) drawDevPanel();
}

void Game::drawScreen(Screen sc) {
    switch (sc) {
        case Screen::Select: drawSelect(); break;
        case Screen::RaceSelect: drawRaceSelect(); break;
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

    const std::string labels[6] = {"Играть", "Механики",
                                   fmt("Достижения  %d/%d", profile_.unlockedCount(), static_cast<int>(achievements().size())),
                                   "Наследие", "Настройки", "Выход"};
    for (int i = 0; i < 6; ++i) {
        const R r = menuBtn(i);
        drawButton(r.x, r.y, r.w, r.h, labels[i], false, true);
    }
    drawTextCentered(fmt("Побед: %d из %d забегов.   Победа над боссом: +%d артефакта.", profile_.wins, profile_.runs, kBossArtifacts),
                     kScreenW / 2.f, 662.f, 15, kDim);
    drawTextCentered(fmt("Окно: %dx%d%s", GetScreenWidth(), GetScreenHeight(), settings_.fullscreen ? " (полный экран)" : ""),
                     kScreenW / 2.f, 686.f, 15, kDim);
}

// Справка в главном меню: слева - механики (прокручивается), справа - особенности каждого героя.
void Game::drawGuide() {
    drawTextCentered("МЕХАНИКИ", kScreenW / 2.f, 16.f, 34, kAccent);
    drawTextCentered("Краткая справка: как устроен бой и что умеет каждый герой", kScreenW / 2.f, 62.f, 17, kDim);

    const int top = 92, h = 546;
    const int leftX = 32, leftW = 556;
    const int rightX = 608, rightW = kScreenW - rightX - 32;  // 640
    const int visibleTop = top + 5, visibleH = h - 10;

    DrawRectangle(leftX, top, leftW, h, Color{24, 26, 34, 240});
    DrawRectangleLines(leftX, top, leftW, h, kGridLine);
    DrawRectangle(rightX, top, rightW, h, Color{24, 26, 34, 240});
    DrawRectangleLines(rightX, top, rightW, h, kGridLine);

    // Прокручиваем только левую колонку (механики), чтобы новые пункты не перекрывали фон и ставали читаемыми.
    beginScissor(leftX, visibleTop, leftW, visibleH);
    float y = top + 30.f - guideScroll_;
    const auto section = [&](const char* title, const char* body) {
        drawText(title, leftX + 16.f, y, 17, kGood);
        y += 22.f;
        for (const auto& line : wrap(body, leftW - 32.f, 15.f)) {
            drawText(line, leftX + 16.f, y, 15, kText);
            y += 18.f;
        }
        y += 8.f;
    };
    drawText("КАК УСТРОЕН БОЙ", leftX + 16.f, y, 19, kAccent); y += 22.f;
    section("Ход боя и скорость", "Герой сначала атакует, затем применяет умение. СКР определяет, кто ходит чаще.");
    section("Защита и урон", "ЗАЩ вычитается из удара, но всегда проходит минимум 25% АТК. Руны снимают ЗАЩ для всей команды.");
    section("Провокация", "Воин и Паладин получают в 3 раза больше вражеских атак. Ставь их вперёд, чтобы беречь лекарей.");
    section("Магия и руны", "Маг игнорирует половину ЗАЩ; каждые 3-й удар - арканный взрыв по всем (60% АТК). Сейдмард снимает броню.");
    section("Крит, ярость, вампиризм", "Крит даёт x2 урона (25% у Вора). Берсерк ниже 50% ЗДР бьёт ещё раз и даёт всем 10% вампиризма.");
    section("Лечение", "Жрица лечит самого раненого (до 60% ЗДР), Паладин - немного, Друид - цепью по всем, если кто-то ранен.");
    section("Торговец и кузница", "Живой Бард даёт -15% у торговца. Живой Сейдмард даёт -20% за улучшение предмета в кузнице.");
    section("Торговец", "Появляется у края карты, но если его комната не стыкуется с подземельем — входа нет. Строительство стоейкое.");
    section("Инвентарь", "Иконки снаряжения: оружие, броня, реликвия, зелья. Выбор кликом по ячейке; I/Tab - расширенное окно при бое и привале.");
    section("Кузница", "Улучшает оружие, броню и реликвии на один уровень (до 4-го). Зелья не улучшаются; цена скидывается до -20%, пока жив Сейдмард.");
    section("Расы", "После выбора отряда игроку предлагаются 6 случайных рас (новый набор на каждый забег). Каждый из четырёх героев берёт свою расу (без повторов) и сохраняет её до конца забега.");
    for (int i = 0; i < kRaceCount; ++i) {
        const RaceInfo& ri = raceInfo(static_cast<Race>(i));
        section(ri.name, ri.desc);
    }
    EndScissorMode();
    // Подстраиваем прокрутку: можно листать до конца контента, но не выше начала.
    const float maxScroll = std::max(0.f, y - (top + 30.f));
    guideScroll_ = std::clamp(guideScroll_, 0.f, maxScroll);
    if (maxScroll > 0.f) {
        const int barX = leftX + leftW - 5, barH = visibleH - 4;
        DrawRectangle(barX, visibleTop + 2, 5, barH, Color{30, 30, 34, 255});
        const int knobH = std::max(8, barH / 4);
        const float frac = guideScroll_ / maxScroll;
        const int knobY = visibleTop + 2 + static_cast<int>(frac * (barH - knobH));
        DrawRectangle(barX - 1, knobY, 7, knobH, kAccent);
    }

    // ---- правая колонка: герои
    drawText("ГЕРОИ", rightX + 16.f, top + 10.f, 19, kAccent);
    struct Entry { Role role; const char* name; const char* title; const char* feature; };
    static const Entry entries[11] = {
        {Role::Warrior,   "Воин",     "Танк",           "Провокация: враги выбирают его в 3 раза чаще."},
        {Role::Mage,      "Маг",      "Магия",          "Игнорирует половину ЗАЩ; каждые 3-й удар - взрыв по всем."},
        {Role::Cleric,    "Жрица",    "Лекарь",         "Лечит самого раненого союзника, если тот ниже 60% ЗДР."},
        {Role::Rogue,     "Вор",      "Крит и ловушки", "25% крит x2; ловушки почти не наносят урона."},
        {Role::Archer,    "Лучник",   "Залп",           "Бьёт двух врагов по 80% урона, одну цель - на 100%."},
        {Role::Paladin,   "Паладин",  "Танк-лекарь",    "Танк; после атаки лечит раненого на 2."},
        {Role::Berserker, "Берсерк",  "Ярость",         "Доп. удар ниже 50% ЗДР; аура вампиризма 10% для всех."},
        {Role::Druid,     "Друид",    "Лекарь группы",  "Цепное лечение 6/5/4/3 + половина его ЗАЩ."},
        {Role::Bard,      "Бард",     "Вдохновитель",   "Усиливает сильнейшего союзника; -15% у торговца."},
        {Role::Seidmard,  "Сейдмард", "Руны",           "-2 ЗАЩ с врага (до -6) для всех; -20% в кузнице."},
        {Role::Monk,      "Монах",    "3 стойки",       "Без оружия/брони; Ветер отражает, Молния бьёт цепью, Земля защищает."},
    };
    float ry = top + 30.f;
    for (int i = 0; i < 11; ++i) {
        const Entry& e = entries[i];
        const R row{rightX + 10, static_cast<int>(ry), rightW - 20, 42};
        DrawRectangle(row.x, row.y, row.w, row.h, i % 2 == 0 ? Color{31, 33, 44, 235} : Color{27, 29, 39, 235});
        drawIconBox(heroArt(e.role), "", roleColor(e.role), firstGlyph(e.name),
                    static_cast<float>(row.x + 8), static_cast<float>(row.y + 6), 28.f);
        drawText(e.name, static_cast<float>(row.x + 44), static_cast<float>(row.y + 2), 15, roleColor(e.role));
        drawText(e.title, static_cast<float>(row.x + 44 + static_cast<int>(textWidth(e.name, 15.f)) + 8),
                 static_cast<float>(row.y + 4), 12, kDim);
        drawText(e.feature, static_cast<float>(row.x + 44), static_cast<float>(row.y + 21), 12, kText);
        ry += 45.f;
    }

    drawTextCentered("Esc - назад в меню  (стрелки / колесо - вниз)", kScreenW / 2.f, 644.f, 14, kDim);
    drawButton(kBackBtn.x, kBackBtn.y, kBackBtn.w, kBackBtn.h, "Назад", false, true);
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

    const int total = static_cast<int>(list.size());
    const int rows = (total + 1) / 2;
    const float contentH = 130.f + rows * 58.f + 20.f;
    const float viewTop = 120.f;
    const float viewBottom = 650.f;
    const float maxScroll = std::max(0.f, contentH - (kScreenH - 70.f));
    const float scroll = std::max(0.f, std::min(achScroll_, maxScroll));

    beginScissor(0, static_cast<int>(viewTop), kScreenW, static_cast<int>(viewBottom - viewTop));
    for (int i = 0; i < total; ++i) {
        const Achievement& a = list[i];
        R r = achRow(i);
        r.y -= static_cast<int>(scroll);
        if (r.y + r.h < static_cast<int>(viewTop) - 10 || r.y > static_cast<int>(viewBottom) + 10) continue;
        const bool done = profile_.isUnlocked(a.id);
        DrawRectangle(r.x, r.y, r.w, r.h, done ? Color{30, 44, 36, 255} : kPanel);
        DrawRectangleLines(r.x, r.y, r.w, r.h, done ? kGood : kGridLine);
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
    EndScissorMode();

    if (maxScroll > 0.f) {
        const float barX = kScreenW - 24.f, barY = viewTop, barH = viewBottom - viewTop;
        DrawRectangle(static_cast<int>(barX), static_cast<int>(barY), 6, static_cast<int>(barH), Color{35, 37, 48, 255});
        const float thumbH = std::max(30.f, barH * (barH / (barH + maxScroll)));
        const float thumbY = barY + (barH - thumbH) * (scroll / maxScroll);
        DrawRectangle(static_cast<int>(barX), static_cast<int>(thumbY), 6, static_cast<int>(thumbH), kAccent);
        drawTextCentered("Колесо/стрелки - листать", kScreenW / 2.f, 106.f, 14, kDim);
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
        const bool cur = settings_.width == resolutions_[i].w && settings_.height == resolutions_[i].h;
        drawButton(r.x, r.y, r.w, r.h,
                   fmt("%d x %d%s%s", resolutions_[i].w, resolutions_[i].h,
                       i == 0 ? "  (родное)" : "", cur ? "  (текущее)" : ""), cur, true);
    }
    drawButton(440, 190 + n * 58 + 14, 400, 48, settings_.fullscreen ? "Полный экран: ВКЛ" : "Полный экран: ВЫКЛ", settings_.fullscreen, true);
    drawButton(440, 190 + n * 58 + 84, 400, 48, "Назад", false, true);
    drawTextCentered(fmt("Окно: %dx%d    Рендер: %dx%d",
                         GetScreenWidth(), GetScreenHeight(), settings_.width, settings_.height),
                     kScreenW / 2.f, 628.f, 16, kDim);
    drawTextCentered("Полный экран без смены режима монитора. Разрешение меняет буфер отрисовки. Esc - назад.",
                     kScreenW / 2.f, 654.f, 15, kDim);
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
    if (currentFloor_ == 1) {
        drawText("Выбери четырёх героев (клик или 1-9, 0, -) и босса (Q/W/E). У Монаха - кнопки стойки. Enter - выбрать расы.",
                 kSelX, 60, 16, kDim);
    } else {
        drawText(fmt("ЭТАЖ %d. Выбери следующего босса (Q/W/E). Отряд сохраняется с прошлого этажа.", currentFloor_),
                 kSelX, 60, 16, kAccent);
    }
    drawText(fmt("Выбрано: %d/%d", chosenCount(), kPartySize), 1100, 28, 22,
             chosenCount() == kPartySize ? kGood : kText);

    Vector2 m = mouse();
    const int maxVal[4] = {45, 12, 5, 8};
    const char* labels[4] = {"ЗДР", "АТК", "ЗАЩ", "СКР"};
    const Color barCols[4] = {{110, 210, 130, 255}, {235, 110, 80, 255}, {100, 150, 235, 255}, {240, 210, 90, 255}};

    // Карточки (особенно нижний ряд с Бардом) не должны заезжать на текст
    // и панель выбора босса: обрезаем их по области прокрутки.
    beginScissor(kSelX, kSelY, 4 * (kSelCardW + kSelGap), static_cast<int>(selViewH_));
    for (int i = 0; i < static_cast<int>(roster_.size()) && i < kMaxRoster; ++i) {
        const Hero& h = roster_[i];
        const int x = kSelX + (i % 4) * (kSelCardW + kSelGap);
        const int y = static_cast<int>(kSelY + (i / 4) * (kSelCardH + kSelGap) - selScroll_);

        // ЖЕСТКАЯ ОБРЕЗКА: карточка рисуется только внутри окна
        if (y + kSelCardH < kSelY || y > kSelY + selViewH_) continue;
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

        const auto variants = heroVariants(h.role);
        if (!variants.empty()) {
            // Универсальные кнопки вариантов: сейчас это три стойки Монаха.
            const int current = heroVariant(h);
            for (int v = 0; v < static_cast<int>(variants.size()); ++v) {
                const R r = variantRect(x, y, v, static_cast<int>(variants.size()));
                const bool sel = v == current;
                const bool hov = currentFloor_ == 1 && inRect(m, r.x, r.y, r.w, r.h);
                DrawRectangle(r.x, r.y, r.w, r.h,
                              sel ? Color{46, 58, 50, 255} : hov ? Color{52, 56, 74, 255} : Color{32, 34, 44, 255});
                DrawRectangleLines(r.x, r.y, r.w, r.h, sel ? kGood : hov ? kAccent : kGridLine);
                drawTextCentered(variants[v].label, r.x + r.w / 2.f, r.y + 6.f, 14, sel ? kGood : kText);
            }
            float dy = static_cast<float>(y + 210);
            int descLines = 0;
            for (const auto& l : wrap(variants[current].desc, kSelCardW - 36.f, 12)) {
                if (descLines++ >= 2) break;
                drawText(l, static_cast<float>(x + 20), dy, 12, kDim);
                dy += 14.f;
            }
        } else {
            float ay = static_cast<float>(y + 176);
            int lines = 0;
            for (const auto& l : wrap(roleAbility(h.role), kSelCardW - 36.f, 15)) {
                if (lines++ >= 4) break;
                drawText(l, static_cast<float>(x + 20), ay, 15, kText);
                ay += 18;
            }
        }
        if (ch) drawText("ВЫБРАН", x + kSelCardW - 92.f, y + 40.f, 16, kGood);
        if (currentFloor_ > 1) {
            DrawRectangle(x, y, kSelCardW, kSelCardH, {0, 0, 0, 150});
        }
    }
    EndScissorMode();

    if (!message_.empty()) drawText(message_, kSelX, 640, 20, messageBad_ ? kBad : kGood);
    if (chosenCount() == kPartySize) drawText("Enter - выбрать расы", kSelX, 672, 22, kAccent);
    else drawText(fmt("Нужно выбрать ещё: %d", kPartySize - chosenCount()), kSelX, 672, 20, kDim);

    // три предложенных босса: выбери одного (Q / W / E или клик)
    drawText("Финальный босс забега (Q/W/E, клик; B - подробности):", 520.f, 608.f, 14, kDim);
    const char* keyNames[3] = {"Q", "W", "E"};
    auto isDefeated = [&](int id) {
        return std::find(defeatedBosses_.begin(), defeatedBosses_.end(), id) != defeatedBosses_.end();
    };
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
        if (isDefeated(offered_[i])) {
            DrawRectangle(bx, by, 238, 62, {0, 0, 0, 150}); // затемняем
            drawText("ПРОЙДЕН", bx + 56.f, by + 20.f, 16, kBad);
        }
    }

    if (showBossCard_) {
        DrawRectangle(0, 0, kScreenW, kScreenH, {0, 0, 0, 190});
        drawBossCard(190, 50, 900, 560, bossId_);
        drawTextCentered("B / Enter - закрыть", kScreenW / 2.f, 624.f, 18, kAccent);
    }
    if (selContentH_ > selViewH_) {
        const int trackX = kSelX + 4 * (kSelCardW + kSelGap) - 30;
        const int trackY = kSelY;
        const int trackH = static_cast<int>(selViewH_);
        const int trackW = 16;

        DrawRectangle(trackX, trackY, trackW, trackH, {30, 32, 42, 200});

        float ratio = selViewH_ / selContentH_;
        int thumbH = std::max(30, static_cast<int>(trackH * ratio));

        float scrollRatio = selScroll_ / std::max(1.f, selContentH_ - selViewH_);
        int thumbY = trackY + static_cast<int>((trackH - thumbH) * scrollRatio);

        DrawRectangle(trackX, thumbY, trackW, thumbH, kAccent);
    }
}

// -------------------------------------------------------------- выбор расы

void Game::drawRaceSelect() {
    drawText("ВЫБОР РАСЫ", 190.f, 20.f, 32, kAccent);

    // Список выбранных героев: текущий подсвечивается, выбранные расы видны.
    int hero = 0;
    for (int i = 0; i < kMaxRoster && hero < kPartySize; ++i) {
        if (!chosen_[i]) continue;
        const Hero& h = roster_[i];
        const int x = 190 + hero * 260;
        const bool cur = hero == raceHero_;
        const bool picked = racePick_[hero] >= 0;
        DrawRectangle(x, 78, 248, 56, cur ? Color{46, 58, 50, 255} : kPanel);
        DrawRectangleLines(x, 78, 248, 56, cur ? kGood : picked ? kGridLine : Color{70, 70, 80, 255});
        drawText(fmt("%d. %s", hero + 1, h.name.c_str()), x + 10.f, 84.f, 16, cur ? kGood : kText);
        drawText(roleTitle(h.role), x + 10.f, 106.f, 13, kDim);
        if (picked) {
            const RaceInfo& ri = raceInfo(static_cast<Race>(offeredRaces_[racePick_[hero]]));
            // Прижимаем имя расы к правому краю карточки и при необходимости
            // уменьшаем шрифт, чтобы длинные названия не вылезали за рамку.
            float fs = 14.f;
            while (fs > 10.f && textWidth(ri.name, fs) > 120.f) fs -= 1.f;
            drawText(ri.name, x + 238.f - textWidth(ri.name, fs), 92.f, fs, kGood);
        }
        ++hero;
    }

    drawText(fmt("Герой %d из %d. Выбери расу: 1-6 или клик. Повторная раса недоступна. Backspace - назад.",
                 raceHero_ + 1, kPartySize), 190.f, 134.f, 15, kDim);

    const Vector2 m = mouse();
    for (int k = 0; k < 6; ++k) {
        const R r = raceCard(k);
        const RaceInfo& ri = raceInfo(static_cast<Race>(offeredRaces_[k]));
        bool taken = false;
        for (int h = 0; h < kPartySize; ++h)
            if (h != raceHero_ && racePick_[h] == k) taken = true;
        const bool sel = racePick_[raceHero_] == k;
        const bool hover = inRect(m, r.x, r.y, r.w, r.h);
        DrawRectangle(r.x, r.y, r.w, r.h, sel ? Color{46, 58, 50, 255} : hover ? Color{40, 42, 54, 255} : kPanel);
        DrawRectangleLines(r.x, r.y, r.w, r.h, sel ? kGood : taken ? Color{70, 50, 50, 255} : kGridLine);
        drawText(fmt("[%d] %s", k + 1, ri.name), r.x + 14.f, r.y + 12.f, 20, sel ? kGood : kText);
        float dy = static_cast<float>(r.y + 46);
        for (const auto& line : wrap(ri.desc, r.w - 28.f, 13)) {
            drawText(line, r.x + 14.f, dy, 13, kDim);
            dy += 17.f;
        }
        if (taken) drawText("уже выбрана", r.x + r.w - 120.f, r.y + 12.f, 13, kBad);
    }

    if (!message_.empty()) drawText(message_, 190.f, 626.f, 18, messageBad_ ? kBad : kGood);
    drawButton(kRaceBackBtn.x, kRaceBackBtn.y, kRaceBackBtn.w, kRaceBackBtn.h, "Назад",
               false, true);
    drawButton(kRaceNextBtn.x, kRaceNextBtn.y, kRaceNextBtn.w, kRaceNextBtn.h,
               raceHero_ + 1 == kPartySize ? "Начать поход" : "Следующий герой",
               true, racePick_[raceHero_] >= 0);
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
        else if (s.card.difficulty > 0) {
            // Размер звёзд совпадает со звёздами предметов в инвентаре.
            drawStars(static_cast<float>(x + 92), static_cast<float>(kCardY + 61), s.card.difficulty, 6.5f);
        }
    }

    DrawRectangle(kPanelX - 10, 10, kPanelW + 20, kScreenH - 20, kPanel);
    float y = 20;
    drawText("ПОСТРОЙ ПОДЗЕМЕЛЬЕ", kPanelX, y, 26, kAccent);
    y += 42;
    drawText(fmt("Этаж: %d", currentFloor_), kPanelX, y, 18, kAccent);
    y += 24;
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
    RoomType types[] = {RoomType::Combat, RoomType::Treasure, RoomType::Trap, RoomType::Rest, RoomType::Shop, RoomType::Forge, RoomType::Boss};
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

    const int ty = static_cast<int>(c.y) + size / 2 + 3;
    drawTextCentered(name, c.x, static_cast<float>(ty), 14, alive ? kText : kDim);
    const int bw = std::max(size, 72) + 12;
    drawBar(static_cast<int>(c.x) - bw / 2, ty + 20, bw, 7, maxHp ? static_cast<float>(hp) / maxHp : 0.f,
            alive ? kGood : kDim);
    drawTextCentered(fmt("%d/%d", std::max(0, hp), maxHp), c.x, static_cast<float>(ty + 29), 13, kDim);
}

// Руны Сейдмарда: по одной на каждые 2 снятых ЗАЩ (максимум 3).
// Рисуем сбоку от спрайта, чтобы не перекрывать полоску здоровья и соседних врагов.
void Game::drawDebuffRunes(const Enemy& e, Vector2 c, int size) const {
    if (!e.alive() || e.defShred <= 0) return;
    const int stacks = std::min(3, (e.defShred + 1) / 2);
    const Color rune = roleColor(Role::Seidmard);
    const float r = 9.f;
    const float step = r * 2.f + 5.f;
    const float x0 = c.x + size / 2.f + 16.f + r;
    const float cy = c.y - 8.f;
    for (int i = 0; i < stacks; ++i) {
        const float x = x0 + i * step;
        const Vector2 top{x, cy - r}, right{x + r * 0.8f, cy}, bottom{x, cy + r}, left{x - r * 0.8f, cy};
        DrawTriangle(top, right, bottom, rune);
        DrawTriangle(top, bottom, left, rune);
        DrawLineV(top, bottom, Color{235, 215, 255, 255});
        drawTextCentered(fmt("%d", (i + 1) * 2), x, cy - 7.f, 12, {25, 18, 40, 255});
    }
    drawTextCentered(fmt("-%d ЗАЩ", e.defShred), x0 + (stacks - 1) * step / 2.f, cy + 13.f, 12, rune);
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
        if (!e.alive()) {
            bool taken = false;
            for (const auto& o : enemies)
                if (o.alive() && o.slot == e.slot) taken = true;
            if (taken) continue;
        }
        drawSprite(enemyCenter(e.slot, slots), e.size, enemyColor(e), e.name, e.hp, e.maxHp, enemyFlash_[i],
                   lungeOffset(false, i), "", e.art, e.bossId >= 0 ? bossArt(e.bossId) : "");
        drawDebuffRunes(e, enemyCenter(e.slot, slots), e.size);
    }

    for (const Proj& p : projs_) {
        if (p.t < 0.f) continue;
        float u = std::min(1.f, p.t / kProjDur);
        float x = p.from.x + (p.to.x - p.from.x) * u;
        float y = p.from.y + (p.to.y - p.from.y) * u;
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 11.f, Fade(p.col, 0.35f));
        DrawCircle(static_cast<int>(x), static_cast<int>(y), 6.f, p.col);
    }

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
        const bool canRevive = anyDead && (run_->canRestReviveFree() || run_->party().coins >= run_->reviveCoinCost() ||
                                          run_->artifacts() >= run_->reviveArtifactCost());

        const int bw = 224, bh = 220, by = kGridY + 130;
        const char* titles[3] = {"[1] Лечение", "[2] Усиление", "[3] Воскрешение"};
        const std::string descs[3] = {
            fmt("Все живые герои восстанавливают %d%% максимального здоровья.", run_->restHealPct()),
            fmt("Выбери двух героев: +%d АТК и +%d ЗАЩ до конца похода. Монах тренируется усерднее: +%d/%d.",
                Run::kBuffAtk, Run::kBuffDef, Run::kMonkBuffAtk, Run::kMonkBuffDef),
            fmt("Бесплатный шанс: %d%%. Гарантия: %d монет или %d артефакта. Можно повторять.",
                run_->reviveChancePct(), run_->reviveCoinCost(), run_->reviveArtifactCost())};
        const Color titleCols[3] = {kHeal, kAccent, kCoin};
        for (int k = 0; k < 3; ++k) {
            const int bx = kGridX + 14 + k * 236;
            const bool enabled = k < 2 || canRevive;
            const bool hover = inRect(m, bx, by, bw, bh) && enabled;   // ← hover только для активных
            DrawRectangle(bx, by, bw, bh, !enabled ? Color{26, 27, 34, 255} : hover ? Color{40, 48, 56, 255} : kPanel);
            DrawRectangleLines(bx, by, bw, bh, !enabled ? kGridLine : hover ? kAccent : kGridLine);
            // описание тоже приглушить:
            float ty = static_cast<float>(by + 56);
            for (const auto& l : wrap(descs[k], bw - 28.f, 16)) {
                drawText(l, static_cast<float>(bx + 14), ty, 16, enabled ? kText : kDim);
                ty += 21;
            }
            if (k == 2) {
                if (!anyDead) drawText("Павших героев нет", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kDim);
                else if (!canRevive) drawText("Нет попытки, монет и артефактов", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kBad);
                else if (!run_->canRestReviveFree()) drawText("Только за плату", static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kCoin);
                else drawText(fmt("Бесплатно: шанс %d%%", run_->reviveChancePct()), static_cast<float>(bx + 14), static_cast<float>(by + bh - 28), 15, kGood);
            }
        }
        if (!restMsg_.empty()) drawText(restMsg_, kGridX + 16.f, kGridY + 358.f, 16, kAccent);

        
        drawText("Состояние отряда", static_cast<float>(kGridX + 30), static_cast<float>(kGridY + 380), 16, kAccent);
        for (int i = 0; i < static_cast<int>(hs.size()) && i < 4; ++i) {
            const Hero& h = hs[i];
            const int hx = kGridX + 30 + i * 172;
            drawText(h.name, static_cast<float>(hx), static_cast<float>(kGridY + 406), 16, h.alive() ? kText : kDim);
            drawBar(hx, kGridY + 430, 150, 9, h.maxHp ? static_cast<float>(h.hp) / h.maxHp : 0.f, kGood);
            drawText(fmt("%d/%d", std::max(0, h.hp), h.maxHp), static_cast<float>(hx), static_cast<float>(kGridY + 444), 14, kDim);
        }
        return;
    }

    if (restStage_ == 2) {
        drawTextCentered("ВОСКРЕШЕНИЕ", cx, kGridY + 22.f, 34, kCoin);
        drawTextCentered("Выбери павшего героя", cx, kGridY + 64.f, 18, kDim);
        drawTextCentered(fmt("Бесплатно: шанс %d%%  |  Гарантия: %d мон. или %d арт.  |  ЗДР: %d / %d / %d%%",
                             run_->reviveChancePct(), run_->reviveCoinCost(), run_->reviveArtifactCost(),
                             Run::kFreeRevivePct, Run::kRevivePct, Run::kReviveArtifactPct),
                         cx, kGridY + 88.f, 14, kDim);
        drawCoin(kGridX + 560.f, kGridY + 30.f, 10.f);
        drawText(fmt("%d", run_->party().coins), kGridX + 576.f, kGridY + 18.f, 20, kCoin);
        drawArtifact(kGridX + 640.f, kGridY + 30.f, 10.f);
        drawText(fmt("%d", run_->artifacts()), kGridX + 656.f, kGridY + 18.f, 20, Color{190, 150, 255, 255});

        std::vector<int> dead;
        for (int i = 0; i < static_cast<int>(hs.size()) && i < kMaxRoster; ++i)
            if (!hs[i].alive()) dead.push_back(i);
        if (dead.empty()) {
            drawTextCentered("Павших героев нет.", cx, kGridY + 180.f, 22, kDim);
        }
        for (int k = 0; k < static_cast<int>(dead.size()); ++k) {
            const int i = dead[k];
            const Hero& h = hs[i];
            const int rx = kGridX + 40, ry = kGridY + 112 + k * 84;
            DrawRectangle(rx, ry, 640, 72, kPanel);
            DrawRectangleLines(rx, ry, 640, 72, kGridLine);
            drawIconBox(heroArt(h.role), "", Color{70, 70, 78, 255}, firstGlyph(h.name),
                        rx + 12.f, ry + 12.f, 48.f, Color{150, 150, 160, 255});
            drawText(fmt("%s  (пал)", h.name.c_str()), static_cast<float>(rx + 74), static_cast<float>(ry + 10), 22, kDim);
            drawText(fmt("ЗДР: %d%% / %d%% / %d%%", Run::kFreeRevivePct, Run::kRevivePct, Run::kReviveArtifactPct),
                     static_cast<float>(rx + 74), static_cast<float>(ry + 40), 15, kDim);

            const bool canFree = run_->canRestReviveFree();
            const bool canCoin = run_->party().coins >= run_->reviveCoinCost();
            const bool canArt = run_->artifacts() >= run_->reviveArtifactCost();
            R freeBtn{rx + 302, ry + 14, 106, 44};
            R coinBtn{rx + 416, ry + 14, 106, 44};
            R artBtn{rx + 530, ry + 14, 106, 44};
            drawButton(freeBtn.x, freeBtn.y, freeBtn.w, freeBtn.h, fmt("%d%% шанс", run_->reviveChancePct()), false, canFree);
            drawButton(coinBtn.x, coinBtn.y, coinBtn.w, coinBtn.h, fmt("%d мон.", run_->reviveCoinCost()), false, canCoin);
            drawButton(artBtn.x, artBtn.y, artBtn.w, artBtn.h, fmt("%d арт.", run_->reviveArtifactCost()), false, canArt);
        }
        if (!run_->canRestReviveFree())
            drawTextCentered("Бесплатная попытка в этом привале уже использована", cx, kGridY + kArenaH - 84.f, 14, kDim);
        if (!restMsg_.empty()) drawTextCentered(restMsg_, cx, kGridY + kArenaH - 62.f, 16, kAccent);
        drawTextCentered("Клик по кнопке    B - назад", cx, kGridY + kArenaH - 36.f, 18, kAccent);
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
        drawText(fmt("АТК %d -> %d    ЗАЩ %d -> %d    ЗДР %d/%d", h.atk, h.atk + restAtkBonus(h), h.def,
                     h.def + restDefBonus(h), h.hp, h.maxHp),
                 static_cast<float>(rx + 74), static_cast<float>(ry + 40), 16, restPick_[i] ? kGood : kDim);
    }
    drawTextCentered("Enter - подтвердить    B - назад", cx, kGridY + kArenaH - 36.f, 18, picked == req ? kAccent : kDim);
}

void Game::drawInventory() {
    const Party& p = run_->party();
    const Vector2 m = mouse();
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, tex("backgrounds/arena.png") ? Color{12, 13, 18, 170} : Color{22, 24, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, kAccent);
    drawText("ИНВЕНТАРЬ", kGridX + 16.f, kGridY + 10.f, 26, kAccent);
    drawText("[I] закрыть", kGridX + 210.f, kGridY + 16.f, 15, kDim);
    drawText(fmt("Рюкзак (%d/%d)", static_cast<int>(p.bag.size()), kBagCells), kGridX + 12.f, kGridY + 54.f, 17, kText);

    const Item* selItem = (invSel_ >= 0 && invSel_ < static_cast<int>(p.bag.size())) ? &p.bag[invSel_] : nullptr;
    const Item* hoverItem = nullptr;

    for (int i = 0; i < kBagCells; ++i) {
        const R r = bagCell(i);
        const Item* it = i < static_cast<int>(p.bag.size()) ? &p.bag[i] : nullptr;
        const bool hover = inR(m, r);
        if (hover && it) hoverItem = it;
        drawBagCell(r.x, r.y, r.w, it, it && i == invSel_, hover && it);
    }

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
            const bool locked = slotLocked(hero, kSlotKinds[k]);
            const bool fits = selItem && selItem->kind == kSlotKinds[k] && hero.alive() && !locked;
            const bool hover = inR(m, sr);
            if (hover && item) hoverItem = &*item;
            drawEquipSlot(sr.x, sr.y, sr.w, kSlotKinds[k], item, fits, hover, locked);
            if (item) drawText(itemBonusText(*item), pr.x + 184.f, by, 12, tierColor(item->tier));
            else drawText("-", pr.x + 184.f, by, 12, kDim);
            by += 15.f;
        }
    }

    float ty = kGridY + 492.f;
    if (selItem) {
        for (const auto& l : wrap(fmt("%s - %s ур.%d: %s", selItem->name.c_str(), itemKindName(selItem->kind), selItem->tier,
                                      itemBonusText(*selItem).c_str()), 335.f, 14)) {
            drawText(l, kGridX + 12.f, ty, 14, kAccent);
            ty += 17;
        }
    }
    if (run_->inShopRoom()) {
        const R sb = sellButton();
        const bool canSell = selItem != nullptr;
        DrawRectangle(sb.x, sb.y, sb.w, sb.h, kPanel);
        DrawRectangleLines(sb.x, sb.y, sb.w, sb.h, canSell ? kCoin : kGridLine);
        drawTextCentered(selItem ? fmt("Продать: +%d мон. (S)", itemSellPrice(*selItem))
                                 : "Продать (выбери вещь)",
                         sb.x + sb.w / 2.f, sb.y + 9.f, 15, canSell ? kCoin : kDim);
    }
    if (!invMsg_.empty()) drawText(invMsg_, kGridX + 12.f, kGridY + 526.f, 14, kGood);
    drawText(run_->inShopRoom()
                 ? "Ячейка -> слот героя: надеть. Надетое: клик - снять. S - продать. ПКМ - отмена."
                 : "Ячейка -> слот героя: надеть. Надетое: клик - снять. ПКМ - отмена.",
             kGridX + 12.f, kGridY + 542.f, 11, kDim);

    if (hoverItem) drawItemTooltip(*hoverItem, m);
}

void Game::drawBossReward() {
    const Vector2 m = mouse();
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, tex("backgrounds/arena.png") ? Color{10, 12, 18, 180} : Color{20, 22, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, kAccent);
    drawText("НАГРАДА ЗА БОССА", kGridX + 20.f, kGridY + 12.f, 32, kAccent);
    drawText("Выбери: 1 предмет 4 тира из 3 или +3 артефакта.", kGridX + 22.f, kGridY + 56.f, 18, kDim);

    const auto& opts = run_->bossRewardOptions();
    const int baseY = kGridY + 110;
    if (!run_->bossRewardClaimed()) {
        for (int i = 0; i < static_cast<int>(opts.size()) && i < 3; ++i) {
            const Item& it = opts[i];
            const R r{kGridX + 22 + i * 228, baseY, 214, 240};
            const bool hover = inR(m, r);
            DrawRectangle(r.x, r.y, r.w, r.h, hover ? Color{46, 52, 66, 255} : kPanel);
            DrawRectangleLines(r.x, r.y, r.w, r.h, hover ? kAccent : tierColor(it.tier));
            drawBagCell(r.x + 75, r.y + 18, 64, &it, false, hover);
            drawTextCentered(it.name, r.x + r.w / 2.f, r.y + 96.f, 18, kText);
            drawTextCentered(itemKindName(it.kind), r.x + r.w / 2.f, r.y + 122.f, 15, kDim);
            for (const auto& line : wrap(itemBonusText(it), r.w - 20.f, 14)) {
                drawTextCentered(line, r.x + r.w / 2.f, r.y + 150.f, 14, kGood);
                break;
            }
            drawTextCentered("Выбрать", r.x + r.w / 2.f, r.y + 210.f, 18, hover ? kAccent : kText);
        }
        const R art{kGridX + 22, baseY + 260, 680, 54};
        const bool hoverA = inR(m, art);
        DrawRectangle(art.x, art.y, art.w, art.h, hoverA ? Color{60, 46, 78, 255} : kPanel);
        DrawRectangleLines(art.x, art.y, art.w, art.h, hoverA ? Color{190,150,255,255} : kGridLine);
        drawArtifact(art.x + 28.f, art.y + 27.f, 10.f);
        drawText("+3 артефакта", art.x + 50.f, art.y + 16.f, 24, Color{190,150,255,255});
        drawTextCentered("I - инвентарь, чтобы сразу надеть предмет", kGridX + kArenaW / 2.f, kGridY + kArenaH - 34.f, 16, kDim);
        return;
    }

    drawTextCentered("Награда получена. Что дальше?", kGridX + kArenaW / 2.f, kGridY + 150.f, 24, kGood);
    const R cont{kGridX + 40, kGridY + 240, 300, 52};
    const R end{kGridX + 380, kGridY + 240, 300, 52};
    const bool hc = inR(m, cont), he = inR(m, end);
    DrawRectangle(cont.x, cont.y, cont.w, cont.h, hc ? Color{46,52,66,255} : kPanel);
    DrawRectangle(end.x, end.y, end.w, end.h, he ? Color{60,40,40,255} : kPanel);
    DrawRectangleLines(cont.x, cont.y, cont.w, cont.h, hc ? kAccent : kGridLine);
    DrawRectangleLines(end.x, end.y, end.w, end.h, he ? kBad : kGridLine);
    drawTextCentered("Следующий этаж", cont.x + cont.w / 2.f, cont.y + 14.f, 24, kText);
    drawTextCentered("Закончить забег", end.x + end.w / 2.f, end.y + 14.f, 24, kText);
    drawTextCentered("После выбора предмет можно надеть через I", kGridX + kArenaW / 2.f, kGridY + kArenaH - 34.f, 16, kDim);
}

void Game::drawForge() {
    const Vector2 m = mouse();
    const Color bronze = roomColor(RoomType::Forge);
    auto& bag = run_->partyMutable().bag;
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH,
                  tex("backgrounds/arena.png") ? Color{18, 12, 8, 170} : Color{28, 22, 18, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, bronze);

    drawText("КУЗНИЦА", kGridX + 20.f, kGridY + 12.f, 34, bronze);
    drawText("Выбери вещь слева - результат и цена справа.", kGridX + 22.f, kGridY + 56.f, 15, kDim);
    drawCoin(kGridX + 556.f, kGridY + 30.f, 11.f);
    drawText(fmt("Монеты: %d", run_->party().coins), kGridX + 574.f, kGridY + 18.f, 22, kCoin);
    if (run_->hasLivingSeidmard())
        drawText("Сейдмард: скидка 20% на улучшение.", kGridX + 430.f, kGridY + 66.f, 14, roleColor(Role::Seidmard));

    // Уникальное оружие из событий можно очистить от отрицательных свойств (E).
    bool hasEventPenalty = false;
    for (const Item& it : bag) hasEventPenalty |= it.eventPenaltyAtk != 0 || it.eventPenaltyDef != 0;
    for (const Hero& h : run_->party().heroes)
        for (const auto* slot : {&h.weapon, &h.armor})
            if (*slot) hasEventPenalty |= (*slot)->eventPenaltyAtk != 0 || (*slot)->eventPenaltyDef != 0;
    if (hasEventPenalty)
        drawTextCentered("E - снять отрицательные свойства (-5 монет)", kScreenW / 2.f, 76.f, 15, kCoin);

    // ---- слева: рюкзак. Цена прямо на ячейке, чтобы не нужно было наводить курсор.
    drawText(fmt("РЮКЗАК (%d/%d)", static_cast<int>(bag.size()), kBagCells),
             kGridX + 12.f, kGridY + 78.f, 16, kText);
    const Item* hoverItem = nullptr;
    for (int i = 0; i < kBagCells; ++i) {
        const R r = forgeCell(i);
        const Item* it = i < static_cast<int>(bag.size()) ? &bag[i] : nullptr;
        const bool hover = it && inR(m, r);
        drawBagCell(r.x, r.y, r.w, it, it && i == invSel_, hover);
        if (hover) hoverItem = it;
        if (!it) continue;
        const bool canUp = it->tier < 4 && it->kind != ItemKind::Potion;
        DrawRectangle(r.x + r.w - 44, r.y, 43, 20, {0, 0, 0, 205});
        if (canUp) {
            const int price = run_->forgePrice(*it);
            drawCoin(r.x + r.w - 37.f, r.y + 10.f, 5.f);
            drawText(fmt("%d", price), static_cast<float>(r.x + r.w - 29), static_cast<float>(r.y + 2), 15,
                     run_->party().coins >= price ? kCoin : kBad);
        } else {
            drawTextCentered(it->tier >= 4 ? "MAX" : "-", r.x + r.w - 22.5f, r.y + 2.f, 14, kDim);
        }
    }

    // ---- справа: понятное сравнение "было -> станет" и кнопка подтверждения
    const int px = kGridX + 372, py = kGridY + 90, pw = 336, ph = 420;
    DrawRectangle(px, py, pw, ph, {30, 25, 22, 240});
    DrawRectangleLines(px, py, pw, ph, bronze);
    drawText("ЧТО ПОЛУЧИШЬ", px + 16.f, py + 12.f, 18, kAccent);

    if (invSel_ < 0 || invSel_ >= static_cast<int>(bag.size())) {
        const float cx = px + pw / 2.f;
        drawTextCentered("Выбери вещь в рюкзаке", cx, py + 84.f, 19, kText);
        drawTextCentered("здесь появится сравнение", cx, py + 112.f, 15, kDim);
        drawTextCentered("и точная цена улучшения.", cx, py + 134.f, 15, kDim);
        drawTextCentered("<<<", cx, py + 196.f, 32, bronze);
    } else {
        const Item& cur = bag[invSel_];
        const bool canUp = cur.tier < 4 && cur.kind != ItemKind::Potion;
        const int price = canUp ? run_->forgePrice(cur) : 0;
        const int fullPrice = canUp ? itemPrice(upgradeItem(cur)) * 60 / 100 : 0;
        const bool discounted = canUp && price < fullPrice;
        const bool afford = canUp && run_->party().coins >= price;

        drawText("СЕЙЧАС", px + 16.f, py + 44.f, 14, kDim);
        drawItemIcon(cur, px + 16.f, py + 66.f, 64.f);
        drawText(cur.name, px + 92.f, py + 68.f, 18, kText);
        drawStars(px + 92.f, py + 94.f, cur.tier, 5.f);
        drawText(itemBonusText(cur), px + 92.f, py + 116.f, 15, kText);

        // стрелка вниз: переводит взгляд от текущей вещи к результату
        const float ax = px + pw / 2.f;
        DrawLineV({ax - 24.f, py + 152.f}, {ax + 24.f, py + 152.f}, kAccent);
        DrawTriangle({ax + 31.f, py + 152.f}, {ax + 19.f, py + 146.f}, {ax + 19.f, py + 158.f}, kAccent);

        drawText("СТАНЕТ", px + 16.f, py + 180.f, 14, kDim);
        if (canUp) {
            const Item up = upgradeItem(cur);
            drawItemIcon(up, px + 16.f, py + 202.f, 64.f);
            drawText(up.name, px + 92.f, py + 204.f, 18, kGood);
            drawStars(px + 92.f, py + 230.f, up.tier, 5.f);
            drawText(itemBonusText(up), px + 92.f, py + 252.f, 15, kGood);
        } else {
            drawTextCentered(cur.tier >= 4 ? "Уже максимальный уровень" : "Зелье не улучшается",
                             ax, py + 216.f, 17, kBad);
        }

        if (canUp) {
            if (discounted) {
                // Старая цена - приглушённая, с тонкой линией ровно по середине цифр.
                const std::string oldPrice = fmt("%d", fullPrice);
                const float oldX = px + 16.f, oldY = py + 282.f;
                drawText("БЫЛО", oldX, oldY - 17.f, 12, kDim);
                drawText(oldPrice, oldX, oldY, 20, Color{150, 150, 158, 255});
                const float oldW = textWidth(oldPrice, 20.f);
                DrawLineV({oldX - 2.f, oldY + 11.f}, {oldX + oldW + 2.f, oldY + 11.f},
                          Color{190, 110, 110, 230});
                // Стрелку рисуем линиями: в шрифте нет символа "->" (он показывался как "?").
                const float ay = py + 300.f;
                DrawLineV({px + 58.f, ay}, {px + 86.f, ay}, bronze);
                DrawTriangle({px + 92.f, ay}, {px + 82.f, ay - 5.f}, {px + 82.f, ay + 5.f}, bronze);
                drawCoin(px + 108.f, py + 298.f, 6.f);
                drawText(fmt("%d", price), px + 118.f, py + 280.f, 30, kCoin);

                const std::string disc = "-20% Сейдмард";
                const float dw = textWidth(disc, 14.f);
                DrawRectangle(px + 196, py + 302, static_cast<int>(dw) + 16, 24, Color{45, 30, 55, 255});
                DrawRectangleLines(px + 196, py + 302, static_cast<int>(dw) + 16, 24, roleColor(Role::Seidmard));
                drawText(disc, px + 204.f, py + 307.f, 14, roleColor(Role::Seidmard));
            } else {
                drawCoin(px + 20.f, py + 298.f, 6.f);
                drawText(fmt("Цена: %d монет", price), px + 38.f, py + 282.f, 24, kCoin);
            }
            if (!afford)
                drawText(fmt("Не хватает %d мон.", price - run_->party().coins), px + 38.f, py + 316.f, 15, kBad);
        }

        const R b = forgeButton();
        const bool hoverBtn = inR(m, b);
        DrawRectangle(b.x, b.y, b.w, b.h,
                      !canUp || !afford ? Color{42, 40, 44, 255} : hoverBtn ? Color{72, 58, 34, 255} : Color{58, 46, 32, 255});
        DrawRectangleLines(b.x, b.y, b.w, b.h, canUp && afford ? (hoverBtn ? kAccent : bronze) : kGridLine);
        const char* label = !canUp ? (cur.tier >= 4 ? "МАКСИМАЛЬНЫЙ УРОВЕНЬ" : "ЗЕЛЬЯ НЕ УЛУЧШАЮТСЯ")
                           : !afford ? "НЕ ХВАТАЕТ МОНЕТ" : "УЛУЧШИТЬ";
        drawTextCentered(label, b.x + b.w / 2.f, b.y + 10.f, 19, canUp && afford ? kText : kDim);
        if (canUp && afford)
            drawTextCentered(fmt("за %d монет", price), b.x + b.w / 2.f, b.y + 30.f, 15, kCoin);
    }

    if (!forgeMsg_.empty()) {
        const Color col = forgeMsg_.rfind("Улучшено", 0) == 0 ? kGood
                        : forgeMsg_.rfind("Не хватает", 0) == 0 ? kBad : kAccent;
        drawText(forgeMsg_, px + 16.f, py + 400.f, 13, col);
    }
    drawTextCentered("Клик по вещи - выбрать   кнопка/Enter - улучшить   Esc - уйти   I - инвентарь",
                     kGridX + kArenaW / 2.f, kGridY + kArenaH - 34.f, 14, kDim);

    // Подсказка рисуется последней - иначе ячейки ниже перекрывают её.
    if (hoverItem) drawItemTooltip(*hoverItem, m);
}

void Game::drawShop() {
    const auto* stock = run_->shopStock();
    const Vector2 m = mouse();
    const Color teal = roomColor(RoomType::Shop);
    drawBackground("backgrounds/arena.png", kGridX, kGridY, kArenaW, kArenaH);
    DrawRectangle(kGridX, kGridY, kArenaW, kArenaH, tex("backgrounds/arena.png") ? Color{12, 16, 18, 170} : Color{22, 27, 30, 255});
    DrawRectangleLines(kGridX, kGridY, kArenaW, kArenaH, teal);
    drawText("ТОРГОВЕЦ", kGridX + 20.f, kGridY + 12.f, 34, teal);
    const R rb = restockButton();
    const bool canRestock = run_->party().coins >= Run::kShopRestockCost;
    DrawRectangle(rb.x, rb.y, rb.w, rb.h, kPanel);
    DrawRectangleLines(rb.x, rb.y, rb.w, rb.h, canRestock ? teal : kGridLine);
    drawTextCentered(fmt("Обновить: %d монет", Run::kShopRestockCost),
                     rb.x + rb.w / 2.f, rb.y + 10.f, 15, canRestock ? teal : kDim);
    drawCoin(kGridX + 590.f, kGridY + 30.f, 11.f);
    drawText(fmt("Монеты: %d", run_->party().coins), kGridX + 608.f, kGridY + 18.f, 20, kCoin);
    drawText("Товары: т1, т2 и редко т3. Клик по лоту - купить. В инвентаре (I) можно продать вещь за полцены.",
             kGridX + 22.f, kGridY + 58.f, 14, kDim);

    const Item* hoverItem = nullptr;
    if (stock) {
        for (int i = 0; i < static_cast<int>(stock->size()); ++i) {
            const ShopOffer& o = (*stock)[i];
            const R r{kGridX + 20, kGridY + 88 + i * 76, 680, 68};
            
            const int finalPrice = run_->effectivePrice(o);
            const bool afford = run_->party().coins >= finalPrice;   // ← по итоговой цене!     
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
                

                if (run_->hasLivingBard()) {
                    drawText(fmt("%d", o.price), r.x + r.w - 100.f, r.y + 20.f, 16, kDim);
                    DrawLine(r.x + r.w - 100.f, r.y + 33.f, r.x + r.w - 60.f, r.y + 33.f, kBad);
                    drawText(fmt("%d", finalPrice), r.x + r.w - 56.f, r.y + 20.f, 26, afford ? kGood : kBad);
                } else {
                    drawText(fmt("%d", finalPrice), r.x + r.w - 100.f, r.y + 20.f, 26, afford ? kGood : kBad);
                }
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

    DrawCircle(static_cast<int>(markerPos_.x), static_cast<int>(markerPos_.y), 9, kAccent);
    if (exploring) {
        const Vec2i p = r.partyPos();
        const char* keyNames[4] = {"W", "D", "S", "A"};
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

    drawText(fmt("Рюкзак: %d предм.   Зелий: %d   Монеты: %d   [I] - инвентарь", static_cast<int>(r.party().bag.size()),
                 r.party().potionCount(), r.party().coins), kGridX, kCardY - 14.f, 16, r.hasNewLoot() ? kAccent : kDim);

    // Постоянная подсказка по клавишам: W/A/S/D шаг, I инвентарь, T торговать, G кузница, Esc меню.
    {
        std::string keys = "WASD - шаг; I - инвентарь; Esc - меню";
        if (exploring && r.inShopRoom()) keys += "; T - торговать";
        if (exploring && r.inForgeRoom()) keys += "; G - кузница";
        drawTextCentered(keys, kScreenW / 2.f, kCardY - 34.f, 15, kDim);
    }
    if (exploring && r.dungeon().hasShop() && !r.dungeon().shopConnected())
        drawTextCentered("Торговец стоит, но не подключён: стыкуете комнаты (клетка у края) и вход.", kScreenW / 2.f, kCardY - 46.f, 15, kBad);
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

    if (r.combat()) drawArena();
    if (r.phase() == RunPhase::RestChoice) drawRestOverlay();
    else if (r.phase() == RunPhase::EventChoice) drawEventChoice();
    else if (invOpen_) drawInventory();
    else if (r.phase() == RunPhase::BossIntro) drawBossIntro();
    else if (r.phase() == RunPhase::Shop) drawShop();
    else if (r.phase() == RunPhase::ForgeChoice) drawForge();
    else if (r.phase() == RunPhase::BossReward) drawBossReward();

    DrawRectangle(kPanelX - 10, 10, kPanelW + 20, kScreenH - 20, kPanel);
    float y = 20;
    drawText("ПОХОД ОТРЯДА", kPanelX, y, 26, kAccent);
    if (r.phase() == RunPhase::Fighting && paused_) drawText("ПАУЗА", kPanelX + 290.f, y + 6.f, 16, kDim);
    y += 40;

    for (const Hero& h : r.party().heroes) {
        drawText(h.name, kPanelX, y, 17, h.alive() ? kText : kDim);
        drawText(fmt("АТК %d  ЗАЩ %d  СКР %d  ЗДР %d/%d", h.atk, h.def, h.spd, std::max(0, h.hp), h.maxHp), kPanelX + 100.f,
                 y + 1.f, 15, kDim);
        drawBar(kPanelX, static_cast<int>(y) + 22, 300, 8, h.maxHp ? static_cast<float>(h.hp) / h.maxHp : 0.f, kGood);
        y += 40;
    }
    // Скорость боя показывается всегда: клавиши ↑/↓ меняют её вне боя.
    if (r.phase() == RunPhase::Fighting || speedIdx_ != 0)
        drawText(fmt("Бой x%.1f  (↑/↓)", kSpeeds[speedIdx_]), kPanelX + 290.f, 30.f, 16, kDim);
    y += 2;
    drawCoin(kPanelX + 8.f, y + 11.f, 8.f);
    drawText(fmt("Монеты: %d", r.party().coins), kPanelX + 22.f, y + 1.f, 18, kCoin);
    drawArtifact(kPanelX + 190.f, y + 11.f, 8.f);
    drawText(fmt("Артефакты: %d", r.artifacts()), kPanelX + 204.f, y + 1.f, 18, Color{190, 150, 255, 255});
    y += 30;

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
                  : r.phase() == RunPhase::EventChoice ? "Выбери вариант события цифрой или кликом"
                  : r.phase() == RunPhase::ForgeChoice ? "Выбери вещь - улучшить   Enter - подтвердить   Esc - уйти   I - инвентарь"
                  : r.phase() == RunPhase::BossReward ? "Выбери награду, затем следующий этаж или конец забега   I - инвентарь"
                  : "WASD - идти   I - инвентарь   T - торговец   G - кузница   Esc - меню";
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
    } else if (run_ && !lastReward_.newAchievements.empty()) {
        const Color art{190, 150, 255, 255};
        const auto& all = achievements();
        for (int id : lastReward_.newAchievements)
            for (const auto& a : all)
                if (a.id == id) reward.push_back({fmt("Достижение: %s (+%d)", a.name.c_str(), a.reward), kAccent});
        reward.push_back({fmt("Всего артефактов: %d", profile_.artifacts), art});
    }

    const int shown = static_cast<int>(std::min<size_t>(reward.size(), 9));
    const int w = 620, h = 330 + (!reward.empty() ? 22 + shown * 26 : 0);
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
        if (!resultWin_ && lastReward_.bossArtifacts == 0 && lastReward_.newAchievements.empty())
            drawTextCentered("Артефакты дают победы над боссами и достижения.", kScreenW / 2.f, ty, 16, kDim);
    } else {
        for (const auto& l : wrap(resultText_, w - 40.f, 20)) {
            drawTextCentered(l, kScreenW / 2.f, ty, 20, kText);
            ty += 26;
        }
    }
    drawTextCentered("N - новая игра (выбор героев)     Esc - меню", kScreenW / 2.f, y + h - 40.f, 18, kAccent);
}

}  // namespace dc