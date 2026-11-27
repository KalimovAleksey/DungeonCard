#pragma once
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include <raylib.h>
#include "core/Assets.h"
#include "core/Boss.h"
#include "core/Card.h"
#include "core/Combat.h"
#include "core/Dungeon.h"
#include "core/Meta.h"
#include "core/Party.h"
#include "core/Profile.h"
#include "core/Rng.h"
#include "core/Run.h"

namespace dc {

// Экраны: MainMenu -> Select (отряд + босс из трёх) -> Build (строим подземелье) -> Run (поход) -> Result.
// Pause (Esc) и Settings (разрешение, полный экран) доступны из меню и во время игры.
class Game {
public:
    Game();
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    void run();

private:
    enum class Screen { MainMenu, Select, Build, Run, Result, Pause, Settings, Achievements, Legacy, Guide };
    struct Settings {
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool customResolution = false;  // пользователь сам выбрал размер окна
    };
    struct Resolution {
        int w, h;
    };
    struct Slot {
        bool has = false;
        Card card;
    };

    // --- анимация боя ---
    struct Lunge {            // рывок атакующего к цели
        bool active = false;
        bool hero = true;
        int idx = 0;
        Vector2 dir{0, 0};
        float t = 0;
    };
    struct Proj {             // снаряд (магия, стрела, лечение)
        Vector2 from{0, 0}, to{0, 0};
        float t = 0;
        Color col{255, 255, 255, 255};
    };
    struct Floater {          // всплывающий текст: красное - урон, зелёное - лечение
        Vector2 pos{0, 0};
        std::string text;
        Color col{255, 255, 255, 255};
        float size = 22;
        float age = 0;
        float delay = 0;
        bool hero = false;
        int idx = 0;
        bool heal = false;
        bool fired = false;
    };
    struct Flash {
        float t = 0;
        bool heal = false;
    };

    static constexpr int kMaxRoster = 11;
    static constexpr int kPartySize = 4;
    static constexpr int kHand = 4;
    static constexpr int kMaxPlacements = 12;
    static constexpr int kMaxDiscards = 4;

    // состояние партии
    void rollRun();                 // новый забег: seed и три предложенных босса
    void resetForNewRun(uint32_t seed = 0);
    void nextFloor();
    void newGame(uint32_t seed);
    void refill(Slot& s);
    void startRun();
    void failBuild(const std::string& why);
    void afterBuildAction();
    bool hasAnyMove() const;
    void setMessage(const std::string& m, bool bad);
    int chosenCount() const;

    // профиль: артефакты, достижения, Наследие (хранится в profile.sav)
    void loadProfile();
    void saveProfile() const;
    void refreshRoster();           // герои с учётом улучшений Наследия
    void recordRunResult();         // итог забега -> артефакты и достижения
    void updateAchievements();
    void updateLegacy();
    void updateGuide();
    void drawAchievements();
    void drawLegacy();
    void drawGuide();
    void drawArtifact(float cx, float cy, float radius) const;

    // меню и настройки
    void loadSettings();
    void saveSettings() const;
    void buildResolutionList();
    void applyDisplay();
    void requestDisplayChange();       // плавный переход: затемнение -> смена режима -> проявление
    void syncRenderTarget();           // пересоздаёт буфер под текущий размер окна
    void viewTransform(float& scaleX, float& scaleY, float& offX, float& offY) const;
    void beginScissor(int x, int y, int w, int h) const;  // область обрезки в логических координатах
    Vector2 mouse() const;          // положение мыши в логических координатах 1280x720
    void updateMainMenu();
    void updatePause();
    void updateSettings();
    void drawMainMenu();
    void drawPause();
    void drawSettings();
    void drawButton(int x, int y, int w, int h, const std::string& label, bool selected, bool enabled) const;

    // обновление
    void update(float dt);
    void updateSelect();
    void updateBuild();
    void updateRun(float dt);
    void updateExplore(float dt);
    void updateRestChoice();
    void updateInventory();
    void updateShop();
    void updateForge();
    void updateBossReward();
    bool updateDevPanel();
    void onRunStep();
    void spawnActions();
    void updateAnims(float dt);
    void clearAnims();

    void updateEventChoice();
    void drawEventChoice();

    // отрисовка
    void draw();
    void drawScreen(Screen s);
    void drawSelect();
    void drawBuild();
    void drawRunView();
    void drawArena();
    void drawRestOverlay();
    void drawInventory();
    void drawShop();
    void drawForge();
    void drawBossReward();
    void drawDevPanel();
    void drawBossCard(int x, int y, int w, int h, int bossId) const;
    void drawBossIntro();
    void drawResult();
    void drawGrid(bool buildMode);
    void drawRoom(int px, int py, int cell, const Card& c, bool labels, float alpha, bool dim) const;
    void drawBar(int x, int y, int w, int h, float ratio, Color fill) const;
    void drawSprite(Vector2 c, int size, Color col, const std::string& name, int hp, int maxHp, Flash fl,
                    Vector2 offset, const std::string& tag, const std::string& art, const std::string& art2) const;
    void drawDebuffRunes(const Enemy& e, Vector2 c, int size) const;  // стаки рун Сейдмарда рядом с врагом

