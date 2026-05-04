#include "syncflow/gui/settings_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QSettings>
#include <QGroupBox>
#include <QFormLayout>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>

#include <filesystem>

#include "syncflow/file_sync/file_sync.h"
#include "syncflow/platform/system_info.h"

namespace syncflow::gui {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    
    setWindowTitle("Settings");
    setMinimumWidth(500);
    setupUI();
    loadDefaults();
}

void SettingsDialog::setConfigPath(const QString& config_path) {
    config_path_ = config_path;
}

QString SettingsDialog::configPath() const {
    return config_path_;
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

void SettingsDialog::loadDefaults() {
    device_name_edit_->setText(QString::fromStdString(syncflow::platform::get_hostname()));
    source_path_edit_->setText("sync/");
    receive_dir_edit_->setText("received/");
    file_sync_checkbox_->setChecked(true);
    security_checkbox_->setChecked(true);
    require_approval_checkbox_->setChecked(true);
}

bool SettingsDialog::loadConfig() {
    if (config_path_.isEmpty()) {
        return false;
    }

    QFile file(config_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        loadDefaults();
        return false;
    }

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        loadDefaults();
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject file_sync = root.value("file_sync").toObject();
    const QJsonObject security = root.value("security").toObject();

    device_name_edit_->setText(file_sync.value("device_name").toString(QString::fromStdString(syncflow::platform::get_hostname())));
    source_path_edit_->setText(file_sync.value("source_path").toString("sync/"));
    receive_dir_edit_->setText(file_sync.value("receive_dir").toString("received"));
    file_sync_checkbox_->setChecked(file_sync.value("enabled").toBool(true));
    security_checkbox_->setChecked(security.value("enabled").toBool(true));
    require_approval_checkbox_->setChecked(security.value("require_approval").toBool(true));
    return true;
}

bool SettingsDialog::saveConfig() {
    if (config_path_.isEmpty()) {
        QMessageBox::warning(this, "No Config Path", "No config.json path is set.");
        return false;
    }

    QJsonObject root;
    {
        QFile read_file(config_path_);
        if (read_file.open(QIODevice::ReadOnly)) {
            const auto existing = QJsonDocument::fromJson(read_file.readAll());
            if (existing.isObject()) {
                root = existing.object();
            }
        }
    }

    QJsonObject file_sync;
    file_sync.insert("enabled", file_sync_checkbox_->isChecked());
    file_sync.insert("source_path", source_path_edit_->text());
    file_sync.insert("receive_dir", receive_dir_edit_->text());
    file_sync.insert("device_name", device_name_edit_->text());

    QJsonObject security = root.value("security").toObject();
    security.insert("enabled", security_checkbox_->isChecked());
    security.insert("require_approval", require_approval_checkbox_->isChecked());
    if (!security.contains("cert_dir")) {
        security.insert("cert_dir", ".syncflow/certs");
    }
    if (!security.contains("trusted_devices_file")) {
        security.insert("trusted_devices_file", ".syncflow/trusted_devices.txt");
    }

    root.insert("file_sync", file_sync);
    root.insert("security", security);

    QFile file(config_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save Failed", QString("Unable to write %1").arg(config_path_));
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.write("\n");
    return true;
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
    if (saveConfig()) {
        accept();
    }
}

void SettingsDialog::onCancelClicked() {
    reject();
}

void SettingsDialog::onResetClicked() {
    if (!loadConfig()) {
        loadDefaults();
    }
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
