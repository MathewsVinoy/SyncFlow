#include "syncflow/gui/sync_worker.h"

#include "syncflow/networking/peer_node.h"
#include "syncflow/networking/peer_node.h"

#include <thread>
#include <chrono>
#include <QThread>

namespace syncflow::gui {

SyncWorker::SyncWorker(QObject* parent)
    : QObject(parent), is_running_(false), peer_node_(nullptr) {
}

SyncWorker::~SyncWorker() {
    stop();
}

void SyncWorker::start() {
    if (is_running_) {
        return;
    }

    is_running_ = true;
    emit syncStarted();

    // Initialize peer node if needed
    if (!peer_node_) {
        try {
            peer_node_ = std::make_unique<networking::PeerNode>("6789");
            emit statusChanged("Peer node initialized");
        } catch (const std::exception& e) {
            emit syncError(QString("Failed to initialize peer node: %1").arg(e.what()));
            is_running_ = false;
            return;
        }
    }

    // Start discovery in background
    discovery_thread_ = std::thread([this]() {
        performDiscovery();
    });
}

void SyncWorker::stop() {
    is_running_ = false;

    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }

    if (peer_node_) {
        // Graceful shutdown of peer node
        peer_node_.reset();
    }

    emit syncCompleted();
    emit statusChanged("Stopped");
}

void SyncWorker::approvePeer(const QString& device_id) {
    if (!peer_node_) {
        emit syncError("Peer node not initialized");
        return;
    }

    try {
        // In a real implementation, this would approve the device
        // and store it in the trusted device list
        emit statusChanged(QString("Device %1 approved").arg(device_id));
    } catch (const std::exception& e) {
        emit syncError(QString("Failed to approve device: %1").arg(e.what()));
    }
}

void SyncWorker::removePeer(const QString& device_id) {
    if (!peer_node_) {
        emit syncError("Peer node not initialized");
        return;
    }

    try {
        // In a real implementation, this would remove the device
        // from the trusted device list
        emit statusChanged(QString("Device %1 removed").arg(device_id));
    } catch (const std::exception& e) {
        emit syncError(QString("Failed to remove device: %1").arg(e.what()));
    }
}

void SyncWorker::performDiscovery() {
    while (is_running_) {
        try {
            // Simulate device discovery and connection monitoring
            // In a real implementation, this would use mDNS/Bonjour for discovery
            // and maintain connections to known peers

            // Check for connected peers
            static int discovery_count = 0;
            discovery_count++;

            if (discovery_count % 5 == 0) {
                // Emit a mock discovered device every 5 iterations
                emit deviceDiscovered("device-1", "192.168.1.100", "00:11:22:33:44:55");
                emit statusChanged("Device discovered: device-1 (192.168.1.100)");
            }

            if (discovery_count % 10 == 0) {
                // Update progress
                static int progress = 0;
                progress = (progress + 10) % 100;
                emit syncProgress(progress);
            }

            // Sleep for a bit to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        } catch (const std::exception& e) {
            if (is_running_) {
                emit syncError(QString("Discovery error: %1").arg(e.what()));
            }
        }
    }
}

void SyncWorker::setDeviceName(const QString& name) {
    device_name_ = name;

    if (peer_node_) {
        // Update peer node with new device name
        emit statusChanged(QString("Device name set to: %1").arg(name));
    }
}

void SyncWorker::setSyncPaths(const QString& source_path, const QString& receive_dir) {
    source_path_ = source_path;
    receive_dir_ = receive_dir;
    emit statusChanged(QString("Sync paths updated: %1 -> %2").arg(source_path, receive_dir));
}

void SyncWorker::setSecurityEnabled(bool enabled) {
    security_enabled_ = enabled;
    emit statusChanged(security_enabled_ ? "Security enabled" : "Security disabled");
}

}  // namespace syncflow::gui
