#include <csignal>
#include <memory>

#include <QApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QTranslator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <QDateTime>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>
#include <3rdparty/WinCommander.hpp>


#include "include/global/Configs.hpp"
#include "include/global/Logger.hpp"

#include "include/ui/mainwindow_interface.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/api/RPC.h"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/control/ThronedControl.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include "include/sys/windows/MiniDump.h"
#include "include/sys/windows/eventHandler.h"
#include "include/sys/windows/WinVersion.h"
#include <qfontdatabase.h>
#endif
#ifdef Q_OS_LINUX
#include <include/sys/linux/coreDump.h>
#include <qfontdatabase.h>
#endif
#ifdef Q_OS_MACOS
#include <QFileOpenEvent>

// On macOS the OS reuses the running app and delivers throne:// URLs, as well as
// files opened with the app, as a QFileOpenEvent to the application object (never
// via argv). This filter feeds both into the common pipelines.
class MacOpenEventFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            const auto openEvent = static_cast<QFileOpenEvent *>(event);
            const QString url = openEvent->url().toString();
            if (url.startsWith("throne://")) {
                Deeplink_Submit(url);
                return true;
            }
            const QString file = openEvent->file().isEmpty() ? openEvent->url().toLocalFile() : openEvent->file();
            if (!file.isEmpty()) {
                LaunchFiles_Submit({file});
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};
#endif

#define LOCAL_SERVER_PREFIX "throned-"

void signal_handler(int signum) {
    GetMainWindow()->prepare_exit();
    qApp->quit();
}

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply");
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes");
    QT_TRANSLATE_NOOP("QPlatformTheme", "No");
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK");
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    //
    const QString diskPath = QCoreApplication::applicationDirPath()+"/translations/" + locale + ".qm";
    const QString qrcPath = ":/translations/" + locale + ".qm";
    bool loadOK=false;
    if (QFileInfo::exists(diskPath)) {
        loadOK = trans->load(diskPath);
    }
    if (!loadOK) {
        loadOK = trans->load(qrcPath);
    }
    if (loadOK) {
        QCoreApplication::installTranslator(trans);
    }
}

namespace {
    constexpr auto FALLBACK_MARKER = "config/.install-dir-unwritable";

