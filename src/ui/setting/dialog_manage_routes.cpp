#include "include/ui/setting/dialog_manage_routes.h"

#include <QClipboard>

#include "3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.hpp"
#include "include/global/GuiUtils.hpp"

#include <QFile>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QTabBar>
#include <QToolTip>
#include <QDialog>
#include <QTextEdit>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <include/api/RPC.h>

#include "include/configs/sub/warp.h"
#include "include/configs/sub/RouteUpdater.hpp"

#include <srslist.h>
#include "include/database/RoutesRepo.h"
#include "include/ui/setting/RawRouteItem.h"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowResizer.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QScrollArea>
#include <QVBoxLayout>

void DialogManageRoutes::reloadProfileItems() {
    if (chainList.empty()) {
        MessageBoxWarning(tr("Invalid state"), tr("The list of routing profiles is empty, this should be an unreachable state, crashes may occur now"));
        return;
    }

    QSignalBlocker blocker = QSignalBlocker(ui->route_prof); // apparently the currentIndexChanged will make us crash if we clear the QComboBox
    ui->route_prof->clear();

    ui->route_profiles->clear();
    bool selectedChainGone = true;
    int i=0;
    for (const auto &item: chainList) {
        ui->route_prof->addItem(item->name);
        ui->route_profiles->addItem(item->name);
        if (item == currentRoute) {
            ui->route_prof->setCurrentIndex(i);
            ui->route_profiles->setCurrentRow(i);
            selectedChainGone=false;
        }
        i++;
    }
    if (selectedChainGone) {
        currentRoute=chainList[0];
        ui->route_prof->setCurrentIndex(0);
        ui->route_profiles->setCurrentRow(0);
    }
    blocker.unblock();
}

void DialogManageRoutes::set_dns_hijack_enability(const bool enable) const {
    ui->dnshijack_allow_lan->setEnabled(enable);
    ui->dnshijack_listenport->setEnabled(enable);
    ui->dnshijack_rules->setEnabled(enable);
    ui->dnshijack_v4resp->setEnabled(enable);
    ui->dnshijack_v6resp->setEnabled(enable);
}

bool DialogManageRoutes::validate_dns_rules(const QString &rawString) {
    auto rules = rawString.split("\n");
    for (const auto& rule : rules) {
        if (!rule.trimmed().isEmpty() && !rule.startsWith("ruleset:") && !rule.startsWith("domain:") && !rule.startsWith("suffix:") && !rule.startsWith("regex:")) return false;
    }
    return true;
}

