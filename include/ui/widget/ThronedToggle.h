#pragma once

#include <QAbstractButton>

// The exact toggle used by the UI preview.  It can mirror an existing
// QCheckBox/QAbstractButton so production logic keeps using the original
// settings control while the visible widget stays pixel-identical.
class ThronedToggle final : public QAbstractButton {
public:
    explicit ThronedToggle(bool checked = false, QWidget *parent = nullptr);

    void bindTo(QAbstractButton *source);

protected:
    void paintEvent(QPaintEvent *event) override;
};