    // QFileInfo::isWritable reports the read-only attribute, not what a UAC-filtered
    // token may actually do under Program Files.
    bool DirIsWritable(const QDir &dir) {
        if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) return false;
        QFile probe(dir.absoluteFilePath(".throne-write-test"));
        if (!probe.open(QIODevice::WriteOnly)) return false;
        probe.close();
        probe.remove();
        return true;
    }

    bool ConfigDirIsUsable(const QDir &configDir) {
        if (!DirIsWritable(configDir)) return false;
        const QString db = configDir.absoluteFilePath("throne.db");
        if (!QFile::exists(db)) return true;
        QFile file(db);
        return file.open(QIODevice::ReadWrite);
    }

    void CopyDirContents(const QString &from, const QString &to) {
        QDir().mkpath(to);
        QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            it.next();
            const QString target = QDir(to).absoluteFilePath(it.fileName());
            if (it.fileInfo().isDir()) CopyDirContents(it.filePath(), target);
            else if (!QFile::exists(target)) QFile::copy(it.filePath(), target);
        }
    }

    void MigrateLegacyConfigIfNeeded(const QDir &targetWd) {
        const QString targetConfig = targetWd.absoluteFilePath("config");
        if (QFile::exists(targetConfig + "/throne.db")) return;

        QStringList candidates;
        const QDir targetParent = QFileInfo(targetWd.absolutePath()).dir();
        if (QFileInfo(targetWd.absolutePath()).fileName().compare("Throned", Qt::CaseInsensitive) == 0) {
            candidates << targetParent.absoluteFilePath("Throne/config");
        }

        const QString appConfig = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        const QDir appConfigParent = QFileInfo(appConfig).dir();
        candidates << appConfigParent.absoluteFilePath("Throne/config");
        candidates.removeDuplicates();

        for (const QString &legacyConfig : candidates) {
            if (!QFile::exists(QDir(legacyConfig).absoluteFilePath("throne.db"))) continue;
            CopyDirContents(legacyConfig, targetConfig);
            if (QFile::exists(targetConfig + "/throne.db")) {
                LOG_INFO(QString("migrated Throne config from %1 to %2").arg(legacyConfig, targetConfig));
                return;
            }
        }
    }

    // An elevated relaunch finds the install dir writable again, so the fallback is
    // pinned by a marker or the two runs land on different databases.
    bool AdoptUserConfigDir(const QDir &installWd, const QDir &userWd) {
        QFile marker(userWd.absoluteFilePath(FALLBACK_MARKER));
        if (marker.open(QIODevice::ReadOnly)) {
            const bool pinnedHere = QString::fromUtf8(marker.readAll()).trimmed() == installWd.absolutePath();
            marker.close();
            if (pinnedHere) return true;
        }

        const QString installConfig = installWd.absoluteFilePath("config");
        if (ConfigDirIsUsable(QDir(installConfig))) return false;

        const QString userConfig = userWd.absoluteFilePath("config");
        QDir().mkpath(userConfig);
        if (!QFile::exists(userConfig + "/throne.db") && QFile::exists(installConfig + "/throne.db")) {
            CopyDirContents(installConfig, userConfig);
            LOG_WARN(QString("copied existing config from %1").arg(installConfig));
        }
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(installWd.absolutePath().toUtf8());
            marker.close();
        }
        LOG_WARN(QString("%1 is not writable, using %2").arg(installConfig, userConfig));
        return true;
    }

    // Throned is a GUI-subsystem binary on Windows, so it owns no console. When
    // it was started from one, borrow the parent's so the JSON reply lands where
    // the caller is looking instead of nowhere.
    void AttachToParentConsole() {
#ifdef Q_OS_WIN
        // Only borrow a console when the caller left us without a usable stdout.
        // If they redirected it into a pipe or a file, that handle is already
        // valid and attaching would send the answer to a terminal instead.
        const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
        if (existing == nullptr || existing == INVALID_HANDLE_VALUE) AttachConsole(ATTACH_PARENT_PROCESS);
#endif
    }

    void PrintLine(const QString &text) {
        const QByteArray bytes = text.toUtf8() + '\n';
#ifdef Q_OS_WIN
        // The CRT's stdout is not wired up in a GUI-subsystem binary, so write to
        // the OS handle, which works for a console and a redirection alike.
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != nullptr && out != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(out, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            return;
        }
#endif
        fwrite(bytes.constData(), 1, bytes.size(), stdout);
        fflush(stdout);
    }

    // Turns a typed-out command line into the JSON the running instance expects.
    // Returns false when the words do not name a command, so the caller can say
    // so instead of sending something the instance would reject.
    bool ParseHumanCommand(const QStringList &words, QJsonObject *request, QString *error) {
        if (words.isEmpty()) return false;
        const QString head = words.first();
        const QStringList rest = words.mid(1);

        const auto needsArgument = [&](const QString &what) {
            *error = QStringLiteral("'%1' needs %2").arg(head, what);
            return false;
        };

        if (head == "status") { *request = {{"cmd", "status"}}; return true; }
        if (head == "servers" || head == "profiles") { *request = {{"cmd", "profiles.list"}}; return true; }
        if (head == "stop") { *request = {{"cmd", "profile.stop"}}; return true; }
        if (head == "start") {
            if (rest.isEmpty()) return needsArgument(QStringLiteral("a server id, as shown by 'servers'"));
            *request = {{"cmd", "profile.start"}, {"id", rest.first().toInt()}};
            return true;
        }
        if (head == "routes") { *request = {{"cmd", "routing.list"}}; return true; }
        if (head == "route") {
            if (rest.isEmpty()) { *request = {{"cmd", "routing.get"}}; return true; }
            const QString verb = rest.first();
            const QStringList args = rest.mid(1);
            if (verb == "use") {
                if (args.isEmpty()) { *error = QStringLiteral("'route use' needs a profile id"); return false; }
                *request = {{"cmd", "routing.select"}, {"id", args.first().toInt()}};
                return true;
            }
            if (verb == "default") {
                if (args.isEmpty()) { *error = QStringLiteral("'route default' needs direct, proxy, block or warp"); return false; }
                *request = {{"cmd", "routing.set_default"}, {"outbound", args.first()}};
                return true;
            }
            if (verb == "rules") {
                if (args.isEmpty() || (args.first() != "on" && args.first() != "off")) {
                    *error = QStringLiteral("'route rules' needs on or off");
                    return false;
                }
                *request = {{"cmd", "routing.set_rules_enabled"}, {"enabled", args.first() == "on"}};
                return true;
            }
            if (verb == "add" || verb == "remove") {
                QString via = QStringLiteral("proxy");
                QJsonArray entries;
                for (int i = 0; i < args.size(); ++i) {
                    if (args.at(i) == "--via" || args.at(i) == "-v") {
                        if (i + 1 >= args.size()) { *error = QStringLiteral("--via needs proxy, direct or block"); return false; }
                        via = args.at(++i);
                        continue;
                    }
                    entries.append(args.at(i));
                }
                if (entries.isEmpty()) { *error = QStringLiteral("'route %1' needs at least one domain").arg(verb); return false; }
                *request = {
                    {"cmd", verb == "add" ? "routing.add_domains" : "routing.remove_domains"},
                    {"action", via},
                    {"domains", entries},
                };
                return true;
            }
            *error = QStringLiteral("unknown 'route' subcommand '%1'").arg(verb);
            return false;
        }
        *error = QStringLiteral("unknown command '%1'").arg(head);
        return false;
    }

    QString FormatValue(const QJsonValue &value) {
        if (value.isBool()) return value.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
        if (value.isDouble()) return QString::number(value.toDouble());
        if (value.isArray()) {
            QStringList parts;
            for (const QJsonValue &item : value.toArray()) parts << item.toString();
            return parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QStringLiteral(", "));
        }
        return value.toString();
    }

    // Renders a reply as lines a person can read. Anything without a tailored
    // shape falls back to "key: value", so a new command still prints sensibly.
    QString FormatReply(const QString &cmd, const QJsonObject &data) {
        QStringList lines;
        const auto profileLine = [](const QJsonObject &p) {
            QString line = QStringLiteral("  [%1] %2").arg(p.value("id").toInt()).arg(p.value("name").toString());
            if (p.value("active").toBool()) line += QStringLiteral("  (active)");
            line += QStringLiteral("\n        default: %1, rules: %2")
                .arg(p.value("default_outbound").toString(),
                     p.value("rules_enabled").toBool() ? QStringLiteral("on") : QStringLiteral("off"));
            if (p.value("raw").toBool()) line += QStringLiteral(", raw");
            return line;
        };

        if (cmd == "status") {
            lines << QStringLiteral("proxy:      %1").arg(data.value("running").toBool()
                ? QStringLiteral("running - %1").arg(data.value("running_profile_name").toString())
                : QStringLiteral("stopped"));
            lines << QStringLiteral("inbound:    mixed on port %1").arg(data.value("mixed_port").toInt());
            lines << QStringLiteral("tun:        %1").arg(FormatValue(data.value("tun_enabled")));
            lines << QStringLiteral("system:     %1").arg(FormatValue(data.value("system_proxy")));
            const QJsonObject routing = data.value("routing").toObject();
            if (!routing.isEmpty()) {
                lines << QStringLiteral("routing:    %1 - default %2, rules %3")
                    .arg(routing.value("name").toString(), routing.value("default_outbound").toString(),
                         routing.value("rules_enabled").toBool() ? QStringLiteral("on") : QStringLiteral("off"));
            }
            return lines.join('\n');
        }
        if (cmd == "routing.list") {
            lines << QStringLiteral("routing profiles:");
            for (const QJsonValue &value : data.value("profiles").toArray()) lines << profileLine(value.toObject());
            return lines.join('\n');
        }
        if (cmd == "routing.get") {
            lines << profileLine(data);
            lines << QStringLiteral("  through proxy: %1").arg(FormatValue(data.value("proxy_domains")));
            lines << QStringLiteral("  direct:        %1").arg(FormatValue(data.value("direct_domains")));
            lines << QStringLiteral("  blocked:       %1").arg(FormatValue(data.value("blocked_domains")));
            return lines.join('\n');
        }
        if (cmd == "profiles.list") {
            lines << QStringLiteral("servers:");
            for (const QJsonValue &value : data.value("profiles").toArray()) {
                const QJsonObject p = value.toObject();
                lines << QStringLiteral("  [%1] %2  (%3)").arg(p.value("id").toInt())
                    .arg(p.value("name").toString(), p.value("type").toString());
            }
            return lines.join('\n');
        }
        if (data.isEmpty()) return QStringLiteral("done");
        for (auto it = data.begin(); it != data.end(); ++it)
            lines << QStringLiteral("%1: %2").arg(it.key(), FormatValue(it.value()));
        return lines.join('\n');
    }

    QString HumanHelpText() {
        return QStringLiteral(R"(Throned command line

  throned --cli <command>

Commands

  status                       what is running, where, and through which route
  servers                      list proxy profiles with their ids
  start <id>                   start a proxy profile
  stop                         stop the running profile

  routes                       list routing profiles
  route                        show the active routing profile and its lists
  route use <id>               make a routing profile active
  route default <where>        traffic that matched no rule: direct, proxy,
                               block or warp
  route rules <on|off>         apply the profile's own rules, or send everything
                               to the default above
  route add <domain>...        route domains, e.g.
                               route add example.com --via proxy
  route remove <domain>...     drop them again

  Add --via proxy|direct|block to choose the list; proxy is the default.
  A bare domain covers its subdomains. Prefixes like ruleset: or processName:
  are passed through untouched.

Options

  --json                       print the raw JSON reply instead of text
  --cli '{"cmd":"..."}'        send a raw command; see --cli '{"cmd":"help"}'
                               for the full machine-facing reference

Routing changes are saved at once and restart the running profile, which
briefly interrupts traffic.
)");
    }

    // The socket is named after the working directory, so an instance started
    // from the install folder and one that fell back to appdata never collide.
    QString LocalServerNameFor(const QDir &dir) {
        QByteArray hashBytes = QCryptographicHash::hash(dir.absolutePath().toUtf8(), QCryptographicHash::Md5)
                                   .toBase64(QByteArray::OmitTrailingEquals);
        hashBytes.replace('+', '0').replace('/', '1');
        return LOCAL_SERVER_PREFIX + QString::fromUtf8(hashBytes);
    }

    int RunControlClient(const QStringList &serverNames, QStringList words) {
        AttachToParentConsole();

        const bool rawJsonOut = words.removeAll(QStringLiteral("--json")) > 0;
        if (words.isEmpty()) words << QStringLiteral("help");

        QJsonObject request;
        // One argument that looks like an object is a raw command; anything else
        // is read as words a person typed.
        const bool rawJsonIn = words.size() == 1 && words.first().trimmed().startsWith('{');
        if (rawJsonIn) {
            QJsonParseError parseError;
            const QJsonDocument parsed = QJsonDocument::fromJson(words.first().toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
                PrintLine(QStringLiteral(R"({"ok":false,"error":"invalid JSON: %1"})").arg(parseError.errorString()));
                return 2;
            }
            request = parsed.object();
        } else if (words.first() == QStringLiteral("help") || words.first() == QStringLiteral("--help")) {
            // Help is answered locally so the reference is readable without a
            // running instance, which is how anyone discovers the rest.
            PrintLine(rawJsonOut ? ThronedControl::HelpText() : HumanHelpText());
            return 0;
        } else {
            QString error;
            if (!ParseHumanCommand(words, &request, &error)) {
                PrintLine(error + QStringLiteral("\nrun: throned --cli help"));
                return 2;
            }
        }

        if (request.value("cmd").toString() == QStringLiteral("help")) {
            PrintLine(ThronedControl::HelpText());
            return 0;
        }
        // Raw JSON in means raw JSON out, so a scripted caller always gets back
        // the same shape it sent, whether or not it passed --json.
        const bool machineOutput = rawJsonOut || rawJsonIn;
        const auto report = [machineOutput](const QString &json, const QString &text) {
            PrintLine(machineOutput ? json : text);
        };

        // An instance may live under the install folder or under appdata, and
        // which one it picked depends on whether the install folder turned out
        // writable. Trying both beats re-deriving that decision, which would
        // mean creating directories a read-only query has no business creating.
        QLocalSocket socket;
        bool connected = false;
        for (const QString &candidate : serverNames) {
            if (candidate.isEmpty()) continue;
            socket.connectToServer(candidate);
            if (socket.waitForConnected(1000)) {
                connected = true;
                break;
            }
            socket.abort();
        }
        if (!connected) {
            report(QStringLiteral(R"({"ok":false,"error":"Throned is not running in this directory"})"),
                   QStringLiteral("throned is not running here; start it first, or run this from its folder"));
            return 3;
        }
        socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
        if (!socket.waitForReadyRead(5000)) {
            report(QStringLiteral(R"({"ok":false,"error":"no answer from Throned"})"),
                   QStringLiteral("throned did not answer; it may be an older build without the control interface"));
            return 4;
        }
        QByteArray reply = socket.readAll();
        while (!reply.contains('\n') && socket.waitForReadyRead(1000)) reply += socket.readAll();
        socket.disconnectFromServer();

        const QString line = QString::fromUtf8(reply).trimmed();
        const QJsonObject answer = QJsonDocument::fromJson(line.toUtf8()).object();
        const bool ok = answer.value("ok").toBool();
        if (machineOutput) {
            PrintLine(line);
        } else if (ok) {
            PrintLine(FormatReply(request.value("cmd").toString(), answer.value("data").toObject()));
        } else {
            PrintLine(QStringLiteral("error: %1").arg(answer.value("error").toString()));
        }
        return ok ? 0 : 1;
    }

    int RunRouteEditorPreview(QApplication &app) {
        QDialog dialog;
        dialog.setObjectName("routeProfileEditor");
        dialog.setWindowTitle(QObject::tr("Throned — Route profile preview"));
        dialog.resize(1120, 700);

        QFont font = app.font();
#ifdef Q_OS_WIN
        font.setFamily(QStringLiteral("Segoe UI Variable Text"));
#endif
        font.setPointSize(10);
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferDefaultHinting);
        dialog.setFont(font);
        themeManager->RegisterStyle(&dialog, RouteProfileSimpleEditor::dialogStyleSheet());

        auto *root = new QVBoxLayout(&dialog);
        root->setContentsMargins(12, 12, 12, 12);
        root->setSpacing(10);
        auto *header = new QFrame;
        header->setObjectName("routeProfileHeader");
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(12, 10, 12, 10);
        auto *nameLayout = new QVBoxLayout;
        auto *nameLabel = new QLabel(QObject::tr("Name"));
        nameLabel->setObjectName("routeFieldLabel");
        auto *name = new QLineEdit(QObject::tr("Development"));
        nameLayout->addWidget(nameLabel);
        nameLayout->addWidget(name);
        headerLayout->addLayout(nameLayout, 1);
        auto *outboundLayout = new QVBoxLayout;
        auto *outboundLabel = new QLabel(QObject::tr("Default outbound"));
        outboundLabel->setObjectName("routeFieldLabel");
        auto *outbound = new QComboBox;
        outbound->setObjectName("def_out");
        outbound->addItems({"direct", "proxy", "block", "warp-bypass"});
        outbound->setMaximumWidth(250);
        outboundLayout->addWidget(outboundLabel);
        outboundLayout->addWidget(outbound);
        headerLayout->addLayout(outboundLayout, 1);
        auto *modeLayout = new QVBoxLayout;
        auto *modeLabel = new QLabel(QObject::tr("Mode"));
        modeLabel->setObjectName("routeFieldLabel");
        auto *mode = new QTabBar;
        mode->setObjectName("routeModeTabs");
        mode->addTab(QObject::tr("Simple"));
        mode->addTab(QObject::tr("Advanced"));
        mode->setUsesScrollButtons(false);
        mode->setExpanding(true);
        mode->setMinimumWidth(220);
        mode->setMaximumWidth(280);
        modeLayout->addWidget(modeLabel);
        modeLayout->addWidget(mode);
        headerLayout->addLayout(modeLayout, 1);
        root->addWidget(header);

        auto *tabs = new QTabWidget;
        auto *editor = new RouteProfileSimpleEditor;
        editor->setRules(0, "domain:updates.example.com\n");
        editor->setRules(1, "domain:ads.example\nip:198.51.100.0/24\n");
        editor->setRules(2,
            "processPath:C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe\n"
            "processName:Discord.exe\ndomain:chatgpt.com\nruleset:geosite-telegram\n"
            "ruleset:geosite-anthropic\nip:172.64.0.0/16\n");
        editor->setRules(3, {});
        editor->setAdvancedRules({QObject::tr("regional-routing"), QObject::tr("fallback-policy")});
        editor->setRuleSetCatalog({
            QStringLiteral("geosite-anthropic"), QStringLiteral("geosite-chatgpt"),
            QStringLiteral("geosite-google"), QStringLiteral("geosite-telegram"),
            QStringLiteral("geoip-cloudflare"), QStringLiteral("geoip-openai"),
            QStringLiteral("geoip-private"), QStringLiteral("geoip-telegram"),
        });
        editor->setLocalProxyTrafficEnabled(true);
        tabs->addTab(editor, QObject::tr("Simple"));
        auto *advanced = new QLabel(QObject::tr("The existing lossless advanced editor remains available here."));
        advanced->setAlignment(Qt::AlignCenter);
        tabs->addTab(advanced, QObject::tr("Advanced"));
        tabs->tabBar()->hide();
        QObject::connect(mode, &QTabBar::currentChanged, tabs, &QTabWidget::setCurrentIndex);
        QObject::connect(tabs, &QTabWidget::currentChanged, mode, &QTabBar::setCurrentIndex);
        root->addWidget(tabs, 1);

        auto *buttons = new QDialogButtonBox;
        auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
        cancel->setObjectName("routeSecondaryButton");
        auto *save = buttons->addButton(QObject::tr("Save profile"), QDialogButtonBox::AcceptRole);
        save->setObjectName("routeSaveButton");
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        root->addWidget(buttons);
        dialog.show();
        return app.exec();
    }
} // namespace