DialogManageRoutes::DialogManageRoutes(QWidget *parent) : QDialog(parent), ui(new Ui::DialogManageRoutes) {
    ui->setupUi(this);
    setObjectName(QStringLiteral("routeProfileEditor"));
    setWindowTitle(tr("Routing settings"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    new ThronedWindowResizer(this);
    setMinimumSize(900, 620);
    resize(1000, 700);
    ui->tabWidget->setStyleSheet({});
    ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    ui->tabWidget->tabBar()->hide();
    ui->tabWidget->setIconSize(QSize(18, 18));
    const QColor navIconColor(QStringLiteral("#AEB7C2"));
    ui->tabWidget->setTabIcon(0, MaterialIcon::icon(MaterialIcon::Glyph::Settings, navIconColor, 18));
    ui->tabWidget->setTabIcon(1, MaterialIcon::icon(MaterialIcon::Glyph::Shield, navIconColor, 18));
    ui->tabWidget->setTabIcon(2, MaterialIcon::icon(MaterialIcon::Glyph::SwapVertical, navIconColor, 18));
    ui->tabWidget->setTabIcon(3, MaterialIcon::icon(MaterialIcon::Glyph::Public, navIconColor, 18));
    ui->tabWidget->setTabIcon(4, MaterialIcon::icon(MaterialIcon::Glyph::Routes, navIconColor, 18));
    ui->tabWidget->setTabText(4, tr("Profiles"));
    ui->tabWidget->setCurrentIndex(4);

    // SettingsPreview uses a real horizontal page stack with a separate left
    // navigation rail.  Reusing the QTabBar as a west rail while forcing a
    // north shape made Qt calculate a zero-sized page on Windows.
    auto *root = ui->verticalLayout;
    while (QLayoutItem *item = root->takeAt(0)) delete item;
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    root->addWidget(new ThronedTitleBar(tr("Routing settings"), this));

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("routeSettingsBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 10, 12, 10);
    bodyLayout->setSpacing(10);
    auto *work = new QHBoxLayout;
    work->setSpacing(12);
    auto *sidebar = new QFrame(body);
    sidebar->setObjectName(QStringLiteral("routeSettingsSidebar"));
    sidebar->setFixedWidth(220);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 12, 10, 10);
    sidebarLayout->setSpacing(6);
    auto *sidebarTitle = new QLabel(tr("Routing settings"), sidebar);
    sidebarTitle->setObjectName(QStringLiteral("routeSettingsTitle"));
    sidebarLayout->addWidget(sidebarTitle);
    sidebarLayout->addSpacing(4);

    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    const QMap<int, MaterialIcon::Glyph> navGlyphs{
        {0, MaterialIcon::Glyph::Settings}, {1, MaterialIcon::Glyph::Shield},
        {2, MaterialIcon::Glyph::SwapVertical}, {3, MaterialIcon::Glyph::Public},
        {4, MaterialIcon::Glyph::Routes},
    };
    const QList<int> navOrder{4, 0, 1, 2, 3};
    for (const int pageIndex : navOrder) {
        auto *button = new QPushButton(ui->tabWidget->tabText(pageIndex), sidebar);
        button->setObjectName(QStringLiteral("routeSettingsNav"));
        button->setCheckable(true);
        button->setIcon(MaterialIcon::icon(navGlyphs.value(pageIndex), navIconColor, 18));
        button->setIconSize(QSize(18, 18));
        button->setCursor(Qt::PointingHandCursor);
        navGroup->addButton(button, pageIndex);
        sidebarLayout->addWidget(button);
    }
    sidebarLayout->addStretch(1);
    navGroup->button(ui->tabWidget->currentIndex())->setChecked(true);
    connect(navGroup, &QButtonGroup::idClicked, ui->tabWidget, &QTabWidget::setCurrentIndex);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [navGroup](int index) {
        if (auto *button = navGroup->button(index)) button->setChecked(true);
    });

    work->addWidget(sidebar);
    ui->tabWidget->setParent(body);
    ui->tabWidget->setObjectName(QStringLiteral("routeSettingsPages"));
    work->addWidget(ui->tabWidget, 1);
    bodyLayout->addLayout(work, 1);
    ui->buttonBox->setParent(body);
    bodyLayout->addWidget(ui->buttonBox, 0, Qt::AlignRight);
    root->addWidget(body, 1);
    ui->verticalLayout_3->setAlignment(Qt::AlignTop);
    ui->verticalLayout_7->setAlignment(Qt::AlignTop);
    ui->verticalLayout_8->setAlignment(Qt::AlignTop);
    ui->verticalLayout_4->setAlignment(Qt::AlignTop);
    ui->gridLayout_2->setHorizontalSpacing(14);
    ui->gridLayout_2->setVerticalSpacing(10);
    ui->gridLayout_2->setColumnStretch(0, 0);
    ui->gridLayout_2->setColumnStretch(1, 1);
    ui->route_profiles->setAlternatingRowColors(false);
    for (QPushButton *button : {ui->new_route, ui->clone_route, ui->export_route, ui->import_route,
                                ui->edit_route, ui->delete_route, ui->update_route}) {
        button->setObjectName(QStringLiteral("routeSecondaryButton"));
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(36);
    }
    ui->new_route->setObjectName(QStringLiteral("routeSaveButton"));
    ui->new_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Add, Qt::white, 17));
    ui->edit_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Settings, navIconColor, 17));
    ui->clone_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::File, navIconColor, 17));
    ui->import_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Folder, navIconColor, 17));
    ui->export_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::File, navIconColor, 17));
    ui->update_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Reload, navIconColor, 17));
    ui->delete_route->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Block, QColor(QStringLiteral("#FF5C67")), 17));

    // The Designer profile page was a single expanding list followed by seven
    // equal-width buttons.  At the real window size this produced a huge empty
    // area and clipped every action.  Rebuild that page with the same compact
    // settings components used by tools/ui-demo/SettingsPreview while keeping
    // the original controls (and therefore all existing signal connections).
    ui->route_profiles->setParent(ui->tab_2);
    for (QPushButton *button : {ui->new_route, ui->clone_route, ui->export_route, ui->import_route,
                                ui->edit_route, ui->delete_route, ui->update_route}) {
        button->setParent(ui->tab_2);
    }
    delete ui->route_profiles_box;
    delete ui->tab_2->layout();

    auto *profilesLayout = new QVBoxLayout(ui->tab_2);
    profilesLayout->setContentsMargins(14, 10, 14, 12);
    profilesLayout->setSpacing(10);
    auto *profilesTitle = new QLabel(tr("Routing profiles"), ui->tab_2);
    profilesTitle->setObjectName(QStringLiteral("routeSettingsHero"));
    profilesLayout->addWidget(profilesTitle);
    auto *profilesSubtitle = new QLabel(
        tr("Choose the active profile, edit its rules, or import a reusable configuration."), ui->tab_2);
    profilesSubtitle->setObjectName(QStringLiteral("routeSettingsMuted"));
    profilesSubtitle->setWordWrap(true);
    profilesLayout->addWidget(profilesSubtitle);

    auto *profilesCard = new QFrame(ui->tab_2);
    profilesCard->setObjectName(QStringLiteral("routeProfilesCard"));
    auto *profilesCardLayout = new QVBoxLayout(profilesCard);
    profilesCardLayout->setContentsMargins(12, 12, 12, 12);
    profilesCardLayout->setSpacing(10);
    auto *profilesCardHeading = new QHBoxLayout;
    auto *profilesCardIcon = new QLabel(profilesCard);
    profilesCardIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Routes, QColor(QStringLiteral("#35C2F1")), 20));
    profilesCardHeading->addWidget(profilesCardIcon);
    auto *profilesCardTitle = new QLabel(tr("Available profiles"), profilesCard);
    profilesCardTitle->setObjectName(QStringLiteral("routeSettingsSectionTitle"));
    profilesCardHeading->addWidget(profilesCardTitle);
    profilesCardHeading->addStretch(1);
    profilesCardHeading->addWidget(ui->new_route);
    profilesCardHeading->addWidget(ui->edit_route);
    profilesCardLayout->addLayout(profilesCardHeading);

    ui->route_profiles->setObjectName(QStringLiteral("routeProfilesList"));
    ui->route_profiles->setMinimumHeight(150);
    ui->route_profiles->setMaximumHeight(240);
    ui->route_profiles->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    profilesCardLayout->addWidget(ui->route_profiles);

    auto *secondaryActions = new QHBoxLayout;
    secondaryActions->setSpacing(7);
    secondaryActions->addWidget(ui->clone_route);
    secondaryActions->addWidget(ui->import_route);
    secondaryActions->addWidget(ui->export_route);
    secondaryActions->addWidget(ui->update_route);
    secondaryActions->addStretch(1);
    secondaryActions->addWidget(ui->delete_route);
    profilesCardLayout->addLayout(secondaryActions);
    profilesLayout->addWidget(profilesCard);

    auto *profileHint = new QFrame(ui->tab_2);
    profileHint->setObjectName(QStringLiteral("routeProfilesHint"));
    auto *hintLayout = new QHBoxLayout(profileHint);
    hintLayout->setContentsMargins(12, 10, 12, 10);
    auto *hintIcon = new QLabel(profileHint);
    hintIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Shield, QColor(QStringLiteral("#237AE9")), 18));
    hintLayout->addWidget(hintIcon);
    auto *hintText = new QLabel(
        tr("The selected profile becomes active after you save routing settings."), profileHint);
    hintText->setObjectName(QStringLiteral("routeSettingsMuted"));
    hintText->setWordWrap(true);
    hintLayout->addWidget(hintText, 1);
    profilesLayout->addWidget(profileHint);
    profilesLayout->addStretch(1);
    if (auto *save = ui->buttonBox->button(QDialogButtonBox::Ok)) {
        save->setObjectName(QStringLiteral("routeSaveButton"));
        save->setText(tr("Save settings"));
    }
    if (auto *cancel = ui->buttonBox->button(QDialogButtonBox::Cancel))
        cancel->setObjectName(QStringLiteral("routeSecondaryButton"));
    const QString routeSettingsStyleTemplate = RouteProfileSimpleEditor::dialogStyleSheet() + QStringLiteral(R"(
QDialog#routeProfileEditor QWidget#routeSettingsBody { background: #1B1E23; }
QDialog#routeProfileEditor QFrame#routeSettingsSidebar {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QLabel#routeSettingsTitle {
    color: #F1F3F5; font-size: 14px; font-weight: 700; padding: 0 3px 4px 3px;
}
QDialog#routeProfileEditor QLabel#routeSettingsHero {
    color: #F1F3F5; font-size: 18px; font-weight: 700;
}
QDialog#routeProfileEditor QLabel#routeSettingsSectionTitle {
    color: #F1F3F5; font-size: 15px; font-weight: 700;
}
QDialog#routeProfileEditor QLabel#routeSettingsMuted { color: #AEB7C2; }
QDialog#routeProfileEditor QFrame#routeProfilesCard,
QDialog#routeProfileEditor QFrame#routeProfilesHint {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QListWidget#routeProfilesList {
    background: #14181E; border: 1px solid #2F3136; border-radius: 6px;
    outline: none; padding: 5px;
}
QDialog#routeProfileEditor QPushButton#routeSettingsNav {
    color: #DDE2E7; background: transparent; border: 1px solid transparent;
    border-radius: 6px; padding: 9px 11px; text-align: left;
}
QDialog#routeProfileEditor QPushButton#routeSettingsNav:hover:!checked {
    background: #222529; border-color: #2F3136;
}
QDialog#routeProfileEditor QPushButton#routeSettingsNav:checked {
    color: white; background: #193452; border-color: #237AE9;
}
QDialog#routeProfileEditor QTabWidget#routeSettingsPages::pane {
    background: transparent; border: none;
}
QDialog#routeProfileEditor QTabWidget#routeSettingsPages > QWidget > QWidget {
    background: transparent;
}
QDialog#routeProfileEditor QTabWidget::pane {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QTabBar::tab {
    color: #AEB7C2; background: #171B21; border: 1px solid transparent;
    border-radius: 6px; margin: 0 7px 5px 0; padding: 10px 14px;
    min-width: 142px; min-height: 22px;
}
QDialog#routeProfileEditor QTabBar::tab:selected {
    color: white; background: #193E69; border-color: #237AE9;
}
QDialog#routeProfileEditor QTabBar::tab:hover:!selected {
    color: #E5E8EB; background: #22272E; border-color: #2F3136;
}
QDialog#routeProfileEditor QGroupBox {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 7px; margin-top: 14px; padding: 12px;
}
QDialog#routeProfileEditor QGroupBox::title {
    subcontrol-origin: margin; left: 12px; padding: 0 5px; color: #DDE2E7; font-weight: 600;
}
QDialog#routeProfileEditor QListWidget#routeProfilesList::item {
    border-radius: 5px; padding: 8px 10px; margin: 2px;
}
QDialog#routeProfileEditor QListWidget#routeProfilesList::item:selected {
    color: white; background: #193E69; border: 1px solid #237AE9;
}
QDialog#routeProfileEditor QCheckBox { color: #DDE2E7; spacing: 8px; }
    )");
    themeManager->RegisterStyle(this, routeSettingsStyleTemplate);
    auto profiles = Configs::dataManager->routesRepo->GetAllRouteProfiles();
    for (const auto &item: profiles) {
        chainList << item;
    }
    if (chainList.empty()) {
        auto defaultChain = Configs::RouteProfile::GetDefaultChain();
        Configs::dataManager->routesRepo->AddRouteProfile(defaultChain);
        chainList.append(defaultChain);
    }
    currentRoute = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
    if (currentRoute == nullptr) currentRoute = chainList[0];

    QString dnsHelpDocumentUrl;

    // All four strategy pickers share one order; they used to disagree, so the same
    // position meant "prefer_ipv4" in one and "ipv4_only" in the next.
    ui->default_domain_strategy->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->domainStrategyCombo->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->dns_object->setPlaceholderText(DecodeB64IfValid("ewogICJzZXJ2ZXJzIjogW10sCiAgInJ1bGVzIjogW10sCiAgImZpbmFsIjogIiIsCiAgInN0cmF0ZWd5IjogIiIsCiAgImRpc2FibGVfY2FjaGUiOiBmYWxzZSwKICAiZGlzYWJsZV9leHBpcmUiOiBmYWxzZSwKICAiaW5kZXBlbmRlbnRfY2FjaGUiOiBmYWxzZSwKICAicmV2ZXJzZV9tYXBwaW5nIjogZmFsc2UsCiAgImZha2VpcCI6IHt9Cn0="));
    dnsHelpDocumentUrl = "https://sing-box.sagernet.org/configuration/dns/";

    ui->direct_dns_strategy->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->remote_dns_strategy->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->local_override->setText(Configs::dataManager->settingsRepo->core_box_underlying_dns);
    ui->cache_cap->setText(Int2String(Configs::dataManager->settingsRepo->dns_cache_capacity));
    ui->disable_cache->setChecked(Configs::dataManager->settingsRepo->dns_disable_cache);
    ui->disable_expire->setChecked(Configs::dataManager->settingsRepo->dns_disable_expire);
    ui->reverse_mapping->setChecked(Configs::dataManager->settingsRepo->dns_reverse_mapping);
    ui->enable_fakeip->setChecked(Configs::dataManager->settingsRepo->fake_dns);
    //
    connect(ui->use_dns_object, &QCheckBox::stateChanged, this, [=,this](int state) {
        auto useDNSObject = state == Qt::Checked;
        ui->simple_dns_box->setDisabled(useDNSObject);
        ui->dns_object->setDisabled(!useDNSObject);
    });
    ui->use_dns_object->stateChanged(Qt::Unchecked); // uncheck to uncheck
    connect(ui->dns_document, &QPushButton::clicked, this, [=,this] {
        MessageBoxInfo("DNS", dnsHelpDocumentUrl);
    });
    connect(ui->format_dns_object, &QPushButton::clicked, this, [=,this] {
        auto obj = QString2QJsonObject(ui->dns_object->toPlainText());
        if (obj.isEmpty()) {
            MessageBoxInfo("DNS", "Invalid json");
        } else {
            ui->dns_object->setPlainText(QJsonObject2QString(obj, false));
        }
    });
    ui->ruleset_mirror->setCurrentIndex(Configs::dataManager->settingsRepo->ruleset_mirror);
    ui->default_domain_strategy->setCurrentText(Configs::dataManager->settingsRepo->default_domain_strategy);
    ui->domainStrategyCombo->setCurrentText(Configs::dataManager->settingsRepo->resolve_domain_strategy);
    ui->use_dns_object->setChecked(Configs::dataManager->settingsRepo->use_dns_object);
    ui->dns_object->setPlainText(Configs::dataManager->settingsRepo->dns_object);
    ui->remote_dns->setCurrentText(Configs::dataManager->settingsRepo->remote_dns);
    ui->remote_dns_strategy->setCurrentText(Configs::dataManager->settingsRepo->remote_dns_strategy);
    ui->direct_dns->setCurrentText(Configs::dataManager->settingsRepo->direct_dns);
    ui->direct_dns_strategy->setCurrentText(Configs::dataManager->settingsRepo->direct_dns_strategy);
    ui->dns_final_out->setCurrentText(Configs::dataManager->settingsRepo->dns_final_out);
    ui->enable_dns_routing->setChecked(Configs::dataManager->settingsRepo->enable_dns_routing);
    reloadProfileItems();

    connect(ui->route_profiles, &QListWidget::itemDoubleClicked, this, [=,this](const QListWidgetItem* item){
        on_edit_route_clicked();
    });

    connect(ui->route_prof, SIGNAL(currentIndexChanged(int)), this, SLOT(updateCurrentRouteProfile(int)));

    deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);

    connect(deleteShortcut, &QShortcut::activated, this, [=,this]{
        on_delete_route_clicked();
    });

    // Ctrl+C / Ctrl+V on the profile list act as Export / Import. Scoped to the list so
    // they don't hijack normal copy/paste in the dialog's many text fields.
    auto exportShortcut = new QShortcut(QKeySequence::Copy, ui->route_profiles);
    exportShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(exportShortcut, &QShortcut::activated, this, [=,this]{
        on_export_route_clicked();
    });

    auto importShortcut = new QShortcut(QKeySequence::Paste, ui->route_profiles);
    importShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(importShortcut, &QShortcut::activated, this, [=,this]{
        on_import_route_clicked();
    });

    // hijack
    ui->dnshijack_enable->setChecked(Configs::dataManager->settingsRepo->enable_dns_server);
    set_dns_hijack_enability(Configs::dataManager->settingsRepo->enable_dns_server);
    ui->dnshijack_allow_lan->setChecked(Configs::dataManager->settingsRepo->dns_server_listen_lan);
    ui->dnshijack_listenport->setValidator(QRegExpValidator_Number);
    ui->dnshijack_listenport->setText(Int2String(Configs::dataManager->settingsRepo->dns_server_listen_port));
    ui->dnshijack_v4resp->setText(Configs::dataManager->settingsRepo->dns_v4_resp);
    ui->dnshijack_v6resp->setText(Configs::dataManager->settingsRepo->dns_v6_resp);
    connect(ui->dnshijack_what, &QPushButton::clicked, this, [=,this] {
        MessageBoxInfo("What is this?", Configs::Information::HijackInfo);
    });

    QStringList ruleItems = {"domain:", "suffix:", "regex:"};
    for (const auto& item : ruleSetList) {
        ruleItems.append("ruleset:" + QString::fromUtf8(item.first.data(), item.first.size()));
    }
    rule_editor = new AutoCompleteTextEdit("", ruleItems, this);
    ui->hijack_box->layout()->replaceWidget(ui->dnshijack_rules, rule_editor);
    rule_editor->setPlainText(Configs::dataManager->settingsRepo->dns_server_rules.join("\n"));
    ui->dnshijack_rules->hide();
