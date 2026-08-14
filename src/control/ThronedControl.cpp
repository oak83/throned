#include "include/control/ThronedControl.h"

#include "include/global/Configs.hpp"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/database/entities/RouteProfile.h"

#include <QJsonArray>
#include <QStringList>

namespace ThronedControl {
namespace {

QJsonObject fail(const QString &error) {
    return QJsonObject{{"ok", false}, {"error", error}};
}

QJsonObject ok(const QJsonObject &data = {}) {
    return QJsonObject{{"ok", true}, {"data", data}};
}

QString outboundName(int outboundID) {
    switch (outboundID) {
    case Configs::directID: return QStringLiteral("direct");
    case Configs::proxyID: return QStringLiteral("proxy");
    case Configs::blockID: return QStringLiteral("block");
    case Configs::warpBypassID: return QStringLiteral("warp");
    default: return QString::number(outboundID);
    }
}

// Returns false for anything that is not one of the four named outbounds, so a
// typo becomes an error instead of silently routing somewhere unintended.
bool outboundFromName(const QString &name, int *out) {
    if (name == QStringLiteral("direct")) *out = Configs::directID;
    else if (name == QStringLiteral("proxy")) *out = Configs::proxyID;
    else if (name == QStringLiteral("block")) *out = Configs::blockID;
    else if (name == QStringLiteral("warp")) *out = Configs::warpBypassID;
    else return false;
    return true;
}

bool actionFromName(const QString &name, Configs::simpleAction *out) {
    if (name == QStringLiteral("proxy")) *out = Configs::proxy;
    else if (name == QStringLiteral("direct")) *out = Configs::bypass;
    else if (name == QStringLiteral("block")) *out = Configs::block;
    else return false;
    return true;
}

std::shared_ptr<Configs::RouteProfile> activeProfile() {
    return Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
}

QJsonObject describeProfile(const std::shared_ptr<Configs::RouteProfile> &profile) {
    return QJsonObject{
        {"id", profile->id},
        {"name", profile->name},
        {"default_outbound", outboundName(profile->defaultOutboundID)},
        {"rules_enabled", profile->applyProfileRules},
        {"raw", profile->isRaw},
        {"remote", profile->isRemote},
    };
}

// Simple rules are stored with a type prefix. A caller that just has a hostname
// should not have to know that, so a bare entry becomes a suffix match, which is
// what "route this site" nearly always means (it covers subdomains too).
QString withRulePrefix(const QString &entry) {
    static const QStringList prefixes{
        QStringLiteral("domain:"), QStringLiteral("suffix:"), QStringLiteral("keyword:"),
        QStringLiteral("regex:"), QStringLiteral("ruleset:"), QStringLiteral("ip:"),
        QStringLiteral("processName:"), QStringLiteral("processPath:"),
    };
    for (const QString &prefix : prefixes)
        if (entry.startsWith(prefix)) return entry;
    return QStringLiteral("suffix:") + entry;
}

QStringList requestedStrings(const QJsonObject &request, const QString &key) {
    QStringList values;
    for (const QJsonValue &value : request.value(key).toArray()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) values << text;
    }
    return values;
}

QJsonObject saveAndApply(const std::shared_ptr<Configs::RouteProfile> &profile, const QJsonObject &data) {
    Configs::dataManager->routesRepo->Save(profile);
    if (hooks.applyRoutingChange) hooks.applyRoutingChange();
    return ok(data);
}

} // namespace