    // картинки: если файл есть в assets/, рисуется он, иначе встроенный шаблон
    const Texture2D* tex(const std::string& rel) const;
    bool drawArt(const std::string& rel, float x, float y, float w, float h, Color tint = Color{255, 255, 255, 255}) const;
    void drawBackground(const std::string& rel, float x, float y, float w, float h) const;
    void drawIconBox(const std::string& art, const std::string& art2, Color fallback, const std::string& glyph, float x,
                     float y, float size, Color tint = Color{255, 255, 255, 255}) const;
    void drawItemIcon(const Item& it, float x, float y, float size) const;
    void drawCoin(float cx, float cy, float radius) const;
    void drawBagCell(int x, int y, int size, const Item* it, bool selected, bool hover) const;
    void drawEquipSlot(int x, int y, int size, ItemKind kind, const std::optional<Item>& item,
                       bool fits, bool hover, bool locked = false) const;
    void drawItemTooltip(const Item& it, Vector2 at) const;
    void drawStar(float cx, float cy, float r, Color col) const;
    void drawStars(float x, float y, int count, float r) const;   // звёзды уровня предмета (крупные, с контуром)

    // текст
    void drawText(const std::string& s, float x, float y, float size, Color c) const;
    void drawTextCentered(const std::string& s, float cx, float y, float size, Color c) const;
    float textWidth(const std::string& s, float size) const;
    std::vector<std::string> wrap(const std::string& s, float maxW, float size) const;

    bool cellAtMouse(Vec2i& out) const;
    Vector2 heroCenter(int i) const;
    Vector2 enemyCenter(int slot, int n) const;
    Vector2 lungeOffset(bool hero, int idx) const;

    Font font_{};
    bool ownFont_ = false;
    std::string assetDir_ = "assets";
    mutable std::map<std::string, Texture2D> tex_;   // кэш загруженных картинок (id == 0 - файла нет)

    std::vector<Hero> roster_;
    bool chosen_[kMaxRoster] = {};
    uint32_t nextSeed_ = 1;
    int offered_[3] = {0, 1, 2};   // три предложенных босса
    int bossPick_ = 0;             // выбранный из трёх
    int bossId_ = 0;               // босс забега
    bool showBossCard_ = false;

    // скролл экрана выбора
    float selScroll_ = 0.0f;
    float selScrollTarget_ = 0.0f;
    float selContentH_ = 0.0f;
    float selViewH_ = 0.0f;

    // этажи и привал
    int currentFloor_ = 1;
    std::vector<int> defeatedBosses_;
    bool restActionDone_ = false;

    // окно, меню, настройки
    Settings settings_;
    Profile profile_;                    // постоянные данные: артефакты, достижения, Наследие
    std::string profilePath_ = "profile.sav";
    RunReward lastReward_;               // награда за последний забег (для экрана итога)
    bool runRecorded_ = false;
    std::string legacyMsg_;
    float achScroll_ = 0.f;
    float guideScroll_ = 0.f;  // прокрутка страницы «Механики» на основной колонке
    std::vector<Resolution> resolutions_;
    RenderTexture2D target_{};
    int targetW_ = 0, targetH_ = 0;   // размер буфера в физических пикселях окна
    float targetScale_ = 1.f;
    float displayFade_ = 0.f;         // 0 - прозрачно, 1 - чёрный экран
    int displayFadeDir_ = 0;          // +1 затемнение, -1 проявление, 0 - покой
    bool displayChangePending_ = false;
    bool quit_ = false;
    Screen prevScreen_ = Screen::MainMenu;     // куда вернуться из паузы
    Screen settingsBack_ = Screen::MainMenu;   // куда вернуться из настроек

    Rng rng_;
    std::mt19937 uiRng_{12345};
    Deck deck_;
    Dungeon dungeon_;
    std::unique_ptr<Run> run_;
    Slot hand_[kHand];
    int selected_ = 0;
    int placementsLeft_ = 0;
    int discardsLeft_ = 0;
    std::string message_;
    bool messageBad_ = false;

    Screen screen_ = Screen::Select;
    std::string resultText_;
    bool resultWin_ = false;

    float stepTimer_ = 0.f;
    float endTimer_ = 0.f;
    float closeTimer_ = 0.f;
    float moveCd_ = 0.f;
    Vector2 markerPos_{0, 0};
    int speedIdx_ = 1;
    bool paused_ = false;

    // привал
    bool inRest_ = false;
    int restStage_ = 0;  // 0 - выбор действия, 1 - выбор героев для баффа
    bool restPick_[kMaxRoster] = {};
    

    // инвентарь
    bool invOpen_ = false;
    int invSel_ = -1;
    std::string invMsg_;
    std::string restMsg_;
    std::string shopMsg_;
    std::string forgeMsg_;

    // анимация боя
    int lastFightId_ = 0;
    Lunge lunge_;
    std::vector<Proj> projs_;
    std::vector<Floater> floaters_;
    Flash heroFlash_[kMaxRoster];
    Flash enemyFlash_[kMaxRoster];

    // интерфейс разработчика
    bool devOpen_ = false;
    int devItemTier_ = 4;
    std::string devMsg_;
};

}  // namespace dc
