#pragma once

#include <QFuture>
#include <QString>

/**
 * @brief Client d'authentification HTTP pour les profils Remote.
 *
 * Appelle l'endpoint POST /auth/login du backend distant pour obtenir
 * un access_token JWT. Ce token est ensuite passe a HttpProjectRepository
 * pour authentifier les requetes /admin/projects/*.
 *
 * Le token n'est pas persiste : il est obtenu a chaque session quand
 * l'utilisateur se connecte a un profil Remote. Son expiration est
 * geree par le serveur (typiquement 15 minutes).
 */
namespace AuthClient {

/**
 * @brief Login HTTP : envoie email/password, recoit access_token.
 *
 * Effectue POST <baseUrl>/auth/login avec un body JSON
 *   {"email":"...","password":"..."}
 * et retourne le champ "access_token" de la reponse JSON.
 *
 * @param baseUrl   URL de base du backend, ex: "http://localhost:8080".
 *                  Pas de slash final (sera nettoye si present).
 * @param email     Identifiant utilisateur.
 * @param password  Mot de passe en clair (transmis en HTTPS en
 *                  production).
 * @return Future portant le access_token en cas de succes.
 * @throws (via onFailed) std::runtime_error en cas d'echec :
 *           - 401 : "Invalid credentials"
 *           - 4xx/5xx : message du serveur ou code HTTP
 *           - reseau : message Qt
 */
QFuture<QString> login(const QString& baseUrl,
                       const QString& email,
                       const QString& password);

} // namespace AuthClient