QJsonObject Execute(const QJsonObject &request) {
    const QString cmd = request.value(QStringLiteral("cmd")).toString();
    if (cmd.isEmpty()) return fail(QStringLiteral("missing \"cmd\""));
    if (!Configs::dataManager) return fail(QStringLiteral("data layer is not ready"));

    if (cmd == QStringLiteral("help")) return ok(QJsonObject{{"text", HelpText()}});

    if (cmd == QStringLiteral("status")) {
        const auto profile = activeProfile();
        const int runningId = hooks.runningProfileId ? hooks.runningProfileId() : -1;
        QJsonObject data{
            {"running_profile_id", runningId},
            {"running", runningId >= 0},
            {"mixed_port", Configs::dataManager->settingsRepo->inbound_socks_port},
            {"tun_enabled", Configs::dataManager->settingsRepo->spmode_vpn},
            {"system_proxy", Configs::dataManager->settingsRepo->spmode_system_proxy},
        };
        if (runningId >= 0) {
            if (const auto running = Configs::dataManager->profilesRepo->GetProfile(runningId))
                data["running_profile_name"] = running->name;
        }
        if (profile) data["routing"] = describeProfile(profile);
        return ok(data);
    }

    if (cmd == QStringLiteral("profiles.list")) {
        QJsonArray items;
        for (const int id : Configs::dataManager->profilesRepo->GetAllProfileIds()) {
            const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
            if (!profile) continue;
            items.append(QJsonObject{
                {"id", profile->id},
                {"name", profile->name},
                {"type", profile->type},
                {"group_id", profile->gid},
            });
        }
        return ok(QJsonObject{{"profiles", items}});
    }

    if (cmd == QStringLiteral("profile.start")) {
        if (!hooks.startProfile) return fail(QStringLiteral("the window is not ready yet"));
        const int id = request.value(QStringLiteral("id")).toInt(-1);
        if (id < 0) return fail(QStringLiteral("\"id\" is required"));
        if (!Configs::dataManager->profilesRepo->GetProfile(id))
            return fail(QStringLiteral("no profile with id %1").arg(id));
        hooks.startProfile(id);
        return ok(QJsonObject{{"started", id}});
    }

    if (cmd == QStringLiteral("profile.stop")) {
        if (!hooks.stopProfile) return fail(QStringLiteral("the window is not ready yet"));
        hooks.stopProfile();
        return ok();
    }

    if (cmd == QStringLiteral("routing.list")) {
        QJsonArray items;
        for (const auto &profile : Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
            QJsonObject item = describeProfile(profile);
            item["active"] = profile->id == Configs::dataManager->settingsRepo->current_route_id;
            items.append(item);
        }
        return ok(QJsonObject{{"profiles", items}});
    }

    if (cmd == QStringLiteral("routing.get")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        QJsonObject data = describeProfile(profile);
        if (!profile->isRaw) {
            data["proxy_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::proxy).split('\n', Qt::SkipEmptyParts));
            data["direct_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::bypass).split('\n', Qt::SkipEmptyParts));
            data["blocked_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::block).split('\n', Qt::SkipEmptyParts));
        }
        return ok(data);
    }

    if (cmd == QStringLiteral("routing.select")) {
        const int id = request.value(QStringLiteral("id")).toInt(-1);
        if (id < 0) return fail(QStringLiteral("\"id\" is required"));
        if (!Configs::dataManager->routesRepo->GetRouteProfile(id))
            return fail(QStringLiteral("no routing profile with id %1").arg(id));
        Configs::dataManager->settingsRepo->current_route_id = id;
        Configs::dataManager->settingsRepo->Save();
        if (hooks.applyRoutingChange) hooks.applyRoutingChange();
        return ok(QJsonObject{{"active", id}});
    }

    if (cmd == QStringLiteral("routing.set_default")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile owns its own final outbound"));
        int outbound = 0;
        if (!outboundFromName(request.value(QStringLiteral("outbound")).toString(), &outbound))
            return fail(QStringLiteral("\"outbound\" must be direct, proxy, block or warp"));
        profile->defaultOutboundID = outbound;
        return saveAndApply(profile, QJsonObject{{"default_outbound", outboundName(outbound)}});
    }

    if (cmd == QStringLiteral("routing.set_rules_enabled")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile has no separable rules"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        profile->applyProfileRules = request.value(QStringLiteral("enabled")).toBool();
        return saveAndApply(profile, QJsonObject{{"rules_enabled", profile->applyProfileRules}});
    }

    if (cmd == QStringLiteral("routing.add_domains") || cmd == QStringLiteral("routing.remove_domains")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile is edited as JSON, not as domain lists"));
        Configs::simpleAction action = Configs::proxy;
        if (!actionFromName(request.value(QStringLiteral("action")).toString(), &action))
            return fail(QStringLiteral("\"action\" must be proxy, direct or block"));
        const QStringList domains = requestedStrings(request, QStringLiteral("domains"));
        if (domains.isEmpty()) return fail(QStringLiteral("\"domains\" must be a non-empty array"));

        QStringList current = profile->GetSimpleRules(action).split('\n', Qt::SkipEmptyParts);
        const bool adding = cmd.endsWith(QStringLiteral("add_domains"));
        for (const QString &domain : domains) {
            const QString entry = withRulePrefix(domain);
            if (adding) {
                if (!current.contains(entry)) current << entry;
            } else {
                current.removeAll(entry);
                // Tolerate removal by the exact stored form as well, so a value
                // read back from routing.get can be handed straight back.
                current.removeAll(domain);
            }
        }
        const QString error = profile->UpdateSimpleRules(current.join('\n'), action);
        if (!error.isEmpty()) return fail(error);
        return saveAndApply(profile, QJsonObject{
            {"action", request.value(QStringLiteral("action")).toString()},
            {"domains", QJsonArray::fromStringList(current)},
        });
    }

    return fail(QStringLiteral("unknown command \"%1\"; send {\"cmd\":\"help\"}").arg(cmd));
}

QString HelpText() {
    return QStringLiteral(R"(Throned control interface

Send one JSON object per invocation to the running instance; one JSON object
comes back. Every reply is either

  {"ok": true,  "data": { ... }}
  {"ok": false, "error": "why it failed"}

so a failure is always parseable rather than free-form text.

Usage:
  Throned --cli '{"cmd":"status"}'

Commands

  {"cmd":"help"}
      This text, as data.text.

  {"cmd":"status"}
      running, running_profile_id, running_profile_name, mixed_port,
      tun_enabled, system_proxy, and the active routing profile.

  {"cmd":"profiles.list"}
      Every proxy profile: id, name, type, group_id.

  {"cmd":"profile.start","id":12}
  {"cmd":"profile.stop"}
      Start a proxy profile by id, or stop the running one.

  {"cmd":"routing.list"}
      Every routing profile, with "active" marking the selected one.

  {"cmd":"routing.get"}
      The active routing profile, plus its proxy/direct/blocked domain lists.

  {"cmd":"routing.select","id":3}
      Make a routing profile active.

  {"cmd":"routing.set_default","outbound":"proxy"}
      Where traffic goes when no rule matched: direct, proxy, block or warp.

  {"cmd":"routing.set_rules_enabled","enabled":false}
      Turn the profile's own rules off, leaving everything to the default
      outbound. Throned's internal rules keep applying either way.

  {"cmd":"routing.add_domains","action":"proxy","domains":["example.com"]}
  {"cmd":"routing.remove_domains","action":"proxy","domains":["example.com"]}
      Edit one of the three domain lists. A bare entry is stored as a suffix
      match, so "example.com" also covers its subdomains. To be explicit, give
      the same typed prefix the routing editor uses:

        domain:      exact domain
        suffix:      domain and its subdomains
        keyword:     substring of the domain
        regex:       regular expression
        ruleset:     a geosite/geoip rule set, e.g. ruleset:geosite-telegram
        ip:          IP or CIDR
        processName: executable name, e.g. processName:discord.exe
        processPath: full path to an executable

      Duplicates are ignored when adding. Removal accepts either the bare form
      or the stored one.

Notes

  A routing change is saved immediately and the running profile is restarted so
  it takes effect, which briefly interrupts traffic.

  Commands that edit routing refuse to touch raw profiles, because those carry a
  verbatim sing-box route object that has no domain lists to merge into.
)");
}

} // namespace ThronedControl
