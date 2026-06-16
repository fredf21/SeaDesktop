#include "profilemanagerdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/**
 * Sous-dialog d'edition d'un profil Remote (nom + URL).
 *
 * Mode "add" : champs vides au depart.
 * Mode "edit" : champs pre-remplis avec les valeurs existantes.
 *
 * Validation : nom non vide, URL non vide. L'unicite du nom est
 * verifiee par l'appelant (qui connait les autres profils).
 */
class RemoteProfileEditDialog : public QDialog
{
public:
    RemoteProfileEditDialog(const Profile& initial,
                            bool isNew,
                            QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(isNew
                           ? tr("Add Remote Profile")
                           : tr("Edit Remote Profile"));
        setMinimumWidth(400);

        auto* form = new QFormLayout;

        _nameEdit = new QLineEdit(initial.name, this);
        _nameEdit->setPlaceholderText(tr("e.g. Production server"));
        form->addRow(tr("Name:"), _nameEdit);

        _urlEdit = new QLineEdit(initial.baseUrl, this);
        _urlEdit->setPlaceholderText(QStringLiteral("http://localhost:8080"));
        form->addRow(tr("Base URL:"), _urlEdit);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (_nameEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, tr("Validation"),
                                     tr("Name is required."));
                return;
            }
            if (_urlEdit->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, tr("Validation"),
                                     tr("Base URL is required."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    [[nodiscard]] QString name() const { return _nameEdit->text().trimmed(); }
    [[nodiscard]] QString baseUrl() const { return _urlEdit->text().trimmed(); }

private:
    QLineEdit* _nameEdit;
    QLineEdit* _urlEdit;
};

} // namespace anonyme


// ─────────────────────────────────────────────────────────────────
// ProfileManagerDialog
// ─────────────────────────────────────────────────────────────────

ProfileManagerDialog::ProfileManagerDialog(QWidget* parent)
    : QDialog(parent)
    , _profiles(ProfileStore::loadAll())
{
    setWindowTitle(tr("Manage Connection Profiles"));
    setMinimumSize(550, 450);

    // ── Liste des profils + boutons d'action ────────────────────
    _listWidget = new QListWidget(this);

    _addButton    = new QPushButton(tr("+ Add Remote"), this);
    _editButton   = new QPushButton(tr("Edit"), this);
    _removeButton = new QPushButton(tr("Remove"), this);

    auto* buttonsLayout = new QVBoxLayout;
    buttonsLayout->addWidget(_addButton);
    buttonsLayout->addWidget(_editButton);
    buttonsLayout->addWidget(_removeButton);
    buttonsLayout->addStretch();

    auto* topLayout = new QHBoxLayout;
    topLayout->addWidget(_listWidget, 1);
    topLayout->addLayout(buttonsLayout);

    // ── Panneau de details du profil selectionne ────────────────
    auto* detailsBox = new QGroupBox(tr("Details"), this);
    auto* detailsForm = new QFormLayout(detailsBox);
    _detailsType     = new QLabel(QStringLiteral("-"), detailsBox);
    _detailsUrl      = new QLabel(QStringLiteral("-"), detailsBox);
    _detailsLastUser = new QLabel(QStringLiteral("-"), detailsBox);
    detailsForm->addRow(tr("Type:"),      _detailsType);
    detailsForm->addRow(tr("Base URL:"),  _detailsUrl);
    detailsForm->addRow(tr("Last user:"), _detailsLastUser);

    // ── Bouton Close ────────────────────────────────────────────
    _closeButton = new QPushButton(tr("Close"), this);
    auto* closeLayout = new QHBoxLayout;
    closeLayout->addStretch();
    closeLayout->addWidget(_closeButton);

    // ── Layout global ───────────────────────────────────────────
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(topLayout);
    mainLayout->addWidget(detailsBox);
    mainLayout->addLayout(closeLayout);

    // ── Connexions ──────────────────────────────────────────────
    connect(_addButton,    &QPushButton::clicked,
            this, &ProfileManagerDialog::onAddClicked);
    connect(_editButton,   &QPushButton::clicked,
            this, &ProfileManagerDialog::onEditClicked);
    connect(_removeButton, &QPushButton::clicked,
            this, &ProfileManagerDialog::onRemoveClicked);
    connect(_closeButton,  &QPushButton::clicked,
            this, &ProfileManagerDialog::saveAndClose);
    connect(_listWidget, &QListWidget::itemSelectionChanged,
            this, &ProfileManagerDialog::onSelectionChanged);

    refreshList();
}

void ProfileManagerDialog::onAddClicked()
{
    Profile draft;
    draft.type = Profile::Type::Remote;

    RemoteProfileEditDialog editor(draft, /*isNew=*/true, this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = editor.name();

    // Verifier l'unicite du nom.
    if (findProfile(newName) != nullptr) {
        QMessageBox::warning(
            this, tr("Add Remote Profile"),
            tr("A profile with this name already exists."));
        return;
    }

    Profile p;
    p.name    = newName;
    p.type    = Profile::Type::Remote;
    p.baseUrl = editor.baseUrl();
    _profiles.append(p);

    refreshList(newName);
}

void ProfileManagerDialog::onEditClicked()
{
    QListWidgetItem* item = _listWidget->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString name = item->text();
    Profile* p = findProfile(name);
    if (p == nullptr || p->isBuiltinLocal()) {
        return;  // garde-fou, le bouton devrait etre desactive
    }

    RemoteProfileEditDialog editor(*p, /*isNew=*/false, this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = editor.name();

    // Si le nom a change, verifier l'unicite.
    if (newName != p->name && findProfile(newName) != nullptr) {
        QMessageBox::warning(
            this, tr("Edit Remote Profile"),
            tr("A profile with this name already exists."));
        return;
    }

    p->name    = newName;
    p->baseUrl = editor.baseUrl();

    refreshList(newName);
}

void ProfileManagerDialog::onRemoveClicked()
{
    QListWidgetItem* item = _listWidget->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString name = item->text();
    Profile* p = findProfile(name);
    if (p == nullptr || p->isBuiltinLocal()) {
        return;  // garde-fou
    }

    const auto answer = QMessageBox::question(
        this, tr("Remove Profile"),
        tr("Are you sure you want to remove the profile \"%1\"?")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    // Trouver l'index et supprimer.
    for (int i = 0; i < _profiles.size(); ++i) {
        if (_profiles[i].name == name) {
            _profiles.removeAt(i);
            break;
        }
    }

    refreshList();
}

void ProfileManagerDialog::onSelectionChanged()
{
    refreshDetails();
    refreshButtonStates();
}

void ProfileManagerDialog::refreshList(const QString& selectedName)
{
    _listWidget->clear();
    for (const Profile& p : _profiles) {
        auto* item = new QListWidgetItem(p.name);
        // Le profil Local affiche un suffixe pour signaler son
        // caractere particulier.
        if (p.isBuiltinLocal()) {
            item->setText(p.name + tr(" (built-in)"));
            item->setData(Qt::UserRole, p.name);  // vrai nom sans suffixe
        } else {
            item->setData(Qt::UserRole, p.name);
        }
        _listWidget->addItem(item);
    }

    // Restaurer la selection si demandee.
    if (!selectedName.isEmpty()) {
        for (int i = 0; i < _listWidget->count(); ++i) {
            if (_listWidget->item(i)->data(Qt::UserRole).toString() ==
                selectedName) {
                _listWidget->setCurrentRow(i);
                break;
            }
        }
    } else if (_listWidget->count() > 0) {
        _listWidget->setCurrentRow(0);
    }

    refreshDetails();
    refreshButtonStates();
}

void ProfileManagerDialog::refreshDetails()
{
    QListWidgetItem* item = _listWidget->currentItem();
    if (item == nullptr) {
        _detailsType->setText(QStringLiteral("-"));
        _detailsUrl->setText(QStringLiteral("-"));
        _detailsLastUser->setText(QStringLiteral("-"));
        return;
    }

    const QString name = item->data(Qt::UserRole).toString();
    Profile* p = findProfile(name);
    if (p == nullptr) {
        return;
    }

    _detailsType->setText(
        p->type == Profile::Type::Remote
            ? tr("Remote (HTTP backend)")
            : tr("Local (filesystem)"));
    _detailsUrl->setText(p->baseUrl.isEmpty() ? QStringLiteral("-")
                                              : p->baseUrl);
    _detailsLastUser->setText(p->lastUsername.isEmpty()
                                  ? QStringLiteral("-")
                                  : p->lastUsername);
}

void ProfileManagerDialog::refreshButtonStates()
{
    QListWidgetItem* item = _listWidget->currentItem();
    if (item == nullptr) {
        _editButton->setEnabled(false);
        _removeButton->setEnabled(false);
        return;
    }

    const QString name = item->data(Qt::UserRole).toString();
    Profile* p = findProfile(name);
    const bool isModifiable = (p != nullptr && !p->isBuiltinLocal());
    _editButton->setEnabled(isModifiable);
    _removeButton->setEnabled(isModifiable);
}

void ProfileManagerDialog::saveAndClose()
{
    ProfileStore::saveAll(_profiles);
    accept();
}

Profile* ProfileManagerDialog::findProfile(const QString& name)
{
    for (Profile& p : _profiles) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}