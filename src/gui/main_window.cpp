#include "syncflow/gui/main_window.h"
#include "syncflow/gui/settings_dialog.h"
#include "syncflow/gui/device_approval_dialog.h"
#include "syncflow/gui/sync_worker.h"
#include "syncflow/platform/system_info.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QThread>
#include <QListWidgetItem>
#include <QSplitter>

namespace syncflow::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      worker_thread_(nullptr),
      is_running_(false) {
    
    setWindowTitle("Syncflow - Peer to Peer File Sync");
    setWindowIcon(QIcon(":/icons/syncflow.png"));
    setGeometry(100, 100, 1000, 700);

    device_name_ = syncflow::platform::get_hostname();
    local_ip_ = syncflow::platform::get_local_ipv4();

    setupUI();
    createMenuBar();
    connectSignals();
    loadSettings();

    // Create sync worker thread
    sync_worker_ = std::make_unique<SyncWorker>();
    worker_thread_ = new QThread(this);
    sync_worker_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::finished, sync_worker_.get(), &QObject::deleteLater);
    connect(this, &MainWindow::destroyed, worker_thread_, &QThread::quit);

    connectSignals();
}

MainWindow::~MainWindow() {
    stopSyncWorker();
    if (worker_thread_) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
}

void MainWindow::setupUI() {
    QWidget* central_widget = new QWidget(this);
    setCentralWidget(central_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(central_widget);

    // Header with device info
    QGroupBox* header_group = new QGroupBox("Device Information", this);
    QVBoxLayout* header_layout = new QVBoxLayout(header_group);

    device_name_label_ = new QLabel(QString("Device: %1").arg(device_name_), this);
    ip_address_label_ = new QLabel(QString("IP Address: %1").arg(local_ip_), this);
    status_label_ = new QLabel("Status: Offline", this);

    header_layout->addWidget(device_name_label_);
    header_layout->addWidget(ip_address_label_);
    header_layout->addWidget(status_label_);

    main_layout->addWidget(header_group);

    // Device list
    QGroupBox* devices_group = new QGroupBox("Connected Devices", this);
    QVBoxLayout* devices_layout = new QVBoxLayout(devices_group);

    device_list_widget_ = new QListWidget(this);
    device_list_widget_->setMinimumHeight(300);
    devices_layout->addWidget(device_list_widget_);

    main_layout->addWidget(devices_group);

    // Current sync status
    QGroupBox* sync_group = new QGroupBox("Sync Status", this);
    QVBoxLayout* sync_layout = new QVBoxLayout(sync_group);

    current_sync_label_ = new QLabel("Ready to sync", this);
    sync_progress_ = new QProgressBar(this);
    sync_progress_->setRange(0, 100);
    sync_progress_->setValue(0);

    sync_layout->addWidget(current_sync_label_);
    sync_layout->addWidget(sync_progress_);

    main_layout->addWidget(sync_group);

    // Control buttons
    QHBoxLayout* button_layout = new QHBoxLayout();

    start_stop_button_ = new QPushButton("Start Sync", this);
    start_stop_button_->setMinimumWidth(100);

    approve_button_ = new QPushButton("Approve Device", this);
    approve_button_->setMinimumWidth(100);

    remove_button_ = new QPushButton("Remove Device", this);
    remove_button_->setMinimumWidth(100);

    refresh_button_ = new QPushButton("Refresh", this);
    refresh_button_->setMinimumWidth(100);

    settings_button_ = new QPushButton("Settings", this);
    settings_button_->setMinimumWidth(100);

    button_layout->addWidget(start_stop_button_);
    button_layout->addWidget(approve_button_);
    button_layout->addWidget(remove_button_);
    button_layout->addWidget(refresh_button_);
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
    
    QAction* settings_action = file_menu->addAction("&Settings");
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
    if (!sync_worker_) return;

    connect(sync_worker_.get(), &SyncWorker::deviceDiscovered,
            this, &MainWindow::onDeviceDiscovered);
    connect(sync_worker_.get(), &SyncWorker::deviceConnected,
            this, &MainWindow::onDeviceConnected);
    connect(sync_worker_.get(), &SyncWorker::deviceDisconnected,
            this, &MainWindow::onDeviceDisconnected);
    connect(sync_worker_.get(), &SyncWorker::syncStarted,
            this, &MainWindow::onSyncStarted);
    connect(sync_worker_.get(), &SyncWorker::syncProgress,
            this, &MainWindow::onSyncProgress);
    connect(sync_worker_.get(), &SyncWorker::syncCompleted,
            this, &MainWindow::onSyncCompleted);
    connect(sync_worker_.get(), &SyncWorker::syncError,
            this, &MainWindow::onSyncError);

    connect(start_stop_button_, &QPushButton::clicked,
            this, &MainWindow::onStartStopClicked);
    connect(settings_button_, &QPushButton::clicked,
            this, &MainWindow::onSettingsClicked);
    connect(approve_button_, &QPushButton::clicked,
            this, &MainWindow::onApproveClicked);
    connect(remove_button_, &QPushButton::clicked,
            this, &MainWindow::onRemoveClicked);
    connect(refresh_button_, &QPushButton::clicked,
            this, &MainWindow::onRefreshClicked);
}

void MainWindow::onDeviceDiscovered(const QString& device_name, const QString& device_ip) {
    QListWidgetItem* item = new QListWidgetItem(
        QString("%1 (%2)").arg(device_name, device_ip), device_list_widget_);
    item->setData(Qt::UserRole, device_name);
    device_list_widget_->addItem(item);
}

void MainWindow::onDeviceConnected(const QString& device_name) {
    for (int i = 0; i < device_list_widget_->count(); ++i) {
        QListWidgetItem* item = device_list_widget_->item(i);
        if (item->data(Qt::UserRole).toString() == device_name) {
            item->setBackground(QColor(144, 238, 144));  // Light green
            break;
        }
    }
}

void MainWindow::onDeviceDisconnected(const QString& device_name) {
    for (int i = 0; i < device_list_widget_->count(); ++i) {
        QListWidgetItem* item = device_list_widget_->item(i);
        if (item->data(Qt::UserRole).toString() == device_name) {
            item->setBackground(QColor(255, 192, 192));  // Light red
            break;
        }
    }
}

void MainWindow::onSyncStarted(const QString& device_name) {
    current_sync_label_->setText(QString("Syncing with: %1").arg(device_name));
    sync_progress_->setValue(0);
}

void MainWindow::onSyncProgress(const QString& file_name, int progress) {
    current_sync_label_->setText(QString("Transferring: %1 (%2%)").arg(file_name).arg(progress));
    sync_progress_->setValue(progress);
}

void MainWindow::onSyncCompleted(const QString& device_name) {
    current_sync_label_->setText(QString("Sync completed with: %1").arg(device_name));
    sync_progress_->setValue(100);
}

void MainWindow::onSyncError(const QString& error_message) {
    QMessageBox::warning(this, "Sync Error", error_message);
    current_sync_label_->setText("Error during sync");
    sync_progress_->setValue(0);
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dialog(this);
    dialog.setDeviceName(device_name_);
    if (dialog.exec() == QDialog::Accepted) {
        device_name_ = dialog.getDeviceName();
        saveSettings();
    }
}

void MainWindow::onApproveClicked() {
    QListWidgetItem* item = device_list_widget_->currentItem();
    if (!item) {
        QMessageBox::information(this, "No Selection", "Please select a device to approve.");
        return;
    }

    QString device_name = item->data(Qt::UserRole).toString();
    DeviceApprovalDialog dialog(device_name, "fingerprint_hash_here", "0.0.0.0", this);
    dialog.exec();
}

void MainWindow::onRemoveClicked() {
    QListWidgetItem* item = device_list_widget_->currentItem();
    if (!item) {
        QMessageBox::information(this, "No Selection", "Please select a device to remove.");
        return;
    }

    int result = QMessageBox::question(this, "Confirm Removal",
        QString("Remove '%1' from trusted devices?").arg(item->text()));

    if (result == QMessageBox::Yes) {
        delete item;
    }
}

void MainWindow::onStartStopClicked() {
    if (!is_running_) {
        startSyncWorker();
        start_stop_button_->setText("Stop Sync");
        status_label_->setText("Status: Online");
        is_running_ = true;
    } else {
        stopSyncWorker();
        start_stop_button_->setText("Start Sync");
        status_label_->setText("Status: Offline");
        is_running_ = false;
    }
}

void MainWindow::onRefreshClicked() {
    device_list_widget_->clear();
    sync_progress_->setValue(0);
    current_sync_label_->setText("Ready to sync");
}

void MainWindow::startSyncWorker() {
    if (!worker_thread_) return;

    QMetaObject::invokeMethod(sync_worker_.get(), [this]() {
        sync_worker_->start(device_name_);
    });

    if (!worker_thread_->isRunning()) {
        worker_thread_->start();
    }
}

void MainWindow::stopSyncWorker() {
    if (!worker_thread_) return;

    QMetaObject::invokeMethod(sync_worker_.get(), &SyncWorker::stop);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    stopSyncWorker();
    QMainWindow::closeEvent(event);
}

void MainWindow::loadSettings() {
    QSettings settings("Syncflow", "Syncflow");
    device_name_ = settings.value("device_name", device_name_).toString();
}

void MainWindow::saveSettings() {
    QSettings settings("Syncflow", "Syncflow");
    settings.setValue("device_name", device_name_);
    settings.sync();
}

}  // namespace syncflow::gui
