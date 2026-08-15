#include "include/ui/mainwindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHeaderView>
#include <QHostAddress>
#include <QMenu>
#include <QTableWidget>
#include <QTimer>
#include <QToolTip>
#include <memory>

#include "include/database/RoutesRepo.h"
#include "include/database/DatabaseManager.h"

void MainWindow::setupConnectionList()
{
    ui->connections->horizontalHeader()->setHighlightSections(false);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    ui->connections->verticalHeader()->hide();
    ui->connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->connections, &QWidget::customContextMenuRequested, this, &MainWindow::showConnectionMenu);
    setupConnectionSortMenu();
    connect(ui->connections, &QTableWidget::cellClicked, this, [=,this](int row, int column)
    {
        auto selected = ui->connections->item(row, column);
        if (selected == nullptr) return;
        QApplication::clipboard()->setText(selected->text());
        QPoint pos = ui->connections->mapToGlobal(ui->connections->visualItemRect(selected).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });
}

namespace {
    // The rule this connection would need, one candidate per column of evidence
    // the row carries. Ordered widest-blast-radius last so the safest option is
    // the first thing under the cursor.
    struct RuleCandidate {
        QString label;
        QString entry;
    };

    QList<RuleCandidate> candidatesFor(const QString &dest, const QString &domain,
                                       const QString &process, const QString &processPath) {
        QList<RuleCandidate> candidates;
        const QString host = domain.isEmpty() ? QString() : domain;
        if (!host.isEmpty()) {
            candidates.append({MainWindow::tr("This domain — %1").arg(host),
                               QStringLiteral("domain:") + host});
            // A bare host as a suffix also covers its subdomains, which is what
            // "route this site" nearly always means.
            candidates.append({MainWindow::tr("Domain and subdomains — *.%1").arg(host),
                               QStringLiteral("suffix:") + host});
        }
        if (!process.isEmpty())
            candidates.append({MainWindow::tr("This process — %1").arg(process),
                               QStringLiteral("processName:") + process});
        if (!processPath.isEmpty())
            candidates.append({MainWindow::tr("This executable — %1").arg(QFileInfo(processPath).fileName()),
                               QStringLiteral("processPath:") + processPath});
        // dest is host:port, and a port makes a poor routing rule on its own.
        const QString address = dest.contains(QLatin1Char(']'))
            ? dest.section(QLatin1Char(']'), 0, 0).mid(1)
            : dest.section(QLatin1Char(':'), 0, 0);
        if (!address.isEmpty() && !QHostAddress(address).isNull())
            candidates.append({MainWindow::tr("This address — %1").arg(address),
                               QStringLiteral("ip:") + address});
        return candidates;
    }

    struct RuleTarget {
        Configs::simpleAction action;
        QString label;
    };

    QList<RuleTarget> ruleTargets() {
        return {
            {Configs::proxy, MainWindow::tr("Through proxy")},
            {Configs::bypass, MainWindow::tr("Directly")},
            {Configs::block, MainWindow::tr("Block")},
        };
    }
} // namespace

// Where an entry already sits in the active profile, so the menu can say so
// instead of silently adding a second copy.
QString MainWindow::existingRuleAction(const QString &entry) const
{
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
    if (!profile || profile->isRaw) return {};
    for (const auto &target : ruleTargets())
        if (profile->GetSimpleRules(target.action).split('\n', Qt::SkipEmptyParts).contains(entry))
            return target.label;
    return {};
}

// Append one simple rule to the active routing profile and put it into effect.
// Same path the quick menu uses: a routing change only reaches the core when the
// running profile is regenerated.
void MainWindow::addRuleFromConnection(const QString &entry, int action)
{
    auto profile = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
    if (!profile) {
        MessageBoxWarning(tr("No routing profile"), tr("There is no active routing profile to add the rule to."));
        return;
    }
    if (profile->isRaw) {
        MessageBoxWarning(tr("Raw routing profile"),
                          tr("“%1” is a raw profile and is edited as JSON, so a rule cannot be appended to it here.")
                              .arg(profile->name));
        return;
    }
    const auto simple = static_cast<Configs::simpleAction>(action);
    QStringList current = profile->GetSimpleRules(simple).split('\n', Qt::SkipEmptyParts);
    if (current.contains(entry)) return;
    current << entry;
    if (const QString error = profile->UpdateSimpleRules(current.join('\n'), simple); !error.isEmpty()) {
        MessageBoxWarning(tr("Rule not added"), error);
        return;
    }
    Configs::dataManager->routesRepo->Save(profile);
    refreshRoutingStatus();
    if (Configs::dataManager->settingsRepo->started_id >= 0)
        profile_start(Configs::dataManager->settingsRepo->started_id);
}

