#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>

// The one place every externally driven operation lives. The CLI speaks to the
// running instance through it, and the UI installs the few hooks it cannot
// provide from the config layer alone (starting a profile, restarting the core
// after a routing change).
//
// Keeping the surface here rather than spreading lambdas over MainWindow means
// the operation set is explicit and reviewable, and it is the natural contract
// to port if the backend ever moves out of C++.
namespace ThronedControl {

struct Hooks {
    std::function<void(int id)> startProfile;
    std::function<void()>       stopProfile;
    // Persist-and-apply: restart the running profile so a routing edit takes
    // effect, and refresh whatever the window shows about it.
    std::function<void()>       applyRoutingChange;
    std::function<int()>        runningProfileId;
};

inline Hooks hooks;

// Runs one command. Must be called on the UI thread. Never throws: failures come
// back as {"ok":false,"error":"..."} so a caller can always parse the answer.
QJsonObject Execute(const QJsonObject &request);

// The command reference, as plain text. Printed by `--cli help`, and written to
// be read by an agent: every command, its arguments and its result shape.
QString HelpText();

} // namespace ThronedControl
