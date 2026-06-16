#pragma once

#include "profile.h"

#include <QDialog>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * @brief Dialog de selection du profil actif au demarrage de SeaUI.
 *
 * Affiche la liste des profils disponibles (Local + tous les profils
 * Remote configures). L'utilisateur choisit son profil et, dans le cas
 * d'un profil Remote, saisit ses identifiants pour obtenir un token JWT.
 *
 * Flow utilisateur :
 *   1. Selection d'un profil dans la combo.
 *   2. Si Remote : saisie email/password (email pre-rempli avec
 *      Profile::lastUsername si disponible).
 *   3. Clic sur Connect.
 *      - Local : accept immediat, token vide.
 *      - Remote : appel a AuthClient::login(...). Si succes, token
 *        memorise et accept. Si echec, message d'erreur et dialog
 *        reste ouvert.
 *
 * Bouton "Manage Profiles..." ouvre ProfileManagerDialog. A la
 * fermeture de celui-ci, la combo est rafraichie.
 *
 * Apres acceptation, le code appelant (typiquement main.cpp) lit :
 *   - activeProfile() : le profil choisi (Local ou Remote).
 *   - token()         : le JWT obtenu (vide pour Local).
 * Et instancie LocalProjectRepository ou HttpProjectRepository.
 */
class ConnectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ConnectionDialog(QWidget* parent = nullptr);

    /**
     * @brief Profil selectionne au moment de l'acceptation du dialog.
     */
    [[nodiscard]] Profile activeProfile() const { return _activeProfile; }

    /**
     * @brief Token JWT obtenu en cas de profil Remote. Vide sinon.
     */
    [[nodiscard]] QString token() const { return _token; }

private slots:
    void onProfileChanged();
    void onManageClicked();
    void onConnectClicked();

private:
    /**
     * Reconstruit la combobox a partir de ProfileStore::loadAll().
     * Tente de restaurer la selection sur le profil ayant le nom passe.
     */
    void refreshProfiles(const QString& selectName = QString());

    /**
     * Active/desactive la zone Authentication selon le type du profil
     * actuellement selectionne dans la combo.
     */
    void updateAuthVisibility();

    /**
     * Retrouve un profil par nom dans _profiles. nullptr si absent.
     */
    [[nodiscard]] Profile* findProfile(const QString& name);

    /**
     * Persiste lastUsername sur le profil donne en fusionnant avec la
     * liste actuelle du store.
     */
    void saveLastUsername(const QString& profileName,
                          const QString& username);

    QList<Profile> _profiles;
    Profile        _activeProfile;
    QString        _token;

    QComboBox*   _profileCombo;
    QLabel*      _profileTypeLabel;

    QGroupBox*   _authGroup;
    QLineEdit*   _emailEdit;
    QLineEdit*   _passwordEdit;

    QPushButton* _manageButton;
    QPushButton* _connectButton;
    QPushButton* _cancelButton;
};