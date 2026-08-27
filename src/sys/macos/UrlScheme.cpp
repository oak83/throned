#include "include/sys/UrlScheme.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>

// Registration is declarative (Info.plist + LaunchServices), but a moved bundle leaves a stale path, hence the forced `lsregister -f`.

static const QString kLsregister =
    "/System/Library/Frameworks/CoreServices.framework/Frameworks/"
    "LaunchServices.framework/Support/lsregister";

// applicationDirPath() is <Bundle>.app/Contents/MacOS; the bundle is two up.
static QString bundlePath() {
    QDir d(QCoreApplication::applicationDirPath());
    if (!d.cdUp() || !d.cdUp()) return {};
    const QString path = d.absolutePath();
    return path.endsWith(".app") ? path : QString();
}

QString UrlScheme_DesiredState() {
    const QString bundle = bundlePath();
    return bundle.isEmpty() ? QString() : "v2|" + bundle;
}

void UrlScheme_Apply() {
    const QString bundle = bundlePath();
    if (bundle.isEmpty()) return;
    QProcess::execute(kLsregister, {"-f", bundle});
}
