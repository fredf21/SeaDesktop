#pragma once

#include "profile.h"

#include <QDialog>
#include <QList>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;

/**
 * @brief Dialog de gestion des profils de connexion SeaUI.
 *
 * Permet a l'utilisateur de :
 *   - Voir la liste de tous les profils (Local + Remote).
 *   - Ajouter un nouveau profil Remote (nom + URL de backend).
 *   - Modifier un profil Remote existant.
 *   - Supprimer un profil Remote.
 *
 * Le profil "Local" est toujours present mais non modifiable et non
 * supprimable : les boutons Edit et Remove sont desactives quand il
 * est selectionne.
 *
 * Ce dialog gere les profils mais ne bascule PAS le profil actif --
 * c'est le role d'un autre composant (dialog de selection au demarrage
 * ou changement en cours de session via le menu).
 *
 * Les modifications sont persistees via ProfileStore::saveAll() au
 * moment de la fermeture du dialog (accept).
 */
class ProfileManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileManagerDialog(QWidget* parent = nullptr);

private slots:
    void onAddClicked();
    void onEditClicked();
    void onRemoveClicked();
    void onSelectionChanged();

private:
    /**
     * Reconstruit le QListWidget a partir de _profiles. Met a jour
     * les details affiches et l'etat des boutons.
     */
    void refreshList(const QString& selectedName = QString());

    /**
     * Affiche les details du profil selectionne dans le panneau
     * details, ou un message si aucun profil n'est selectionne.
     */
    void refreshDetails();

    /**
     * Active/desactive les boutons Edit/Remove selon que le profil
     * selectionne est modifiable (Local non modifiable).
     */
    void refreshButtonStates();

    /**
     * Sauvegarde les profils via ProfileStore et accepte le dialog.
     * Connecte au bouton Close.
     */
    void saveAndClose();

    /**
     * Retrouve le profil par nom dans _profiles. nullptr si introuvable.
     */
    [[nodiscard]] Profile* findProfile(const QString& name);

    QList<Profile> _profiles;

    QListWidget* _listWidget;
    QPushButton* _addButton;
    QPushButton* _editButton;
    QPushButton* _removeButton;
    QPushButton* _closeButton;

    QLabel*      _detailsType;
    QLabel*      _detailsUrl;
    QLabel*      _detailsLastUser;
};