#ifndef Q_OS_LINUX
    ui->dnshijack_listenport->setVisible(false);
    ui->dnshijack_listenport_l->setVisible(false);
#endif

    ui->redirect_enable->setChecked(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenaddr->setEnabled(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenaddr->setText(Configs::dataManager->settingsRepo->redirect_listen_address);
    ui->redirect_listenport->setEnabled(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenport->setValidator(QRegExpValidator_Number);
    ui->redirect_listenport->setText(Int2String(Configs::dataManager->settingsRepo->redirect_listen_port));

    connect(ui->dnshijack_enable, &QCheckBox::stateChanged, this, [=,this](bool state) {
        set_dns_hijack_enability(state);
    });
    connect(ui->redirect_enable, &QCheckBox::stateChanged, this, [=,this](bool state) {
        ui->redirect_listenaddr->setEnabled(state);
        ui->redirect_listenport->setEnabled(state);
    });

    // warp
    ui->enable_warp->setChecked(Configs::dataManager->settingsRepo->enable_warp);
    ui->warp_private_key->setText(Configs::dataManager->settingsRepo->warp_private_key);
    ui->warp_public_key->setText(Configs::dataManager->settingsRepo->warp_public_key);
    ui->warp_ifc_addrs->setText(Configs::dataManager->settingsRepo->warp_ifc_addrs.join(","));
    ui->warp_ep->setText(Configs::dataManager->settingsRepo->warp_ep);
    ui->warp_reserved->setText(Configs::dataManager->settingsRepo->warp_reserved.join(","));
    connect(ui->warp_autogen, &QPushButton::clicked, this, [=,this] {
        auto originalText = ui->warp_autogen->text();
        ui->warp_autogen->setText("Getting keypair...");
        bool ok;
        auto keyPair = API::defaultClient->GenWgKeyPair(&ok);
        if (!ok) {
            runOnUiThread([=] {
               MessageBoxWarning("Failed to get key pair", keyPair.error->c_str());
            });
            ui->warp_autogen->setText(originalText);
            return;
        }
        ui->warp_autogen->setText("Generating config...");
        QString error;
        auto conf = Configs_network::genWarpConfig(&error, keyPair.private_key->c_str(), keyPair.public_key->c_str());
        if (!error.isEmpty()) {
            runOnUiThread([=] {
                MessageBoxWarning("Failed to generate warp config", error);
            });
            ui->warp_autogen->setText(originalText);
            return;
        }
        ui->warp_private_key->setText(conf->privateKey);
        ui->warp_public_key->setText(conf->publicKey);
        ui->warp_ep->setText(conf->endpoint);
        ui->warp_ifc_addrs->setText(conf->ipv4Address + "/32," + conf->ipv6Address + "/128");
        ui->warp_reserved->setText(QListInt2QListString(conf->reserved).join(","));
        ui->warp_autogen->setText("Success!");
        setTimeout([=,this] { ui->warp_autogen->setText(originalText); }, this, 2000);
    });

    ADD_ASTERISK(this)
}

void DialogManageRoutes::updateCurrentRouteProfile(int idx) {
    currentRoute = chainList[idx];
}

DialogManageRoutes::~DialogManageRoutes() {
    delete ui;
}

void DialogManageRoutes::accept() {
    if (chainList.empty()) {
        MessageBoxInfo(tr("Invalid settings"), tr("Routing profile cannot be empty"));
        return;
    }
    if (!validate_dns_rules(rule_editor->toPlainText())) {
        MessageBoxInfo(tr("Invalid settings"), tr("DNS Rules are not valid"));
        return;
    }

    Configs::dataManager->settingsRepo->ruleset_mirror = ui->ruleset_mirror->currentIndex();
    Configs::dataManager->settingsRepo->resolve_domain_strategy = ui->domainStrategyCombo->currentText();
    Configs::dataManager->settingsRepo->default_domain_strategy = ui->default_domain_strategy->currentText();
    Configs::dataManager->settingsRepo->use_dns_object = ui->use_dns_object->isChecked();
    Configs::dataManager->settingsRepo->dns_object = ui->dns_object->toPlainText();
    Configs::dataManager->settingsRepo->remote_dns = ui->remote_dns->currentText();
    Configs::dataManager->settingsRepo->remote_dns_strategy = ui->remote_dns_strategy->currentText();
    Configs::dataManager->settingsRepo->dns_cache_capacity = ui->cache_cap->text().toInt();
    Configs::dataManager->settingsRepo->dns_disable_cache = ui->disable_cache->isChecked();
    Configs::dataManager->settingsRepo->dns_disable_expire = ui->disable_expire->isChecked();
    Configs::dataManager->settingsRepo->dns_reverse_mapping = ui->reverse_mapping->isChecked();
    Configs::dataManager->settingsRepo->direct_dns = ui->direct_dns->currentText();
    Configs::dataManager->settingsRepo->direct_dns_strategy = ui->direct_dns_strategy->currentText();
    Configs::dataManager->settingsRepo->core_box_underlying_dns = ui->local_override->text().trimmed();
    Configs::dataManager->settingsRepo->dns_final_out = ui->dns_final_out->currentText();
    Configs::dataManager->settingsRepo->fake_dns = ui->enable_fakeip->isChecked();
    Configs::dataManager->settingsRepo->enable_dns_routing = ui->enable_dns_routing->isChecked();

    Configs::dataManager->routesRepo->UpdateRouteProfiles(chainList);
    Configs::dataManager->settingsRepo->current_route_id = currentRoute->id;

    Configs::dataManager->settingsRepo->enable_dns_server = ui->dnshijack_enable->isChecked();
    Configs::dataManager->settingsRepo->dns_server_listen_port = ui->dnshijack_listenport->text().toInt();
    Configs::dataManager->settingsRepo->dns_v4_resp = ui->dnshijack_v4resp->text();
    Configs::dataManager->settingsRepo->dns_v6_resp = ui->dnshijack_v6resp->text();
    auto rawRules = rule_editor->toPlainText().split("\n");
    QStringList dnsRules;
    for (const auto& rawRule : rawRules) {
        if (rawRule.trimmed().isEmpty()) continue;
        dnsRules.append(rawRule.trimmed());
    }
    Configs::dataManager->settingsRepo->dns_server_rules = dnsRules;

    Configs::dataManager->settingsRepo->dns_server_listen_lan = ui->dnshijack_allow_lan->isChecked();
    Configs::dataManager->settingsRepo->enable_redirect = ui->redirect_enable->isChecked();
    Configs::dataManager->settingsRepo->redirect_listen_address = ui->redirect_listenaddr->text();
    Configs::dataManager->settingsRepo->redirect_listen_port = ui->redirect_listenport->text().toInt();

    // warp
    Configs::dataManager->settingsRepo->enable_warp = ui->enable_warp->isChecked();
    Configs::dataManager->settingsRepo->warp_ep = ui->warp_ep->text();
    Configs::dataManager->settingsRepo->warp_ifc_addrs = SplitAndTrim(ui->warp_ifc_addrs->text(), ",", false);
    Configs::dataManager->settingsRepo->warp_private_key = ui->warp_private_key->text();
    Configs::dataManager->settingsRepo->warp_public_key = ui->warp_public_key->text();
    Configs::dataManager->settingsRepo->warp_reserved = SplitAndTrim(ui->warp_reserved->text(), ",", false);

    //
    MW_dialog_message(MwMessage::UpdateSettings, {MwArg::Route});

    QDialog::accept();
}

void DialogManageRoutes::on_new_route_clicked() {
    QMenu menu(this);
    menu.addAction(tr("Structured profile"));
    auto* rawAct = menu.addAction(tr("Raw profile"));
    auto* remoteAct = menu.addAction(tr("Remote profile"));
    auto* chosen = menu.exec(ui->new_route->mapToGlobal(QPoint(0, ui->new_route->height())));
    if (chosen == nullptr) return;

    auto newProfile = Configs::dataManager->routesRepo->NewRouteProfile();
    auto onCreated = [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        chainList << chain;
        reloadProfileItems();
    };
    if (chosen == rawAct) {
        newProfile->isRaw = true;
        auto* rawWidget = new RawRouteItem(this, newProfile);
        rawWidget->setWindowModality(Qt::ApplicationModal);
        rawWidget->show();
        connect(rawWidget, &RawRouteItem::settingsChanged, this, onCreated);
    } else {
        // Remote profiles are structured underneath: reuse the structured editor, which shows
        // the extra "Remote source" section (URL / auto-update / preview) when isRemote is set.
        if (chosen == remoteAct) newProfile->isRemote = true;
        routeChainWidget = new RouteItem(this, newProfile);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, onCreated);
    }
}

void DialogManageRoutes::on_export_route_clicked()
{
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    QApplication::clipboard()->setText(chainList[idx]->ToShareLink());

    QToolTip::showText(QCursor::pos(), tr("Copied!"), this);
    int r = ++tooltipID;
    QTimer::singleShot(1500, [=,this] {
        if (tooltipID != r) return;
        QToolTip::hideText();
    });
}

void DialogManageRoutes::applyImportedProfile(const std::shared_ptr<Configs::RouteProfile>& profile, bool wasOldArray)
{
    if (wasOldArray) {
        // A legacy rule array carries no name / default outbound: open the editor
        // pre-filled with the rules so the user can complete it before saving.
        auto shell = Configs::dataManager->routesRepo->NewRouteProfile();
        shell->Rules = profile->Rules;
        routeChainWidget = new RouteItem(this, shell);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
            chainList << chain;
            reloadProfileItems();
        });
    } else {
        // A complete profile: add it directly, no editor.
        chainList << profile;
        currentRoute = profile;
        reloadProfileItems();
    }
}

