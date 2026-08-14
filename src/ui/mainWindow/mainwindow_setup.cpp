#include "include/ui/mainwindow.h"

#include "include/ui/mainWindow/MainWindowInternal.h"
// Full definition: MainWindow's destructor lives here and destroys the unique_ptr.
#include "include/ui/mainWindow/TestRunner.h"

#include <QMenu>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/configs/sub/RouteUpdater.hpp"
#include "include/global/PeriodicRunner.hpp"
#include "include/global/Logger.hpp"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/ui/stats/dialog_auto_selector.h"
#include "include/sys/Process.hpp"
#include "include/sys/AutoRun.hpp"
#include "include/sys/UrlScheme.hpp"

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/stats/dialog_traffic_stats.h"
#include "include/ui/stats/dialog_runtime_stats.h"
#include "include/ui/widget/StartStopButton.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include <QPainter>
#include "include/ui/widget/ThronedToggle.h"
#include "include/ui/widget/ThronedWindowResizer.h"
#include "include/control/ThronedControl.h"

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/global/Common.h"

#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/ProfilesTableModel.h"

#include "include/ui/group/dialog_edit_group.h"

#ifdef Q_OS_WIN
#include <windows.h>
// <windows.h> pulls in winspool.h's `#define SetPort SetPortW`, which under unity
// builds clobbers Configs::outbound::SetPort in sibling files. Drop it.
#undef SetPort
#else
#ifdef Q_OS_LINUX
#include <QDBusInterface>
#include <QDBusReply>
#include <sys/socket.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/socket.h>
#include <sys/un.h>
#endif
#include <unistd.h>
#endif

#include <QUuid>

#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QPalette>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QScrollBar>
#include <QDesktopServices>
#include <QTimer>
#include <QMessageBox>
#include <QDir>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif
#include <QFileDialog>
#include <QToolButton>
#include <QTabBar>
#include <QVBoxLayout>
#include <QHeaderView>
#include <include/global/HTTPRequestHelper.hpp>
#include "include/global/DeviceDetailsHelper.hpp"

void UI_InitMainWindow() {
    mainwindow = new MainWindow;
}

// Caller must hold coreProcessMutex (reads core_process lock-free by design).
bool MainWindow::verify_core_pid(QLocalSocket *socket) {
    if (!core_process) return false;
    qint64 expectedPid = core_process->processId();
    if (expectedPid <= 0) return false;

#if defined(Q_OS_LINUX)
    struct ucred cred = {};
    socklen_t credLen = sizeof(cred);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_SOCKET, SO_PEERCRED, &cred, &credLen) == 0) {
        return static_cast<qint64>(cred.pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_MACOS)
    pid_t pid = 0;
    socklen_t pidLen = sizeof(pid);
    if (getsockopt(static_cast<int>(socket->socketDescriptor()), SOL_LOCAL, LOCAL_PEERPID, &pid, &pidLen) == 0) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#elif defined(Q_OS_WIN)
    ULONG pid = 0;
    HANDLE hPipe = reinterpret_cast<HANDLE>(static_cast<qintptr>(socket->socketDescriptor()));
    if (GetNamedPipeClientProcessId(hPipe, &pid)) {
        return static_cast<qint64>(pid) == expectedPid;
    }
    return false;
#else
    Q_UNUSED(socket)
    return true;
#endif
}

