#include "syncflow/gui/main_window.h"
#include "syncflow/gui/settings_dialog.h"
#include "syncflow/platform/system_info.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QCloseEvent>
#include <filesystem>
#include <array>

namespace {

std::filesystem::path find_config_path() {
    const std::array<std::filesystem::path, 4> candidates{
        std::filesystem::current_path() / "config.json",
        std::filesystem::path("./") / "config.json",
        std::filesystem::current_path().parent_path() / "config.json",
        std::filesystem::current_path().parent_path().parent_path() / "config.json"
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) {
            return c;
        }
    }
    return candidates.front();
}

}  // namespace

namespace syncflow::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    
    setWindowTitle("Syncflow - Config Editor");
    setWindowIcon(QIcon(":/icons/syncflow.png"));
    setGeometry(100, 100, 1000, 600);

    config_path_ = QString::fromStdString(find_config_path().string());
    device_name_ = QString::fromStdString(syncflow::platform::get_hostname());
    local_ip_ = QString::fromStdString(syncflow::platform::get_local_ipv4());

    setupUI();
    createMenuBar();
    loadSettings();
    refreshConfigView();
    connectSignals();

    launchBackgroundPeer();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUI() {
    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);

    QGroupBox* config_group = new QGroupBox("Configuration", this);
    QFormLayout* config_layout = new QFormLayout(config_group);

    config_path_label_ = new QLabel(this);
    device_name_label_ = new QLabel(this);
    file_sync_label_ = new QLabel(this);
    source_path_label_ = new QLabel(this);
    receive_dir_label_ = new QLabel(this);
    security_label_ = new QLabel(this);
    approval_label_ = new QLabel(this);

    config_layout->addRow("Config file:", config_path_label_);
    config_layout->addRow("Device name:", device_name_label_);
    config_layout->addRow("File sync:", file_sync_label_);
    config_layout->addRow("Source path:", source_path_label_);
    config_layout->addRow("Receive dir:", receive_dir_label_);
    config_layout->addRow("Security:", security_label_);
    config_layout->addRow("Device approval:", approval_label_);

    main_layout->addWidget(config_group);

    QGroupBox* background_group = new QGroupBox("Background Sync", this);
    QVBoxLayout* background_layout = new QVBoxLayout(background_group);

    background_status_label_ = new QLabel(this);
    background_status_label_->setWordWrap(true);
    background_layout->addWidget(background_status_label_);

    main_layout->addWidget(background_group);

    // Control buttons
    QHBoxLayout* button_layout = new QHBoxLayout();

    reload_button_ = new QPushButton("Reload", this);
    reload_button_->setMinimumWidth(120);

    settings_button_ = new QPushButton("Edit Config", this);
    settings_button_->setMinimumWidth(120);

    button_layout->addWidget(reload_button_);
    button_layout->addWidget(settings_button_);
    button_layout->addStretch();

    main_layout->addLayout(button_layout);

    central_widget->setLayout(main_layout);
}

void MainWindow::createMenuBar() {
    QMenuBar* menu_bar = new QMenuBar(this);
    setMenuBar(menu_bar);

    // File menu
    QMenu* file_menu = menu_bar->addMenu("&File");
    
    QAction* reload_action = file_menu->addAction("&Reload Config");
    connect(reload_action, &QAction::triggered, this, &MainWindow::onReloadClicked);

    QAction* settings_action = file_menu->addAction("&Edit Config");
    connect(settings_action, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    file_menu->addSeparator();

    QAction* exit_action = file_menu->addAction("E&xit");
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    // Help menu
    QMenu* help_menu = menu_bar->addMenu("&Help");
    
    QAction* about_action = help_menu->addAction("&About");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Syncflow",
            "Syncflow v1.0\n\nPeer-to-peer file synchronization system.\n\n"
            "Supports cross-platform synchronization across Linux, macOS, Windows, and Android.");
    });
}

