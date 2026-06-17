#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "IProjectRepository.h"
#include "entitylistmodel.h"
#include "profile.h"
#include "servicelistmodel.h"
#include "projectlistmodel.h"
#include "fieldlistmodel.h"
#include "routelistmodel.h"
#include <QFileSystemWatcher>
#include "servicestatuscheck.h"
#include "translation_manager.h"
#include <QActionGroup>
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(TranslationManager* translationManager, std::unique_ptr<IProjectRepository> repository,
                        Profile activeProfile, QString token,
                        QWidget* parent = nullptr);
    ~MainWindow() override;
    void loadProjects();
    void startService(const QString& serviceName, const QString& yamlPath);
    void stopService(const QString& serviceName);
    void restartService(const QString& serviceName, const QString& yamlPath);
    QString serviceProcessKey(const QString& projectName,
                              const QString& serviceName,
                              int port) const;

protected:
    /**
     * @brief Intercepte les changements d'etat de la fenetre.
     *
     * Lorsque la langue de l'application change (QEvent::LanguageChange),
     * l'interface est retraduite a chaud sans redemarrage.
     *
     * @param event Evenement Qt.
     */
    void changeEvent(QEvent* event) override;

private slots:
    void on_projectListView_clicked(const QModelIndex &index);

    void on_serviceListView_clicked(const QModelIndex &index);

    void on_entityListView_clicked(const QModelIndex &index);

    void on_fieldListView_clicked(const QModelIndex &index);
    // ← nouveaux slots pour le ServiceClient
    void onStatusUpdated(const QString& service, const QString& status, int port);
    void onServiceUnreachable(const QString& service);
    void on_swaggerServiceButton_clicked();

    void on_openEntityDataButton_clicked();

    void on_serviceLoginButton_clicked();

    void on_serviceLogoutButton_clicked();

    void on_actionAdd_New_Project_triggered();

    /**
     * @brief Ajoute un nouveau service a un projet existant.
     */
    void on_actionAdd_New_Service_triggered();

    /**
     * @brief Ajoute une nouvelle entite a un service d'un projet.
     */
    void on_actionAdd_New_Entity_triggered();

    /**
     * @brief Importe un fichier YAML externe dans le dossier configs/.
     */
    void on_actionImport_Yaml_triggered();

    /**
     * @brief Exporte le YAML d'un projet vers un emplacement choisi.
     */
    void on_actionExport_Yaml_triggered();

    /**
     * @brief Ouvre le YAML d'un projet dans un editeur integre.
     */
    void on_actionEdit_Yaml_triggered();

    /**
     * @brief Modifie le port d'un service existant.
     */
    void on_actionEdit_Service_triggered();

    /**
     * @brief Renomme un projet (cle name: et fichier .yaml).
     */
    void on_actionEdit_Project_triggered();

    /**
     * @brief Modifie les options d'une entite existante.
     */
    void on_actionEdit_Entity_triggered();

    /**
     * @brief Ouvre une fenetre affichant les journaux de tous les services.
     */
    void on_actionShow_All_Services_Logs_triggered();

    /**
     * @brief Demande a l'utilisateur un service, puis affiche son journal.
     */
    void on_actionChoose_a_service_to_show_Logs_triggered();

    /**
     * @brief Demarre tous les services de tous les projets.
     */
    void on_actionStart_All_Services_triggered();

    /**
     * @brief Arrete tous les services de tous les projets.
     */
    void on_actionStop_All_Services_triggered();

    /**
     * @brief Redemarre tous les services de tous les projets.
     */
    void on_actionRestart_All_Services_triggered();

    /**
     * @brief Recharge tous les services : relit le YAML puis redemarre.
     */
    void on_actionReload_All_Services_triggered();

    /**
     * @brief Bascule l'application en anglais.
     */
    void on_actionEnglish_triggered();

    /**
     * @brief Bascule l'application en francais.
     */
    void on_actionFrancais_triggered();

    /**
     * @brief Bascule vers un autre profil.
     */
    void on_actionSwitch_Connection_triggered();