bool DialogManageRoutes::tryImportRemoteRoutesLink(const QString& text)
{
    bool wasRemoteRouteLink = false;
    QString error;
    auto profiles = Configs::RouteProfile::FromRemoteRoutesLink(text, &wasRemoteRouteLink, &error);
    if (!wasRemoteRouteLink) return false; // not a remoteRoute link; let the caller try other formats

    if (profiles.isEmpty()) {
        MessageBoxWarning(tr("Add remote routing profiles"),
                          error.isEmpty() ? tr("No valid remote routing profiles in the link.") : error);
        return true;
    }

    QString prompt = tr("Add these remote routing profiles?") + "\n";
    for (int i = 0; i < profiles.size(); ++i) {
        prompt += QString("\n%1. %2  (%3: %4)")
                      .arg(i + 1)
                      .arg(profiles[i]->remoteURL, tr("auto update"), profiles[i]->autoUpdate ? tr("On") : tr("Off"));
    }
    if (QMessageBox::question(this, tr("Add remote routing profiles"), prompt) != QMessageBox::StandardButton::Yes) {
        return true; // it was a remoteRoute link; the user declined
    }

    for (const auto& p : profiles) chainList << p;
    reloadProfileItems();
    // Fetch the newly added profiles with the Update-button progress UI; persisted on accept().
    updateRemoteProfiles(profiles);
    return true;
}

