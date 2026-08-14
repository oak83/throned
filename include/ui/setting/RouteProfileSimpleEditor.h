#pragma once

#include <QMap>
#include <QStringList>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QVBoxLayout;

class RouteProfileSimpleEditor final : public QWidget {
    Q_OBJECT

public:
    explicit RouteProfileSimpleEditor(QWidget *parent = nullptr);

    [[nodiscard]] static QString dialogStyleSheet();

    void setRules(int action, const QString &rules);
    [[nodiscard]] QString rules(int action) const;
    void setAdvancedRuleCount(int count);
    void setAdvancedRules(const QStringList &names);
    void setRuleSetCatalog(const QStringList &names);
    void setLocalProxyTrafficEnabled(bool enabled);

signals:
    void rulesChanged(int action, const QString &rules);
    void localProxyTrafficChanged(bool enabled);
    void advancedEditorRequested();

private:
    void selectAction(int action);
    void rebuild();
    void addApplicationRules();
    void addRule(const QString &section);
    void removeRule(const QString &rule);
    void updateActionButtons();
    void updateTotalCount();

    int selectedAction_ = 2;
    int advancedRuleCount_ = 0;
    QStringList advancedRules_;
    QStringList ruleSetCatalog_;
    QMap<int, QStringList> rules_;
    QMap<int, QAbstractButton *> actionButtons_;
    QLabel *heading_ = nullptr;
    QLabel *description_ = nullptr;
    QLabel *totalLabel_ = nullptr;
    QVBoxLayout *cardsLayout_ = nullptr;
    QWidget *quickOptionsCard_ = nullptr;
    QAbstractButton *localProxyToggle_ = nullptr;
    bool localProxyTrafficEnabled_ = false;
};