int main(int argc, char* argv[]) {
    Logging::InstallQtMessageHandler();

    // Core dump
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif
#ifdef Q_OS_LINUX
    enable_core_dumps();
#endif

    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication a(argc, argv);

    if (a.arguments().contains(QStringLiteral("--route-editor-preview"))) {
        return RunRouteEditorPreview(a);
    }

#ifdef Q_OS_MACOS
    // Install before the event loop so launch-by-deeplink FileOpen events are caught.
    a.installEventFilter(new MacOpenEventFilter(&a));
#endif

#if !defined(Q_OS_MACOS) && (QT_VERSION >= QT_VERSION_CHECK(6,9,0))
    // Load the emoji fonts
#ifdef Q_OS_WIN
    int fontId = QFontDatabase::addApplicationFont(WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_11_22H2) ? ":/font/notoEmoji" : ":/font/Twemoji");
#else
    int fontId = QFontDatabase::addApplicationFont(":/font/notoEmoji");
#endif
    if (fontId >= 0)
    {
        QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
        QFontDatabase::setApplicationEmojiFontFamilies(fontFamilies);
    } else
    {
        qDebug() << "could not load emoji font!";
    }
#endif

    QStringList arguments = QApplication::arguments();
    // A throne:// URL may be passed as a launch argument (Windows/Linux), and so may
    // config files opened with the app. Both are delivered after the window is up, or
    // forwarded to the primary instance via the socket below. Files are resolved
    // before the working directory moves, since their paths may be relative to it.
    const QString launchDeeplink = Deeplink_ExtractFromArgs(arguments);
    const QStringList launchFiles = LaunchFiles_ExtractFromArgs(arguments, QDir::current());

    // Clean
    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }

    // dirs & clean
    auto wd = QDir(QApplication::applicationDirPath());
    bool useAppdata = false;
    QString appdataDir;
    if (arguments.contains("-appdata")) {
        useAppdata = true;
        int appdataIndex = arguments.indexOf("-appdata");
        if (arguments.size() > appdataIndex + 1 && !arguments.at(appdataIndex + 1).startsWith("-")) {
            appdataDir = arguments.at(appdataIndex + 1);
        }
    }