void DialogManageRoutes::on_import_route_clicked()
{
    // Fast path: if the clipboard already holds a usable candidate, just confirm and
    // import it — no need to make the user paste back what they already copied.
    const QString clip = QApplication::clipboard()->text().trimmed();
    // A throne://remoteRoute deep link adds one or more remote profiles at once.
    if (tryImportRemoteRoutesLink(clip)) return;
    if (!clip.isEmpty()) {
        QString fatal, warnings;
        bool wasOldArray = false;
        if (auto profile = Configs::RouteProfile::FromShareInput(clip, &fatal, &warnings, &wasOldArray)) {
            const QString what = wasOldArray ? tr("a routing rule list")
                                             : tr("routing profile \"%1\"").arg(profile->name);
            if (QMessageBox::question(this, tr("Import from clipboard"),
                                      tr("Import %1 from the clipboard?").arg(what))
                == QMessageBox::StandardButton::Yes) {
                if (!warnings.isEmpty()) MessageBoxInfo(tr("Imported with warnings"), warnings);
                applyImportedProfile(profile, wasOldArray);
                return;
            }
            // Declined: fall through to the manual paste dialog below.
        }
    }

    // Manual path: let the user paste; the placeholder explains the accepted formats.
    auto w = new QDialog(this);
    w->setWindowTitle(tr("Import routing profile"));
    w->setWindowModality(Qt::ApplicationModal);

    auto layout = new QGridLayout(w);
    auto tEdit = new QTextEdit(w);
    tEdit->setPlaceholderText(tr("Paste a Throned route link, a remoteRoute link, a base64 blob, or a JSON rule array"));
    layout->addWidget(tEdit, 0, 0);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, w);
    layout->addWidget(buttons, 1, 0);

    connect(buttons, &QDialogButtonBox::accepted, w, [=, this] {
        // remoteRoute deep link: add remote profiles and close.
        if (tryImportRemoteRoutesLink(tEdit->toPlainText())) { w->accept(); return; }
        QString fatal, warnings;
        bool wasOldArray = false;
        auto profile = Configs::RouteProfile::FromShareInput(tEdit->toPlainText(), &fatal, &warnings, &wasOldArray);
        if (!profile) {
            MessageBoxWarning(tr("Invalid input"), tr("Could not import this routing profile:\n") + fatal);
            return;
        }
        if (!warnings.isEmpty()) MessageBoxInfo(tr("Imported with warnings"), warnings);
        applyImportedProfile(profile, wasOldArray);
        w->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, w, &QDialog::reject);

    w->exec();
    w->deleteLater();
}

