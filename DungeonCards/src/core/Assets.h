#pragma once
#include <string>
#include <vector>
#include "Boss.h"
#include "Card.h"
#include "Party.h"

namespace dc {

// Перечень картинок, которыми можно заменить встроенные «шаблоны» (цветные фигуры).
// Файлы кладутся в папку assets/ под этими именами; если файла нет - рисуется шаблон.
struct AssetSpec {
    std::string path;   // относительно assets/, например "heroes/mage.png"
    int w = 0, h = 0;   // рекомендуемый размер, пикс.
    std::string group;  // раздел
    std::string desc;   // что это и где используется
    bool optional = false;  // необязателен даже при полной замене (есть запасной вариант)
};

std::vector<AssetSpec> assetManifest();

// Ключи (пути) для конкретных объектов игры.
std::string heroArt(Role r);              // heroes/<роль>.png
std::string bossArt(int bossId);          // bosses/<босс>.png
std::string enemyArt(const std::string& key);  // enemies/<ключ>.png
std::string itemArt(const Item& it);      // items/<вид>_<ур.|эффект>.png
std::string roomArt(RoomType t);          // rooms/<тип>.png
const char* roleKey(Role r);
const char* bossKey(int bossId);

}  // namespace dc