void MainWindow::showConnectionMenu(const QPoint &pos)
{
    const int row = ui->connections->rowAt(pos.y());
    if (row < 0) return;
    const auto *anchor = ui->connections->item(row, 0);
    if (anchor == nullptr) return;
    const QString dest = anchor->data(Stats::DESTKEY).toString();
    const QString domain = anchor->data(Stats::DOMAINKEY).toString();
    const QString process = anchor->data(Stats::PROCESSKEY).toString();
    const QString processPath = anchor->data(Stats::PROCESSPATHKEY).toString();
    const QString outbound = anchor->data(Stats::OUTBOUNDKEY).toString();

    QMenu menu(this);
    // The verdict this connection actually got. The table already answers
    // "where does this go" - the menu just makes it the subject of the action.
    auto *verdict = menu.addAction(tr("%1 → %2")
        .arg(domain.isEmpty() ? dest : domain, outbound.isEmpty() ? tr("unknown") : outbound));
    verdict->setEnabled(false);
    menu.addSeparator();

    const auto candidates = candidatesFor(dest, domain, process, processPath);
    if (candidates.isEmpty()) {
        auto *none = menu.addAction(tr("Nothing to build a rule from"));
        none->setEnabled(false);
    }
    for (const auto &candidate : candidates) {
        const QString already = existingRuleAction(candidate.entry);
        auto *submenu = menu.addMenu(already.isEmpty()
            ? candidate.label
            : tr("%1  ·  already %2").arg(candidate.label, already.toLower()));
        submenu->setToolTip(candidate.entry);
        for (const auto &target : ruleTargets()) {
            auto *action = submenu->addAction(target.label);
            const QString entry = candidate.entry;
            const int simple = target.action;
            connect(action, &QAction::triggered, this, [this, entry, simple] {
                addRuleFromConnection(entry, simple);
            });
        }
    }

    menu.addSeparator();
    if (!processPath.isEmpty()) {
        auto *copyPath = menu.addAction(tr("Copy executable path"));
        connect(copyPath, &QAction::triggered, this, [processPath] {
            QApplication::clipboard()->setText(processPath);
        });
    }
    auto *copyDest = menu.addAction(tr("Copy destination"));
    connect(copyDest, &QAction::triggered, this, [dest, domain] {
        QApplication::clipboard()->setText(domain.isEmpty() ? dest : domain);
    });
    menu.exec(ui->connections->viewport()->mapToGlobal(pos));
}

// Right-click the Traffic / Speed headers to pick the sub-field they sort by;
// left-clicking still sorts by total.
void MainWindow::setupConnectionSortMenu()
{
    auto* header = ui->connections->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [=,this](const QPoint& pos)
    {
        const int columnIndex = header->logicalIndexAt(pos);
        const bool isTraffic = columnIndex == 4;
        const bool isSpeed = columnIndex == 5;
        if (!isTraffic && !isSpeed) return;

        struct SortOption { Stats::ConnectionSort value; QString label; };
        const QList<SortOption> options = isTraffic
            ? QList<SortOption>{
                { Stats::ByTraffic, tr("Total") },
                { Stats::ByDownload, tr("Downloaded") },
                { Stats::ByUpload, tr("Uploaded") } }
            : QList<SortOption>{
                { Stats::BySpeed, tr("Total") },
                { Stats::ByDownloadSpeed, tr("Download Speed") },
                { Stats::ByUploadSpeed, tr("Upload Speed") } };

        QMenu menu(this);
        auto* sortByLabel = menu.addAction(tr("Sort By:"));
        sortByLabel->setEnabled(false);

        const auto current = Stats::connection_lister->getSort();
        for (const auto& opt : options)
        {
            auto* act = menu.addAction(opt.label);
            act->setData(static_cast<int>(opt.value));
            act->setCheckable(true);
            act->setChecked(current == opt.value);
        }

        auto* chosen = menu.exec(header->mapToGlobal(pos));
        if (chosen == nullptr || !chosen->data().isValid()) return;

        Stats::connection_lister->setSort(static_cast<Stats::ConnectionSort>(chosen->data().toInt()));
        Stats::connection_lister->ForceUpdate();
    });
}

