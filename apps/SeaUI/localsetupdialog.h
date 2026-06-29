#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

/**
 * @brief Dialog de configuration au premier lancement en mode Local.
 *
 * Affichee uniquement la premiere fois que l'utilisateur choisit
 * le profil Local. Demande :
 *   - Le chemin du dossier ou SeaUI stockera les fichiers YAML
 *     (defaut : ~/.local/share/SeaDesktop/SeaUI/configs/)
 *   - Si on copie un projet d'exemple (BlogDemo.yaml) pour demarrer
 *   - Les credentials MySQL (host, port, user, password) que le
 *     backend utilisera quand SeaUI le lance localement
 *   - Le secret JWT (auto-genere, bouton Regenerate disponible)
 *
 * A la validation, les credentials sont sauvegardes dans :
 *   <configsDir>/seadesktop.env  (permissions 0600)
 *
 * Format du fichier (compatible avec docker-compose .env et avec
 * std::getenv() utilise par le parser YAML du backend) :
 *
 *   MYSQL_HOST=127.0.0.1
 *   MYSQL_PORT=3306
 *   MYSQL_USER=root
 *   MYSQL_PASSWORD=...
 *   SEA_DESKTOP_JWT_SECRET=...
 *
 * Le chemin du dossier configs est persiste dans QSettings sous la
 * cle [local]/configsDir et lu par resolveConfigsDir().
 *
 * Detection du premier lancement : la cle QSettings n'existe pas.
 */
class LocalSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LocalSetupDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString configsDir() const { return _chosenDir; }
    [[nodiscard]] bool copyExample() const { return _copyExample; }

    [[nodiscard]] static bool isFirstLaunch();
    [[nodiscard]] static QString persistedConfigsDir();

private slots:
    void onBrowseClicked();
    void onContinueClicked();
    void onRegenerateJwtClicked();
    void onTogglePasswordVisibility();

private:
    [[nodiscard]] static QString generateJwtSecret();
    [[nodiscard]] bool writeEnvFile(const QString& configsDir) const;

    QString    _chosenDir;
    bool       _copyExample = true;

    QLineEdit* _pathEdit;
    QCheckBox* _exampleCheck;
    QPushButton* _browseButton;

    QLineEdit* _mysqlHostEdit;
    QSpinBox*  _mysqlPortSpin;
    QLineEdit* _mysqlUserEdit;
    QLineEdit* _mysqlPasswordEdit;
    QPushButton* _togglePasswordButton;

    QLineEdit* _jwtSecretEdit;
    QPushButton* _regenerateJwtButton;

    QPushButton* _continueButton;
    QPushButton* _cancelButton;
};