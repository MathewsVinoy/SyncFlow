#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QString>

namespace syncflow::gui {

class DeviceApprovalDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceApprovalDialog(const QString& device_name, const QString& fingerprint,
                                   const QString& ip_address, QWidget* parent = nullptr);

    bool isApproved() const { return approved_; }

private slots:
    void onApproveClicked();
    void onDenyClicked();
    void onCopyFingerprintClicked();

private:
    void setupUI();

    QString device_name_;
    QString fingerprint_;
    QString ip_address_;
    bool approved_;

    QLabel* info_label_;
    QLabel* fingerprint_label_;
    QPushButton* approve_button_;
    QPushButton* deny_button_;
    QPushButton* copy_button_;
};

}  // namespace syncflow::gui
