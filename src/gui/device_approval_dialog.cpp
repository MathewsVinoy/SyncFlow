#include "syncflow/gui/device_approval_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>

namespace syncflow::gui {

DeviceApprovalDialog::DeviceApprovalDialog(const QString& device_name,
                                                                                     const QString& device_ip,
                                                                                     const QString& fingerprint,
                                                                                     QWidget* parent)
        : QDialog(parent),
            device_name_(device_name),
            fingerprint_(fingerprint),
            device_ip_(device_ip),
            approved_(false) {
    
    setWindowTitle("Device Approval");
    setMinimumWidth(600);
    setModal(true);
    setupUI();
}

void DeviceApprovalDialog::setupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(15);

    // Header message
    QLabel* message_label = new QLabel(
        "A new device is requesting to connect to your Syncflow network.\n"
        "Please review the device information and fingerprint below before approving.",
        this);
    message_label->setWordWrap(true);
    main_layout->addWidget(message_label);

    // Device Info Section
    QLabel* info_label = new QLabel("Device Information:", this);
    QFont font = info_label->font();
    font.setBold(true);
    info_label->setFont(font);
    main_layout->addWidget(info_label);

    QVBoxLayout* info_layout = new QVBoxLayout();

    device_name_display_ = new QLabel(device_name_, this);
    info_layout->addLayout(createLabelValueLayout("Device Name:", device_name_display_));

    device_ip_display_ = new QLabel(device_ip_, this);
    info_layout->addLayout(createLabelValueLayout("Device IP:", device_ip_display_));

    main_layout->addLayout(info_layout);

    // Fingerprint Section
    QLabel* fingerprint_label = new QLabel("Device Fingerprint (SHA-256):", this);
    font = fingerprint_label->font();
    font.setBold(true);
    fingerprint_label->setFont(font);
    main_layout->addWidget(fingerprint_label);

    QHBoxLayout* fingerprint_layout = new QHBoxLayout();

    fingerprint_display_ = new QTextEdit(this);
    fingerprint_display_->setText(fingerprint_);
    fingerprint_display_->setReadOnly(true);
    fingerprint_display_->setMaximumHeight(80);
    fingerprint_layout->addWidget(fingerprint_display_);

    copy_button_ = new QPushButton("Copy", this);
    copy_button_->setMaximumWidth(80);
    connect(copy_button_, &QPushButton::clicked,
            this, &DeviceApprovalDialog::onCopyFingerprintClicked);
    fingerprint_layout->addWidget(copy_button_, 0, Qt::AlignTop);

    main_layout->addLayout(fingerprint_layout);

    // Spacer
    main_layout->addSpacing(20);

    // Warning/Info message
    QLabel* warning_label = new QLabel(
        "⚠ Only approve devices you recognize and trust.\n"
        "Check the fingerprint against the connecting device to prevent unauthorized access.",
        this);
    warning_label->setWordWrap(true);
    warning_label->setStyleSheet("color: #ff6600; background-color: #fffacd; padding: 10px; border-radius: 4px;");
    main_layout->addWidget(warning_label);

    // Buttons
    main_layout->addSpacing(20);
    QHBoxLayout* button_layout = new QHBoxLayout();
    button_layout->addStretch();

    approve_button_ = new QPushButton("Approve", this);
    approve_button_->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 5px 20px; }");
    connect(approve_button_, &QPushButton::clicked,
            this, &DeviceApprovalDialog::onApproveClicked);
    button_layout->addWidget(approve_button_);

    deny_button_ = new QPushButton("Deny", this);
    deny_button_->setStyleSheet("QPushButton { background-color: #f44336; color: white; padding: 5px 20px; }");
    connect(deny_button_, &QPushButton::clicked,
            this, &DeviceApprovalDialog::onDenyClicked);
    button_layout->addWidget(deny_button_);

    main_layout->addLayout(button_layout);

    setLayout(main_layout);
}

QHBoxLayout* DeviceApprovalDialog::createLabelValueLayout(const QString& label_text,
                                                          QWidget* value_widget) {
    QHBoxLayout* layout = new QHBoxLayout();
    QLabel* label = new QLabel(label_text, this);
    label->setMinimumWidth(100);
    layout->addWidget(label);
    layout->addWidget(value_widget);
    return layout;
}

void DeviceApprovalDialog::onCopyFingerprintClicked() {
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(fingerprint_);
    QMessageBox::information(this, "Copied", "Fingerprint copied to clipboard.");
}

void DeviceApprovalDialog::onApproveClicked() {
    approved_ = true;
    setResult(QDialog::Accepted);
    accept();
}

void DeviceApprovalDialog::onDenyClicked() {
    approved_ = false;
    setResult(QDialog::Rejected);
    reject();
}

QString DeviceApprovalDialog::getDeviceName() const {
    return device_name_;
}

QString DeviceApprovalDialog::getDeviceIP() const {
    return device_ip_;
}

QString DeviceApprovalDialog::getFingerprint() const {
    return fingerprint_;
}

}  // namespace syncflow::gui