void DialogManageRoutes::on_clone_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    auto chainCopy = std::make_shared<Configs::RouteProfile>(*chainList[idx]);
    chainCopy->name = chainCopy->name + " clone";
    chainCopy->id = -1;
    chainList.append(chainCopy);
    reloadProfileItems();
}

void DialogManageRoutes::on_edit_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    auto onEdited = [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        if (currentRoute == chainList[idx]) currentRoute = chain;
        chainList[idx] = chain;
        reloadProfileItems();
    };

    if (chainList[idx]->isRaw) {
        auto* rawWidget = new RawRouteItem(this, chainList[idx]);
        rawWidget->setWindowModality(Qt::ApplicationModal);
        rawWidget->show();
        connect(rawWidget, &RawRouteItem::settingsChanged, this, onEdited);
    } else {
        routeChainWidget = new RouteItem(this, chainList[idx]);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, onEdited);
    }
}

void DialogManageRoutes::on_delete_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;
    if (chainList.size() == 1) {
        MessageBoxWarning(tr("Invalid operation"), tr("Routing Profiles cannot be empty, try adding another profile or editing this one"));
        return;
    }

    auto profileToDel = chainList[idx];
    chainList.removeAt(idx);
    if (profileToDel == currentRoute) {
        currentRoute = chainList[0];
    }
    reloadProfileItems();
}

