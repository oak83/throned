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

// A caller naming an application gives either "discord.exe" or a full path.
// The first matches wherever the program runs from, the second pins one binary.
QString asProcessEntry(const QString &app) {
    if (app.startsWith(QStringLiteral("processName:")) || app.startsWith(QStringLiteral("processPath:")))
        return app;
    const bool looksLikePath = app.contains('/') || app.contains('\\');
    return (looksLikePath ? QStringLiteral("processPath:") : QStringLiteral("processName:")) + app;
}

QStringList requestedStrings(const QJsonObject &request, const QString &key) {
    QStringList values;
    for (const QJsonValue &value : request.value(key).toArray()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) values << text;
    }
    return values;
}

// One description of every command, used to dispatch documentation and to build
// the JSON schema. Keeping it beside the handlers is what stops the reference
// from quietly describing a surface that no longer exists.
struct Argument {
    const char *name;
    const char *type;     // "int", "bool", "string", "string[]"
    bool required;
    const char *accepts;  // allowed values, empty when free-form
    const char *summary;
};

struct Command {
    const char *name;
    const char *summary;
    QList<Argument> arguments;
    const char *returns;
};

const QList<Command> &commandTable() {
    static const QList<Command> table{
        {"help", "This reference as text.", {}, "text"},
        {"schema", "This reference as JSON.", {}, "commands"},
        {"status", "What is running, where, and through which route.", {},
         "running, running_profile_id, running_profile_name, mixed_port, tun_enabled, system_proxy, routing"},
        {"profiles.list", "Every proxy profile.", {}, "profiles[]: id, name, type, group_id"},
        {"profile.start", "Start a proxy profile.",
         {{"id", "int", true, "", "profile id from profiles.list"}}, "started"},
        {"profile.stop", "Stop the running profile.", {}, ""},
        {"tun.set", "Turn TUN mode on or off.",
         {{"enabled", "bool", true, "true, false", ""}},
         "tun_enabled. Refuses to turn TUN on when the app is not elevated, because that "
         "path restarts it behind a UAC prompt."},
        {"system_proxy.set", "Turn the system proxy on or off.",
         {{"enabled", "bool", true, "true, false", ""}}, "system_proxy"},
        {"routing.list", "Every routing profile; \"active\" marks the selected one.", {},
         "profiles[]: id, name, default_outbound, rules_enabled, raw, remote, active"},
        {"routing.get", "The active routing profile and its domain lists.", {},
         "id, name, default_outbound, rules_enabled, proxy_domains, direct_domains, blocked_domains"},
        {"routing.select", "Make a routing profile active.",
         {{"id", "int", true, "", "profile id from routing.list"}}, "active"},
        {"routing.set_default", "Where traffic goes when no rule matched.",
         {{"outbound", "string", true, "direct, proxy, block, warp", ""}}, "default_outbound"},
        {"routing.set_rules_enabled",
         "Apply the profile's own rules, or send everything to the default outbound. "
         "Throned's internal rules and the local-proxy quick option keep applying either way.",
         {{"enabled", "bool", true, "true, false", ""}}, "rules_enabled"},
        {"routing.add_domains", "Add entries to one of the three routing lists.",
         {{"action", "string", true, "proxy, direct, block", "which list to edit"},
          {"domains", "string[]", true, "",
           "a bare host becomes a suffix match covering subdomains; the typed prefixes "
           "domain:, suffix:, keyword:, regex:, ruleset:, ip:, processName: and processPath: "
           "are kept as given"}},
         "action, domains (the resulting list)"},
        {"routing.remove_domains", "Remove entries from one of the three routing lists.",
         {{"action", "string", true, "proxy, direct, block", "which list to edit"},
          {"domains", "string[]", true, "", "accepts the bare form or the stored one"}},
         "action, domains (the resulting list)"},
        {"routing.add_apps", "Route applications by their process.",
         {{"action", "string", true, "proxy, direct, block", "which list to edit"},
          {"apps", "string[]", true, "",
           "an executable name such as discord.exe, or a full path; a path is matched "
           "exactly, a bare name matches wherever the program runs from"}},
         "action, apps (the resulting process entries)"},
        {"routing.remove_apps", "Stop routing applications by their process.",
         {{"action", "string", true, "proxy, direct, block", "which list to edit"},
          {"apps", "string[]", true, "", "accepts the bare name, a path, or the stored form"}},
         "action, apps (the resulting process entries)"},
    };
    return table;
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
    if (cmd == QStringLiteral("schema")) return ok(Schema());

    if (cmd == QStringLiteral("tun.set")) {
        if (!hooks.setTun) return fail(QStringLiteral("the window is not ready yet"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        const bool enabled = request.value(QStringLiteral("enabled")).toBool();
        // Enabling TUN unelevated makes the app relaunch itself through a UAC
        // prompt, so the answer to this command would never arrive. Say so.
        if (enabled && hooks.isElevated && !hooks.isElevated())
            return fail(QStringLiteral("TUN needs elevated rights; turn it on from the window, "
                                       "which can ask for them"));
        hooks.setTun(enabled);
        return ok(QJsonObject{{"tun_enabled", Configs::dataManager->settingsRepo->spmode_vpn}});
    }

    if (cmd == QStringLiteral("system_proxy.set")) {
        if (!hooks.setSystemProxy) return fail(QStringLiteral("the window is not ready yet"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        hooks.setSystemProxy(request.value(QStringLiteral("enabled")).toBool());
        return ok(QJsonObject{{"system_proxy", Configs::dataManager->settingsRepo->spmode_system_proxy}});
    }

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

    if (cmd == QStringLiteral("routing.add_apps") || cmd == QStringLiteral("routing.remove_apps")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile is edited as JSON, not as app lists"));
        Configs::simpleAction action = Configs::proxy;
        if (!actionFromName(request.value(QStringLiteral("action")).toString(), &action))
            return fail(QStringLiteral("\"action\" must be proxy, direct or block"));
        const QStringList apps = requestedStrings(request, QStringLiteral("apps"));
        if (apps.isEmpty()) return fail(QStringLiteral("\"apps\" must be a non-empty array"));

        QStringList current = profile->GetSimpleRules(action).split('\n', Qt::SkipEmptyParts);
        const bool adding = cmd.endsWith(QStringLiteral("add_apps"));
        for (const QString &app : apps) {
            const QString entry = asProcessEntry(app);
            if (adding) {
                if (!current.contains(entry)) current << entry;
            } else {
                current.removeAll(entry);
                current.removeAll(app);
            }
        }
        const QString error = profile->UpdateSimpleRules(current.join('\n'), action);
        if (!error.isEmpty()) return fail(error);
        QStringList processEntries;
        for (const QString &entry : current)
            if (entry.startsWith(QStringLiteral("processName:")) || entry.startsWith(QStringLiteral("processPath:")))
                processEntries << entry;
        return saveAndApply(profile, QJsonObject{
            {"action", request.value(QStringLiteral("action")).toString()},
            {"apps", QJsonArray::fromStringList(processEntries)},
        });
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
    QStringList lines;
    lines << QStringLiteral("throned control interface")
          << QString()
          << QStringLiteral("Send one JSON object per invocation to the running instance; one JSON")
          << QStringLiteral("object comes back. Every reply is either")
          << QString()
          << QStringLiteral(R"(  {"ok": true,  "data": { ... }})")
          << QStringLiteral(R"(  {"ok": false, "error": "why it failed"})")
          << QString()
          << QStringLiteral("so a failure is always parseable rather than free-form text.")
          << QString()
          << QStringLiteral("Usage:")
          << QStringLiteral(R"(  throned --cli '{"cmd":"status"}')")
          << QStringLiteral(R"(  throned --cli '{"cmd":"schema"}'   the same reference as JSON)")
          << QString()
          << QStringLiteral("Commands")
          << QString();

    for (const Command &command : commandTable()) {
        QString head = QStringLiteral(R"(  {"cmd":"%1")").arg(QString::fromLatin1(command.name));
        for (const Argument &argument : command.arguments) {
            head += QStringLiteral(",\"%1\":<%2>").arg(QString::fromLatin1(argument.name),
                                                       QString::fromLatin1(argument.type));
        }
        lines << head + QStringLiteral("}");
        lines << QStringLiteral("      ") + QString::fromLatin1(command.summary);
        for (const Argument &argument : command.arguments) {
            QString detail = QStringLiteral("        %1 (%2%3)")
                .arg(QString::fromLatin1(argument.name), QString::fromLatin1(argument.type),
                     argument.required ? QStringLiteral(", required") : QString());
            if (*argument.accepts) detail += QStringLiteral(" - one of: %1").arg(QString::fromLatin1(argument.accepts));
            if (*argument.summary) detail += QStringLiteral(" - %1").arg(QString::fromLatin1(argument.summary));
            lines << detail;
        }
        if (*command.returns) lines << QStringLiteral("      returns: ") + QString::fromLatin1(command.returns);
        lines << QString();
    }

    lines << QStringLiteral("Notes")
          << QString()
          << QStringLiteral("  A routing change is saved at once and restarts the running profile so it")
          << QStringLiteral("  takes effect, which briefly interrupts traffic.")
          << QString()
          << QStringLiteral("  Commands that edit routing refuse to touch raw profiles, because those")
          << QStringLiteral("  carry a verbatim sing-box route object with no domain lists to merge into.");
    return lines.join('\n') + '\n';
}

QJsonObject Schema() {
    QJsonArray commands;
    for (const Command &command : commandTable()) {
        QJsonArray arguments;
        for (const Argument &argument : command.arguments) {
            QJsonObject described{
                {"name", QString::fromLatin1(argument.name)},
                {"type", QString::fromLatin1(argument.type)},
                {"required", argument.required},
            };
            if (*argument.accepts) {
                QJsonArray accepted;
                for (const QString &value : QString::fromLatin1(argument.accepts).split(QStringLiteral(", ")))
                    accepted.append(value);
                described["accepts"] = accepted;
            }
            if (*argument.summary) described["summary"] = QString::fromLatin1(argument.summary);
            arguments.append(described);
        }
        QJsonObject described{
            {"name", QString::fromLatin1(command.name)},
            {"summary", QString::fromLatin1(command.summary)},
            {"arguments", arguments},
        };
        if (*command.returns) described["returns"] = QString::fromLatin1(command.returns);
        commands.append(described);
    }
    return QJsonObject{
        {"interface", QStringLiteral("throned-control")},
        {"version", 1},
        {"reply", QStringLiteral(R"({"ok":true,"data":{...}} or {"ok":false,"error":"..."})")},
        {"commands", commands},
    };
}

} // namespace ThronedControl
