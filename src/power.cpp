#include "internal.hpp"
#include "power_math.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <set>
#include <sstream>

namespace mns {
namespace {
using P = power_detail::Power;
using I = std::int64_t;
const Json nil;
const Json empty_array = Json::array();
const Json &field(const Json &j, const char *k) {
    return j.contains(k) ? j.at(k) : nil;
}
I number(const Json &j, const char *label, I lo = 0, I hi = INT64_MAX) {
    I n{};
    if (j.is_number_integer())
        n = j.get<I>();
    else if (j.is_string()) {
        const auto s = j.get<std::string>();
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
        if (ec != std::errc{} || end != s.data() + s.size() || s.empty())
            throw Error(std::string(label) + ": expected integer");
    } else
        throw Error(std::string(label) + ": expected integer");
    if (n < lo || n > hi)
        throw Error(std::string(label) + ": out of range");
    return n;
}
I nget(const Json &j, const char *k, I fallback = 0, I lo = 0) {
    return j.contains(k) ? number(j.at(k), k, lo) : fallback;
}
I raw(const Json &j, const char *k, I fallback = 0) {
    return nget(j, k, fallback, INT64_MIN);
}
const Json &array_field(const Json &j, const char *k) {
    if (!j.contains(k))
        return empty_array;
    const auto &a = j.at(k);
    if (!a.is_array())
        throw Error(std::string(k) + ": expected array");
    return a;
}
const Json &records(const Json &j, const char *k) {
    const auto &a = array_field(j, k);
    for (const auto &r : a)
        if (!r.is_object())
            throw Error(std::string(k) + ": expected object records");
    return a;
}
void keys(const Json &j, std::initializer_list<std::string_view> allowed, const char *label) {
    if (!j.is_object())
        throw Error(std::string(label) + ": expected object");
    for (const auto &[k, v] : j.items())
        if (std::find(allowed.begin(), allowed.end(), k) == allowed.end())
            throw Error(std::string(label) + ": unknown field " + k);
}
std::map<I, const Json *> index(const Json &a, const char *k, const char *label) {
    std::map<I, const Json *> out;
    for (const auto &r : a) {
        auto id = nget(r, k, 0, 1);
        if (!out.emplace(id, &r).second)
            throw Error(std::string(label) + ": duplicate " + k);
    }
    return out;
}
const Json &lookup(const std::map<I, const Json *> &map, I id, const char *label) {
    auto it = map.find(id);
    if (it == map.end())
        throw Error(std::string(label) + ": missing reference " + std::to_string(id));
    return *it->second;
}
const Json &one(const Master &m, const char *table, std::initializer_list<std::pair<const char *, I>> query) {
    const Json *found = nullptr;
    for (const auto &r : m.rows(table)) {
        bool match = true;
        for (const auto &[k, v] : query)
            if (raw(r, k) != v) {
                match = false;
                break;
            }
        if (match) {
            if (found)
                throw Error(std::string(table) + ": duplicate logical key");
            found = &r;
        }
    }
    if (!found)
        throw Error(std::string(table) + ": missing row");
    return *found;
}
void unique(const Master &m, const char *table, std::initializer_list<const char *> fields) {
    std::set<std::vector<I>> seen;
    for (const auto &r : m.rows(table)) {
        std::vector<I> key;
        for (auto f : fields)
            key.push_back(raw(r, f));
        if (!seen.insert(key).second)
            throw Error(std::string(table) + ": duplicate logical key");
    }
}
Json vec(const std::array<I, 3> &p) {
    return Json::array({p[0], p[1], p[2]});
}
Json describe(const P &p) {
    return Json{{"bp", vec(p.bp)}, {"points", vec(p.integer())}, {"total", p.total()}};
}
std::array<I, 3> triple(const Json &j, const char *label) {
    if (!j.is_array() || j.size() != 3)
        throw Error(std::string(label) + ": expected three integers");
    return {number(j[0], label), number(j[1], label), number(j[2], label)};
}
std::array<I, 3> maxima(const Json &r) {
    return {raw(r, "_performancePowerMax"), raw(r, "_technicPowerMax"), raw(r, "_visualPowerMax")};
}
std::array<I, 3> rates(const Json &r) {
    return {raw(r, "_performanceRate"), raw(r, "_technicRate"), raw(r, "_visualRate")};
}
P pointrow(const Json &r) {
    return P::points({raw(r, "_performance"), raw(r, "_technic"), raw(r, "_visual")});
}
bool contains(const Json &array, I value) {
    if (array.is_null())
        return false;
    for (const auto &v : array)
        if (number(v, "target category", INT64_MIN) == value)
            return true;
    return false;
}
bool overlap(const Json &a, const Json &b, bool skip_zero = false) {
    if (a.is_null() || b.is_null())
        return false;
    for (const auto &v : a) {
        const auto x = number(v, "category", INT64_MIN);
        if ((!skip_zero || x) && contains(b, x))
            return true;
    }
    return false;
}
struct Member {
    bool present = false;
    I id = 0, character = 0, band = 0, type = 0, mission = 0, leader = 0, leader_level = 0, rank = 0;
    const Json *card = nullptr;
    const Json *rank_row = nullptr;
    Json tags = Json::array(), live = Json::array(), gekisou = Json::array();
    P base, mem, support;
    I support_type = 0, support_link = 0;
    bool has_support = false;
};
bool target_member(const Json &t, const Member &m, bool memory = false) {
    auto type = raw(t, "_cardType");
    auto mt = m.type;
    if (memory) {
        if (type < 1 || type > 5)
            type = 0;
        if (mt < 1 || mt > 5)
            mt = 0;
    }
    return (raw(t, "_bandID") > 0 && raw(t, "_bandID") == m.band) || (type && type == mt) ||
           (raw(t, "_characterID") > 0 && raw(t, "_characterID") == m.character) ||
           (raw(t, "_tagID") > 0 && contains(m.tags, raw(t, "_tagID"))) ||
           overlap(field(t, "_liveSkillCategories"), m.live, true) ||
           overlap(field(t, "_gekisouSkillCategories"), m.gekisou, true) ||
           (raw(t, "_gekisouMissionType") && raw(t, "_gekisouMissionType") == m.mission);
}
bool any_target(const Master &master, const Json &ids, const Member &m, bool empty = true,
                bool memory = false) {
    if (ids.is_null() || ids.empty())
        return empty;
    for (const auto &id : ids)
        if (target_member(master.by_id("MasterSkillTarget", number(id, "target", 1)), m, memory))
            return true;
    return false;
}
P rank_bonus(const Master &m, const char *table, const char *key, I rank) {
    I threshold = INT64_MIN, bonus = 0;
    for (const auto &r : m.rows(table)) {
        const auto k = raw(r, key);
        if (k <= rank && k >= threshold) {
            threshold = k;
            bonus = raw(r, "_bonus");
        }
    }
    return P::points(bonus);
}
I parameter(const Master &m, const char *key) {
    for (const auto &r : m.rows("MasterParameter"))
        if (r.at("_id").get<std::string>() == key)
            return number(r.at("_value"), key, INT64_MIN);
    throw Error(std::string("missing parameter ") + key);
}
I timestamp(const std::string &value) {
    std::string s = value;
    std::replace(s.begin(), s.end(), '/', '-');
    std::replace(s.begin(), s.end(), 'T', ' ');
    int y, mo, d, h, mi, se, used = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d%n", &y, &mo, &d, &h, &mi, &se, &used) != 6)
        throw Error("invalid Memory release date/context.as_of");
    using namespace std::chrono;
    const year_month_day date{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (!date.ok() || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59)
        throw Error("invalid Memory release date/context.as_of");
    int offset = 9 * 3600;
    const auto tail = s.substr(static_cast<std::size_t>(used));
    if (tail == "Z")
        offset = 0;
    else if (!tail.empty()) {
        int oh, om, n = 0;
        char sign = 0;
        if (std::sscanf(tail.c_str(), "%c%d:%d%n", &sign, &oh, &om, &n) != 3 ||
            n != static_cast<int>(tail.size()) || (sign != '+' && sign != '-') || oh < 0 || oh > 23 ||
            om < 0 || om > 59)
            throw Error("invalid Memory release date/context.as_of");
        offset = (sign == '-' ? -1 : 1) * (oh * 3600 + om * 60);
    }
    return duration_cast<seconds>(sys_days{date}.time_since_epoch()).count() + h * 3600 + mi * 60 + se -
           offset;
}
bool started(const Json &r, const Json &context) {
    const auto &v = field(r, "_startAt");
    if (v.is_null())
        return true;
    const auto s = v.get<std::string>();
    if (s.empty() || s.starts_with("0001-01-01") || s.starts_with("0001/01/01"))
        return true;
    const auto &a = field(context, "as_of");
    if (!a.is_string())
        throw Error("nonempty Memory configuration requires context.as_of");
    return timestamp(a.get<std::string>()) >= timestamp(s);
}
struct Memory {
    P member, support;
    std::vector<std::pair<P, Json>> groups;
    std::string source = "GetPlayerData/master";
};
Json explicit_target(const Json &r) {
    keys(r,
         {"band_id", "member_type", "character_id", "tag_id", "live_categories", "gekisou_categories",
          "gekisou_mission", "skill_target_type", "judgement", "music_type"},
         "memory target");
    static const std::pair<const char *, const char *> names[] = {
        {"band_id", "_bandID"},
        {"member_type", "_cardType"},
        {"character_id", "_characterID"},
        {"tag_id", "_tagID"},
        {"live_categories", "_liveSkillCategories"},
        {"gekisou_categories", "_gekisouSkillCategories"},
        {"gekisou_mission", "_gekisouMissionType"}};
    Json t = Json::object();
    for (const auto &[a, b] : names)
        if (r.contains(a))
            t[b] = r.at(a);
    return t;
}
Memory memory(const Master &m, const Json &player, const Json &context,
              const std::map<I, const Json *> &owned_members,
              const std::map<I, const Json *> &owned_supports) {
    Memory out;
    if (context.contains("memory") && !context.at("memory").is_null()) {
        const auto &mem = context.at("memory");
        keys(mem, {"groups", "member_level", "support_level", "source"}, "memory");
        out.source = mem.value("source", "caller-resolved");
        for (const auto &g : records(mem, "groups")) {
            keys(g, {"current", "targets"}, "memory group");
            if (field(g, "current").is_null())
                continue;
            Json targets = Json::array();
            for (const auto &t : records(g, "targets"))
                targets.push_back(explicit_target(t));
            out.groups.emplace_back(P::points(triple(g.at("current").at("points"), "memory points")),
                                    std::move(targets));
        }
        if (!field(mem, "member_level").is_null())
            out.member = P::points(triple(mem.at("member_level").at("points"), "memory member points"));
        if (!field(mem, "support_level").is_null())
            out.support = P::points(triple(mem.at("support_level").at("points"), "memory support points"));
        return out;
    }
    for (const auto *name : {"MasterMemoryMemberLevel", "MasterMemorySupportLevel", "MasterMemoryMusic",
                             "MasterMemoryMusicBonus", "MasterMemoryMusicGroup"})
        (void)m.rows(name);
    const Json status = field(player, "memoryStatus").is_null() ? Json::object() : player.at("memoryStatus");
    keys(status, {"memoryMusics", "memoryMembers", "memorySupports"}, "memoryStatus");
    const auto members = index(records(status, "memoryMembers"), "memberCardId", "memoryMembers");
    const auto supports = index(records(status, "memorySupports"), "supportCardId", "memorySupports");
    auto count = [&](const auto &unlocked, const auto &owned, const char *table, const char *level_table) {
        if (!unlocked.empty() && m.rows(level_table).empty())
            throw Error("unlocked Memory cards require level master");
        I value = 0;
        for (const auto &[id, r] : unlocked) {
            const auto &card = m.by_id(table, id);
            if (owned.contains(id) && started(card, context))
                ++value;
        }
        return value;
    };
    auto select_level = [&](const char *table, I count) {
        P power;
        for (const auto &r : m.rows(table))
            if (raw(r, "_point") <= count)
                power = pointrow(r);
        return power;
    };
    out.member = select_level("MasterMemoryMemberLevel",
                              count(members, owned_members, "MasterMemberCard", "MasterMemoryMemberLevel"));
    out.support =
        select_level("MasterMemorySupportLevel",
                     count(supports, owned_supports, "MasterSupportCard", "MasterMemorySupportLevel"));
    std::map<I, I> unlocked;
    for (const auto &r : records(status, "memoryMusics")) {
        const auto &bonus = m.by_id("MasterMemoryMusicBonus", nget(r, "memoryMusicBonusGroup", 0, 1));
        if (!unlocked.emplace(raw(bonus, "_groupId"), raw(bonus, "_scoreRank")).second)
            throw Error("duplicate unlocked Memory music group");
    }
    for (const auto &[id, rank] : unlocked)
        (void)m.by_id("MasterMemoryMusicGroup", id);
    for (const auto &group : m.rows("MasterMemoryMusicGroup")) {
        if (!started(group, context))
            continue;
        const auto id = raw(group, "_id");
        bool music_found = false;
        for (const auto &r : m.rows("MasterMemoryMusic"))
            if (raw(r, "_groupId") == id)
                music_found = true;
        if (!music_found)
            throw Error("Memory group has no music rows");
        bool bonus_found = false;
        const Json *current = nullptr;
        for (const auto &r : m.rows("MasterMemoryMusicBonus"))
            if (raw(r, "_groupId") == id) {
                bonus_found = true;
                if (raw(r, "_scoreRank") <= unlocked[id])
                    current = &r;
            }
        if (!bonus_found)
            throw Error("Memory group has no bonus rows");
        Json targets = Json::array();
        for (const auto &t : array_field(group, "_skillTargetIds"))
            targets.push_back(m.by_id("MasterSkillTarget", number(t, "Memory target", 1)));
        if (current)
            out.groups.emplace_back(pointrow(*current), std::move(targets));
    }
    return out;
}
P memory_power(const Memory &mem, const Member &m) {
    P out = mem.member + mem.support;
    for (const auto &[power, targets] : mem.groups)
        for (const auto &t : targets)
            if (target_member(t, m, true)) {
                out += power;
                break;
            }
    return out;
}
void add_effect(P &rate, I type, I value) {
    if (type == 1000 || type == 1500) {
        rate += P(value);
        return;
    }
    std::size_t i;
    if (type == 1001 || type == 1501)
        i = 1;
    else if (type == 1002 || type == 1502)
        i = 2;
    else if (type == 1003 || type == 1503)
        i = 0;
    else
        throw Error("unsupported leader power effect");
    rate.bp[i] = power_detail::wrap_add(rate.bp[i], value);
}
std::array<P, 5> leader_rates(const Master &m, const std::array<Member, 5> &members, I music_type) {
    std::array<P, 5> out{};
    if (!members[2].present || !members[2].leader)
        return out;
    bool any = false;
    for (const auto &e : m.rows("MasterLeaderSkillEffect")) {
        if (raw(e, "_leaderSkillID") != members[2].leader || raw(e, "_level") != members[2].leader_level)
            continue;
        any = true;
        const auto type = raw(e, "_skillEffectType");
        if (type < 1000 || (type > 1003 && type < 1500) || type > 1503)
            throw Error("unsupported leader power effect");
        bool passes = true;
        const auto group = raw(e, "_skillConditionGroup");
        if (group > 0) {
            bool found = false;
            for (const auto &set : m.rows("MasterSkillConditionSet"))
                if (raw(set, "_group") == group) {
                    found = true;
                    for (const auto &id : array_field(set, "_conditionIds")) {
                        const auto &c = m.by_id("MasterSkillCondition", number(id, "leader condition", 1));
                        const auto ct = raw(c, "_conditionType");
                        const auto &targets = array_field(c, "_conditionTargetIDs");
                        if (targets.empty() || ct == 0)
                            continue;
                        // Resolve every reference even on native pass-through condition types.
                        for (const auto &t : targets)
                            (void)m.by_id("MasterSkillTarget", number(t, "condition target", 1));
                        if (ct == 3000) {
                            bool hit = false;
                            for (const auto &member : members)
                                if (member.present && any_target(m, targets, member))
                                    hit = true;
                            passes &= hit;
                        } else if (ct == 3001) {
                            for (const auto &member : members)
                                if (member.present && !any_target(m, targets, member))
                                    passes = false;
                        } else if (ct == 4012) {
                            bool hit = false;
                            for (const auto &t : targets)
                                if (raw(m.by_id("MasterSkillTarget", number(t, "target", 1)),
                                        "_liveMusicType") == music_type)
                                    hit = true;
                            passes &= hit;
                        }
                    }
                }
            if (!found)
                throw Error("missing leader condition group");
        }
        const auto cid = raw(e, "_skillCumulativeConditionID");
        const Json *cumulative = cid ? &m.by_id("MasterSkillCumulativeCondition", cid) : nullptr;
        if (!passes)
            continue;
        I value = raw(e, "_effectValue");
        if (type >= 1500 && cumulative) {
            I count = 0;
            const auto ct = raw(*cumulative, "_skillCumulativeConditionType");
            const auto &targets = array_field(*cumulative, "_conditionTargetIDs");
            for (const auto &t : targets)
                (void)m.by_id("MasterSkillTarget", number(t, "cumulative target", 1));
            if (ct == 3004 || ct == 3005) {
                std::set<I> distinct;
                for (const auto &member : members)
                    if (member.present)
                        distinct.insert(ct == 3004 ? member.band : member.type);
                count = static_cast<I>(distinct.size());
            } else if (ct >= 3000 && ct <= 3003) {
                for (std::size_t i = 0; i < 5; ++i) {
                    const auto &member = members[i];
                    if (!member.present)
                        continue;
                    if ((ct == 3000 && any_target(m, targets, member)) ||
                        (ct == 3001 && i != 2 && any_target(m, targets, member)) ||
                        (ct == 3002 && member.band == members[2].band) ||
                        (ct == 3003 && member.band != members[2].band))
                        ++count;
                }
            } else
                count = 1;
            const auto maximum = raw(*cumulative, "_maxCumulativeCount");
            if (maximum > 0)
                count = std::min(count, maximum);
            value = power_detail::wrap32(power_detail::wrap_mul(value, count));
        }
        const auto &targets = array_field(e, "_skillTargetIDs");
        for (const auto &t : targets)
            (void)m.by_id("MasterSkillTarget", number(t, "leader target", 1));
        for (std::size_t i = 0; i < 5; ++i)
            if (members[i].present && any_target(m, targets, members[i]))
                add_effect(out[i], type, value);
    }
    if (!any)
        throw Error("leader skill has no effects at derived rank level");
    return out;
}
std::array<P, 5> band_rates(const Master &m, const Json &player, const std::array<Member, 5> &members) {
    std::array<P, 5> out{};
    const auto items = index(records(player, "bandItems"), "masterId", "bandItems");
    for (const auto &[id, item] : items) {
        (void)m.by_id("MasterBandItem", id);
        const auto level = nget(*item, "level", 0, 1);
        (void)one(m, "MasterBandItemLevel", {{"_bandItemId", id}, {"_level", level}});
        const auto &e = one(m, "MasterBandItemSkillEffect", {{"_bandItemId", id}, {"_level", level}});
        const auto type = raw(e, "_skillEffectType");
        if (type < 1000 || type > 1003)
            throw Error("unsupported band item effect");
        for (const auto &target : array_field(e, "_skillTargetIDs")) {
            const auto &t = m.by_id("MasterSkillTarget", number(target, "band target", 1));
            for (std::size_t i = 0; i < 5; ++i) {
                const auto &member = members[i];
                if (!member.present)
                    continue;
                const I matches = (raw(t, "_bandID") > 0 && raw(t, "_bandID") == member.band) +
                                  (raw(t, "_characterID") > 0 && raw(t, "_characterID") == member.character) +
                                  (raw(t, "_cardType") != 0 && raw(t, "_cardType") == member.type);
                P change;
                add_effect(change, type, power_detail::wrap_mul(raw(e, "_effectValue"), matches));
                for (std::size_t k = 0; k < 3; ++k)
                    out[i].bp[k] = power_detail::wrap32(power_detail::wrap_add(out[i].bp[k], change.bp[k]));
            }
        }
    }
    return out;
}
Json structured_player(const Master &m, const Json &request) {
    const auto &deck = request.at("deck");
    keys(deck, {"members", "supports", "performance_order"}, "deck");
    const auto &members = array_field(deck, "members");
    const auto &supports = array_field(deck, "supports");
    if (members.size() != 5 || supports.size() > 5)
        throw Error("deck requires five members and at most five supports");
    Json order =
        deck.contains("performance_order") ? deck.at("performance_order") : Json::array({0, 1, 2, 3, 4});
    if (!order.is_array() || order.size() != 5)
        throw Error("performance_order must permute 0..4");
    std::array<int, 5> inverse{};
    std::set<I> seen;
    for (std::size_t i = 0; i < 5; ++i) {
        auto slot = number(order[i], "performance_order", 0, 4);
        if (!seen.insert(slot).second)
            throw Error("performance_order must permute 0..4");
        inverse[static_cast<std::size_t>(slot)] = static_cast<int>(i);
    }
    Json account = request.contains("account") ? request.at("account") : Json::object();
    keys(account,
         {"character_exp", "band_items", "vip_point", "memory_status", "owned_members", "owned_supports"},
         "account");
    Json player{
        {"memberCards", Json::array()},
        {"supportCards", Json::array()},
        {"characterRank", Json::array()},
        {"bandItems", Json::array()},
        {"vip", Json{{"point", nget(account, "vip_point")}}},
        {"memoryStatus", account.contains("memory_status") ? account.at("memory_status") : Json::object()}};
    auto input_card = [&](const Json &c, bool support) {
        keys(c,
             support
                 ? std::initializer_list<std::string_view>{"support_card_id", "level", "rank"}
                 : std::initializer_list<std::string_view>{"member_card_id", "level", "awake_count", "rank",
                                                           "live_skill_level", "gekisou_skill_level"},
             support ? "support" : "member");
        const auto id = nget(c, support ? "support_card_id" : "member_card_id", 0, 1);
        const auto &card = m.by_id(support ? "MasterSupportCard" : "MasterMemberCard", id);
        const auto level = nget(c, "level", 1, 1);
        const auto group = raw(card, support ? "_supportCardLevelGroup" : "_memberCardLevelGroup");
        const auto &lr = one(m, support ? "MasterSupportCardLevel" : "MasterMemberCardLevel",
                             {{"_group", group}, {"_level", level}});
        Json out{{"masterId", id}, {"exp", raw(lr, "_exp")}, {"cardRank", nget(c, "rank", 1)}};
        if (!support) {
            out["awakeCount"] = nget(c, "awake_count", 1);
            out["liveSkillLevel"] = nget(c, "live_skill_level", 1);
            out["performanceSkillLevel"] = nget(c, "gekisou_skill_level", 1);
        }
        return out;
    };
    Json slots = Json::array();
    std::set<I> owned_m, owned_s;
    for (std::size_t i = 0; i < 5; ++i) {
        I member_id = -1, support_id = -1;
        if (!members[i].is_null()) {
            auto card = input_card(members[i], false);
            member_id = nget(card, "masterId");
            player["memberCards"].push_back(std::move(card));
            owned_m.insert(member_id);
        }
        if (i < supports.size() && !supports[i].is_null()) {
            auto card = input_card(supports[i], true);
            support_id = nget(card, "masterId");
            player["supportCards"].push_back(std::move(card));
            owned_s.insert(support_id);
        }
        slots.push_back(Json{{"slotIndex", i},
                             {"performanceOrderIndex", inverse[i]},
                             {"memberCardId", member_id},
                             {"supportCardId", support_id}});
    }
    for (const auto &id : array_field(account, "owned_members")) {
        auto n = number(id, "owned_members", 1);
        (void)m.by_id("MasterMemberCard", n);
        if (owned_m.insert(n).second)
            player["memberCards"].push_back(Json{{"masterId", n}});
    }
    for (const auto &id : array_field(account, "owned_supports")) {
        auto n = number(id, "owned_supports", 1);
        (void)m.by_id("MasterSupportCard", n);
        if (owned_s.insert(n).second)
            player["supportCards"].push_back(Json{{"masterId", n}});
    }
    for (const auto &r : records(account, "character_exp")) {
        keys(r, {"character_id", "exp"}, "character_exp");
        player["characterRank"].push_back(
            Json{{"characterId", nget(r, "character_id", 0, 1)}, {"exp", nget(r, "exp")}});
    }
    for (const auto &r : records(account, "band_items")) {
        keys(r, {"band_item_id", "level"}, "band_items");
        player["bandItems"].push_back(
            Json{{"masterId", nget(r, "band_item_id", 0, 1)}, {"level", nget(r, "level", 1, 1)}});
    }
    player["decks"] =
        Json::array({Json{{"id", nget(request, "deck_id", 1, 1)}, {"cards", std::move(slots)}}});
    return player;
}
Json resolve_impl(const Master &m, const Json &request) {
    if (!request.is_object())
        throw Error("deck request must be an object");
    if (request.contains("player_data") == request.contains("deck"))
        throw Error("provide exactly one of player_data and deck");
    if (request.contains("player_data") && request.contains("account"))
        throw Error("account is only valid with structured deck");
    Json player = request.contains("player_data") ? request.at("player_data") : structured_player(m, request);
    if (player.contains("playerData")) {
        Json unwrapped = player.at("playerData");
        player = std::move(unwrapped);
    }
    if (!player.is_object())
        throw Error("player_data must be an object");
    const auto deck_id = nget(request, "deck_id", 1, 1);
    const auto score_id = nget(request, "music_score_id", 0, 1);
    Json context = request.contains("deck_context") ? request.at("deck_context") : Json::object();
    keys(context,
         {"music_type_extra_bonus_bp", "music_tag_extra_bonus_bp", "member_event_rates",
          "support_event_rates", "music_memory_points", "character_memory_points", "memory", "as_of"},
         "deck_context");
    std::array<P, 5> member_event{}, support_event{};
    std::array<I, 5> music_memory{}, char_memory{};
    auto read_powers = [&](const char *k, std::array<P, 5> &out) {
        if (!context.contains(k))
            return;
        if (!context.at(k).is_array() || context.at(k).size() != 5)
            throw Error(std::string(k) + ": expected five slot values");
        for (std::size_t i = 0; i < 5; ++i)
            out[i] = P(triple(context.at(k)[i], k));
    };
    auto read_points = [&](const char *k, std::array<I, 5> &out) {
        if (!context.contains(k))
            return;
        if (!context.at(k).is_array() || context.at(k).size() != 5)
            throw Error(std::string(k) + ": expected five slot values");
        for (std::size_t i = 0; i < 5; ++i)
            out[i] = number(context.at(k)[i], k);
    };
    read_powers("member_event_rates", member_event);
    read_powers("support_event_rates", support_event);
    read_points("music_memory_points", music_memory);
    read_points("character_memory_points", char_memory);
    const auto type_extra = nget(context, "music_type_extra_bonus_bp"),
               tag_extra = nget(context, "music_tag_extra_bonus_bp");
    const auto decks = index(records(player, "decks"), "id", "decks");
    const auto &deck = lookup(decks, deck_id, "deck");
    const auto &raw_slots = records(deck, "cards");
    if (raw_slots.size() != 5)
        throw Error("slotIndex and performanceOrderIndex must permute 0..4");
    std::array<const Json *, 5> slots{};
    std::array<int, 5> performance{};
    std::set<I> seen_slots, seen_orders;
    for (const auto &s : raw_slots) {
        const auto slot = number(s.contains("slotIndex") ? s.at("slotIndex") : Json(0), "slotIndex", 0, 4);
        const auto order =
            number(s.contains("performanceOrderIndex") ? s.at("performanceOrderIndex") : Json(0),
                   "performanceOrderIndex", 0, 4);
        if (!seen_slots.insert(slot).second || !seen_orders.insert(order).second)
            throw Error("slotIndex and performanceOrderIndex must permute 0..4");
        slots[static_cast<std::size_t>(slot)] = &s;
        performance[static_cast<std::size_t>(order)] = static_cast<int>(slot);
    }
    const auto owned_members = index(records(player, "memberCards"), "masterId", "memberCards");
    const auto owned_supports = index(records(player, "supportCards"), "masterId", "supportCards");
    (void)m.by_id("MasterLiveMusicScore", score_id);
    const Json *song = nullptr;
    for (const auto &r : m.rows("MasterLiveMusic")) {
        bool match = false;
        for (const auto *k : {"_easyID", "_normalID", "_hardID", "_expertID"})
            if (raw(r, k) == score_id)
                match = true;
        if (match) {
            if (song)
                throw Error("music score song is ambiguous");
            song = &r;
        }
    }
    if (!song)
        throw Error("music score song is missing");
    auto music_type = raw(*song, "_musicType");
    if (request.contains("context")) {
        const auto &live_context = request.at("context");
        if (!live_context.is_object())
            throw Error("context must be an object");
        if (live_context.contains("music_type")) {
            music_type = number(live_context.at("music_type"), "context.music_type", 1, 99);
            if (music_type > 5 && music_type != 99)
                throw Error("unsupported context.music_type");
        }
    }
    const auto &tags = array_field(*song, "_bestMusicTagIDs");
    unique(m, "MasterMemberCardLevel", {"_group", "_level"});
    unique(m, "MasterMemberCardAwake", {"_group", "_awakeCount"});
    unique(m, "MasterMemberCardRank", {"_group", "_rank"});
    unique(m, "MasterMemberCardLevelLimit", {"_rarity", "_awakeCount"});
    unique(m, "MasterSupportCardRank", {"_group", "_rank"});
    unique(m, "MasterSupportCardLevel", {"_group", "_level"});
    unique(m, "MasterCharacterRank", {"_rank"});
    unique(m, "MasterCharacterTotalRank", {"_totalRank"});
    unique(m, "MasterVip", {"_point"});
    auto character_exp = index(records(player, "characterRank"), "characterId", "characterRank");
    for (const auto &[id, row] : character_exp)
        (void)m.by_id("MasterCharacter", id);
    std::map<I, I> character_rank;
    I total_rank = 0;
    for (const auto &character : m.rows("MasterCharacter")) {
        const auto id = raw(character, "_id");
        const auto exp = character_exp.contains(id) ? nget(*character_exp.at(id), "exp") : 0;
        I rank = -1;
        for (const auto &r : m.rows("MasterCharacterRank"))
            if (raw(r, "_exp") <= exp)
                rank = std::max(rank, raw(r, "_rank"));
        if (rank < 0)
            throw Error("character exp below first master threshold");
        character_rank[id] = rank;
        if (rank > INT32_MAX - total_rank)
            throw Error("character total rank outside int32");
        total_rank += rank;
    }
    const Json vip = field(player, "vip").is_null() ? Json::object() : player.at("vip");
    if (!vip.is_object())
        throw Error("vip must be an object");
    const auto vip_point = nget(vip, "point");
    I vip_rank = -1, vip_threshold = -1, vip_bonus = 0;
    for (const auto &r : m.rows("MasterVip")) {
        const auto threshold = raw(r, "_point");
        if (threshold <= vip_point && threshold > vip_threshold) {
            vip_rank = raw(r, "_vipRank");
            vip_threshold = threshold;
        }
    }
    if (vip_rank < 0)
        throw Error("VIP point below first master threshold");
    bool vip_found = false;
    for (const auto &r : m.rows("MasterVipRankBonus"))
        if (raw(r, "_vipRank") == vip_rank && raw(r, "_vipBonusType") == 7) {
            if (vip_found)
                throw Error("duplicate VIP parameter bonus");
            vip_found = true;
            vip_bonus = raw(r, "_value");
        }
    const auto mem = memory(m, player, context, owned_members, owned_supports);
    std::array<Member, 5> members{};
    Json details = Json::array(), member_skills = Json::array(), support_skills = Json::array();
    std::set<I> used_members, used_supports;
    for (std::size_t i = 0; i < 5; ++i) {
        const auto &s = *slots[i];
        const auto member_id = nget(s, "memberCardId", 0, -1), support_id = nget(s, "supportCardId", 0, -1),
                   order = nget(s, "performanceOrderIndex");
        Json detail{{"slot_index", i},
                    {"performance_order_index", order},
                    {"member_id", member_id},
                    {"support", nullptr}};
        if (member_id <= 0) {
            if (support_id > 0)
                throw Error("support cannot occupy an empty member slot");
            details.push_back(std::move(detail));
            continue;
        }
        if (!used_members.insert(member_id).second)
            throw Error("duplicate member card in formation");
        const auto &owned = lookup(owned_members, member_id, "owned member");
        const auto &card = m.by_id("MasterMemberCard", member_id);
        auto &member = members[i];
        member.present = true;
        member.id = member_id;
        member.card = &card;
        member.character = raw(card, "_characterID");
        member.band = raw(m.by_id("MasterCharacter", member.character), "_bandID");
        member.type = raw(card, "_cardType");
        member.tags = array_field(card, "_bestMusicTagIDs");
        member.leader = raw(card, "_leaderSkillID");
        member.rank = nget(owned, "cardRank");
        const auto awake = nget(owned, "awakeCount"), exp = nget(owned, "exp");
        const Json *lr = nullptr;
        for (const auto &r : m.rows("MasterMemberCardLevel"))
            if (raw(r, "_group") == raw(card, "_memberCardLevelGroup") && raw(r, "_exp") <= exp &&
                (!lr || raw(r, "_exp") > raw(*lr, "_exp")))
                lr = &r;
        if (!lr)
            throw Error("member exp below first level threshold");
        const auto level = raw(*lr, "_level");
        const auto &ar = one(m, "MasterMemberCardAwake",
                             {{"_group", raw(card, "_memberCardAwakeGroup")}, {"_awakeCount", awake}});
        const auto &rr = one(m, "MasterMemberCardRank",
                             {{"_group", raw(card, "_memberCardRankGroup")}, {"_rank", member.rank}});
        member.rank_row = &rr;
        member.leader_level = raw(rr, "_leaderSkillLevel");
        const auto &limit =
            one(m, "MasterMemberCardLevelLimit", {{"_rarity", raw(card, "_rarity")}, {"_awakeCount", awake}});
        if (level > raw(limit, "_limitLevel"))
            throw Error("member level exceeds master limit");
        const auto max = maxima(card);
        const P growth = power_detail::growth(max, rates(*lr), 0) + power_detail::growth(max, rates(ar), 1) +
                         power_detail::growth(max, rates(rr), 2);
        const auto live_id = raw(card, "_liveSkillID"), gekisou_id = raw(card, "_gekisouSkillID");
        if (live_id)
            member.live = array_field(m.by_id("MasterLiveSkill", live_id), "_skillCategories");
        if (gekisou_id) {
            const auto &g = m.by_id("MasterGekisouSkill", gekisou_id);
            member.gekisou = array_field(g, "_skillCategories");
            member.mission = raw(g, "_gekisouMissionType");
        }
        member.mem = memory_power(mem, member);
        member.base = growth + member.mem;
        Json skill{{"slot_index", i},
                   {"performance_order_index", order},
                   {"member_card_id", member_id},
                   {"live_skill_id", live_id},
                   {"live_skill_level", nget(owned, "liveSkillLevel")},
                   {"gekisou_skill_id", gekisou_id},
                   {"gekisou_skill_level", nget(owned, "performanceSkillLevel")},
                   {"gekisou_mission_type", member.mission}};
        member_skills.push_back(skill);
        if (support_id > 0) {
            if (!used_supports.insert(support_id).second)
                throw Error("duplicate support card in formation");
            const auto &owned_s = lookup(owned_supports, support_id, "owned support");
            const auto &sc = m.by_id("MasterSupportCard", support_id);
            const auto rank = nget(owned_s, "cardRank"), sx = nget(owned_s, "exp");
            const Json *sl = nullptr;
            for (const auto &r : m.rows("MasterSupportCardLevel"))
                if (raw(r, "_group") == raw(sc, "_supportCardLevelGroup") && raw(r, "_exp") <= sx &&
                    (!sl || raw(r, "_exp") > raw(*sl, "_exp")))
                    sl = &r;
            if (!sl)
                throw Error("support exp below first level threshold");
            const auto &sr = one(m, "MasterSupportCardRank",
                                 {{"_group", raw(sc, "_supportCardRankGroup")}, {"_rank", rank}});
            if (raw(*sl, "_level") > raw(sr, "_limitLevel"))
                throw Error("support level exceeds master limit");
            member.has_support = true;
            member.support = P(power_detail::growth(maxima(sc), rates(*sl), 2).integer());
            member.support_type = raw(sc, "_cardType");
            member.support_link = raw(sr, "_cardTypeLinkBonusRate");
            Json sd{{"master_id", support_id},
                    {"level", raw(*sl, "_level")},
                    {"rank", rank},
                    {"power_bonus_bp", vec(member.support.bp)},
                    {"normal_skill_ids",
                     Json::array({raw(sc, "_supportSkillId01"), raw(sc, "_supportSkillId02")})},
                    {"normal_skill_levels",
                     Json::array({raw(sr, "_supportSkill01Level"), raw(sr, "_supportSkill02Level")})},
                    {"gekisou_skill_ids",
                     Json::array({raw(sc, "_gekisouSupportSkillId01"), raw(sc, "_gekisouSupportSkillId02")})},
                    {"gekisou_skill_levels", Json::array({raw(sr, "_gekisouSupportSkill01Level"),
                                                          raw(sr, "_gekisouSupportSkill02Level")})}};
            detail["support"] = sd;
            sd["slot_index"] = i;
            sd["performance_order_index"] = order;
            support_skills.push_back(std::move(sd));
        }
        detail["character_id"] = member.character;
        detail["band_id"] = member.band;
        detail["card_type"] = member.type;
        detail["tags"] = member.tags;
        detail["level"] = level;
        detail["awake_count"] = awake;
        detail["rank"] = member.rank;
        detail["character_rank"] = character_rank.at(member.character);
        detail["growth"] = describe(growth);
        detail["memory"] = describe(member.mem);
        detail["leader_skill_id"] = member.leader;
        detail["leader_skill_level"] = member.leader_level;
        for (const auto &[k, v] : skill.items())
            if (k != "member_card_id")
                detail[k] = v;
        details.push_back(std::move(detail));
    }
    const auto leader = leader_rates(m, members, music_type), band = band_rates(m, player, members);
    const I type_base = parameter(m, "music_type_base_bonus_rate"),
            tag_base = parameter(m, "music_tag_base_bonus_rate"),
            link_base = parameter(m, "type_link_base_bonus_rate");
    static constexpr const char *component_names[] = {"base",
                                                      "character_rank",
                                                      "character_total_rank",
                                                      "support",
                                                      "type_link",
                                                      "band_item",
                                                      "music_type",
                                                      "music_tag",
                                                      "leader",
                                                      "memory",
                                                      "vip"};
    static constexpr const char *rate_names[] = {"support",   "music_type", "music_tag",
                                                 "type_link", "leader",     "vip"};
    std::map<std::string, P> totals;
    P total;
    for (std::size_t i = 0; i < 5; ++i) {
        const auto &member = members[i];
        std::map<std::string, P> components, rates_bp;
        for (auto k : component_names)
            components.emplace(k, P{});
        for (auto k : rate_names)
            rates_bp.emplace(k, P{});
        if (member.present) {
            components["base"] = member.base + (member.base * member_event[i]).to_floor();
            components["character_rank"] =
                rank_bonus(m, "MasterCharacterRank", "_rank", character_rank.at(member.character));
            components["character_total_rank"] =
                rank_bonus(m, "MasterCharacterTotalRank", "_totalRank", total_rank);
            components["memory"] = P::points(power_detail::wrap_add(music_memory[i], char_memory[i]));
            const auto base = components["base"] + components["character_rank"] +
                              components["character_total_rank"] + components["memory"];
            rates_bp["support"] = member.support + support_event[i];
            rates_bp["leader"] = leader[i];
            rates_bp["vip"] = P(vip_bonus);
            if (member.has_support && member.type == member.support_type)
                rates_bp["type_link"] = P(power_detail::wrap_add(link_base, member.support_link));
            if (member.type == 99 || music_type == 99 || member.type == music_type)
                rates_bp["music_type"] = P(power_detail::wrap_add(
                    power_detail::wrap_add(type_base, raw(*member.rank_row, "_musicTypeBonusRate")),
                    type_extra));
            if (overlap(member.tags, tags))
                rates_bp["music_tag"] = P(power_detail::wrap_add(
                    power_detail::wrap_add(tag_base, raw(*member.rank_row, "_musicTagBonusRate")),
                    tag_extra));
            for (const auto &[k, rate] : rates_bp)
                components[k] = (base * rate).to_floor();
            components["band_item"] = (base * band[i]).to_floor();
        }
        P slot_total;
        Json cj = Json::object(), rj = Json::object();
        for (const auto &[k, v] : components) {
            slot_total += v;
            totals[k] += v;
            cj[k] = describe(v);
        }
        for (const auto &[k, v] : rates_bp)
            rj[k] = vec(v.bp);
        total += slot_total;
        auto &d = details[i];
        d["power"] = describe(slot_total);
        d["components"] = std::move(cj);
        d["rates_bp"] = std::move(rj);
        d["leader_bonus_bp"] = vec(leader[i].bp);
        d["band_item_bonus_bp"] = vec(band[i].bp);
    }
    Json component_report = Json::object();
    for (const auto &[k, v] : totals)
        component_report[k] = describe(v);
    Json order = Json::array();
    for (auto i : performance)
        order.push_back(i);
    Json mer = Json::array(), ser = Json::array(), mm = Json::array(), cm = Json::array();
    for (std::size_t i = 0; i < 5; ++i) {
        mer.push_back(vec(member_event[i].bp));
        ser.push_back(vec(support_event[i].bp));
        mm.push_back(music_memory[i]);
        cm.push_back(char_memory[i]);
    }
    return Json{
        {"deck_id", deck_id},
        {"music_score_id", score_id},
        {"music_id", raw(*song, "_id")},
        {"total_power", total.total()},
        {"power", describe(total)},
        {"slots", std::move(details)},
        {"performance_order", std::move(order)},
        {"skills", Json{{"members", std::move(member_skills)}, {"supports", std::move(support_skills)}}},
        {"components", std::move(component_report)},
        {"character_total_rank", total_rank},
        {"vip", Json{{"point", vip_point}, {"rank", vip_rank}, {"bonus_type", 7}, {"bonus_bp", vip_bonus}}},
        {"context", Json{{"music_type", music_type},
                         {"music_tags", tags},
                         {"music_type_extra_bonus_bp", type_extra},
                         {"music_tag_extra_bonus_bp", tag_extra},
                         {"member_event_rates_bp", std::move(mer)},
                         {"support_event_rates_bp", std::move(ser)},
                         {"music_memory_points", std::move(mm)},
                         {"character_memory_points", std::move(cm)},
                         {"memory_source", mem.source},
                         {"as_of", field(context, "as_of")}}},
        {"source", Json{{"master", m.source},
                        {"rules", "recovered-client-unpatched"},
                        {"adapter", request.contains("deck") ? "structured-deck-v1" : "GetPlayerData"}}}};
}
} // namespace
Json resolve_deck(const Master &master, const Json &request) {
    try {
        return resolve_impl(master, request);
    } catch (const Error &) {
        throw;
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::exception &e) {
        throw Error(std::string("deck resolution: ") + e.what());
    }
}
} // namespace mns
