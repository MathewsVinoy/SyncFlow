#pragma once

#include <QDialog>
#include <QLabel>
#include <QTextEdit>
#include <QHBoxLayout>
#include <QPushButton>
#include <QString>

namespace syncflow::gui {

class DeviceApprovalDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceApprovalDialog(const QString& device_name, const QString& device_ip,
                                   const QString& fingerprint, QWidget* parent = nullptr);

    bool isApproved() const { return approved_; }

    QString getDeviceName() const;
    QString getDeviceIP() const;
    QString getFingerprint() const;

private slots:
    void onApproveClicked();
    void onDenyClicked();
    void onCopyFingerprintClicked();

private:
    void setupUI();
    QHBoxLayout* createLabelValueLayout(const QString& label_text, QWidget* value_widget);

    QString device_name_;
    QString fingerprint_;
    QString device_ip_;
    bool approved_;

    QLabel* info_label_;
    QLabel* fingerprint_label_;
    QLabel* device_name_display_;
    QLabel* device_ip_display_;
    QTextEdit* fingerprint_display_;
    QPushButton* approve_button_;
    QPushButton* deny_button_;
    QPushButton* copy_button_;
};

}  // namespace syncflow::gui