void DialogManageRoutes::on_update_route_clicked() {
    // While a batch is running the button shows progress; clicking it offers only Cancel.
    if (routeUpdateRunning) {
        QMenu menu(this);
        auto* cancelAct = menu.addAction(tr("Cancel"));
        auto* chosen = menu.exec(ui->update_route->mapToGlobal(QPoint(0, ui->update_route->height())));
        // Guard against the batch having finished while the menu was open.
        if (chosen == cancelAct && routeUpdateRunning) {
            routeUpdateCancel = true;
            ui->update_route->setText(tr("Cancelling..."));
        }
        return;
    }

    const int idx = ui->route_profiles->currentRow();
    const bool selIsRemote = idx >= 0 && chainList[idx]->isRemote;

    QMenu menu(this);
    // Only offer "Update selected" when the selection is actually a remote profile, so the
    // menu never presents an action that would just error out.
    QAction* updateSelAct = selIsRemote ? menu.addAction(tr("Update selected")) : nullptr;
    auto* updateAllAct = menu.addAction(tr("Update all"));
    auto* chosen = menu.exec(ui->update_route->mapToGlobal(QPoint(0, ui->update_route->height())));
    if (chosen == nullptr) return;

    if (chosen == updateSelAct) {
        updateRemoteProfiles({chainList[idx]});
        return;
    }

    if (chosen == updateAllAct) {
        QList<std::shared_ptr<Configs::RouteProfile>> remotes;
        for (const auto& p : chainList) {
            if (p->isRemote && !p->remoteURL.trimmed().isEmpty()) remotes << p;
        }
        if (remotes.isEmpty()) {
            MessageBoxInfo(tr("No remote profiles"), tr("There are no remote routing profiles to update."));
            return;
        }
        updateRemoteProfiles(remotes);
    }
}