#ifdef NKR_CPP_USE_APPDATA
    useAppdata = true; // Example: Package & MacOS
#endif
    QApplication::setApplicationName("Throned");

    // Control mode talks to a running instance and exits. It has to happen
    // before the config directory is resolved, because that step creates
    // directories and copies a legacy Throne profile into them - which a
    // read-only query run from an arbitrary folder must never do.
    if (const int cliAt = arguments.indexOf(QStringLiteral("--cli")); cliAt >= 0) {
        const QDir appdataWd(appdataDir.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : appdataDir);
        QStringList candidates{LocalServerNameFor(useAppdata ? appdataWd : wd)};
        if (!useAppdata) candidates << LocalServerNameFor(appdataWd);
        return RunControlClient(candidates, arguments.mid(cliAt + 1));
    }

    if(useAppdata) {
        if (!appdataDir.isEmpty()) {
            wd.setPath(appdataDir);
        } else {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
        MigrateLegacyConfigIfNeeded(wd);
    } else {
        const QDir userWd(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        MigrateLegacyConfigIfNeeded(wd);
        if (AdoptUserConfigDir(wd, userWd)) {
            wd = userWd;
            useAppdata = true;
            MigrateLegacyConfigIfNeeded(wd);
        }
    }
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    if (!wd.exists("config")) wd.mkdir("config");
    const QString configDir = wd.absoluteFilePath("config");
    QDir::setCurrent(configDir);
    QDir("temp").removeRecursively();

    // Record app start for the Runtime Stats uptime readout.
    appStartEpoch = QDateTime::currentSecsSinceEpoch();

    // Load database
    Configs::initDB(QString(QDir::currentPath() + QDir::separator() + "throne.db").toStdString());

    Logging::SetLevel(Logging::LevelFromString(Configs::dataManager->settingsRepo->log_file_level));

    // Start traffic-statistics maintenance (startup downsample + background rollup).
    Stats::trafficStatsManager->Init();

    // Store Flags
    Configs::dataManager->settingsRepo->argv = arguments;
    if (Configs::dataManager->settingsRepo->argv.contains("-many")) Configs::dataManager->settingsRepo->flag_many = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-tray")) Configs::dataManager->settingsRepo->flag_tray = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-debug")) Configs::dataManager->settingsRepo->flag_debug = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_tun_on")) Configs::dataManager->settingsRepo->flag_restart_tun_on = true;
    if (Configs::dataManager->settingsRepo->argv.contains("-flag_restart_dns_set")) Configs::dataManager->settingsRepo->flag_dns_set = true;
    Configs::dataManager->settingsRepo->flag_use_appdata = useAppdata;
    if(useAppdata && !appdataDir.isEmpty()) Configs::dataManager->settingsRepo->appdataDir = appdataDir;
