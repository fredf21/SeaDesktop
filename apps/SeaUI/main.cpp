#include "connectiondialog.h"
#include "httpprojectrepository.h"
#include "localprojectrepository.h"
#include "mainwindow.h"
#include "profile.h"
#include "translation_manager.h"

#include <QApplication>
#include <qdialog.h>
#include <qdir.h>
#include <qstandardpaths.h>

namespace {

/**
 * Résout le dossier configs/ local pour le mode Local. Réplique de
 * appConfigsDir() de mainwindow.cpp (non exposée). Pour v1.0, on
 * accepte la duplication ; v2.0 pourra factoriser.
 *
 * Priorité :
 *   1. Variable d'environnement SEA_DESKTOP_CONFIGS_DIR
 *   2. Dossier 'configs' à côté du repo source (mode dev)
 *   3. AppDataLocation/configs (mode release / install système)
 */
QString resolveConfigsDir()
{
    const QString envDir = qEnvironmentVariable("SEA_DESKTOP_CONFIGS_DIR");
    if (!envDir.isEmpty()) {
        return envDir;
    }

    // Mode dev : ../configs depuis le binaire compilé.
    const QString devDir = QDir(QCoreApplication::applicationDirPath())
                               .absoluteFilePath("../configs");
    if (QDir(devDir).exists()) {
        return QDir(devDir).absolutePath();
    }

    // Mode release.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/configs";
}

/**
 * Construit le repository correspondant au profil/token choisi par
 * l'utilisateur dans le ConnectionDialog.
 */
std::unique_ptr<IProjectRepository> buildRepository(
    const Profile& profile, const QString& token)
{
    if (profile.type == Profile::Type::Local) {
        return std::make_unique<LocalProjectRepository>(resolveConfigsDir());
    }
    return std::make_unique<HttpProjectRepository>(profile.baseUrl, token);
}

} // namespace anonyme
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

    // Dialog de connexion au démarrage : choix du profil + login si
    // Remote. Si l'utilisateur annule, on quitte proprement.
    ConnectionDialog connDialog;
    if (connDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    // Construction du repository selon le profil choisi.
    auto repository = buildRepository(connDialog.activeProfile(),
                                      connDialog.token());
    MainWindow w(&translationManager,
                 std::move(repository),
                 connDialog.activeProfile(),
                 connDialog.token());
    w.show();

    return a.exec();
}