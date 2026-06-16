#include "connectiondialog.h"

#include "auth_client.h"
#include "profilemanagerdialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>

ConnectionDialog::ConnectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connect to SeaDesktop"));
    setMinimumWidth(450);

    // ── Sélection du profil ─────────────────────────────────────
    _profileCombo     = new QComboBox(this);
    _profileTypeLabel = new QLabel(this);
    _profileTypeLabel->setStyleSheet(QStringLiteral("color: gray;"));

    auto* profileLayout = new QVBoxLayout;
    profileLayout->addWidget(new QLabel(tr("Profile:"), this));
    profileLayout->addWidget(_profileCombo);
    profileLayout->addWidget(_profileTypeLabel);

    // ── Zone authentification (Remote uniquement) ───────────────
    _authGroup = new QGroupBox(tr("Authentication (Remote only)"), this);
    auto* authForm = new QFormLayout(_authGroup);

    _emailEdit = new QLineEdit(_authGroup);
    _emailEdit->setPlaceholderText(QStringLiteral("admin@example.com"));
    authForm->addRow(tr("Email:"), _emailEdit);

    _passwordEdit = new QLineEdit(_authGroup);
    _passwordEdit->setEchoMode(QLineEdit::Password);
    authForm->addRow(tr("Password:"), _passwordEdit);

    // ── Boutons ─────────────────────────────────────────────────
    _manageButton  = new QPushButton(tr("Manage Profiles..."), this);
    _connectButton = new QPushButton(tr("Connect"), this);
    _cancelButton  = new QPushButton(tr("Cancel"), this);
    _connectButton->setDefault(true);

    auto* buttonsLayout = new QHBoxLayout;
    buttonsLayout->addWidget(_manageButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(_connectButton);
    buttonsLayout->addWidget(_cancelButton);

    // ── Layout global ───────────────────────────────────────────
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(profileLayout);
    mainLayout->addWidget(_authGroup);
    mainLayout->addLayout(buttonsLayout);

    // ── Connexions ──────────────────────────────────────────────
    connect(_profileCombo, &QComboBox::currentIndexChanged,
            this, &ConnectionDialog::onProfileChanged);
    connect(_manageButton,  &QPushButton::clicked,
            this, &ConnectionDialog::onManageClicked);
    connect(_connectButton, &QPushButton::clicked,
            this, &ConnectionDialog::onConnectClicked);
    connect(_cancelButton,  &QPushButton::clicked,
            this, &QDialog::reject);

    // ── Chargement initial ──────────────────────────────────────
    // Si un profil actif est connu, le pre-selectionner.
    const QString lastActive = ProfileStore::loadActiveProfileName();
    refreshProfiles(lastActive);
}

void ConnectionDialog::refreshProfiles(const QString& selectName)
{
    _profiles = ProfileStore::loadAll();

    _profileCombo->clear();
    for (const Profile& p : _profiles) {
        const QString display = p.isBuiltinLocal()
        ? p.name + tr(" (built-in)")
        : p.name;
        _profileCombo->addItem(display, p.name);
    }

    // Restaurer la selection.
    if (!selectName.isEmpty()) {
        for (int i = 0; i < _profileCombo->count(); ++i) {
            if (_profileCombo->itemData(i).toString() == selectName) {
                _profileCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    updateAuthVisibility();
}

void ConnectionDialog::onProfileChanged()
{
    updateAuthVisibility();

    // Pre-remplir l'email avec lastUsername du profil selectionne.
    const QString name = _profileCombo->currentData().toString();
    Profile* p = findProfile(name);
    if (p != nullptr && p->type == Profile::Type::Remote) {
        _emailEdit->setText(p->lastUsername);
        _passwordEdit->clear();
    }
}

void ConnectionDialog::updateAuthVisibility()
{
    const QString name = _profileCombo->currentData().toString();
    Profile* p = findProfile(name);
    const bool isRemote = (p != nullptr && p->type == Profile::Type::Remote);

    _authGroup->setEnabled(isRemote);

    if (p != nullptr) {
        _profileTypeLabel->setText(
            isRemote
                ? tr("Remote: %1").arg(p->baseUrl)
                : tr("Local filesystem (built-in)"));
    } else {
        _profileTypeLabel->setText(QString());
    }
}

void ConnectionDialog::onManageClicked()
{
    ProfileManagerDialog dlg(this);
    dlg.exec();

    // Rafraichir la combo en preservant la selection actuelle si possible.
    const QString current = _profileCombo->currentData().toString();
    refreshProfiles(current);
}

void ConnectionDialog::onConnectClicked()
{
    const QString name = _profileCombo->currentData().toString();
    Profile* p = findProfile(name);
    if (p == nullptr) {
        QMessageBox::critical(this, tr("Error"),
                              tr("No profile selected."));
        return;
    }

    // Local : succes immediat.
    if (p->type == Profile::Type::Local) {
        _activeProfile = *p;
        _token.clear();
        ProfileStore::saveActiveProfileName(_activeProfile.name);
        accept();
        return;
    }

    // Remote : login HTTP.
    const QString email    = _emailEdit->text().trimmed();
    const QString password = _passwordEdit->text();

    if (email.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, tr("Authentication"),
                             tr("Email and password are required."));
        return;
    }

    // Desactiver les boutons pendant la requete pour eviter les
    // soumissions multiples.
    _connectButton->setEnabled(false);
    _cancelButton->setEnabled(false);
    _manageButton->setEnabled(false);
    _connectButton->setText(tr("Connecting..."));

    const QString profileName = p->name;
    const QString baseUrl     = p->baseUrl;

    AuthClient::login(baseUrl, email, password)
        .then(this, [this, profileName, email](const QString& token) {
            // Persiste le lastUsername pour la prochaine session.
            saveLastUsername(profileName, email);

            Profile* p = findProfile(profileName);
            if (p != nullptr) {
                p->lastUsername  = email;
                _activeProfile   = *p;
            }
            _token = token;

            ProfileStore::saveActiveProfileName(_activeProfile.name);
            accept();
        })
        .onFailed(this, [this](const std::exception& e) {
            QMessageBox::critical(
                this, tr("Authentication failed"),
                tr("Login failed: %1")
                    .arg(QString::fromUtf8(e.what())));

            // Reactiver les boutons.
            _connectButton->setEnabled(true);
            _cancelButton->setEnabled(true);
            _manageButton->setEnabled(true);
            _connectButton->setText(tr("Connect"));
        });
}

Profile* ConnectionDialog::findProfile(const QString& name)
{
    for (Profile& p : _profiles) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

void ConnectionDialog::saveLastUsername(const QString& profileName,
                                        const QString& username)
{
    // Recharger la liste actuelle pour ne pas ecraser d'autres
    // modifications faites entre-temps par ProfileManagerDialog.
    QList<Profile> allProfiles = ProfileStore::loadAll();
    for (Profile& p : allProfiles) {
        if (p.name == profileName && p.type == Profile::Type::Remote) {
            p.lastUsername = username;
            break;
        }
    }
    ProfileStore::saveAll(allProfiles);
}