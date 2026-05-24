#include "mainwindow.h"
#include "translation_manager.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Identite de l'application : indispensable pour que QSettings
    // dispose d'un emplacement de stockage stable (la preference de
    // langue y est persistee par TranslationManager).
    QApplication::setOrganizationName(QStringLiteral("SeaDesktop"));
    QApplication::setApplicationName(QStringLiteral("SeaUI"));

    // Initialisation de l'internationalisation.
    //
    // loadPersistedLanguage() doit etre appele AVANT la construction de
    // MainWindow : ainsi la fenetre se construit directement dans la
    // langue choisie, sans retraduction supplementaire au demarrage.
    TranslationManager translationManager;
    translationManager.loadPersistedLanguage();

    MainWindow w(&translationManager);
    w.show();

    return a.exec();
}