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

// AppImage: point at the outer image ($APPIMAGE), not the extracted binary, which disappears after exit.
static QString execTarget() {
    auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains("APPIMAGE")) return env.value("APPIMAGE");
    return QApplication::applicationFilePath();
}

static QString desktopFilePath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    return dir + "/" + kDesktopId;
}

// "throned" is in no icon theme for the /opt and AppImage layouts, so unpack a
// copy and reference it by absolute path. The resource path is the fork's own:
// upstream's :/Throne/Throne.png does not exist here, and a failed copy would
// silently leave the entry pointing at an icon name nothing resolves.
static QString iconTarget() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString path = dir + "/throned.png";
    QDir().mkpath(dir);
    QFile::remove(path);
    return QFile::copy(":/Throned.png", path) ? path : QStringLiteral("throned");
}

QString UrlScheme_DesiredState() {
    return "v3|" + execTarget();
}

void UrlScheme_Apply() {
    const QString path = desktopFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << "[Desktop Entry]\n"
           << "Type=Application\n"
           << "Name=Throned\n"
           << "Icon=" << iconTarget() << "\n"
           << "Exec=\"" << execTarget() << "\" %U\n"
           << "MimeType=x-scheme-handler/throne;application/json;application/yaml;text/yaml;text/plain;\n"
           << "Terminal=false\n"
           << "NoDisplay=true\n";
        ts.flush();
        f.close();
    }

    // Both tools may be absent on minimal systems; execute() just returns nonzero then.
    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QProcess::execute("update-desktop-database", {appsDir});
    QProcess::execute("xdg-mime", {"default", kDesktopId, "x-scheme-handler/throne"});
}