// Maps a theme name to the log viewer's syntax-highlight mode (true = dark, false = light).
// Stylesheet themes have a known brightness; plain QStyle themes follow the OS preference.
static bool themeUsesDarkLog(const QString &theme) {
    return themeManager->IsDarkTheme(theme);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    mainwindow = this;
    setAcceptDrops(true);
    MW_dialog_message = [=,this](MwMessage cmd, QStringList args) {
        runOnUiThread([=,this]
        {
            dialog_message_impl(cmd, args);
        });
    };
    MW_handle_deeplink = [=,this](const QString &url) {
        runOnUiThread([=,this]
        {
            handle_deeplink_impl(url);
        });
    };
    MW_import_files = [=,this](const QStringList &paths) {
        runOnUiThread([=,this]
        {
            importFromFiles(paths);
        });
    };

    // handle AutoRun migration and stale task settings
    AutoRun_FixTaskIfNeeded();
    AutoRun_MigrateIfNeeded();

    // register the throne:// URL scheme and the config file handler (self-heals if
    // the install was moved)
    UrlScheme_RegisterIfNeeded();

    // Setup misc UI
    // migrate old themes
    bool isNum;
    Configs::dataManager->settingsRepo->theme.toInt(&isNum);
    if (isNum) {
        Configs::dataManager->settingsRepo->theme = QStringLiteral("Throned Midnight");
    } else if (!themeManager->ThronedThemes().contains(Configs::dataManager->settingsRepo->theme)) {
        // Retire the old platform/QSS theme mix in favour of palettes that cover
        // every redesigned screen consistently.
        Configs::dataManager->settingsRepo->theme = themeUsesDarkLog(Configs::dataManager->settingsRepo->theme)
            ? QStringLiteral("Throned Midnight") : QStringLiteral("System");
    }
    themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme);
    ui->setupUi(this);

    // MainPreview's exact production shell: the same title bar, block order,
    // dimensions and palette that generate docs/ui-preview/main-en.png.
    auto *redesignedCentral = new QWidget(this);
    redesignedCentral->setObjectName(QStringLiteral("previewRoot"));
    auto *rootLayout = new QVBoxLayout(redesignedCentral);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(new ThronedTitleBar({}, redesignedCentral));

    auto *body = new QWidget(redesignedCentral);
    body->setObjectName(QStringLiteral("body"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(10, 10, 10, 10);
    bodyLayout->setSpacing(7);

    // The command bar belongs to the window chrome, not to the content: it sits
    // edge to edge directly under the title bar so both read as one header band
    // closed by a single hairline, instead of a card floating over another card.
    auto *commandBar = new QFrame(redesignedCentral);
    commandBar->setObjectName(QStringLiteral("commandBar"));
    commandBar->setFixedHeight(54);
    auto *commandLayout = new QHBoxLayout(commandBar);
    commandLayout->setContentsMargins(14, 7, 10, 7);
    commandLayout->setSpacing(5);

    const QList<QPair<QToolButton *, MaterialIcon::Glyph>> navigation{
        {ui->toolButton_program, MaterialIcon::Glyph::Desktop},
        {ui->toolButton_preferences, MaterialIcon::Glyph::Settings},
        {ui->toolButton_testing, MaterialIcon::Glyph::Users},
        {ui->toolButton_routing, MaterialIcon::Glyph::Routes},
        {ui->toolButton_tools, MaterialIcon::Glyph::Tools},
    };
    for (const auto &[button, glyph] : navigation) {
        button->setParent(commandBar);
        button->setStyleSheet({});
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(19, 19));
        button->setMinimumWidth(0);
        button->setFixedHeight(38);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        commandLayout->addWidget(button);
        if (button != navigation.constLast().first) {
            auto *separator = new QFrame(commandBar);
            separator->setObjectName(QStringLiteral("vSeparator"));
            separator->setFixedSize(1, 33);
            commandLayout->addWidget(separator);
        }
    }
    commandLayout->addStretch(1);

    auto addToggle = [commandBar, commandLayout](const QString &text, QCheckBox *toggle) {
        auto *label = new QLabel(text, commandBar);
        label->setObjectName(QStringLiteral("controlLabel"));
        commandLayout->addWidget(label);
        toggle->setParent(commandBar);
        toggle->hide();
        auto *visualToggle = new ThronedToggle(toggle->isChecked(), commandBar);
        visualToggle->bindTo(toggle);
        commandLayout->addWidget(visualToggle);
    };
    addToggle(tr("TUN mode"), ui->checkBox_VPN);
    auto *modeSeparator = new QFrame(commandBar);
    modeSeparator->setObjectName(QStringLiteral("vSeparator"));
    modeSeparator->setFixedSize(1, 33);
    commandLayout->addWidget(modeSeparator);
    addToggle(tr("System proxy"), ui->checkBox_SystemProxy);
    ui->checkBox_VPN->setParent(commandBar);
    ui->system_dns->setParent(commandBar);
    ui->system_dns->hide();
    ui->toolButton_startstop->setParent(commandBar);
    ui->toolButton_startstop->setFixedSize(40, 40);
    commandLayout->addSpacing(8);
    commandLayout->addWidget(ui->toolButton_startstop);
    rootLayout->addWidget(commandBar);

    ui->data_view->setParent(redesignedCentral);
    ui->data_view->setObjectName(QStringLiteral("selectionStatus"));
    ui->data_view->setFixedHeight(0);
    ui->data_view->hide();

    ui->splitter->setParent(body);
    ui->tabWidget->setStyleSheet({});
    ui->stats_widget->setStyleSheet({});
    ui->tabWidget->setObjectName(QStringLiteral("groupsCard"));
    ui->stats_widget->setObjectName(QStringLiteral("logsCard"));
    ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    ui->stats_widget->tabBar()->setUsesScrollButtons(false);
    auto *logTools = new QWidget(ui->stats_widget);
    logTools->setObjectName(QStringLiteral("logTools"));
    auto *logToolsLayout = new QHBoxLayout(logTools);
    logToolsLayout->setContentsMargins(0, 0, 8, 5);
    logToolsLayout->setSpacing(6);
    auto *clearLog = new QPushButton(tr("Clear"), logTools);
    auto *copyLog = new QPushButton(tr("Copy"), logTools);
    for (auto *button : {clearLog, copyLog}) button->setObjectName(QStringLiteral("logToolButton"));
    logToolsLayout->addWidget(clearLog);
    logToolsLayout->addWidget(copyLog);
    auto *autoScrollLabel = new QLabel(tr("Auto-scroll"), logTools);
    autoScrollLabel->setObjectName(QStringLiteral("logAutoScrollLabel"));
    logToolsLayout->addWidget(autoScrollLabel);
    auto *autoScrollSource = new QCheckBox(logTools);
    autoScrollSource->setChecked(Configs::dataManager->settingsRepo->log_auto_scroll);
    autoScrollSource->hide();
    auto *autoScroll = new ThronedToggle(autoScrollSource->isChecked(), logTools);
    autoScroll->bindTo(autoScrollSource);
    logToolsLayout->addWidget(autoScroll);
    auto *logLevel = new QComboBox(logTools);
    logLevel->setObjectName(QStringLiteral("logLevelSelector"));
    logLevel->addItem(QStringLiteral("INFO"), QStringLiteral("info"));
    logLevel->addItem(QStringLiteral("DEBUG"), QStringLiteral("debug"));
    logLevel->addItem(QStringLiteral("WARNING"), QStringLiteral("warning"));
    const int currentLogLevel = logLevel->findData(Configs::dataManager->settingsRepo->log_level);
    logLevel->setCurrentIndex(currentLogLevel >= 0 ? currentLogLevel : 0);
    logLevel->setFixedWidth(106);
    logLevel->setToolTip(tr("Core log level; applies on the next start"));
    logToolsLayout->addWidget(logLevel);
    connect(clearLog, &QPushButton::clicked, this, [this] {
        qvLogDocument->clear();
        ui->masterLogBrowser->clear();
    });
    connect(copyLog, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(ui->masterLogBrowser->toPlainText());
    });
    connect(autoScrollSource, &QCheckBox::toggled, this, [](bool enabled) {
        Configs::dataManager->settingsRepo->log_auto_scroll = enabled;
        Configs::dataManager->settingsRepo->Save();
    });
    connect(logLevel, &QComboBox::currentIndexChanged, this, [logLevel] {
        Configs::dataManager->settingsRepo->log_level = logLevel->currentData().toString();
        Configs::dataManager->settingsRepo->Save();
    });
    ui->stats_widget->setCornerWidget(logTools, Qt::TopRightCorner);
    bodyLayout->addWidget(ui->splitter, 1);

    // Bottom chrome mirrors the header: a full-width strip closed by a hairline
    // rather than another bordered card stacked inside the content area.
    auto *statusCard = new QFrame(redesignedCentral);
    statusCard->setObjectName(QStringLiteral("statusCard"));
    statusCard->setFixedHeight(68);
    auto *statusLayout = new QHBoxLayout(statusCard);
    statusLayout->setContentsMargins(15, 7, 13, 7);
    statusLayout->setSpacing(12);
    const QList<QPair<QLabel *, MaterialIcon::Glyph>> statusItems{
        {ui->label_running, MaterialIcon::Glyph::Public},
        {ui->label_inbound, MaterialIcon::Glyph::Desktop},
        {ui->label_speed, MaterialIcon::Glyph::SwapVertical},
    };
    QList<QPair<QLabel *, MaterialIcon::Glyph>> mutedIcons;
    for (const auto &[label, glyph] : statusItems) {
        auto *icon = new QLabel(statusCard);
        mutedIcons.append({icon, glyph});
        statusLayout->addWidget(icon);
        label->setParent(statusCard);
        label->setObjectName(QStringLiteral("statusValue"));
        statusLayout->addWidget(label, glyph == MaterialIcon::Glyph::Desktop ? 3 : 2);
    }

    // Routing segment: a summary of the active profile that opens the quick menu.
    auto *routingIcon = new QLabel(statusCard);
    mutedIcons.append({routingIcon, MaterialIcon::Glyph::Routes});
    statusLayout->addWidget(routingIcon);
    auto *routingStatus = new QLabel(statusCard);
    routingStatus->setObjectName(QStringLiteral("routingStatus"));
    routingStatus->setCursor(Qt::PointingHandCursor);
    routingStatus->setToolTip(tr("Routing profile and default traffic"));
    routingStatus->installEventFilter(this);
    statusLayout->addWidget(routingStatus, 2);
    auto *selectionCard = new QFrame(redesignedCentral);
    selectionCard->setObjectName(QStringLiteral("selectionCard"));
    auto *selectionLayout = new QHBoxLayout(selectionCard);
    selectionLayout->setContentsMargins(15, 8, 13, 8);
    selectionLayout->setSpacing(10);
    auto *selectionIcon = new QLabel(selectionCard);
    selectionLayout->addWidget(selectionIcon);
    auto *selectionText = new QLabel(selectionCard);
    selectionText->setObjectName(QStringLiteral("selectionText"));
    selectionLayout->addWidget(selectionText);
    selectionLayout->addStretch(1);
    const QList<QPair<QString, QAction *>> selectionActions{
        {tr("URL test"), ui->actionUrl_Test_Selected},
        {tr("Speed test"), ui->actionSpeedtest_Selected},
        {tr("Resolve IP"), ui->actionResolve_Selected_Out_IP},
    };
    for (const auto &[text, action] : selectionActions) {
        auto *button = new QPushButton(text, selectionCard);
        button->setObjectName(QStringLiteral("selectionAction"));
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        selectionLayout->addWidget(button);
    }
    selectionCard->setFixedHeight(68);
    selectionCard->hide();
    rootLayout->addWidget(body, 1);
    rootLayout->addWidget(ui->data_view);
    rootLayout->addWidget(selectionCard);
    rootLayout->addWidget(statusCard);

    // Icons are rasterised, so they have to be repainted whenever the theme
    // changes; otherwise a blue glyph survives into a warm palette.
    const auto retintIcons = [navigation, mutedIcons, selectionIcon] {
        const auto colors = themeManager->Colors();
        for (const auto &[button, glyph] : navigation)
            button->setIcon(MaterialIcon::icon(glyph, colors.textMuted, 19));
        for (const auto &[label, glyph] : mutedIcons)
            label->setPixmap(MaterialIcon::pixmap(glyph, colors.textMuted, 18));
        selectionIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::List, colors.accent, 21));
    };
    retintIcons();
    connect(themeManager, &ThemeManager::themeChanged, this, retintIcons);
    refreshRoutingStatus();

    setWindowFlag(Qt::FramelessWindowHint, true);
    new ThronedWindowResizer(this);
    setMinimumSize(960, 680);
    ui->centralwidget = redesignedCentral;
    setCentralWidget(redesignedCentral);

    ui->profilesTableView->setAlternatingRowColors(false);
    ui->profilesTableView->setShowGrid(false);
    ui->profilesTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->profilesTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(34);
    ui->profilesTableView->setCornerButtonEnabled(false);
    auto tablePalette = ui->profilesTableView->palette();
    tablePalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#143C48")));
    tablePalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
    ui->profilesTableView->setPalette(tablePalette);
    ui->connections->setShowGrid(false);
    ui->masterLogBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    const QString mainStyleTemplate = QStringLiteral(R"(
* { font-size: %BASE_FONT_PX%px; color: #F1F3F5; }
QMainWindow { background: #1B1E23; }
QWidget#previewRoot { background: #1B1E23; border: 1px solid #2F3136; }
QWidget#body { background: #1B1E23; }
QFrame#titleBar { background: #1B1E23; border: none; }
QLabel#titleBrand { font-size: 18px; font-weight: 700; }
QLabel#titleContext { font-size: 14px; font-weight: 650; color: #D8DCE1; }
QFrame#titleBar QToolButton { background: transparent; border: none; }
QFrame#titleBar QToolButton:hover { background: #292D33; }
QFrame#titleBar QToolButton#titleClose:hover { background: #C42B35; }
QFrame#vSeparator { background: #2F3136; border: none; }
QFrame#commandBar {
    background: #1B1E23; border: none; border-bottom: 1px solid #2F3136;
}
QFrame#statusCard, QFrame#selectionCard {
    background: #1B1E23; border: none; border-top: 1px solid #2F3136;
}
QFrame#commandBar QToolButton {
    background: transparent; border: none; border-radius: 6px; font-weight: 550; padding: 7px 9px;
}
QFrame#commandBar QToolButton:hover { background: #292D33; }
QFrame#commandBar QToolButton::menu-indicator { image: none; width: 0px; }
QLabel#controlLabel { font-weight: 550; }
QWidget#tableTools, QWidget#logTools { background: transparent; }
QPushButton#logToolButton {
    background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px 10px;
}
QPushButton#logToolButton:hover { background: #292D33; border-color: #4A4F57; }
QToolButton#tableFilterButton {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
}
QToolButton#tableFilterButton:hover { background: #292D33; border-color: #4A4F57; }
QToolButton#tableFilterButton:checked { background: #182530; border-color: #237AE9; }
QLineEdit#serverSearch {
    background: #171B21; border: 1px solid #2F3136; border-radius: 6px; padding: 6px 9px 6px 5px;
}
QLineEdit#serverSearch:hover { border-color: #4A535E; }
QLineEdit#serverSearch:focus { border-color: #2F91FF; }
QComboBox#logLevelSelector {
    background: #171B21; border: 1px solid #2F3136; border-radius: 5px;
    padding: 6px 28px 6px 9px;
}
QComboBox#logLevelSelector:hover { border-color: #4A535E; }
QTabWidget#groupsCard, QTabWidget#logsCard { background: transparent; }
QTabWidget#groupsCard::pane, QTabWidget#logsCard::pane {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px; top: -1px;
}
QTabWidget#groupsCard::tab-bar, QTabWidget#logsCard::tab-bar { left: 3px; }
QTabWidget#groupsCard QTabBar, QTabWidget#logsCard QTabBar { background: transparent; qproperty-drawBase: 0; }
QTabWidget#groupsCard QTabBar::tab, QTabWidget#logsCard QTabBar::tab {
    background: transparent; border: none; border-bottom: 2px solid transparent;
    padding: 6px 13px; margin-right: 3px; color: #A4ABB4; font-weight: 500;
}
QTabWidget#groupsCard QTabBar::tab:hover, QTabWidget#logsCard QTabBar::tab:hover { color: #F1F3F5; }
QTabWidget#groupsCard QTabBar::tab:selected, QTabWidget#logsCard QTabBar::tab:selected {
    color: #F1F3F5; background: #182530; border-bottom: 2px solid #237AE9;
}
QTableView, QTableWidget, QTextBrowser {
    background: #171B21; border: none; outline: none;
    selection-color: white; selection-background-color: #143C48;
}
QHeaderView::section {
    background: #171B21; color: #C2C7CE; border: none; border-right: 1px solid #2F3136;
    border-bottom: 1px solid #2F3136; padding: 5px 8px; font-weight: 500;
}
QHeaderView { background: #171B21; }
QHeaderView::section:vertical,
QHeaderView::section:vertical:checked,
QHeaderView::section:vertical:pressed {
    color: #8295A6; background: #171B21; border-right: 1px solid #2F3136;
}
QTableCornerButton::section { background: #171B21; border: none; border-right: 1px solid #2F3136; border-bottom: 1px solid #2F3136; }
QTableView::item, QTableWidget::item { border-bottom: 1px solid #2F3136; padding: 3px 7px; }
QTableView::item:selected, QTableWidget::item:selected {
    color: white; background: #143C48;
    border-top: 1px solid #1D7585; border-bottom: 1px solid #1D7585;
}
QSplitter::handle { background: transparent; height: 8px; }
QTextBrowser#masterLogBrowser { padding: 8px 10px; font-family: "Cascadia Mono", "Consolas", monospace; font-size: 13px; }
QFrame#statusCard QLabel#statusValue {
    background: transparent; border: none; padding: 3px 0;
}
QFrame#statusCard QLabel#routingStatus {
    color: #DDE2E7; background: transparent; border: none; padding: 3px 0;
}
QFrame#statusCard QLabel#routingStatus:hover { color: #F1F3F5; }
QFrame#selectionCard QLabel#selectionText { color: #F1F3F5; font-weight: 600; }
QFrame#selectionCard QPushButton#selectionAction {
    background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px 10px;
}
QFrame#selectionCard QPushButton#selectionAction:hover { background: #292D33; border-color: #4A4F57; }
QTextBrowser#selectionStatus {
    color: #E5E8EB; background: #1B1E23; border: none;
    border-top: 1px solid #2F3136; padding: 8px 15px;
}
QScrollBar:vertical { background: transparent; width: 11px; margin: 3px; }
QScrollBar::handle:vertical { background: #344759; border-radius: 4px; min-height: 34px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
    themeManager->RegisterStyle(this, mainStyleTemplate);

    // init shortcuts
    setActionsData();
    loadShortcuts();

    last_running_profile_id = Configs::dataManager->settingsRepo->remember_id;

    // geometry remembering
    if (!Configs::dataManager->settingsRepo->mainWindowGeometry.isEmpty()) {
        auto geo = DecodeB64IfValid(Configs::dataManager->settingsRepo->mainWindowGeometry);
        this->restoreGeometry(geo);
    }

    // setup log
    ui->splitter->restoreState(DecodeB64IfValid(Configs::dataManager->settingsRepo->splitter_state));
    ui->splitter->setChildrenCollapsible(false);
    ui->splitter->setStretchFactor(0, 3);
    ui->splitter->setStretchFactor(1, 2);
    // Splitter states saved by the legacy layout can allocate more than half
    // the window to the log and make the profile table look compressed.  Keep
    // sensible user-adjusted states, but migrate obviously legacy proportions
    // to MainPreview's 3:2 table/log balance after the first layout pass.
    QTimer::singleShot(0, ui->splitter, [this] {
        const QList<int> sizes = ui->splitter->sizes();
        if (sizes.size() != 2) return;
        const int total = sizes[0] + sizes[1];
        if (total <= 0) return;
        if (sizes[0] * 100 < total * 52 || sizes[1] < 120)
            ui->splitter->setSizes({total * 3 / 5, total * 2 / 5});
    });
    setLogHighlighter(themeUsesDarkLog(Configs::dataManager->settingsRepo->theme));
    qvLogDocument->setUndoRedoEnabled(false);
    qvLogDocument->setMaximumBlockCount(Configs::dataManager->settingsRepo->max_log_line);
    ui->masterLogBrowser->setUndoRedoEnabled(false);
    ui->masterLogBrowser->setDocument(qvLogDocument);
    applyLogBrowserFont();
    updateLogFilterFields();
    runOnThread([=, this] {
        log_process_loop();
    }, LogThread);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [=,this](const Qt::ColorScheme& scheme) {
        setLogHighlighter(scheme == Qt::ColorScheme::Dark);
        themeManager->ApplyTheme(Configs::dataManager->settingsRepo->theme, true);
    });
