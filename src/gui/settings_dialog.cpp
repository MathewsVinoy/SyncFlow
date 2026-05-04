#include "syncflow/gui/settings_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QSettings>
#include <QGroupBox>
#include <QFormLayout>

namespace syncflow::gui {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    
    setWindowTitle("Settings");
    setMinimumWidth(500);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);

    // General Settings
    QGroupBox* general_group = new QGroupBox("General Settings", this);
    QFormLayout* general_layout = new QFormLayout(general_group);

    device_name_edit_ = new QLineEdit(this);
    general_layout->addRow("Device Name:", device_name_edit_);

    file_sync_checkbox_ = new QCheckBox("Enable File Sync", this);
    general_layout->addRow(file_sync_checkbox_);

    general_group->setLayout(general_layout);
    main_layout->addWidget(general_group);

    // File Sync Settings
    QGroupBox* sync_group = new QGroupBox("File Sync Settings", this);
    QFormLayout* sync_layout = new QFormLayout(sync_group);

    source_path_edit_ = new QLineEdit(this);
    browse_source_button_ = new QPushButton("Browse", this);
    QHBoxLayout* source_layout = new QHBoxLayout();
    source_layout->addWidget(source_path_edit_);
    source_layout->addWidget(browse_source_button_);
    sync_layout->addRow("Source Directory:", source_layout);

    receive_dir_edit_ = new QLineEdit(this);
    browse_receive_button_ = new QPushButton("Browse", this);
    QHBoxLayout* receive_layout = new QHBoxLayout();
    receive_layout->addWidget(receive_dir_edit_);
    receive_layout->addWidget(browse_receive_button_);
    sync_layout->addRow("Receive Directory:", receive_layout);

    sync_group->setLayout(sync_layout);
    main_layout->addWidget(sync_group);

    // Security Settings
    QGroupBox* security_group = new QGroupBox("Security Settings", this);
    QFormLayout* security_layout = new QFormLayout(security_group);

    security_checkbox_ = new QCheckBox("Enable Security", this);
    security_layout->addRow(security_checkbox_);

    require_approval_checkbox_ = new QCheckBox("Require Device Approval", this);
    security_layout->addRow(require_approval_checkbox_);

    security_group->setLayout(security_layout);
    main_layout->addWidget(security_group);

    // Buttons
    QHBoxLayout* button_layout = new QHBoxLayout();

    apply_button_ = new QPushButton("Apply", this);
    cancel_button_ = new QPushButton("Cancel", this);
    reset_button_ = new QPushButton("Reset", this);

    button_layout->addWidget(apply_button_);
    button_layout->addWidget(reset_button_);
    button_layout->addWidget(cancel_button_);
    button_layout->addStretch();

    main_layout->addLayout(button_layout);

    // Connect signals
    connect(browse_source_button_, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseSourceClicked);
    connect(browse_receive_button_, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseReceiveDirClicked);
    connect(apply_button_, &QPushButton::clicked,
            this, &SettingsDialog::onApplyClicked);
    connect(cancel_button_, &QPushButton::clicked,
            this, &SettingsDialog::onCancelClicked);
    connect(reset_button_, &QPushButton::clicked,
            this, &SettingsDialog::onResetClicked);
}

void SettingsDialog::loadSettings() {
    QSettings settings("Syncflow", "Syncflow");

    device_name_edit_->setText(settings.value("device_name", "my-device").toString());
    source_path_edit_->setText(settings.value("source_path", "sync/").toString());
    receive_dir_edit_->setText(settings.value("receive_dir", "received/").toString());
    file_sync_checkbox_->setChecked(settings.value("file_sync_enabled", true).toBool());
    security_checkbox_->setChecked(settings.value("security_enabled", true).toBool());
    require_approval_checkbox_->setChecked(settings.value("require_approval", true).toBool());
}

void SettingsDialog::onBrowseSourceClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Source Directory");
    if (!dir.isEmpty()) {
        source_path_edit_->setText(dir);
    }
}

void SettingsDialog::onBrowseReceiveDirClicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Receive Directory");
    if (!dir.isEmpty()) {
        receive_dir_edit_->setText(dir);
    }
}

void SettingsDialog::onApplyClicked() {
    QSettings settings("Syncflow", "Syncflow");
    settings.setValue("device_name", device_name_edit_->text());
    settings.setValue("source_path", source_path_edit_->text());
    settings.setValue("receive_dir", receive_dir_edit_->text());
    settings.setValue("file_sync_enabled", file_sync_checkbox_->isChecked());
    settings.setValue("security_enabled", security_checkbox_->isChecked());
    settings.setValue("require_approval", require_approval_checkbox_->isChecked());
    settings.sync();

    accept();
}

void SettingsDialog::onCancelClicked() {
    reject();
}

void SettingsDialog::onResetClicked() {
    loadSettings();
}

QString SettingsDialog::getDeviceName() const {
    return device_name_edit_->text();
}

void SettingsDialog::setDeviceName(const QString& name) {
    device_name_edit_->setText(name);
}

QString SettingsDialog::getSourcePath() const {
    return source_path_edit_->text();
}

void SettingsDialog::setSourcePath(const QString& path) {
    source_path_edit_->setText(path);
}

QString SettingsDialog::getReceiveDir() const {
    return receive_dir_edit_->text();
}

void SettingsDialog::setReceiveDir(const QString& path) {
    receive_dir_edit_->setText(path);
}

bool SettingsDialog::isFileSyncEnabled() const {
    return file_sync_checkbox_->isChecked();
}

void SettingsDialog::setFileSyncEnabled(bool enabled) {
    file_sync_checkbox_->setChecked(enabled);
}

bool SettingsDialog::isSecurityEnabled() const {
    return security_checkbox_->isChecked();
}

void SettingsDialog::setSecurityEnabled(bool enabled) {
    security_checkbox_->setChecked(enabled);
}

bool SettingsDialog::requiresDeviceApproval() const {
    return require_approval_checkbox_->isChecked();
}

void SettingsDialog::setRequireDeviceApproval(bool require) {
    require_approval_checkbox_->setChecked(require);
}

}  // namespace syncflow::gui