void DialogManageRoutes::updateRemoteProfiles(const QList<std::shared_ptr<Configs::RouteProfile>>& profiles) {
    if (routeUpdateRunning || profiles.isEmpty()) return;
    routeUpdateRunning = true;
    routeUpdateCancel = false;
    const int total = profiles.size();

    // "Updating..." for a single profile; a running "Updating (n / total)" for a batch. The
    // button stays enabled during the run so its click can offer Cancel (see the slot above).
    auto progressText = [total](int current) {
        return total <= 1 ? tr("Updating...") : tr("Updating (%1 / %2)").arg(current).arg(total);
    };
    ui->update_route->setText(progressText(1));

    runOnNewThread([=, this] {
        QStringList failures;
        int ok = 0;
        for (int i = 0; i < profiles.size(); ++i) {
            if (routeUpdateCancel.load()) break;
            const int current = i + 1;
            runOnUiThread([=, this] {
                if (routeUpdateRunning && !routeUpdateCancel.load())
                    ui->update_route->setText(progressText(current));
            });
            QString warnings;
            const QString err = RouteUpdate::UpdateProfile(profiles[i], &warnings);
            if (err.isEmpty()) ok++;
            else failures << (profiles[i]->name + ": " + err);
        }
        const bool cancelled = routeUpdateCancel.load();
        runOnUiThread([=, this] {
            routeUpdateRunning = false;
            ui->update_route->setText(tr("Update"));
            reloadProfileItems();
            if (cancelled) {
                MessageBoxInfo(tr("Update cancelled"),
                               tr("Cancelled: updated %1 of %2, %3 failed.").arg(ok).arg(total).arg(failures.size()));
            } else if (failures.isEmpty()) {
                MessageBoxInfo(tr("Update complete"), tr("Updated %1 remote routing profile(s).").arg(ok));
            } else {
                MessageBoxWarning(tr("Update finished with errors"),
                                  tr("Updated %1, failed %2:\n%3").arg(ok).arg(failures.size()).arg(failures.join("\n")));
            }
        });
    });
}