#endif
    connect(themeManager, &ThemeManager::themeChanged, this, [=,this](const QString& theme){
        setLogHighlighter(themeUsesDarkLog(theme));
        scheduleProxyListRefresh();
    });
    MW_show_log = [=,this](const QString &log) {
        append_log(log);
        Logging::WriteUserLog(log);
    };

    // Listen port if random
    if (Configs::dataManager->settingsRepo->random_inbound_port)
    {
        Configs::dataManager->settingsRepo->inbound_socks_port = MkPort(Configs::dataManager->settingsRepo->inbound_address);
    }

    //init HWID data
    runOnNewThread([=, this] {GetDeviceDetails(); });

    // Prepare core
    auto core_path = QApplication::applicationDirPath() + "/";
    core_path += "ThronedCore";

    bool coreDebugMode = (Configs::dataManager->settingsRepo->log_level == "debug");

    // Create IPC server with a random UUID name
    Configs::dataManager->settingsRepo->core_socket_name =
        "thronedIPC-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    core_server = new QLocalServer(this);
    core_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!core_server->listen(Configs::dataManager->settingsRepo->core_socket_name)) {
        qWarning() << "Failed to start IPC server:" << core_server->errorString();
        qApp->quit();
    }

    connect(core_server, &QLocalServer::newConnection, this, [=, this]() {
        auto socket = core_server->nextPendingConnection();
        int profileId = -1;
        {
            // Hold coreProcessMutex so we never observe a half-published
            // core_process while DS_cores is still constructing/starting it.
            QMutexLocker lock(&coreProcessMutex);
            if (!verify_core_pid(socket)) {
                MW_show_log("[Warn] IPC connection from unexpected process rejected");
                socket->close();
                socket->deleteLater();
                return;
            }
            if (core_process) {
                profileId = core_process->start_profile_when_core_is_up;
                core_process->start_profile_when_core_is_up = -1;
            }
        }
        setup_rpc(socket);
        Configs::dataManager->settingsRepo->core_running = true;
        LOG_INFO(QString("elevated: %1").arg(Configs::IsAdmin() ? "yes" : "no"));
        MW_dialog_message(MwMessage::CoreStarted, {Int2String(profileId)});
    });

    // Start core
    auto socketFullName = core_server->fullServerName();
    runOnThread(
        [=, this] {
            QMutexLocker lock(&coreProcessMutex);
            core_process = new Configs_sys::CoreProcess(core_path, socketFullName, coreDebugMode);
            if (Configs::dataManager->settingsRepo->remember_enable &&
                Configs::dataManager->settingsRepo->remember_id >= 0) {
                core_process->start_profile_when_core_is_up =
                    Configs::dataManager->settingsRepo->remember_id;
            }
            core_process->Start();
        },
        DS_cores);

    if (!Configs::dataManager->settingsRepo->font.isEmpty()) {
        auto font = qApp->font();
        font.setFamily(Configs::dataManager->settingsRepo->font);
        qApp->setFont(font);
    }
    if (Configs::dataManager->settingsRepo->font_size != 0) {
        auto font = qApp->font();
        font.setPointSize(Configs::dataManager->settingsRepo->font_size);
        qApp->setFont(font);
    }

    parallelCoreCallPool->setMaxThreadCount(10); // constant value
    testRunner = std::make_unique<TestRunner>(this);
    //
    // The .ui carries Return; numpad Enter is the same gesture.
    ui->menu_start->setShortcuts({QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
    connect(ui->menu_start, &QAction::triggered, this, [=,this]() { profile_start(); });
    connect(ui->menu_stop, &QAction::triggered, this, [=,this]() { profile_stop(false, false, true); });
    connect(ui->toolButton_startstop, &QAbstractButton::clicked, this, [=,this]() {
        // The button is disabled while Connecting/Disabled, so a click here means
        // either a running profile (stop it) or a selected, idle one (start it).
        if (running != nullptr) profile_stop(false, false, true);
        else profile_start();
    });
    connect(ui->tabWidget->tabBar(), &QTabBar::tabMoved, this, [=,this](int from, int to) {
        // use tabData to track tab & gid
        QList<int> tabOrder;
        for (int i = 0; i < ui->tabWidget->tabBar()->count(); i++) {
            tabOrder += ui->tabWidget->tabBar()->tabData(i).toInt();
        }
        Configs::dataManager->groupsRepo->SetGroupsTabOrder(tabOrder);
        on_tabWidget_currentChanged(ui->tabWidget->tabBar()->currentIndex());
    });
    ui->label_running->installEventFilter(this);
    ui->label_inbound->installEventFilter(this);
    ui->splitter->installEventFilter(this);
    ui->tabWidget->installEventFilter(this);
    //
    auto btnFilter = new QToolButton(this);
    btnFilter->setObjectName(QStringLiteral("tableFilterButton"));
    btnFilter->setToolTip(QString("%1\n%2").arg(tr("Enable Filter"), QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    btnFilter->setShortcut(QKeySequence::Find);
    btnFilter->setCheckable(true);
    btnFilter->setIconSize(QSize(17, 17));
    btnFilter->setFixedSize(32, 32);
    connect(btnFilter, &QToolButton::toggled, static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::setFiltersVisible);
    connect(static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader()), &ProfilesTableFilterHeader::closeRequested,
            btnFilter, [btnFilter] { btnFilter->setChecked(false); });
    auto *tableTools = new QWidget(ui->tabWidget);
    tableTools->setObjectName(QStringLiteral("tableTools"));
    auto *tableToolsLayout = new QHBoxLayout(tableTools);
    tableToolsLayout->setContentsMargins(0, 0, 8, 5);
    tableToolsLayout->setSpacing(6);
    tableToolsLayout->addWidget(btnFilter);
    auto *serverSearch = new QLineEdit(tableTools);
    serverSearch->setObjectName(QStringLiteral("serverSearch"));
    serverSearch->setPlaceholderText(tr("Search servers..."));
    serverSearch->setClearButtonEnabled(true);
    serverSearch->setFixedWidth(230);
    auto *searchAction = serverSearch->addAction(QIcon(), QLineEdit::LeadingPosition);
    tableToolsLayout->addWidget(serverSearch);
    connect(serverSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        globalFilterString = text;
        if (m_filterRefreshDebounce) m_filterRefreshDebounce->start();
    });
    ui->tabWidget->setCornerWidget(tableTools, Qt::TopRightCorner);
    const auto retintTableTools = [btnFilter, searchAction] {
        const auto colors = themeManager->Colors();
        btnFilter->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Filter,
                                              btnFilter->isChecked() ? colors.accent : colors.textMuted, 17));
        // Qt centres a line-edit action on the frame, which puts the glyph above
        // the text's optical centre. Drawing it one pixel down inside a slightly
        // larger square lands it on the same line as the placeholder.
        QPixmap glyph(18, 18);
        glyph.fill(Qt::transparent);
        QPainter painter(&glyph);
        painter.drawPixmap(0, 1, MaterialIcon::pixmap(MaterialIcon::Glyph::Search, colors.textSubtle, 17));
        painter.end();
        searchAction->setIcon(QIcon(glyph));
    };
    retintTableTools();
    connect(btnFilter, &QToolButton::toggled, this, retintTableTools);
    connect(themeManager, &ThemeManager::themeChanged, this, retintTableTools);
    //
    RegisterHotkey(false);
    //
    auto last_size = Configs::dataManager->settingsRepo->mw_size.split("x");
    if (last_size.length() == 2) {
        auto w = last_size[0].toInt();
        auto h = last_size[1].toInt();
        if (w > 0 && h > 0) {
            resize(w, h);
        }
    }

    // software_name
    software_name = "Throned";
    software_core_name = "sing-box";
    //
    if (auto dashDir = QDir("dashboard"); !dashDir.exists() && QDir().mkdir("dashboard")) {
        if (auto dashFile = QFile(":/Throned/dashboard-notice.html"); dashFile.exists() && dashFile.open(QIODevice::ReadOnly))
        {
            auto data = dashFile.readAll();
            if (auto dest = QFile("dashboard/index.html"); dest.open(QIODevice::Truncate | QIODevice::WriteOnly))
            {
                dest.write(data);
                dest.close();
            }
            dashFile.close();
        }
    }
    if (auto iconsDir = QDir("icons"); !iconsDir.exists()) {
        QDir().mkdir("icons") ? qDebug("created icons dir") : qDebug("Failed to create icons dir");
    }

    // top bar
    ui->toolButton_program->setMenu(ui->menu_program);
    ui->toolButton_preferences->setMenu(ui->menu_preferences);
    ui->toolButton_routing->setMenu(ui->menuRouting_Menu);
    ui->toolButton_testing->setMenu(ui->menuTesting);
    ui->toolButton_tools->setMenu(ui->menuTools);
    ui->toolButton_program->installEventFilter(this);

    designMinimumSize = minimumSize();
    applyTopBarMetrics();

    ui->menubar->setVisible(false);
    connect(ui->actionRuntime_Stats, &QAction::triggered, this, [=, this]() {
        USE_DIALOG(DialogRuntimeStats)
    });
    ui->actionTraffic_Stats->setVisible(!Configs::dataManager->settingsRepo->disable_traffic_aggregation);
    connect(ui->actionTraffic_Stats, &QAction::triggered, this, [=, this]() {
        USE_DIALOG(DialogTrafficStats)
    });
    // Only meaningful while a selector profile is running; refresh_auto_selector_view
    // shows and hides it as the monitor starts and stops.
    ui->actionAuto_Selector->setVisible(false);
    connect(ui->actionAuto_Selector, &QAction::triggered, this, [=,this]() {
        if (m_autoSelectorDialog == nullptr) {
            m_autoSelectorDialog = new DialogAutoSelector(this);
            connect(m_autoSelectorDialog, &QDialog::finished, this, [this] {
                m_autoSelectorDialog->deleteLater();
                m_autoSelectorDialog = nullptr;
            });
        }
        m_autoSelectorDialog->refresh();
        m_autoSelectorDialog->show();
        m_autoSelectorDialog->raise();
        m_autoSelectorDialog->activateWindow();
    });
    connect(ui->actionCheck_For_Update, &QAction::triggered, this, [=,this] { runOnNewThread([=,this] { CheckUpdate(); }); });
    if (!QFile::exists(QApplication::applicationDirPath() + "/updater") && !QFile::exists(QApplication::applicationDirPath() + "/updater.exe"))
    {
        ui->actionCheck_For_Update->setDisabled(true);
    }

    // setup connection UI
    setupConnectionList();
    ui->stats_widget->tabBar()->setCurrentIndex(Configs::dataManager->settingsRepo->stats_tab);
    connect(ui->stats_widget->tabBar(), &QTabBar::currentChanged, this, [=,this](int index)
    {
        Configs::dataManager->settingsRepo->stats_tab = ui->stats_widget->tabBar()->currentIndex();
        syncConnectionViewState();
    });
    // Seed the lister's view state from the restored tab selection.
    syncConnectionViewState();
    connect(ui->connections->horizontalHeader(), &QHeaderView::sectionClicked, this, [=,this](int index)
    {
            Stats::ConnectionSort sortType;

            switch (index)
            {
            case 1: sortType = Stats::ByProcess; break;
            case 2: sortType = Stats::ByProtocol; break;
            case 3: sortType = Stats::ByOutbound; break;
            case 4: sortType = Stats::ByTraffic; break;
            case 5: sortType = Stats::BySpeed; break;
            default: sortType = Stats::Default; break;
            }

            Stats::connection_lister->setSort(sortType);
            Stats::connection_lister->ForceUpdate();
    });

    // setup Speed Chart
    speedChartWidget = new SpeedWidget(this);
    ui->graph_tab->layout()->addWidget(speedChartWidget);

    // table UI: model-backed view with on-demand row data
    profilesTableModel = new ProfilesTableModel(this);
    profilesFilterModel = new ProfilesFilterProxyModel(this);
    profilesFilterModel->setSourceModel(profilesTableModel);
    ui->profilesTableView->setModel(profilesFilterModel);
    // Keep the start/stop button's enabled/disabled state in sync with selection.
    connect(ui->profilesTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] {
                refresh_startstop_button();
                const int selected = ui->profilesTableView->selectionModel()->selectedRows().size();
                auto *connected = findChild<QFrame *>(QStringLiteral("statusCard"));
                auto *selection = findChild<QFrame *>(QStringLiteral("selectionCard"));
                auto *selectionText = findChild<QLabel *>(QStringLiteral("selectionText"));
                if (!connected || !selection || !selectionText || ui->data_view->isVisible()) return;
                selectionText->setText(tr("%n profiles selected", nullptr, selected));
                selection->setVisible(selected > 1);
                connected->setVisible(selected <= 1);
            });
    ui->profilesTableView->rowsSwapped = [this](int row1, int row2)
    {
        // A drop position in a filtered list says nothing about the group's real order.
        if (profilesFilterModel->hasActiveFilter()) return;
        if (row1 == row2) return;
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        group->EmplaceProfile(row1, row2);
        profilesTableModel->emplaceProfiles(row1, row2);
        Configs::dataManager->groupsRepo->Save(group);
    };
    connect(ui->profilesTableView->horizontalHeader(), &QHeaderView::sectionClicked, this, [=, this](int logicalIndex) {
        GroupSortAction action;
        if (proxy_last_order == logicalIndex) {
            action.descending = true;
            proxy_last_order = -1;
        } else {
            proxy_last_order = logicalIndex;
        }
        if (logicalIndex == ProfilesTableModel::ColType) {
            auto group = Configs::dataManager->groupsRepo->CurrentGroup();
            action.method = (Configs::dataManager->settingsRepo->show_config_security && group
                             && group->type_sort_by == Configs::typeBy::bySecurity)
                                ? GroupSortMethod::BySecurity
                                : GroupSortMethod::ByType;
        } else if (logicalIndex == ProfilesTableModel::ColAddress) {
            action.method = GroupSortMethod::ByAddress;
        } else if (logicalIndex == ProfilesTableModel::ColName) {
            action.method = GroupSortMethod::ByName;
        } else if (logicalIndex == ProfilesTableModel::ColTestResult) {
            action.method = GroupSortMethod::ByTestResult;
        } else if (logicalIndex == ProfilesTableModel::ColTraffic) {
            action.method = GroupSortMethod::ByTraffic;
        } else {
            return;
        }
        runOnNewThread([=, this] {
            auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
            if (currGroup == nullptr) return;
            if (!currGroup->SortProfiles(action)) {
                runOnUiThread([=] {
                    MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                });
                return;
            }
            Configs::dataManager->groupsRepo->Save(currGroup);
            runOnUiThread([=, this] {
                refresh_proxy_list({}, true);
            });
        });
    });
    connect(ui->profilesTableView->horizontalHeader(), &QHeaderView::sectionResized, this, [=, this](int, int, int) {
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (Configs::dataManager->settingsRepo->refreshing_group || group == nullptr) return;
        group->column_width.clear();
        for (int i = 0; i < ui->profilesTableView->horizontalHeader()->count(); i++) {
            group->column_width.push_back(ui->profilesTableView->horizontalHeader()->sectionSize(i));
        }
        Configs::dataManager->groupsRepo->Save(Configs::dataManager->groupsRepo->CurrentGroup());
    });
    ui->profilesTableView->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->profilesTableView->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* header = ui->profilesTableView->horizontalHeader();
        int columnIndex = header->logicalIndexAt(pos);
        auto group = Configs::dataManager->groupsRepo->CurrentGroup();
        if (group == nullptr) return;
        if (columnIndex == ProfilesTableModel::ColType) {
            if (!Configs::dataManager->settingsRepo->show_config_security) return;
            QMenu menu(this);
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct TypeSortOption { Configs::typeBy value; QString label; };
            const QList<TypeSortOption> options = {
                { Configs::typeBy::byType, tr("Type") },
                { Configs::typeBy::bySecurity, tr("Security") },
            };
            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(static_cast<int>(opt.value));
                act->setCheckable(true);
                act->setChecked(group->type_sort_by == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            group->type_sort_by = static_cast<Configs::typeBy>(chosen->data().toInt());
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = group->type_sort_by == Configs::typeBy::bySecurity
                                ? GroupSortMethod::BySecurity
                                : GroupSortMethod::ByType;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                    });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(currGroup);
                runOnUiThread([=, this] {
                    refresh_proxy_list({}, true);
                });
            });
            return;
        }
        if (columnIndex == ProfilesTableModel::ColTestResult) {
            QMenu menu(this);
            auto* includeLabel = menu.addAction(tr("Include:"));
            includeLabel->setEnabled(false);

            auto* actionShowOutIP = menu.addAction(tr("Out IP"));
            actionShowOutIP->setCheckable(true);
            actionShowOutIP->setChecked(group->test_items_to_show == Configs::testShowItems::all ||
                group->test_items_to_show == Configs::testShowItems::ipOnly);

            auto* actionShowSpeed = menu.addAction(tr("Speed"));
            actionShowSpeed->setCheckable(true);
            actionShowSpeed->setChecked(group->test_items_to_show == Configs::testShowItems::all ||
                group->test_items_to_show == Configs::testShowItems::speedOnly);

            auto updateTestItemsToShow = [this, group, actionShowOutIP, actionShowSpeed] {
                    const bool ip = actionShowOutIP->isChecked();
                    const bool speed = actionShowSpeed->isChecked();
                    if (ip && speed) group->test_items_to_show = Configs::testShowItems::all;
                    else if (ip) group->test_items_to_show = Configs::testShowItems::ipOnly;
                    else if (speed) group->test_items_to_show = Configs::testShowItems::speedOnly;
                    else group->test_items_to_show = Configs::testShowItems::none;
                    Configs::dataManager->groupsRepo->Save(group);
                    if (group->calculated_column_width.size() > ProfilesTableModel::ColTestResult) {
                        group->calculated_column_width[ProfilesTableModel::ColTestResult] = 0;
                    }
                    refresh_proxy_list();
                };

            connect(actionShowOutIP, &QAction::triggered, this, updateTestItemsToShow);
            connect(actionShowSpeed, &QAction::triggered, this, updateTestItemsToShow);

            menu.addSeparator();
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct SortOption { int value; QString label; };
            QList<SortOption> options = {
                { static_cast<int>(Configs::testBy::latency), tr("Latency") },
                { static_cast<int>(Configs::testBy::dlSpeed), tr("Download Speed") },
                { static_cast<int>(Configs::testBy::ulSpeed), tr("Upload Speed") },
                { static_cast<int>(Configs::testBy::ipOut), tr("IP Out") }
            };
            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(opt.value);
                act->setCheckable(true);
                act->setChecked(static_cast<int>(group->test_sort_by) == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            int testSortBy = chosen->data().toInt();
            group->test_sort_by = static_cast<Configs::testBy>(testSortBy);
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = GroupSortMethod::ByTestResult;
            action.descending = true;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                        });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(currGroup);
                runOnUiThread([=, this] {
                    refresh_proxy_list({}, true);
                    });
                });
            return;
        }
        if (columnIndex == ProfilesTableModel::ColTraffic) {
            QMenu menu(this);
            auto* sortByLabel = menu.addAction(tr("Sort By:"));
            sortByLabel->setEnabled(false);

            struct TrafficSortOption { int value; QString label; };
            QList<TrafficSortOption> options = {
                { 0, tr("Total") },
                { 1, tr("Downloaded") },
                { 2, tr("Uploaded") }
            };

            for (const auto& opt : options) {
                auto* act = menu.addAction(opt.label);
                act->setData(opt.value);
                act->setCheckable(true);
                act->setChecked(static_cast<int>(group->traffic_sort_by) == opt.value);
            }

            auto* chosen = menu.exec(header->mapToGlobal(pos));
            if (chosen == nullptr || !chosen->data().isValid()) return;

            int trafficSortBy = chosen->data().toInt();
            group->traffic_sort_by = static_cast<Configs::trafficBy>(trafficSortBy);
            Configs::dataManager->groupsRepo->Save(group);
            GroupSortAction action;
            action.method = GroupSortMethod::ByTraffic;
            action.descending = false;
            runOnNewThread([=, this] {
                auto currGroup = Configs::dataManager->groupsRepo->CurrentGroup();
                if (currGroup == nullptr) return;
                if (!currGroup->SortProfiles(action)) {
                    runOnUiThread([=] {
                        MessageBoxWarning("Action already in progress", "A sort action is already in progress");
                        });
                    return;
                }
                Configs::dataManager->groupsRepo->Save(Configs::dataManager->groupsRepo->CurrentGroup());
                runOnUiThread([=, this] {
                    refresh_proxy_list();
                    });
                });
            return;
        }
    });
    ui->profilesTableView->verticalHeader()->setStretchLastSection(false);
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(34);
    ui->profilesTableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->profilesTableView->setTabKeyNavigation(false);
    ui->profilesTableView->horizontalHeader()->setResizeContentsPrecision(0);

    connect(ui->profilesTableView->verticalScrollBar(), &QScrollBar::valueChanged, ui->profilesTableView, [=, this] {
        if (!ui->profilesTableView->isVisible()) return;
        refresh_proxy_list_column_size();
    });

    // search box
    auto *filterHeader = static_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader());
    filterHeader->setLastFilterColumn(Configs::dataManager->settingsRepo->last_filter_column);
    connect(filterHeader, &ProfilesTableFilterHeader::lastFilterColumnChanged, this, [](int column)
    {
        Configs::dataManager->settingsRepo->last_filter_column = column;
        Configs::dataManager->settingsRepo->Save();
    });

    m_filterRefreshDebounce = new QTimer(this);
    m_filterRefreshDebounce->setSingleShot(true);
    m_filterRefreshDebounce->setInterval(50);
    connect(m_filterRefreshDebounce, &QTimer::timeout, this, [this] { applyProfileFilters(); });

    connect(filterHeader, &ProfilesTableFilterHeader::typeFilterChanged, this, [this](const QString& currentText)
    {
       typeFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::addressFilterChanged, this, [this](const QString& currentText)
    {
       addressFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::nameFilterChanged, this, [this](const QString& currentText)
    {
       nameFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::testFilterChanged, this, [this](const QString& currentText)
    {
       countryFilterString = currentText;
       m_filterRefreshDebounce->start();
    });
    connect(filterHeader, &ProfilesTableFilterHeader::focusTableRequested, this,
            [this](bool selectFirst) { focusProfilesTable(selectFirst); });

    // refresh
    this->refresh_groups();

    // Setup Tray
    tray = new QSystemTrayIcon(nullptr);
    tray->setIcon(GetTrayIcon(Icon::NONE));
    QApplication::setWindowIcon(Icon::GetTrayIcon(Icon::NONE));
    trayMenu = new QMenu();
    trayMenu->addAction(ui->actionShow_window);
    trayMenu->addSeparator();
    trayMenu->addAction(ui->actionStart_with_system);
    trayMenu->addAction(ui->actionRemember_last_proxy);
    trayMenu->addAction(ui->actionAllow_LAN);
    trayMenu->addSeparator();

    auto *actSelectServer = new QAction(tr("Select Profile"), trayMenu);
    connect(actSelectServer, &QAction::triggered, this, [this]() { openTraySelector(false); });
    trayMenu->addAction(actSelectServer);
    auto *actSelectRouting = new QAction(tr("Select Routing"), trayMenu);
    connect(actSelectRouting, &QAction::triggered, this, [this]() { openTraySelector(true); });
    trayMenu->addAction(actSelectRouting);
    // MacOS cannot reuse menus across different parents properly
    if (getOS() == Darwin) {
        auto* traySpmodeMenu = new QMenu(ui->menu_spmode->title(), trayMenu);
        traySpmodeMenu->addAction(ui->menu_spmode_system_proxy);
        traySpmodeMenu->addAction(ui->menu_spmode_vpn);
        connect(traySpmodeMenu, &QMenu::aboutToShow, this, [=,this]() {
            ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
            ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
            ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
        });
        trayMenu->addMenu(traySpmodeMenu);
    } else {
        trayMenu->addMenu(ui->menu_spmode);
    }
    trayMenu->addSeparator();

    trayMenu->addAction(ui->actionRestart_Proxy);
    trayMenu->addAction(ui->actionRestart_Program);
    trayMenu->addAction(ui->menu_exit);
    tray->setVisible(!Configs::dataManager->settingsRepo->disable_tray);
    tray->setContextMenu(trayMenu);
    connect(tray, &QSystemTrayIcon::activated, qApp, [=, this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && getOS() != Darwin) {
            trayClickEvent();
        }
    });

    // Misc menu
    ui->actionRemember_last_proxy->setChecked(Configs::dataManager->settingsRepo->remember_enable);
    ui->actionStart_with_system->setChecked(AutoRun_IsEnabled());
    ui->actionAllow_LAN->setChecked(QStringList{"::", "0.0.0.0"}.contains(Configs::dataManager->settingsRepo->inbound_address));

    connect(ui->actionHide_window, &QAction::triggered, this, [=, this](){ HideWindow(this); });
    connect(ui->menu_open_config_folder, &QAction::triggered, this, [=,this] { QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::currentPath())); });
    connect(ui->actionRestart_Proxy, &QAction::triggered, this, [=,this] {
        runOnThread([=, this] {
            profile_stop(true, true, true);
            core_process->Kill();
        }, DS_cores);
    });
    connect(ui->actionRestart_Program, &QAction::triggered, this, [=,this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    connect(ui->actionShow_window, &QAction::triggered, this, [=,this] { ActivateWindow(this); });
    connect(ui->actionRemember_last_proxy, &QAction::triggered, this, [=,this](bool checked) {
        Configs::dataManager->settingsRepo->remember_enable = checked;
        ui->actionRemember_last_proxy->setChecked(checked);
        Configs::dataManager->settingsRepo->Save();
    });
    connect(ui->actionStart_with_system, &QAction::triggered, this, [=,this](bool checked) {
        AutoRun_SetEnabled(checked);
        ui->actionStart_with_system->setChecked(checked);
    });
    connect(ui->actionAllow_LAN, &QAction::triggered, this, [=,this](bool checked) {
        Configs::dataManager->settingsRepo->inbound_address = checked ? "::" : "127.0.0.1";
        ui->actionAllow_LAN->setChecked(checked);
        MW_dialog_message(MwMessage::UpdateSettings, {});
    });
    //
    connect(ui->checkBox_VPN, &QCheckBox::clicked, this, [=,this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->checkBox_SystemProxy, &QCheckBox::clicked, this, [=,this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode, &QMenu::aboutToShow, this, [=,this]() {
        ui->menu_spmode_disabled->setChecked(!(Configs::dataManager->settingsRepo->spmode_system_proxy || Configs::dataManager->settingsRepo->spmode_vpn));
        ui->menu_spmode_system_proxy->setChecked(Configs::dataManager->settingsRepo->spmode_system_proxy);
        ui->menu_spmode_vpn->setChecked(Configs::dataManager->settingsRepo->spmode_vpn);
    });
    connect(ui->menu_spmode_system_proxy, &QAction::triggered, this, [=,this](bool checked) { set_spmode_system_proxy(checked); });
    connect(ui->menu_spmode_vpn, &QAction::triggered, this, [=,this](bool checked) { set_spmode_vpn(checked); });
    connect(ui->menu_spmode_disabled, &QAction::triggered, this, [=,this]() {
        set_spmode_system_proxy(false);
        set_spmode_vpn(false);
    });
    connect(ui->menu_qr, &QAction::triggered, this, [=,this]() { display_qr_link(false); });
    connect(ui->system_dns, &QCheckBox::clicked, this, [=,this](bool checked) {
        if (const auto ok = set_system_dns(checked); !ok) {
            ui->system_dns->setChecked(!checked);
        } else {
            refresh_status();
        }
    });
    if (Configs::dataManager->settingsRepo->show_system_dns) ui->system_dns->show();
    else ui->system_dns->hide();

    connect(ui->menu_server, &QMenu::aboutToShow, this, [=,this](){
        if (auto selected = get_now_selected_list(); selected.empty())
        {
            ui->actionSpeedtest_Selected->setEnabled(false);
            ui->actionUrl_Test_Selected->setEnabled(false);
            ui->menu_resolve_selected->setEnabled(false);
            ui->actionResolve_Selected_Out_IP->setEnabled(false);
        } else
        {
            ui->actionSpeedtest_Selected->setEnabled(true);
            ui->actionUrl_Test_Selected->setEnabled(true);
            ui->menu_resolve_selected->setEnabled(true);
            ui->actionResolve_Selected_Out_IP->setEnabled(true);
        }
        if (testRunner->isRunning()) {
            ui->menu_server->addAction(ui->menu_stop_testing);
        } else {
            ui->menu_server->removeAction(ui->menu_stop_testing);
        }
    });

    connect(ui->menuTesting, &QMenu::aboutToShow, this, [=,this](){
        // Deleting the last remaining group is not allowed.
        ui->actionDelete_Group->setEnabled(Configs::dataManager->groupsRepo->GetAllGroupIds().size() > 1);
        if (testRunner->isRunning()) {
            ui->menuTesting->addAction(ui->menu_stop_testing);
        } else {
            ui->menuTesting->removeAction(ui->menu_stop_testing);
        }
    });

    connect(ui->menuTools, &QMenu::aboutToShow, this, [=,this](){
        // Speedtest Current only makes sense against a live instance.
        ui->actionSpeedtest_Current->setEnabled(running != nullptr);
    });

    connect(ui->actionAdd_New_Group, &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->NewGroup();
        auto dialog = new DialogEditGroup(ent, this);
        int ret = dialog->exec();
        dialog->deleteLater();

        if (ret == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->AddGroup(ent);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });

    connect(ui->actionEdit_Group, &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->CurrentGroup();
        auto dialog = new DialogEditGroup(ent, this);
        connect(dialog, &QDialog::finished, this, [=,this] {
            if (dialog->result() == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->Save(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
            dialog->deleteLater();
        });
        dialog->show();
    });

    connect(ui->actionDelete_Group, &QAction::triggered, this, [=,this]{
        if (Configs::dataManager->groupsRepo->GetAllGroupIds().size() <= 1) return;
        auto id = Configs::dataManager->groupsRepo->CurrentGroup()->id;
        if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(Configs::dataManager->groupsRepo->GetGroup(id)->name)) ==
            QMessageBox::StandardButton::Yes) {
            if (running != nullptr) {
                if (running->gid == id) profile_stop(false, true, false);
            }
            Configs::dataManager->groupsRepo->DeleteGroup(id);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });

    connect(ui->actionUpdate_All_Subscriptions, &QAction::triggered, this, [=,this]{
        if (QMessageBox::question(this, tr("Confirmation"), tr("Update all subscriptions?")) == QMessageBox::StandardButton::Yes) {
            UI_update_all_groups();
        }
    });

    connect(ui->actionRefresh_Column_Widths, &QAction::triggered, this, [=, this] {
        auto ent = Configs::dataManager->groupsRepo->CurrentGroup();
        ent->column_width.clear();
        Configs::dataManager->groupsRepo->Save(ent);
        show_group(ent->id);
    });

    connect(ui->menuRouting_Menu, &QMenu::aboutToShow, this, [=,this]()
    {
        ui->menuRouting_Menu->clear();
        ui->menuRouting_Menu->addAction(ui->menu_routing_settings);

        auto* actionAdblock = new QAction(ui->menuRouting_Menu);
        actionAdblock->setText(tr("Enable AdBlock"));
        actionAdblock->setCheckable(true);
        actionAdblock->setChecked(Configs::dataManager->settingsRepo->adblock_enable);
        connect(actionAdblock, &QAction::triggered, this, [=,this](bool checked) {
            Configs::dataManager->settingsRepo->adblock_enable = checked;
            actionAdblock->setChecked(checked);
            Configs::dataManager->settingsRepo->Save();
            if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
        });
        ui->menuRouting_Menu->addAction(actionAdblock);

        auto* actionWarp = new QAction(ui->menuRouting_Menu);
        actionWarp->setText(tr("Enable Warp"));
        actionWarp->setCheckable(true);
        actionWarp->setChecked(Configs::dataManager->settingsRepo->enable_warp);
        connect(actionWarp, &QAction::triggered, this, [=,this](bool checked) {
            Configs::dataManager->settingsRepo->enable_warp = checked;
            actionWarp->setChecked(checked);
            Configs::dataManager->settingsRepo->Save();
            if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
        });
        ui->menuRouting_Menu->addAction(actionWarp);

        QMenu* profilesMenu = ui->menuRouting_Menu->addMenu(QObject::tr("Download Profiles"));
        for (const QString &country : QStringList{"China", "Iran", "Russia"})
        {
            auto* action = new QAction(profilesMenu);
            action->setText(country);
            connect(action, &QAction::triggered, this, [=,this]()
            {
                auto resp = NetworkRequestHelper::HttpGet(Configs::get_jsdelivr_link("https://raw.githubusercontent.com/throneproj/routeprofiles/profile/Profile_" + country));
                if (!resp.error.isEmpty()) {
                    runOnUiThread([=] {
                        MessageBoxWarning(QObject::tr("Download Profiles"), QObject::tr("Requesting profile error: %1").arg(resp.error + "\n" + resp.data));
                    });
                    return;
                }
                handle_add_remote_routes(resp.data);
            });
            profilesMenu->addAction(action);
        }

        ui->menuRouting_Menu->addSeparator();
        for (const auto& route : Configs::dataManager->routesRepo->GetAllRouteProfiles())
        {
            auto* action = new QAction(ui->menuRouting_Menu);
            action->setText(route->name);
            action->setData(route->id);
            action->setCheckable(true);
            action->setChecked(Configs::dataManager->settingsRepo->current_route_id == route->id);
            connect(action, &QAction::triggered, this, [=,this]()
            {
                auto routeID = action->data().toInt();
                if (Configs::dataManager->settingsRepo->current_route_id == routeID) return;
                Configs::dataManager->settingsRepo->current_route_id = routeID;
                Configs::dataManager->settingsRepo->Save();
                if (Configs::dataManager->settingsRepo->started_id >= 0) profile_start(Configs::dataManager->settingsRepo->started_id);
            });
            ui->menuRouting_Menu->addAction(action);
        }
    });
    connect(ui->actionClear_Test_Result, &QAction::triggered, this, [=, this]() {
        auto entIDs = get_now_selected_list();
        auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
        if (ents.empty()) return;
        for (const auto &ent: ents) {
            ent->ClearTestResults();
        }
        Configs::dataManager->profilesRepo->SaveBatch(ents);
        if (auto group = Configs::dataManager->groupsRepo->GetGroup(ents.first()->gid); group &&
            group->calculated_column_width.size() > ProfilesTableModel::ColTestResult)
            group->calculated_column_width[ProfilesTableModel::ColTestResult] = 0;
        refresh_proxy_list();
    });
    connect(ui->actionUrl_Test_Selected, &QAction::triggered, this, [=,this]() {
        testRunner->runUrlTests(get_now_selected_list());
    });
    connect(ui->actionUrl_Test_Group, &QAction::triggered, this, [=,this]() {
        testRunner->runUrlTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->actionSpeedtest_Current, &QAction::triggered, this, [=,this]()
    {
        if (running != nullptr)
        {
            testRunner->runSpeedTests({}, true);
        }
    });
    connect(ui->actionSpeedtest_Selected, &QAction::triggered, this, [=,this]()
    {
        testRunner->runSpeedTests(get_now_selected_list());
    });
    connect(ui->actionSpeedtest_Group, &QAction::triggered, this, [=,this]()
    {
        testRunner->runSpeedTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->actionResolve_Selected_Out_IP, &QAction::triggered, this, [=,this]() {
        testRunner->runIpTests(get_now_selected_list());
    });
    connect(ui->actionResolve_Out_IP, &QAction::triggered, this, [=,this]() {
        testRunner->runIpTests(Configs::dataManager->groupsRepo->CurrentGroup()->Profiles());
    });
    connect(ui->menu_stop_testing, &QAction::triggered, this, [=,this]() { testRunner->stop(); });
    //
    auto set_selected_or_group = [=,this](int mode) {
        // 0=group 1=select 2=unknown(menu is hide)
        ui->menu_server->setProperty("selected_or_group", mode);
    };
    connect(ui->menu_server, &QMenu::aboutToHide, this, [=,this] {
        setTimeout([=,this] { set_selected_or_group(2); }, this, 200);
    });
    set_selected_or_group(2);
    //
    connect(ui->menu_share_item, &QMenu::aboutToShow, this, [=,this] {
        QString name;
        auto selected = get_now_selected_list();

        ui->menu_export_config->setVisible(false);
        ui->actionExport_Xray_config->setVisible(false);
        if (selected.isEmpty()) return;

        auto profile = Configs::dataManager->profilesRepo->GetProfile(selected.first());
        if (!profile) return;

        if (selected.count() == 1 && profile->DisplayTestResult().trimmed().isEmpty()) {
            ui->actionCopy_Test_Result->setVisible(false);
        } else {
            ui->actionCopy_Test_Result->setVisible(true);
        }

        ui->menu_export_config->setVisible(true);
        if (profile->outbound->IsXray() || profile->type == "chain") ui->actionExport_Xray_config->setVisible(true);
    });
    connect(ui->actionExport_Xray_config, &QAction::triggered, this, [=,this]() {
        auto ents = get_now_selected_list();
        if (ents.count() != 1) return;
        auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());

        auto result = Configs::BuildSingBoxConfig(ent);
        if (!result->error.isEmpty()) {
            MessageBoxWarning("Build config error", result->error);
            return;
        }
        const QString singConfig = QJsonObject2QString(result->coreConfig, true);
        QStringList xrayConfigs;
        if (!result->xrayConfig.isEmpty()) {
            xrayConfigs << QJsonObject2QString(result->xrayConfig, true);
        }
        xrayConfigs.append(result->xrayFullConfigs);
        QString config_core = xrayConfigs.join("\n\n");
        const QString pairedConfigs =
            QStringLiteral("=== sing-box ===\n") + singConfig +
            QStringLiteral("\n\n=== Xray ===\n") + config_core;
        QApplication::clipboard()->setText(config_core);

        QMessageBox msg(QMessageBox::Information, tr("Config copied"), config_core);
        QPushButton *button_1 = msg.addButton(tr("Copy sing-box config"), QMessageBox::YesRole);
        QPushButton *button_pair = msg.addButton(tr("Copy paired configs"), QMessageBox::YesRole);
        QPushButton *button_2 = msg.addButton(tr("Copy test config"), QMessageBox::YesRole);
        msg.addButton(QMessageBox::Ok);
        msg.setEscapeButton(QMessageBox::Ok);
        msg.setDefaultButton(QMessageBox::Ok);
        msg.exec();
        if (msg.clickedButton() == button_1) {
            QApplication::clipboard()->setText(singConfig);
        } else if (msg.clickedButton() == button_pair) {
            QApplication::clipboard()->setText(pairedConfigs);
        } else if (msg.clickedButton() == button_2) {
            auto res = Configs::BuildTestConfig({ent});
            if (!res->error.isEmpty()) {
                MessageBoxWarning("Build Test config error", res->error);
                return;
            }
            config_core = QJsonObject2QString(res->xrayConfig, true);
            QApplication::clipboard()->setText(config_core);
        }
    });
    connect(ui->actionCopy_Test_Result, &QAction::triggered, this, [=,this]() {
        auto ents = get_now_selected_list();
        if (ents.count() == 0 || ents.count() > 1000) return;
        auto entList = Configs::dataManager->profilesRepo->GetProfileBatch(ents);
        QString res;
        int counter = 0;
        for (auto ent : entList) {
            auto testRes = ent->DisplayTestResult();
            if (!testRes.trimmed().isEmpty()) {
                res += testRes.trimmed() + "\n";
                counter++;
            }
        }
        QApplication::clipboard()->setText(res);
        MW_show_log(QString::number(counter) + tr(" Test result(s) copied to clipboard!"));
    });
    connect(ui->actionAdd_profile_from_File, &QAction::triggered, this, [=,this]()
    {
        // "All files" is listed first so it is the default: config files routinely
        // carry no extension, and a type filter would hide them from the picker.
        const auto filters = QStringList{
            tr("All files (*)"),
            tr("Config files (*.json *.conf *.txt *.yaml *.yml *.ini)"),
            tr("QR code images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"),
        };
        const auto paths = QFileDialog::getOpenFileNames(this, tr("Select profile files"), QString(), filters.join(";;"));
        if (paths.isEmpty()) return;
        importFromFiles(paths);
    });

    connect(qApp, &QGuiApplication::commitDataRequest, this, &MainWindow::on_commitDataRequest);

    auto t = new QTimer;
    connect(t, &QTimer::timeout, this, [=,this]() { refresh_status(); });
    t->start(2000);

    t = new QTimer;
    connect(t, &QTimer::timeout, this, [&] { Configs_sys::logCounter.fetchAndStoreRelaxed(0); });
    t->start(1000);

    // Debounced so font/theme/resize changes settle; fired from changeEvent,
    // resizeEvent and ThemeManager::themeChanged.
    m_proxyListRefreshDebounce = new QTimer(this);
    m_proxyListRefreshDebounce->setSingleShot(true);
    connect(m_proxyListRefreshDebounce, &QTimer::timeout, this, [this] { refresh_proxy_list({}, false); });

    // The auto-selector monitor polls the core from its own thread; both
    // handlers are queued onto the UI thread.
    connect(Stats::autoSelectorMonitor, &Stats::AutoSelectorMonitor::poolExhausted, this,
            [this](int profileID) { on_auto_selector_exhausted(profileID); }, Qt::QueuedConnection);
    connect(Stats::autoSelectorMonitor, &Stats::AutoSelectorMonitor::updated, this,
            [this] { refresh_auto_selector_view(); }, Qt::QueuedConnection);

    // The runner persists each job's last-run time, so closing the app past the
    // interval still triggers an update next launch instead of resetting the clock.
    {
        auto* runner = Throne::PeriodicRunner::instance();
        // Settings store the interval sign-encoded (negative = disabled); < 30 min is
        // treated as off, matching the "invalid if less than 30" UI hint.
        const auto minutesOf = [](int v) { return v >= 30 ? v : 0; };
        runner->Add({
            tr("subscriptions"),
            [minutesOf] { return minutesOf(Configs::dataManager->settingsRepo->sub_auto_update); },
            [] { return Configs::dataManager->settingsRepo->sub_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->sub_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [] { UI_update_all_groups(true); },
        });
        runner->Add({
            tr("routing profiles"),
            [minutesOf] { return minutesOf(Configs::dataManager->settingsRepo->route_auto_update); },
            [] { return Configs::dataManager->settingsRepo->route_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->route_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [] { UI_update_all_remote_routes(true); },
        });
        runner->Add({
            tr("updates"),
            // Release checks are cheap and rate-limited by GitHub, so they are not
            // held to the 30-minute floor the subscription sweeps use.
            [] {
                const int v = Configs::dataManager->settingsRepo->app_auto_update;
                return v > 0 ? v : 0;
            },
            [] { return Configs::dataManager->settingsRepo->app_auto_update_last; },
            [](qint64 t) {
                Configs::dataManager->settingsRepo->app_auto_update_last = t;
                Configs::dataManager->settingsRepo->Save();
            },
            [this] { runOnNewThread([this] { CheckUpdate(true); }); },
        });
    }

    // The control surface reaches the config layer on its own; these are the
    // few operations only the window can perform.
    ThronedControl::hooks.startProfile = [this](int id) { profile_start(id); };
    ThronedControl::hooks.stopProfile = [this] { profile_stop(false, false, true); };
    ThronedControl::hooks.runningProfileId = [this] { return running ? running->id : -1; };
    ThronedControl::hooks.applyRoutingChange = [this] {
        refreshRoutingStatus();
        if (Configs::dataManager->settingsRepo->started_id >= 0)
            profile_start(Configs::dataManager->settingsRepo->started_id);
    };

    connect(tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!pendingUpdatePrompt) return;
        const auto prompt = std::exchange(pendingUpdatePrompt, {});
        prompt();
    });

    if (!Configs::dataManager->settingsRepo->flag_tray) show();

    ui->data_view->setStyleSheet("background: transparent; border: none;");
}

MainWindow::~MainWindow() {
    delete ui;
}

