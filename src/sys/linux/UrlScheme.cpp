#include "include/sys/UrlScheme.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

static const QString kDesktopId = "throned-url-handler.desktop";

// For AppImage the launcher must point at the outer image ($APPIMAGE), not the
// extracted binary inside the mount, which disappears after exit.
static QString execTarget() {
    auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains("APPIMAGE")) return env.value("APPIMAGE");
    return QApplication::applicationFilePath();
}

static QString desktopFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    return dir + "/" + kDesktopId;
}

QString UrlScheme_DesiredState() {
    return "v2|" + execTarget();
}

void UrlScheme_Apply() {
    const QString path = desktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        // The config mime types are declared but never claimed as a default, which
        // puts Throne in the file manager's "Open With" list without taking .json
        // away from whatever the user reads it with. That list skips NoDisplay
        // entries, so the entry has to be a visible one.
        ts << "[Desktop Entry]\n"
           << "Type=Application\n"
           << "Name=Throned\n"
           << "Icon=throned\n"
           << "Exec=\"" << execTarget() << "\" %U\n"
           << "MimeType=x-scheme-handler/throne;application/json;application/yaml;text/yaml;text/plain;\n"
           << "Terminal=false\n";
        ts.flush();
        f.close();
    }

    // Refresh the desktop database and set us as the default handler. Both tools
    // may be absent on minimal systems; execute() just returns nonzero then.
    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QProcess::execute("update-desktop-database", {appsDir});
    QProcess::execute("xdg-mime", {"default", kDesktopId, "x-scheme-handler/throne"});
}