void MainWindow::UpdateConnectionList(const QMap<QString, Stats::ConnectionMetadata>& toUpdate, const QMap<QString, Stats::ConnectionMetadata>& toAdd)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    for (int row=0;row<ui->connections->rowCount();row++)
    {
        const auto key = ui->connections->item(row, 0)->data(Stats::IDKEY).toString();
        if (!toUpdate.contains(key))
        {
            ui->connections->removeRow(row);
            row--;
            continue;
        }

        const auto conn = toUpdate[key];
        // C0: Dest (Domain)
        ui->connections->item(row, 0)->setText(DisplayDest(conn.dest, conn.domain));

        // C1: Process
        ui->connections->item(row, 1)->setText(conn.process);

        // C2: Protocol
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        ui->connections->item(row, 2)->setText(prot);

        // C3: Outbound
        ui->connections->item(row, 3)->setText(conn.outbound);
        // The verdict is what the row is asked about, and it can change while
        // the connection is open.
        ui->connections->item(row, 0)->setData(Stats::OUTBOUNDKEY, conn.outbound);
        ui->connections->item(row, 0)->setData(Stats::DOMAINKEY, conn.domain);

        // C4: Traffic
        ui->connections->item(row, 4)->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");

        // C5: Speed
        ui->connections->item(row, 5)->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
    }
    int row = ui->connections->rowCount();
    for (const auto& conn : toAdd)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);
        f0->setData(Stats::DESTKEY, conn.dest);
        f0->setData(Stats::DOMAINKEY, conn.domain);
        f0->setData(Stats::PROCESSKEY, conn.process);
        f0->setData(Stats::PROCESSPATHKEY, conn.processPath);
        f0->setData(Stats::OUTBOUNDKEY, conn.outbound);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        // C5: Speed
        f = f0->clone();
        f->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
        ui->connections->setItem(row, 5, f);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

void MainWindow::UpdateConnectionListWithRecreate(const QList<Stats::ConnectionMetadata>& connections)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    ui->connections->setRowCount(0);
    int row=0;
    for (const auto& conn : connections)
    {
        ui->connections->insertRow(row);
        auto f0 = std::make_unique<QTableWidgetItem>();
        f0->setData(Stats::IDKEY, conn.id);
        f0->setData(Stats::DESTKEY, conn.dest);
        f0->setData(Stats::DOMAINKEY, conn.domain);
        f0->setData(Stats::PROCESSKEY, conn.process);
        f0->setData(Stats::PROCESSPATHKEY, conn.processPath);
        f0->setData(Stats::OUTBOUNDKEY, conn.outbound);

        // C0: Dest (Domain)
        auto f = f0->clone();
        f->setText(DisplayDest(conn.dest, conn.domain));
        ui->connections->setItem(row, 0, f);

        // C1: Process
        f = f0->clone();
        f->setText(conn.process);
        ui->connections->setItem(row, 1, f);

        // C2: Protocol
        f = f0->clone();
        auto prot = conn.network;
        if (!conn.protocol.isEmpty()) prot += " ("+conn.protocol+")";
        f->setText(prot);
        ui->connections->setItem(row, 2, f);

        // C3: Outbound
        f = f0->clone();
        f->setText(conn.outbound);
        ui->connections->setItem(row, 3, f);

        // C4: Traffic
        f = f0->clone();
        f->setText(ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓");
        ui->connections->setItem(row, 4, f);

        // C5: Speed
        f = f0->clone();
        f->setText(ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓");
        ui->connections->setItem(row, 5, f);

        row++;
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}
