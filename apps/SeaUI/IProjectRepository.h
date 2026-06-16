#pragma once

#include "project.h"

#include <QFuture>
#include <QString>
#include <QStringList>

#include <vector>

/**
 * @brief Interface asynchrone d'acces aux fichiers YAML de projet.
 *
 * Cette interface decouple SeaUI du moyen d'acces aux projets. Deux
 * implementations sont prevues :
 *
 *   - LocalProjectRepository : lit/ecrit directement dans le dossier
 *     configs/ local (mode dev historique).
 *   - HttpProjectRepository  : parle a un backend distant via les
 *     endpoints /admin/projects/* (mode remote, deploiement Docker).
 *
 * Les deux implementations exposent la meme API a SeaUI, qui n'a pas
 * a savoir si elle parle au filesystem local ou a un serveur distant.
 *
 * Modele asynchrone :
 *   Toutes les operations retournent un QFuture. Le code appelant
 *   utilise le pattern :
 *
 *     repo->readRawYaml(name)
 *         .then(this, [this](const QString& content) {
 *             // succes : utiliser content
 *         })
 *         .onFailed(this, [this](const std::exception& e) {
 *             // erreur : afficher QMessageBox
 *         });
 *
 *   Passer `this` en premier argument lie la lambda a l'objet
 *   MainWindow : si la fenetre est detruite avant la fin de la
 *   requete, la lambda n'est PAS appelee (cancellation automatique).
 *   Pas de use-after-free.
 *
 * Convention :
 *   - projectName designe le nom logique du projet (ex: "TestDemo"),
 *     SANS extension. C'est la donnee la plus naturelle pour l'UI.
 *   - Les implementations ajoutent l'extension .yaml en interne pour
 *     construire le nom de fichier (LocalProjectRepository) ou le
 *     segment d'URL (HttpProjectRepository).
 *
 * Gestion des erreurs :
 *   - Les operations qui peuvent echouer remontent une exception via
 *     le mecanisme QFuture (capturee par onFailed).
 *   - L'exception est une std::runtime_error avec message lisible.
 *   - projectExists retourne un QFuture<bool> ; le "projet absent"
 *     n'est PAS une erreur, c'est un retour false normal.
 *   - Les operations "void" (writeRawYaml, removeProject) retournent
 *     un QFuture<bool> qui vaut true en cas de succes ; l'exception
 *     remonte par onFailed comme pour les autres methodes.
 */
class IProjectRepository
{
public:
    /**
     * @brief Resultat de listProjects().
     *
     * Contient les projets correctement parses ET les eventuelles
     * erreurs de parsing par fichier. Une erreur sur un fichier
     * n'empeche pas le retour des autres : on liste ce qu'on peut.
     */
    struct ListResult
    {
        std::vector<sea::domain::Project> projects;
        QStringList errors;
    };

    virtual ~IProjectRepository() = default;

    /**
     * @brief Liste tous les projets disponibles.
     *
     * Pour chaque fichier YAML accessible, tente de le parser en
     * sea::domain::Project. Les fichiers qui echouent au parsing
     * sont rapportes dans ListResult::errors (un message par fichier)
     * et n'apparaissent pas dans ListResult::projects.
     *
     * @return Future portant un ListResult.
     * @throws (via onFailed) std::exception en cas d'erreur d'acces
     *         au dossier lui-meme (permissions refusees, reseau down,
     *         etc.).
     */
    virtual QFuture<ListResult> listProjects() = 0;

    /**
     * @brief Lit le contenu brut d'un fichier YAML.
     *
     * Retourne le YAML tel quel, avec ses commentaires et son
     * formatage d'origine. Utilise par les operations qui modifient
     * un YAML existant tout en preservant les commentaires
     * (insertion d'un nouveau service, edition manuelle, etc.).
     *
     * @param projectName Nom du projet (sans extension).
     * @return Future portant le contenu brut du fichier YAML.
     * @throws (via onFailed) std::exception si le projet n'existe
     *         pas ou si la lecture echoue.
     */
    virtual QFuture<QString> readRawYaml(const QString& projectName) = 0;

    /**
     * @brief Ecrit (ou cree) le contenu brut d'un fichier YAML.
     *
     * Si le fichier existe, son contenu est remplace integralement.
     * Si le fichier n'existe pas, il est cree.
     *
     * L'implementation HttpProjectRepository validera le YAML cote
     * serveur avant ecriture. L'implementation LocalProjectRepository
     * ecrit sans validation -- on suppose que SeaUI a fait sa propre
     * validation avant d'appeler.
     *
     * @param projectName Nom du projet (sans extension).
     * @param content     Contenu YAML brut a ecrire.
     * @return Future portant un bool valant true en cas de succes.
     * @throws (via onFailed) std::exception en cas d'echec d'ecriture.
     */
    virtual QFuture<bool> writeRawYaml(const QString& projectName,
                                       const QString& content) = 0;

    /**
     * @brief Indique si un projet existe.
     *
     * Verification rapide, sans parsing du fichier. Utilisee
     * typiquement avant la creation d'un nouveau projet pour
     * eviter d'ecraser un projet existant.
     *
     * @param projectName Nom du projet (sans extension).
     * @return Future portant true si le projet existe, false sinon.
     * @throws (via onFailed) std::exception en cas d'erreur d'acces
     *         (reseau down). Le cas "projet inexistant" n'est PAS
     *         une exception, c'est un retour false normal.
     */
    virtual QFuture<bool> projectExists(const QString& projectName) = 0;

    /**
     * @brief Supprime un projet.
     *
     * Suppression definitive : pas de corbeille, pas de backup.
     * L'appelant est responsable de demander confirmation a
     * l'utilisateur avant d'invoquer cette methode.
     *
     * @param projectName Nom du projet (sans extension).
     * @return Future portant un bool valant true en cas de succes.
     * @throws (via onFailed) std::exception si le projet n'existe
     *         pas ou si la suppression echoue.
     */
    virtual QFuture<bool> removeProject(const QString& projectName) = 0;
};