#include "Profile.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include "Util.h"

namespace dc {

const std::vector<Achievement>& achievements() {
    static const std::vector<Achievement> list = [] {
        std::vector<Achievement> v;
        for (int b = 0; b < kBossCount; ++b)
            v.push_back({b, "Боссы", "Победа: " + bossInfo(b).name, "Победить босса: " + bossInfo(b).name + ".", kAchievementArtifacts});
        const auto roster = heroRoster();
        for (const auto& h : roster)
            v.push_back({100 + static_cast<int>(h.role), "Герои", "Путь героя: " + h.name,
                         "Победить босса с героем " + h.name + " в отряде.", kAchievementArtifacts});
        v.push_back({200, "Особые", "Без потерь", "Победить босса, не потеряв ни одного героя (воскрешённый тоже считается потерей).",
                     kAchievementArtifacts});
        v.push_back({201, "Особые", "Последний герой", "Победить босса, когда в живых остался лишь один герой.", kAchievementArtifacts});
        return v;
    }();
    return list;
}

RunReward Profile::recordRun(const RunSummary& s) {
    RunReward r;
    ++runs;
    if (!s.victory) return r;
    ++wins;
    if (s.bossId >= 0 && s.bossId < kBossCount) ++bossWins[s.bossId];
    for (Role role : s.squad) {
        const int i = static_cast<int>(role);
        if (i >= 0 && i < 8) ++heroWins[i];
    }
    if (s.casualties == 0) ++flawlessWins;
    if (s.aliveAtEnd == 1) ++lastStandWins;

    r.bossArtifacts = kBossArtifacts;
    for (const auto& a : achievements()) {
        if (isUnlocked(a.id)) continue;
        bool ok = false;
        if (a.id < kBossCount) ok = bossWins[a.id] > 0;
        else if (a.id >= 100 && a.id < 108) ok = heroWins[a.id - 100] > 0;
        else if (a.id == 200) ok = flawlessWins > 0;
        else if (a.id == 201) ok = lastStandWins > 0;
        if (!ok) continue;
        unlocked.insert(a.id);
        r.newAchievements.push_back(a.id);
        r.achievementArtifacts += a.reward;
    }
    artifacts += r.total();
    artifactsEarned += r.total();
    return r;
}

bool Profile::buyUpgrade(int u) {
    if (u < 0 || u >= kUpgradeCount || meta.maxed(u)) return false;
    const int cost = meta.nextCost(u);
    if (cost <= 0 || artifacts < cost) return false;
    artifacts -= cost;
    ++meta.level[u];
    return true;
}

// ---------------------------------------------------------------- сохранение

std::string Profile::serialize() const {
    std::ostringstream o;
    o << "dc_profile 1\n";
    o << "artifacts " << artifacts << "\n";
    o << "earned " << artifactsEarned << "\n";
    o << "runs " << runs << "\n";
    o << "wins " << wins << "\n";
    o << "flawless " << flawlessWins << "\n";
    o << "laststand " << lastStandWins << "\n";
    o << "bosswins";
    for (int v : bossWins) o << " " << v;
    o << "\nherowins";
    for (int v : heroWins) o << " " << v;
    o << "\nunlocked";
    for (int id : unlocked) o << " " << id;
    o << "\nmeta";
    for (int v : meta.level) o << " " << v;
    o << "\n";
    return o.str();
}

Profile Profile::parse(const std::string& text) {
    Profile p;
    std::istringstream in(text);
    std::string line;
    auto nonNeg = [](int v) { return std::max(0, v); };
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;
        if (key == "artifacts") { ls >> p.artifacts; p.artifacts = nonNeg(p.artifacts); }
        else if (key == "earned") { ls >> p.artifactsEarned; p.artifactsEarned = nonNeg(p.artifactsEarned); }
        else if (key == "runs") { ls >> p.runs; p.runs = nonNeg(p.runs); }
        else if (key == "wins") { ls >> p.wins; p.wins = nonNeg(p.wins); }
        else if (key == "flawless") { ls >> p.flawlessWins; p.flawlessWins = nonNeg(p.flawlessWins); }
        else if (key == "laststand") { ls >> p.lastStandWins; p.lastStandWins = nonNeg(p.lastStandWins); }
        else if (key == "bosswins") { for (int& v : p.bossWins) { if (!(ls >> v)) break; v = nonNeg(v); } }
        else if (key == "herowins") { for (int& v : p.heroWins) { if (!(ls >> v)) break; v = nonNeg(v); } }
        else if (key == "unlocked") {
            int id;
            while (ls >> id)
                for (const auto& a : achievements())
                    if (a.id == id) p.unlocked.insert(id);
        } else if (key == "meta") {
            for (int u = 0; u < kUpgradeCount; ++u) {
                int v = 0;
                if (!(ls >> v)) break;
                p.meta.level[u] = std::max(0, std::min(upgradeInfo(u).maxLevel, v));
            }
        }
    }
    return p;
}

bool Profile::saveFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << serialize();
    return static_cast<bool>(f);
}

Profile Profile::loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return Profile{};
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

}  // namespace dc
