#pragma once

// Headers that are both heavy and included nearly everywhere. Precompiling them is
// what keeps a full build tolerable, but it force-includes them into every C++ unit,
// so a file that forgets one of these still compiles here. Build with
// -DTHRONED_PRECOMPILE_HEADERS=OFF (and unity off) when that has to be caught.
//
// Keep this list short: a header added here costs every translation unit, whether or
// not it is used.

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>
