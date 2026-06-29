#include "localsetupdialog.h"

#include <QByteArray>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

/**
 * Cle QSettings sous laquelle le configsDir est persiste.
 */
constexpr auto kConfigsDirKey = "local/configsDir";

QString defaultSuggestedDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
    + "/configs";
}

/**
 * A partir d'un dossier configs choisi par l'utilisateur, deduit le
 * dossier environment au meme niveau hierarchique :
 *
 *   <parent>/configs       <-- choisi par l'utilisateur
 *   <parent>/environment   <-- automatique
 *
 * Exemple :
 *   ~/.local/share/SeaDesktop/SeaUI/configs
 *   -> ~/.local/share/SeaDesktop/SeaUI/environment
 *
 *   ~/Documents/MyProject/configs
 *   -> ~/Documents/MyProject/environment
 *
 * Cette separation permet de versionner configs/ dans Git tout en
 * gardant environment/ (qui contient les secrets) en local.
 */
QString deriveEnvironmentDir(const QString& configsDir)
{
    QDir parent(configsDir);
    parent.cdUp();
    return parent.absoluteFilePath("environment");
}

} // namespace

bool LocalSetupDialog::isFirstLaunch()
{
    QSettings settings;
    return !settings.contains(kConfigsDirKey);
}

QString LocalSetupDialog::persistedConfigsDir()
{
    QSettings settings;
    return settings.value(kConfigsDirKey).toString();
}

QString LocalSetupDialog::generateJwtSecret()
{
    // 32 octets aleatoires = 64 caracteres hex. Largement suffisant
    // pour un secret HMAC-SHA256 (RFC 7518 recommande au moins 256
    // bits soit 32 octets).
    auto* rng = QRandomGenerator::system();
    QByteArray bytes;
    bytes.resize(32);
    for (int i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>(rng->bounded(256));
    }
    return QString::fromLatin1(bytes.toHex());
}

