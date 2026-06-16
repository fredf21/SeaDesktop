#pragma once

#include <QList>
#include <QString>

/**
 * @brief Profil de connexion SeaUI.
 *
 * Un profil decrit comment SeaUI accede aux fichiers YAML de projet :
 *   - "local"  : lecture/ecriture directe dans le dossier configs/
 *                local de la machine.
 *   - "remote" : appels HTTP vers un backend distant via les endpoints
 *                /admin/projects/* (Phase 1 du chantier remote-first).
 *
 * Le profil "Local" est toujours present par defaut, non modifiable et
 * non supprimable. L'utilisateur peut creer autant de profils "Remote"
 * que necessaire pour administrer differents services / environnements.
 *
 * Le token JWT n'est PAS persiste pour des raisons de securite :
 *   - Expiration courte des tokens (~15 minutes par defaut).
 *   - Un fichier de config volable est un risque inutile.
 * L'utilisateur se reauthentifie a chaque session pour les profils
 * Remote (dialog de login affiche au moment de la connexion).
 */
struct Profile
{
    /// Type de profil. Valeurs : "local" ou "remote".
    enum class Type { Local, Remote };

    /// Nom affiche du profil, par exemple "Production server" ou
    /// "Local dev". Doit etre unique parmi les profils.
    QString name;

    /// Type du profil.
    Type type = Type::Local;

    /// Pour Remote uniquement : URL de base du backend.
    /// Ex: "http://localhost:8080" ou "https://api.example.com".
    QString baseUrl;

    /// Pour Remote uniquement : dernier email utilise pour le login.
    /// Memorise pour pre-remplir le dialog de connexion. N'est pas
    /// sensible (ce n'est pas un mot de passe ni un token).
    QString lastUsername;

    /**
     * @brief Indique si ce profil est le profil Local par defaut.
     *
     * Le profil Local est toujours present, non modifiable et non
     * supprimable. Cette methode permet a la UI de le distinguer.
     */
    [[nodiscard]] bool isBuiltinLocal() const;

    /**
     * @brief Construit le profil Local par defaut.
     */
    [[nodiscard]] static Profile makeLocal();
};


/**
 * @brief Stockage persistant des profils utilisateur.
 *
 * Utilise QSettings (registry Windows, plist Mac, conf Linux) pour
 * une persistance native cross-platform. Les profils Remote sont
 * stockes sous la cle "profiles/<name>" avec leurs attributs.
 * Le profil Local n'est pas serialise (il existe toujours par defaut).
 */
class ProfileStore
{
public:
    /**
     * @brief Charge tous les profils persistes + le profil Local.
     *
     * Le profil Local est toujours premier dans la liste retournee,
     * suivi des profils Remote dans l'ordre alphabetique.
     */
    [[nodiscard]] static QList<Profile> loadAll();

    /**
     * @brief Persiste tous les profils Remote.
     *
     * Le profil Local est ignore (jamais persiste, toujours present
     * par defaut). Les anciens profils non presents dans la liste
     * sont supprimes des settings.
     */
    static void saveAll(const QList<Profile>& profiles);

    /**
     * @brief Charge le nom du dernier profil actif.
     *
     * @return Nom du profil ou QString vide si jamais defini.
     */
    [[nodiscard]] static QString loadActiveProfileName();

    /**
     * @brief Persiste le nom du profil actif.
     */
    static void saveActiveProfileName(const QString& name);
};