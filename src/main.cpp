#include <QApplication>
#include <QGuiApplication>
#include <QFile>
#include <QIcon>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("Taiul Paint"));
    QApplication::setApplicationDisplayName(QStringLiteral("Taiul Paint"));
    QApplication::setOrganizationName(QStringLiteral("gnome-paint"));

    // No Wayland o GNOME casa a janela ao .desktop pelo "desktop file name"
    // (app_id). Deve bater com o nome do arquivo gnome-paint.desktop, sem a
    // extensão, para a janela herdar o ícone correto.
    QGuiApplication::setDesktopFileName(QStringLiteral("gnome-paint"));

    // Ícone da janela (embutido no binário, para não depender de arquivos).
    app.setWindowIcon(QIcon(QStringLiteral(":/app/paint.png")));

    // Estética GNOME/Adwaita no modo escuro (tema fixo).
    QFile styleFile(QStringLiteral(":/styles/adwaita-dark.qss"));
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    MainWindow window;
    window.show();

    return app.exec();
}