LocalSetupDialog::LocalSetupDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to SeaUI"));
    setMinimumWidth(720);
    setMinimumHeight(620);
    resize(760, 660);

    // ── Texte d'explication ─────────────────────────────────────
    auto* title = new QLabel(this);
    title->setText(QStringLiteral("<h3>%1</h3>").arg(tr("Local mode setup")));
    title->setTextFormat(Qt::RichText);

    auto* description = new QLabel(this);
    description->setText(tr(
        "In Local mode, SeaUI reads and writes YAML configuration files "
        "in a folder on your disk, and launches the backend service "
        "natively when you start a project."));
    description->setWordWrap(true);

    // ── Section 1 : Dossier de configuration ────────────────────
    auto* folderGroup = new QGroupBox(tr("Configuration folder"), this);
    auto* folderLayout = new QVBoxLayout(folderGroup);

    auto* folderHint = new QLabel(tr(
                                      "SeaUI will store your YAML files here. You can keep this folder "
                                      "anywhere — for example, in a Git-versioned directory to share "
                                      "configs with your team. Secrets (MySQL credentials, JWT) are "
                                      "kept separately in a sibling 'environment/' folder so they are "
                                      "never versioned with your configs."), folderGroup);
    folderHint->setWordWrap(true);
    folderHint->setStyleSheet(QStringLiteral("color: #57606a;"));

    _pathEdit = new QLineEdit(folderGroup);
    _pathEdit->setText(defaultSuggestedDir());

    _browseButton = new QPushButton(tr("Browse..."), folderGroup);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(_pathEdit, 1);
    pathRow->addWidget(_browseButton);

    _exampleCheck = new QCheckBox(
        tr("Copy an example project (BlogDemo) to get started"), folderGroup);
    _exampleCheck->setChecked(true);

    folderLayout->addWidget(folderHint);
    folderLayout->addLayout(pathRow);
    folderLayout->addWidget(_exampleCheck);

    // ── Section 2 : Credentials MySQL ───────────────────────────
    auto* dbGroup = new QGroupBox(tr("MySQL credentials"), this);
    auto* dbLayout = new QVBoxLayout(dbGroup);

    auto* dbHint = new QLabel(tr(
                                  "The backend connects to MySQL with these credentials. They are "
                                  "saved in '<parent>/environment/seadesktop.env' (permissions 0600), "
                                  "where <parent> is the folder containing your configs/."), dbGroup);
    dbHint->setWordWrap(true);
    dbHint->setStyleSheet(QStringLiteral("color: #57606a;"));

    auto* dbForm = new QFormLayout;

    _mysqlHostEdit = new QLineEdit(dbGroup);
    _mysqlHostEdit->setText(QStringLiteral("127.0.0.1"));
    dbForm->addRow(tr("Host:"), _mysqlHostEdit);

    _mysqlPortSpin = new QSpinBox(dbGroup);
    _mysqlPortSpin->setRange(1, 65535);
    _mysqlPortSpin->setValue(3306);
    dbForm->addRow(tr("Port:"), _mysqlPortSpin);

    _mysqlUserEdit = new QLineEdit(dbGroup);
    _mysqlUserEdit->setText(QStringLiteral("root"));
    dbForm->addRow(tr("User:"), _mysqlUserEdit);

    _mysqlPasswordEdit = new QLineEdit(dbGroup);
    _mysqlPasswordEdit->setEchoMode(QLineEdit::Password);
    _mysqlPasswordEdit->setPlaceholderText(tr("(leave empty if no password)"));

    _togglePasswordButton = new QPushButton(tr("Show"), dbGroup);
    _togglePasswordButton->setCheckable(true);
    _togglePasswordButton->setMaximumWidth(80);

    auto* passwordRow = new QHBoxLayout;
    passwordRow->addWidget(_mysqlPasswordEdit, 1);
    passwordRow->addWidget(_togglePasswordButton);
    dbForm->addRow(tr("Password:"), passwordRow);

    dbLayout->addWidget(dbHint);
    dbLayout->addLayout(dbForm);

    // ── Section 3 : JWT Secret ──────────────────────────────────
    auto* jwtGroup = new QGroupBox(tr("JWT secret"), this);
    auto* jwtLayout = new QVBoxLayout(jwtGroup);

    auto* jwtHint = new QLabel(tr(
                                   "The JWT secret signs authentication tokens for projects that "
                                   "use auth. Auto-generated for you — keep this safe."), jwtGroup);
    jwtHint->setWordWrap(true);
    jwtHint->setStyleSheet(QStringLiteral("color: #57606a;"));

    _jwtSecretEdit = new QLineEdit(jwtGroup);
    _jwtSecretEdit->setText(generateJwtSecret());
    _jwtSecretEdit->setReadOnly(true);
    _jwtSecretEdit->setStyleSheet(QStringLiteral(
        "font-family: monospace; background-color: #f6f8fa;"));

    _regenerateJwtButton = new QPushButton(tr("Regenerate"), jwtGroup);
    _regenerateJwtButton->setMaximumWidth(120);

    auto* jwtRow = new QHBoxLayout;
    jwtRow->addWidget(_jwtSecretEdit, 1);
    jwtRow->addWidget(_regenerateJwtButton);

    jwtLayout->addWidget(jwtHint);
    jwtLayout->addLayout(jwtRow);

    // ── Boutons globaux ─────────────────────────────────────────
    _continueButton = new QPushButton(tr("Continue"), this);
    _cancelButton   = new QPushButton(tr("Cancel"), this);
    _continueButton->setDefault(true);

    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(_continueButton);
    buttonsLayout->addWidget(_cancelButton);

    // ── Layout global ───────────────────────────────────────────
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(title);
    mainLayout->addWidget(description);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(folderGroup);
    mainLayout->addWidget(dbGroup);
    mainLayout->addWidget(jwtGroup);
    mainLayout->addLayout(buttonsLayout);

    // ── Connexions ──────────────────────────────────────────────
    connect(_browseButton,       &QPushButton::clicked,
            this, &LocalSetupDialog::onBrowseClicked);
    connect(_continueButton,     &QPushButton::clicked,
            this, &LocalSetupDialog::onContinueClicked);
    connect(_cancelButton,       &QPushButton::clicked,
            this, &QDialog::reject);
    connect(_regenerateJwtButton, &QPushButton::clicked,
            this, &LocalSetupDialog::onRegenerateJwtClicked);
    connect(_togglePasswordButton, &QPushButton::clicked,
            this, &LocalSetupDialog::onTogglePasswordVisibility);
}

