#include "include/sys/UrlScheme.hpp"

#include <QApplication>
#include <QDir>
#include <QSettings>

#include <shlobj.h>

// Per-user registration under HKCU\Software\Classes — needs no admin and takes
// precedence over any system-wide handler. In QSettings NativeFormat the value
// name "Default" maps to a registry key's unnamed (Default) value, and '/'
// separates subkeys.

static const QString kClasses = "HKEY_CURRENT_USER\\Software\\Classes";
static const QString kProgId = "Throned.Config";
static const QString kLegacyProgId = "Throne.Config";
// The key is deliberately stable. The actual executable filename is allowed to
// change for portable builds (for example, Throned-1.3.7-test.exe), but Windows
// should still see one logical application in every "Open with" dialog.
static const QString kApplicationKey = "Throned.exe";
static const QString kLegacyApplicationKey = "Throne.exe";

// Extensions config files usually arrive with. Registering these only adds
// Throned to the "Open with" list; the extension keeps whatever default it has.
static const QStringList kConfigExtensions = {".json", ".conf", ".yaml", ".yml", ".ini", ".txt"};

static QString openCommand() {
    return "\"" + QDir::toNativeSeparators(QApplication::applicationFilePath()) + "\" \"%1\"";
}

QString UrlScheme_DesiredState() {
    return "v3|" + openCommand();
}

static bool isLegacyApplicationKey(const QString &key) {
    return key.compare(kLegacyApplicationKey, Qt::CaseInsensitive) == 0 ||
           ((key.startsWith("Throned-", Qt::CaseInsensitive) || key.startsWith("Throne-", Qt::CaseInsensitive)) &&
            key.endsWith(".exe", Qt::CaseInsensitive));
}

static bool isOurLegacyApplication(const QString &key, QSettings &app) {
    const QString friendlyName = app.value("FriendlyAppName").toString();
    if (friendlyName.compare("Throned", Qt::CaseInsensitive) == 0) return true;
    return isLegacyApplicationKey(key) && friendlyName.compare("Throne", Qt::CaseInsensitive) == 0;
}

static void removeLegacyRegistrations() {
    // Before the application key was made stable, every renamed portable exe
    // created another Applications\\<exe> entry. Remove only entries written by
    // our previous registration code; unrelated applications are left intact.
    QSettings applications(kClasses + "\\Applications", QSettings::NativeFormat);
    for (const QString &key : applications.childGroups()) {
        if (key.compare(kApplicationKey, Qt::CaseInsensitive) == 0 || !isLegacyApplicationKey(key)) continue;

        QSettings app(kClasses + "\\Applications\\" + key, QSettings::NativeFormat);
        if (isOurLegacyApplication(key, app)) {
            app.remove("");
            app.sync();
        }
    }

    // Also remove the old product name left by builds from before the rename.
    QSettings legacyProgId(kClasses + "\\" + kLegacyProgId, QSettings::NativeFormat);
    legacyProgId.remove("");
    legacyProgId.sync();

    for (const QString &ext : kConfigExtensions) {
        QSettings assoc(kClasses + "\\" + ext + "\\OpenWithProgids", QSettings::NativeFormat);
        assoc.remove(kLegacyProgId);
        assoc.sync();
    }
}

void UrlScheme_Apply() {
    const QString command = openCommand();
    const QString exe = QDir::toNativeSeparators(QApplication::applicationFilePath());

    removeLegacyRegistrations();

    QSettings scheme(kClasses + "\\throne", QSettings::NativeFormat);
    scheme.setValue("Default", "URL:Throned Protocol");
    scheme.setValue("URL Protocol", "");
    scheme.setValue("shell/open/command/Default", command);

    QSettings progId(kClasses + "\\" + kProgId, QSettings::NativeFormat);
    progId.setValue("Default", "Throned profile");
    progId.setValue("DefaultIcon/Default", exe + ",0");
    progId.setValue("shell/open/command/Default", command);

    // OpenWithProgids is the additive half of an association: the extension lists
    // us as one possible handler, its default (HKCU\...\<ext>\Default) is left alone.
    for (const QString &ext : kConfigExtensions) {
        QSettings assoc(kClasses + "\\" + ext + "\\OpenWithProgids", QSettings::NativeFormat);
        assoc.setValue(kProgId, "");
    }

    // Applications\Throned.exe is what "Open with > Choose another app" reads
    // for a file whose extension has no explicit association. Keep this key
    // stable even when the portable executable itself has a test-specific name.
    QSettings app(kClasses + "\\Applications\\" + kApplicationKey, QSettings::NativeFormat);
    app.setValue("FriendlyAppName", "Throned");
    app.setValue("DefaultIcon/Default", exe + ",0");
    app.setValue("shell/open/command/Default", command);
    for (const QString &ext : kConfigExtensions) {
        app.setValue("SupportedTypes/" + ext, "");
    }

    // QSettings only reaches the registry on sync, and the shell caches association
    // data until told otherwise, so flush before announcing the change.
    scheme.sync();
    progId.sync();
    app.sync();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