void MainWindow::connectSignals() {
    connect(reload_button_, &QPushButton::clicked,
            this, &MainWindow::onReloadClicked);
    connect(settings_button_, &QPushButton::clicked,
            this, &MainWindow::onSettingsClicked);
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dialog(this);
    dialog.setConfigPath(config_path_);
    dialog.loadConfig();
    if (dialog.exec() == QDialog::Accepted) {
        loadSettings();
        saveSettings();
        refreshConfigView();
    }
}

void MainWindow::onReloadClicked() {
    loadSettings();
    refreshConfigView();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings() {
    QFile file(config_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        config_ = ConfigView{};
        config_.device_name = QString::fromStdString(syncflow::platform::get_hostname());
        config_.source_path = "sync/";
        config_.receive_dir = "received";
        refreshConfigView();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        refreshConfigView();
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonObject file_sync = root.value("file_sync").toObject();
    const QJsonObject security = root.value("security").toObject();

    config_.device_name = file_sync.value("device_name").toString(QString::fromStdString(syncflow::platform::get_hostname()));
    config_.source_path = file_sync.value("source_path").toString("sync/");
    config_.receive_dir = file_sync.value("receive_dir").toString("received");
    config_.file_sync_enabled = file_sync.value("enabled").toBool(true);
    config_.security_enabled = security.value("enabled").toBool(true);
    config_.require_approval = security.value("require_approval").toBool(true);
    refreshConfigView();
}

void MainWindow::saveSettings() {
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
    file_sync.insert("enabled", config_.file_sync_enabled);
    file_sync.insert("source_path", config_.source_path);
    file_sync.insert("receive_dir", config_.receive_dir);
    file_sync.insert("device_name", config_.device_name);

    QJsonObject security = root.value("security").toObject();
    security.insert("enabled", config_.security_enabled);
    security.insert("require_approval", config_.require_approval);
    if (!security.contains("cert_dir")) {
        security.insert("cert_dir", ".syncflow/certs");
    }
    if (!security.contains("trusted_devices_file")) {
        security.insert("trusted_devices_file", ".syncflow/trusted_devices.txt");
    }

    root.insert("file_sync", file_sync);
    root.insert("security", security);

    QFile file(config_path_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.write("\n");
    }
}

void MainWindow::refreshConfigView() {
    if (!config_path_label_) {
        return;
    }
    config_path_label_->setText(config_path_);
    device_name_label_->setText(config_.device_name);
    file_sync_label_->setText(config_.file_sync_enabled ? "Enabled" : "Disabled");
    source_path_label_->setText(config_.source_path);
    receive_dir_label_->setText(config_.receive_dir);
    security_label_->setText(config_.security_enabled ? "Enabled" : "Disabled");
    approval_label_->setText(config_.require_approval ? "Required" : "Not required");

    background_status_label_->setText(
        "Background sync uses syncflow_peer as a detached process. "
        "Edit the config here, then restart the background peer to apply changes.");
}

QString MainWindow::findPeerExecutablePath() const {
    const QDir app_dir(QApplication::applicationDirPath());
#ifdef Q_OS_WIN
    const QString exe_name = "syncflow_peer.exe";
#else
    const QString exe_name = "syncflow_peer";
#endif

    const QStringList candidates{
        app_dir.filePath(exe_name),
        app_dir.filePath("../" + exe_name),
        app_dir.filePath("../../" + exe_name)
    };

    for (const auto& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void MainWindow::launchBackgroundPeer() {
    const QString peer_exe = findPeerExecutablePath();
    if (peer_exe.isEmpty()) {
        background_status_label_->setText(
            "Background peer executable was not found. Build syncflow_peer and place it next to syncflow_gui.");
        return;
    }

    const QStringList args{QStringLiteral("--config"), config_path_, QStringLiteral("--detach")};
    const bool started = QProcess::startDetached(peer_exe, args, QApplication::applicationDirPath());
    background_status_label_->setText(started
        ? "Background peer launched with the current config."
        : "Failed to launch background peer.");
}

}  // namespace syncflow::gui