#ifdef NKR_CPP_DEBUG
    Configs::dataManager->settingsRepo->flag_debug = true;
#endif

#ifdef Q_OS_LINUX
    QApplication::addLibraryPath(QApplication::applicationDirPath() + "/usr/plugins");
#endif

    // dispatchers
    DS_cores = new QThread;
    DS_cores->start();

    LogThread = new QThread;
    LogThread->start();

// icons
    QIcon::setFallbackSearchPaths(QStringList{
        ":/icon",
    });

    // icon for no theme
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

#ifdef Q_OS_WIN
    if (Configs::dataManager->settingsRepo->windows_set_admin && !Configs::IsAdmin() && !Configs::dataManager->settingsRepo->disable_run_admin)
    {
        Configs::dataManager->settingsRepo->windows_set_admin = false; // so that if permission denied, we will run as user on the next run
        Configs::dataManager->settingsRepo->Save();
        WinCommander::runProcessElevated(QApplication::applicationFilePath(), {}, "", 1, false);
        QApplication::quit();
        return 0;
    }
#endif

    // dataManager->settingsRepo & Flags
    if (Configs::dataManager->settingsRepo->start_minimal) Configs::dataManager->settingsRepo->flag_tray = true;

    // Translate
    QString locale;
    switch (Configs::dataManager->settingsRepo->language) {
        case 1: // English
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR"; // farsi(iran)
            break;
        case 4:
            locale = "ru_RU"; // Russian
            break;
        default:
            locale = QLocale().name();
    }
    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);

    // Check if another instance is running
    auto serverName = LocalServerNameFor(wd);
    qDebug() << "server name: " << serverName;

    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (socket.waitForConnected(250))
    {
        qDebug() << "Another instance is running, let's wake it up and quit";
        // Hand off whatever we were launched with so the primary instance handles it:
        // one item per line, a throne:// url or a file:// url. Paths go over as urls
        // so that a name containing a newline cannot break the framing.
        QStringList payload;
        if (!launchDeeplink.isEmpty()) payload << launchDeeplink;
        for (const auto &file : launchFiles) payload << QUrl::fromLocalFile(file).toString();
        if (!payload.isEmpty()) {
            socket.write(payload.join('\n').toUtf8());
            socket.flush();
            socket.waitForBytesWritten(250);
        }
        socket.disconnectFromServer();
        return 0;
    }

    // Must follow the single-instance check: opening the log earlier truncates
    // the running instance's file and leaves a marker it would report as a crash.
    Logging::Init(configDir);
    LOG_INFO(QString("appdata mode: %1").arg(useAppdata ? "yes" : "no"));
