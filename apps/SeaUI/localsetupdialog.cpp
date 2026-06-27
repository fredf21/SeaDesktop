#include "localsetupdialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

/**
 * Cle QSettings sous laquelle le configsDir est persiste.
 * Cle composite [local]/configsDir.
 */
constexpr auto kConfigsDirKey = "local/configsDir";

/**
 * Calcule le chemin par defaut suggere a l'utilisateur :
 *   AppDataLocation/configs
 * Soit typiquement ~/.local/share/SeaDesktop/SeaUI/configs/ sur Linux.
 */
QString defaultSuggestedDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
    + "/configs";
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

LocalSetupDialog::LocalSetupDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Welcome to SeaUI"));
    setMinimumWidth(650);
    setMinimumHeight(320);
    resize(700, 360);

    // ── Texte d'explication ─────────────────────────────────────
    auto* title = new QLabel(this);
    title->setText(QStringLiteral("<h3>%1</h3>").arg(tr("Local mode setup")));
    title->setTextFormat(Qt::RichText);

    auto* description = new QLabel(this);
    description->setText(tr(
        "In Local mode, SeaUI reads and writes YAML configuration files "
        "in a folder on your disk. You can keep this folder anywhere — "
        "for example, in a Git-versioned directory to share configs with "
        "your team."));
    description->setWordWrap(true);

    // ── Choix du chemin ─────────────────────────────────────────
    auto* pathLabel = new QLabel(tr("Configuration folder:"), this);

    _pathEdit = new QLineEdit(this);
    _pathEdit->setText(defaultSuggestedDir());

    _browseButton = new QPushButton(tr("Browse..."), this);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(_pathEdit, 1);
    pathRow->addWidget(_browseButton);

    // ── Option : copier le YAML d'exemple ───────────────────────
    _exampleCheck = new QCheckBox(
        tr("Copy an example project (BlogDemo) to get started"), this);
    _exampleCheck->setChecked(true);

    // ── Boutons ─────────────────────────────────────────────────
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
    mainLayout->addSpacing(12);
    mainLayout->addWidget(pathLabel);
    mainLayout->addLayout(pathRow);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(_exampleCheck);
    mainLayout->addStretch();
    mainLayout->addLayout(buttonsLayout);

    // ── Connexions ──────────────────────────────────────────────
    connect(_browseButton,   &QPushButton::clicked,
            this, &LocalSetupDialog::onBrowseClicked);
    connect(_continueButton, &QPushButton::clicked,
            this, &LocalSetupDialog::onContinueClicked);
    connect(_cancelButton,   &QPushButton::clicked,
            this, &QDialog::reject);
}

void LocalSetupDialog::onBrowseClicked()
{
    const QString current = _pathEdit->text();
    QString startDir = current;

    // Si le chemin actuel n'existe pas (ex: AppDataLocation pas encore
    // cree), on demarre depuis le home pour ne pas confondre l'utilisateur.
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

void LocalSetupDialog::onContinueClicked()
{
    const QString chosen = _pathEdit->text().trimmed();

    if (chosen.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid path"),
                             tr("Please choose a configuration folder."));
        return;
    }

    // Cree le dossier si necessaire. mkpath() est idempotent et
    // ne fait rien si le dossier existe deja.
    QDir dir;
    if (!dir.mkpath(chosen)) {
        QMessageBox::critical(this, tr("Cannot create folder"),
                              tr("Failed to create the folder:\n%1\n\n"
                                 "Choose another location with write access.").arg(chosen));
        return;
    }

    // Verifie que le dossier est accessible en ecriture.
    QFile testFile(chosen + "/.seaui_write_test");
    if (!testFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Permission denied"),
                              tr("The folder exists but is not writable:\n%1").arg(chosen));
        return;
    }
    testFile.remove();

    // Persiste le chemin choisi.
    QSettings settings;
    settings.setValue(kConfigsDirKey, chosen);
    settings.sync();

    _chosenDir   = chosen;
    _copyExample = _exampleCheck->isChecked();

    accept();
}