void LocalSetupDialog::onBrowseClicked()
{
    const QString current = _pathEdit->text();
    QString startDir = current;
    if (!QDir(startDir).exists()) {
        startDir = QDir::homePath();
    }

    const QString chosen = QFileDialog::getExistingDirectory(
        this,
        tr("Choose configuration folder"),
        startDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!chosen.isEmpty()) {
        _pathEdit->setText(chosen);
    }
}

void LocalSetupDialog::onRegenerateJwtClicked()
{
    _jwtSecretEdit->setText(generateJwtSecret());
}

void LocalSetupDialog::onTogglePasswordVisibility()
{
    if (_togglePasswordButton->isChecked()) {
        _mysqlPasswordEdit->setEchoMode(QLineEdit::Normal);
        _togglePasswordButton->setText(tr("Hide"));
    } else {
        _mysqlPasswordEdit->setEchoMode(QLineEdit::Password);
        _togglePasswordButton->setText(tr("Show"));
    }
}

void LocalSetupDialog::onContinueClicked()
{
    const QString chosen = _pathEdit->text().trimmed();

    if (chosen.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid path"),
                             tr("Please choose a configuration folder."));
        return;
    }

    // Cree le dossier configs/ si necessaire.
    QDir dir;
    if (!dir.mkpath(chosen)) {
        QMessageBox::critical(this, tr("Cannot create folder"),
                              tr("Failed to create the folder:\n%1\n\n"
                                 "Choose another location with write access.").arg(chosen));
        return;
    }

    // Test ecriture.
    QFile testFile(chosen + "/.seaui_write_test");
    if (!testFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Permission denied"),
                              tr("The folder exists but is not writable:\n%1").arg(chosen));
        return;
    }
    testFile.remove();

    // Cree le dossier environment/ frere de configs/.
    const QString envDir = deriveEnvironmentDir(chosen);
    if (!dir.mkpath(envDir)) {
        QMessageBox::critical(this, tr("Cannot create environment folder"),
                              tr("Failed to create the environment folder:\n%1").arg(envDir));
        return;
    }

    // Ecrit le fichier d'environnement.
    if (!writeEnvFile(envDir)) {
        QMessageBox::critical(this, tr("Cannot save credentials"),
                              tr("Failed to write seadesktop.env in:\n%1").arg(envDir));
        return;
    }

    // Persiste le chemin choisi.
    QSettings settings;
    settings.setValue(kConfigsDirKey, chosen);
    settings.sync();

    _chosenDir   = chosen;
    _copyExample = _exampleCheck->isChecked();

    accept();
}

bool LocalSetupDialog::writeEnvFile(const QString& envDir) const
{
    const QString envPath = envDir + "/seadesktop.env";
    QFile file(envPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "# SeaDesktop environment file\n";
    out << "# Generated by SeaUI on first Local setup\n";
    out << "# This file is read by SeaUI to inject env vars into the\n";
    out << "# backend process when starting a local service.\n";
    out << "# Format: KEY=VALUE, no quoting needed, lines starting with # are comments.\n";
    out << "\n";
    out << "MYSQL_HOST=" << _mysqlHostEdit->text().trimmed() << "\n";
    out << "MYSQL_PORT=" << _mysqlPortSpin->value() << "\n";
    out << "MYSQL_USER=" << _mysqlUserEdit->text().trimmed() << "\n";
    out << "MYSQL_PASSWORD=" << _mysqlPasswordEdit->text() << "\n";
    out << "\n";
    out << "SEA_DESKTOP_JWT_SECRET=" << _jwtSecretEdit->text() << "\n";

    file.close();

    // Permissions 0600 : lecture/ecriture par le user uniquement.
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    return true;
}