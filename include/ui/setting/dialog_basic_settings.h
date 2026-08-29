#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QTimer>
#include "include/ui/setting/SettingsBindings.h"
#include "ui_dialog_basic_settings.h"

namespace Ui {
    class DialogBasicSettings;
}

class DialogBasicSettings : public QDialog {
    Q_OBJECT

public:
    explicit DialogBasicSettings(QWidget *parent = nullptr);

    ~DialogBasicSettings();

public slots:

    void accept();

private:
    // Pages migrated off the .ui build their controls where they are used and bind
    // each one to its setting, so nothing has to be moved between parents and nothing
    // has to be remembered in accept(). Pages still on the .ui are unaffected.
    SettingsBindings bindings_;
    QCheckBox *alwaysStandardUser_ = nullptr;
    QTimer *themeApplyDebounce_ = nullptr;

    Ui::DialogBasicSettings *ui;

    // Quick Links (custom toolbar buttons)
    QLineEdit *m_quickLinkName1 = nullptr;
    QLineEdit *m_quickLink1 = nullptr;
    QLineEdit *m_quickLinkName2 = nullptr;
    QLineEdit *m_quickLink2 = nullptr;
    QLineEdit *m_quickLinkName3 = nullptr;
    QLineEdit *m_quickLink3 = nullptr;

    void applySelectedTheme();
    void applyRegexHighlighting();

    void downloadXrayGeoAsset(const QString &url, const QString &fileName);

    struct {
        QString custom_inbound;
        bool needRestart = false;
        bool updateDisableTray = false;
        bool updateTrayIcon = false;
        bool updateSystemDns = false;
        bool updateMaxLogLines = false;
        bool updateDisableAdmin = false;
    } CACHE;

private slots:
    void on_backup_create_clicked();
    void on_backup_restore_clicked();
    void on_xray_geoip_download_clicked();
    void on_xray_geosite_download_clicked();
};
