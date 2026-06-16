#pragma once

#include "project.h"

#include <QString>
#include <QStringList>

#include <vector>

/**
 * @brief Interface d'acces aux fichiers YAML de projet.
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
 * Convention :
 *   - projectName designe le nom logique du projet (ex: "TestDemo"),
 *     SANS extension. C'est la donnee la plus naturelle pour l'UI.
 *   - Les implementations ajoutent l'extension .yaml en interne pour
 *     construire le nom de fichier (LocalProjectRepository) ou le
 *     segment d'URL (HttpProjectRepository).
 *
 * Gestion des erreurs :
 *   - Toutes les methodes peuvent lancer une exception std::exception
 *     en cas d'erreur critique (I/O, reseau, parsing).
 *   - L'appelant est responsable de catch et d'afficher un message
 *     utilisateur (typiquement une QMessageBox).
 *   - Les operations de check non-destructives (projectExists)
 *     retournent un booleen plutot que de lancer.
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
     * @return Liste des projets et erreurs de parsing.
     * @throws std::exception en cas d'erreur d'acces au dossier
     *         lui-meme (permissions refusees, reseau down, etc.).
     */
    virtual ListResult listProjects() = 0;

    /**
     * @brief Lit le contenu brut d'un fichier YAML.
     *
     * Retourne le YAML tel quel, avec ses commentaires et son
     * formatage d'origine. Utilise par les operations qui modifient
     * un YAML existant tout en preservant les commentaires
     * (insertion d'un nouveau service, edition manuelle, etc.).
     *
     * @param projectName Nom du projet (sans extension).
     * @return Contenu brut du fichier YAML.
     * @throws std::exception si le projet n'existe pas ou si la
     *         lecture echoue.
     */
    virtual QString readRawYaml(const QString& projectName) = 0;

    /**
     * @brief Ecrit (ou cree) le contenu brut d'un fichier YAML.
     *
     * Si le fichier existe, son contenu est remplace integralement.
     * Si le fichier n'existe pas, il est cree.
     *
     * L'implementation HttpProjectRepository validera le YAML cote
     * serveur avant ecriture (voir backend GetProject/SaveProject
     * handlers). L'implementation LocalProjectRepository ecrit sans
     * validation -- on suppose que SeaUI a fait sa propre validation
     * avant d'appeler.
     *
     * @param projectName Nom du projet (sans extension).
     * @param content     Contenu YAML brut a ecrire.
     * @throws std::exception en cas d'echec d'ecriture (I/O, reseau,
     *         YAML invalide en mode remote).
     */
    virtual void writeRawYaml(const QString& projectName,
                              const QString& content) = 0;

    /**
     * @brief Indique si un projet existe.
     *
     * Verification rapide, sans parsing du fichier. Utilisee
     * typiquement avant la creation d'un nouveau projet pour
     * eviter d'ecraser un projet existant.
     *
     * @param projectName Nom du projet (sans extension).
     * @return true si le projet existe, false sinon.
     * @throws std::exception en cas d'erreur d'acces (reseau down).
     *         Le cas "projet inexistant" n'est PAS une exception,
     *         c'est un retour false normal.
     */
    virtual bool projectExists(const QString& projectName) = 0;

    /**
     * @brief Supprime un projet.
     *
     * Suppression definitive : pas de corbeille, pas de backup.
     * L'appelant est responsable de demander confirmation a
     * l'utilisateur avant d'invoquer cette methode.
     *
     * @param projectName Nom du projet (sans extension).
     * @throws std::exception si le projet n'existe pas ou si la
     *         suppression echoue.
     */
    virtual void removeProject(const QString& projectName) = 0;
};