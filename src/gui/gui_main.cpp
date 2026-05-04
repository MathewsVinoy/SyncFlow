#include <QApplication>
#include "syncflow/gui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Set application metadata
    QApplication::setApplicationName("Syncflow");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setApplicationDisplayName("Syncflow - Secure Peer-to-Peer File Sync");
    QApplication::setOrganizationName("Syncflow");
    QApplication::setOrganizationDomain("syncflow.local");

    // Create and show main window
    syncflow::gui::MainWindow window;
    window.show();

    return app.exec();
}