private:
    Ui::MainWindow *ui;
    ProjectListModel* _projectModel;
    ServiceListModel* _serviceModel;
    EntityListModel* _entityModel;
    FieldListModel* _fieldModel;
    RouteListModel* _routeModel;
    QFileSystemWatcher* _watcher;
    ServiceStatusCheck*    _statusCheck;      // ← nouveau
    std::vector<sea::application::RouteDefinition> _currentServiceRoutes;
    int _currentProjectRow = -1;
    int _currentServiceRow = -1;
    int _currentEntityRow = -1;
    QMap<QString, QProcess*> _processes; // serviceName → process
    const QString _configsPath = "/home/frederic/QtProjects/SeaDesktop/configs/";

    QString yamlPathForProject(const QString& projectName) const {
        return _configsPath + projectName + ".yaml";
    }
    QNetworkAccessManager* _networkManager = nullptr;
    void showJsonArrayInTable(const QJsonArray& array, const QString& title);
    QString entityCollectionPath(const QString& entityName) const;
    QString _authToken;
    QString _refreshToken;
    void promptLogin();
    void loginUser(const QString& email, const QString& password);
    void updateServicesActionsMenuState();
    void updateAuditsMenuState();
    /**
     * @brief Affiche un modal demandant le nom du projet et du service.
     *
     * Les deux champs sont obligatoires. Les valeurs saisies sont
     * normalisees (espaces et caracteres invalides retires) avant d'etre
     * retournees.
     *
     * @param[out] projectName Nom du projet normalise.
     * @param[out] serviceName Nom du service normalise.
     * @return true si l'utilisateur a valide avec deux champs non vides,
     *         false s'il a annule.
     */
    bool promptNewProject(QString& projectName, QString& serviceName);

    /**
     * @brief Construit le contenu YAML d'un projet en configuration
     *        minimale de production.
     *
     * La configuration generee inclut : un service avec base MySQL et
     * migrations, une section security (JWT, CORS, headers stricts,
     * limites HTTP) et un logging production (console + fichier JSON
     * avec rotation, mode asynchrone).
     *
     * @param projectName Nom du projet (deja normalise).
     * @param serviceName Nom du service (deja normalise).
     * @return Le contenu YAML complet pret a etre ecrit sur disque.
     */
    QString buildProductionYaml(const QString& projectName,
                                const QString& serviceName) const;

    /**
     * @brief Construit le bloc YAML d'un service en configuration de
     *        production.
     *
     * Genere une entree de la sequence 'services:' (prefixe '  - name:')
     * incluant base MySQL, securite et logging. Ce bloc est partage par
     * la creation de projet et l'ajout de service.
     *
     * @param projectName Nom du projet (utilise pour issuer, db_name, log).
     * @param serviceName Nom du service.
     * @return Le bloc YAML du service, indente pour la sequence services.
     */
    QString buildProductionServiceBlock(const QString& projectName,
                                        const QString& serviceName) const;

    /**
     * @brief Brouillon d'un champ d'entite saisi par l'utilisateur.
     */
    struct EntityFieldDraft
    {
        QString name;            ///< Nom du champ.
        QString type;            ///< Type YAML (string, int, uuid, ...).
        bool    required = false;///< Champ obligatoire.
        bool    unique   = false;///< Valeur unique.
        bool    indexed  = false;///< Champ indexe.
    };

    /**
     * @brief Affiche un dialogue de selection d'un service d'un projet.
     *
     * @param project     Projet dont on liste les services.
     * @param title       Titre du dialogue.
     * @param[out] chosen Nom du service selectionne.
     * @return true si l'utilisateur a valide un service, false sinon.
     */
    bool promptSelectService(const sea::domain::Project& project,
                             const QString& title,
                             QString& chosen);

    /**
     * @brief Affiche un dialogue de saisie d'un champ d'entite.
     *
     * @param[out] draft Champ saisi (nom, type, attributs).
     * @return true si l'utilisateur a valide un champ valide, false s'il
     *         a annule.
     */
    bool promptEntityField(EntityFieldDraft& draft);

    /**
     * @brief Construit le bloc YAML d'une entite, indente pour 'entities:'.
     *
     * @param entityName  Nom de l'entite.
     * @param enableCrud  Option enable_crud.
     * @param timestamps  Option timestamps.
     * @param softDelete  Option soft_delete.
     * @param fields      Champs de l'entite.
     * @return Le bloc YAML de l'entite (prefixe '      - name:').
     */
    QString buildEntityBlock(const QString& entityName,
                             bool enableCrud,
                             bool timestamps,
                             bool softDelete,
                             const QVector<EntityFieldDraft>& fields) const;

    /**
     * @brief Insere un bloc entite dans le service cible d'un fichier YAML.
     *
     * Localise le bloc du service dans le contenu YAML, puis insere le
     * bloc entite sous sa section 'entities:' (creee si absente). Les
     * commentaires et le formatage du reste du fichier sont preserves.
     *
     * @param yamlContent  Contenu YAML complet (modifie en place).
     * @param serviceName  Nom du service cible.
     * @param entityBlock  Bloc YAML de l'entite a inserer.
     * @return true si l'insertion a reussi, false si le service est
     *         introuvable dans le contenu.
     */
    bool insertEntityIntoYaml(QString& yamlContent,
                              const QString& serviceName,
                              const QString& entityBlock) const;

    /**
     * @brief Remplace la valeur du port d'un service dans un YAML.
     *
     * Localise le bloc du service cible, puis la cle 'port:' indentee a
     * 4 espaces (le port du service, a ne pas confondre avec le port de
     * la base de donnees indente a 6 espaces). Le reste du fichier,
     * commentaires compris, est preserve.
     *
     * @param yamlContent Contenu YAML complet (modifie en place).
     * @param serviceName Nom du service cible.
     * @param newPort     Nouveau port.
     * @return true si le port a ete remplace, false si le service ou la
     *         cle 'port:' est introuvable.
     */
    bool replacePortInService(QString& yamlContent,
                              const QString& serviceName,
                              int newPort) const;

    /**
     * @brief Remplace le nom du projet dans un YAML.
     *
     * Localise la section racine 'project:' puis sa cle 'name:' indentee
     * a 2 espaces, et remplace sa valeur. Le reste du fichier, commentaires
     * compris, est preserve.
     *
     * @param yamlContent Contenu YAML complet (modifie en place).
     * @param newName     Nouveau nom de projet.
     * @return true si le nom a ete remplace, false si 'project:' ou sa
     *         cle 'name:' est introuvable.
     */
    bool replaceProjectName(QString& yamlContent,
                            const QString& newName) const;

    /**
     * @brief Affiche un dialogue de selection d'une entite d'un service.
     *
     * @param service     Service dont on liste les entites.
     * @param title       Titre du dialogue.
     * @param[out] chosen Nom de l'entite selectionnee.
     * @return true si l'utilisateur a valide une entite, false sinon.
     */
    bool promptSelectEntity(const sea::domain::Service& service,
                            const QString& title,
                            QString& chosen);

    /**
     * @brief Remplace les options d'une entite dans un YAML.
     *
     * Localise le service cible, puis l'entite cible dans ce service,
     * puis sa section 'options:' (creee si absente) et y ecrit les trois
     * options. Le reste du fichier, commentaires compris, est preserve.
     *
     * @param yamlContent Contenu YAML complet (modifie en place).
     * @param serviceName Nom du service contenant l'entite.
     * @param entityName  Nom de l'entite cible.
     * @param enableCrud  Nouvelle valeur de enable_crud.
     * @param timestamps  Nouvelle valeur de timestamps.
     * @param softDelete  Nouvelle valeur de soft_delete.
     * @return true si les options ont ete mises a jour, false si le
     *         service ou l'entite est introuvable.
     */
    bool replaceEntityOptions(QString& yamlContent,
                              const QString& serviceName,
                              const QString& entityName,
                              bool enableCrud,
                              bool timestamps,
                              bool softDelete) const;

    /**
     * @brief Affiche un dialogue de selection d'un projet charge.
     *
     * @param title       Titre du dialogue.
     * @param[out] chosen Nom du projet selectionne.
     * @return true si l'utilisateur a valide un projet, false s'il a annule
     *         ou si aucun projet n'est disponible.
     */
    bool promptSelectProject(const QString& title, QString& chosen);

    /**
     * @brief Decrit un service et l'emplacement de son fichier de journal.
     */
    struct ServiceLogInfo
    {
        QString projectName;  ///< Nom du projet contenant le service.
        QString serviceName;  ///< Nom du service.
        int     port = 0;     ///< Port HTTP du service.
        QString logPath;      ///< Chemin absolu du fichier .log.
        bool    logExists = false; ///< true si le fichier .log existe.
    };

    /**
     * @brief Collecte tous les services de tous les projets charges.
     *
     * Parcourt le modele de projets et, pour chaque service, calcule
     * l'emplacement de son fichier de journal et indique s'il existe.
     *
     * @return La liste de tous les services avec leurs journaux.
     */
    QVector<ServiceLogInfo> collectAllServiceLogs() const;

    /**
     * @brief Ouvre une fenetre a onglets affichant des journaux.
     *
     * Chaque entree donne lieu a un onglet. Les services dont le fichier
     * de journal n'existe pas encore affichent un message d'indisponibilite
     * au lieu du contenu.
     *
     * @param entries Services dont les journaux doivent etre affiches.
     * @param title   Titre de la fenetre.
     */
    void openLogsWindow(const QVector<ServiceLogInfo>& entries,
                        const QString& title);

    /**
     * @brief Demarre le processus backend d'un service donne.
     *
     * Helper independant de la selection courante de l'interface : toutes
     * les informations necessaires sont passees en parametres. Si le
     * processus tourne deja, l'appel est sans effet (idempotent).
     *
     * @param projectName Nom du projet.
     * @param serviceName Nom du service.
     * @param port        Port HTTP du service.
     * @param yamlPath    Chemin du fichier YAML du projet.
     */
    void startServiceProcess(const QString& projectName,
                             const QString& serviceName,
                             int port,
                             const QString& yamlPath);

    /**
     * @brief Arrete le processus backend d'un service donne.
     *
     * Helper independant de la selection courante. Sans effet si aucun
     * processus n'est associe au service.
     *
     * @param projectName Nom du projet.
     * @param serviceName Nom du service.
     * @param port        Port HTTP du service.
     */
    void stopServiceProcess(const QString& projectName,
                            const QString& serviceName,
                            int port);

    /**
     * @brief Applique une action a tous les services de tous les projets.
     *
     * Facteur commun des slots Services Actions : parcourt le modele de
     * projets et invoque le callback fourni pour chaque service.
     *
     * @param action Callback recevant (projet, service, port, yamlPath).
     */
    void forEachService(
        const std::function<void(const QString& projectName,
                                 const QString& serviceName,
                                 int port,
                                 const QString& yamlPath)>& action);
    void logoutUser();
    void updateAuthUi();

    /// Gestionnaire d'internationalisation injecte (propriete du main).
    TranslationManager* _translationManager = nullptr;

    /// Groupe rendant les actions de langue mutuellement exclusives.
    QActionGroup* _languageGroup = nullptr;

    /**
     * @brief Initialise le sous-menu de selection de langue.
     *
     * Rend les actions de langue cochables et exclusives, puis coche
     * celle correspondant a la langue actuellement active.
     */
    void setupLanguageMenu();

    /**
     * @brief Met a jour la coche du sous-menu langue selon la langue active.
     *
     * @param code Code de la langue desormais active ("en_US", "fr_FR").
     */
    void syncLanguageMenu(const QString& code);

    std::unique_ptr<IProjectRepository> _projectRepository;
    Profile _activeProfile;
    QString _token;
    bool    _isRemoteMode = false;
};
#endif // MAINWINDOW_H