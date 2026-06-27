#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * @brief Dialog de configuration au premier lancement en mode Local.
 *
 * Affichee uniquement la premiere fois que l'utilisateur choisit
 * le profil Local. Demande :
 *   - Le chemin du dossier ou SeaUI stockera les fichiers YAML
 *     (defaut : ~/.local/share/SeaDesktop/SeaUI/configs/)
 *   - Si on copie un projet d'exemple (BlogDemo.yaml) pour demarrer
 *
 * Le chemin choisi est sauvegarde dans QSettings sous la cle
 * [local]/configsDir et lu par resolveConfigsDir() dans main.cpp.
 *
 * Detection du premier lancement : la cle QSettings n'existe pas.
 *
 * L'utilisateur peut a tout moment changer ce dossier en supprimant
 * la cle dans ~/.config/SeaDesktop/SeaUI.conf (manuellement).
 * Pour v1.0, on n'expose pas de menu Preferences ; v1.1 le fera.
 */
class LocalSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LocalSetupDialog(QWidget* parent = nullptr);

    /**
     * @brief Chemin choisi par l'utilisateur (absolute path).
     */
    [[nodiscard]] QString configsDir() const { return _chosenDir; }

    /**
     * @brief True si l'utilisateur veut copier le YAML d'exemple.
     */
    [[nodiscard]] bool copyExample() const { return _copyExample; }

    /**
     * @brief Helper statique : true si le premier lancement Local n'a
     *        pas encore eu lieu (cle [local]/configsDir absente).
     */
    [[nodiscard]] static bool isFirstLaunch();

    /**
     * @brief Helper statique : lit le configsDir persiste, vide si
     *        absent.
     */
    [[nodiscard]] static QString persistedConfigsDir();

private slots:
    void onBrowseClicked();
    void onContinueClicked();

private:
    QString    _chosenDir;
    bool       _copyExample = true;

    QLineEdit* _pathEdit;
    QCheckBox* _exampleCheck;
    QPushButton* _browseButton;
    QPushButton* _continueButton;
    QPushButton* _cancelButton;
};