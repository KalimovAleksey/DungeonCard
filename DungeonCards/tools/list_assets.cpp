// Печатает таблицу картинок, которые можно подложить в assets/ (Markdown).
#include <cstdio>
#include "core/Assets.h"

int main() {
    std::string group;
    for (const auto& a : dc::assetManifest()) {
        if (a.group != group) {
            group = a.group;
            std::printf("\n### %s\n\n| Файл (в assets/) | Размер | Назначение |\n|---|---|---|\n", group.c_str());
        }
        std::printf("| `%s` | %dx%d | %s%s |\n", a.path.c_str(), a.w, a.h, a.desc.c_str(), a.optional ? " (необязательно)" : "");
    }
}
