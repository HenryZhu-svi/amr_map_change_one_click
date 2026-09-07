#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AMRTools"));
    QCoreApplication::setApplicationName(QStringLiteral("AMRMapManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/svi-logo-128.png")));

    MainWindow window;
    window.show();
    return app.exec();
}
