#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AMRTools"));
    QCoreApplication::setApplicationName(QStringLiteral("AMRMapManager"));

    MainWindow window;
    window.show();
    return app.exec();
}