#ifdef Q_OS_WIN
    Windows_SetCrashDumpPath();
    Windows_ConfigureWER();
#endif

    // QLocalServer
    QLocalServer server(qApp);
    // The socket now accepts commands that rewrite routing, so it is restricted
    // to this user instead of every process on the machine.
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(serverName)) {
        qWarning() << "Failed to start QLocalServer! Error:" << server.errorString();
        Logging::Shutdown();
        return 1;
    }
    QObject::connect(&server, &QLocalServer::newConnection, qApp, [&] {
        auto s = server.nextPendingConnection();
        qDebug() << "Another instance tried to wake us up on " << serverName << s;
        // The waking instance may forward deeplinks and opened files as payload, one
        // url per line. Only whole lines are handled as they arrive; the tail, which
        // carries no trailing newline, is flushed once the peer is done.
        auto pending = std::make_shared<QByteArray>();
        // A control client is not a user asking for the window; only a second
        // launch or a forwarded deeplink should bring it to the front.
        auto isControlClient = std::make_shared<bool>(false);
        auto handleLine = [s, isControlClient](const QString &line) {
            // A control command is a JSON object and expects an answer; the older
            // deeplink/file payload is fire-and-forget and stays as it was.
            if (line.startsWith('{')) {
                *isControlClient = true;
                QJsonParseError parseError;
                const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
                QJsonObject reply;
                if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                    reply = QJsonObject{{"ok", false},
                                        {"error", QStringLiteral("invalid JSON: %1").arg(parseError.errorString())}};
                } else {
                    reply = ThronedControl::Execute(doc.object());
                }
                s->write(QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n');
                s->flush();
                return;
            }
            if (line.startsWith("throne://")) {
                Deeplink_Submit(line);
            } else if (line.startsWith("file://")) {
                LaunchFiles_Submit({QUrl(line).toLocalFile()});
            }
        };
        auto readPayload = [s, pending, handleLine](bool last) {
            pending->append(s->readAll());
            while (true) {
                const auto at = pending->indexOf('\n');
                if (at < 0) break;
                handleLine(QString::fromUtf8(pending->first(at)).trimmed());
                pending->remove(0, at + 1);
            }
            if (last) {
                handleLine(QString::fromUtf8(*pending).trimmed());
                pending->clear();
            }
        };
        QObject::connect(s, &QLocalSocket::readyRead, s, [readPayload] { readPayload(false); });
        QObject::connect(s, &QLocalSocket::disconnected, s, [readPayload] { readPayload(true); });
        QObject::connect(s, &QLocalSocket::disconnected, s, &QLocalSocket::deleteLater);
        readPayload(false); // in case the payload already arrived
        // Raise on the next turn, once the first line has told us who the peer is.
        QTimer::singleShot(0, qApp, [isControlClient] {
            if (!*isControlClient) MW_dialog_message(MwMessage::Raise, {});
        });
    });
    QObject::connect(qApp, &QApplication::aboutToQuit, [&]
    {
        server.close();
        QLocalServer::removeServer(serverName);
        // Every quit path lands here; missing it is reported as a crash next start.
        Logging::Shutdown();
    });

