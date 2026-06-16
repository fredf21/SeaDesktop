#pragma once

#include "IProjectRepository.h"

#include <QFuture>
#include <QNetworkAccessManager>
#include <QString>
#include <QUrl>

/**
 * @brief Implementation HTTP de IProjectRepository.
 *
 * Parle au backend distant via les endpoints /admin/projects/*
 * (Phase 1 du chantier remote-first) :
 *
 *   listProjects   -> GET /admin/projects puis N x GET /{file}
 *   readRawYaml    -> GET /admin/projects/{name}.yaml
 *   writeRawYaml   -> projectExists puis PUT (existe) ou POST (nouveau)
 *   projectExists  -> GET /admin/projects + recherche
 *   removeProject  -> DELETE /admin/projects/{name}.yaml
 *
 * Authentification :
 *   Chaque requete inclut un header `Authorization: Bearer <token>`
 *   avec le JWT admin fourni au constructeur. Le token est obtenu
 *   en amont par un mecanisme de login (gere par la couche profil,
 *   Phase 5). Si le token est invalide ou expire, les requetes
 *   echouent avec 401 -> std::runtime_error("Authentication failed").
 *
 * QNetworkAccessManager :
 *   Une instance par repository, creee dans le constructeur. Reutilise
 *   par toutes les requetes (gere la pool de connexions HTTP).
 *
 * Mapping HTTP -> exception :
 *   200/201 : succes
 *   400     : runtime_error avec message du serveur (validation)
 *   401     : runtime_error("Authentication required")
 *   403     : runtime_error("Admin role required")
 *   404     : runtime_error("Project not found") (sauf projectExists)
 *   409     : runtime_error("Project already exists")
 *   500     : runtime_error("Server error: ...")
 *   reseau  : runtime_error avec message Qt
 *
 * Le QNetworkAccessManager est asynchrone par nature (signal/slot).
 * On wrappe son comportement en QFuture via un QPromise resolu dans
 * le slot finished du QNetworkReply.
 */
class HttpProjectRepository : public IProjectRepository
{
public:
    /**
     * @param baseUrl URL de base du backend, ex: "http://localhost:8080"
     *                ou "https://api.example.com". Pas de slash final.
     * @param token   JWT admin valide.
     * @param parent  Parent QObject pour le QNetworkAccessManager interne.
     */
    HttpProjectRepository(QString baseUrl,
                          QString token,
                          QObject* parent = nullptr);

    QFuture<ListResult> listProjects() override;
    QFuture<QString>    readRawYaml(const QString& projectName) override;
    QFuture<bool>       writeRawYaml(const QString& projectName,
                               const QString& content) override;
    QFuture<bool>       projectExists(const QString& projectName) override;
    QFuture<bool>       removeProject(const QString& projectName) override;

private:
    /**
     * Construit l'URL d'un YAML projet a partir de son nom logique.
     * Ajoute l'extension .yaml et le prefixe /admin/projects/.
     */
    [[nodiscard]] QUrl projectUrl(const QString& projectName) const;

    /**
     * Construit l'URL du endpoint de listing /admin/projects.
     */
    [[nodiscard]] QUrl listUrl() const;

    QString                _baseUrl;
    QString                _token;
    QNetworkAccessManager* _nam;
};