#ifdef Q_OS_LINUX
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
#endif

#ifdef Q_OS_WIN
    auto eventFilter = new PowerOffTaskkillFilter(signal_handler);
    a.installNativeEventFilter(eventFilter);
#endif

#ifdef Q_OS_MACOS
    QObject::connect(qApp, &QGuiApplication::commitDataRequest, [&](QSessionManager &manager)
    {
        Q_UNUSED(manager);
        signal_handler(0);
    });
#endif

    API::defaultClient = new API::Client();

    // Establish the readable production font before any redesigned widget or
    // stylesheet is created. Appearance changes use the same path at runtime.
    QFont appFont = a.font();
    if (!Configs::dataManager->settingsRepo->font.isEmpty()) {
        appFont.setFamily(Configs::dataManager->settingsRepo->font);
    }
#ifdef Q_OS_WIN
    else {
        appFont.setFamily(QStringLiteral("Segoe UI Variable Text"));
    }
#endif
    appFont.setPointSize(Configs::dataManager->settingsRepo->font_size > 0
        ? Configs::dataManager->settingsRepo->font_size : 10);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    appFont.setHintingPreference(QFont::PreferDefaultHinting);
    a.setFont(appFont);

    UI_InitMainWindow();

    Configs::dataManager->RunDeferredMaintenance();

    if (Logging::PreviousSessionCrashed()) {
        MW_show_log(QObject::tr("[Warn] Throned did not shut down cleanly last time. "
                                "Diagnostics were saved to: %1").arg(Logging::LogDir()));
    }

    // Deliver a deeplink and any files passed on the command line (cold start), then
    // replay whatever arrived during startup (e.g. a macOS FileOpen event before the
    // window existed).
    if (!launchDeeplink.isEmpty()) Deeplink_Submit(launchDeeplink);
    Deeplink_FlushPending();
    LaunchFiles_Submit(launchFiles);
    LaunchFiles_FlushPending();

    return QApplication::exec();
}
