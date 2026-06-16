#include "profile.h"

#include <QSettings>
#include <QStringList>

namespace {

/// Cle racine dans QSettings ou sont stockes les profils Remote.
constexpr const char* kProfilesGroup = "profiles";

/// Cle dans QSettings ou est stocke le nom du profil actif.
constexpr const char* kActiveProfileKey = "activeProfile";

/// Nom affiche du profil Local par defaut.
const QString kLocalProfileName = QStringLiteral("Local");

/// Convertit un Profile::Type en chaine pour la persistance.
[[nodiscard]] QString typeToString(Profile::Type type)
{
    return (type == Profile::Type::Remote)
    ? QStringLiteral("remote")
    : QStringLiteral("local");
}

/// Parse une chaine en Profile::Type. Defaut : Local.
[[nodiscard]] Profile::Type typeFromString(const QString& s)
{
    return (s == QStringLiteral("remote"))
    ? Profile::Type::Remote
    : Profile::Type::Local;
}

} // namespace anonyme


// ─────────────────────────────────────────────────────────────────
// Profile
// ─────────────────────────────────────────────────────────────────

bool Profile::isBuiltinLocal() const
{
    return type == Type::Local && name == kLocalProfileName;
}

Profile Profile::makeLocal()
{
    Profile p;
    p.name = kLocalProfileName;
    p.type = Type::Local;
    return p;
}


// ─────────────────────────────────────────────────────────────────
// ProfileStore
// ─────────────────────────────────────────────────────────────────

QList<Profile> ProfileStore::loadAll()
{
    QList<Profile> result;

    // 1. Le profil Local est toujours present en premier.
    result.append(Profile::makeLocal());

    // 2. Charger les profils Remote depuis QSettings.
    QSettings settings;
    settings.beginGroup(kProfilesGroup);
    const QStringList groups = settings.childGroups();

    QList<Profile> remoteProfiles;
    for (const QString& groupName : groups) {
        settings.beginGroup(groupName);
        Profile p;
        p.name         = settings.value(QStringLiteral("name")).toString();
        p.type         = typeFromString(
            settings.value(QStringLiteral("type")).toString());
        p.baseUrl      = settings.value(QStringLiteral("baseUrl")).toString();
        p.lastUsername = settings.value(QStringLiteral("lastUsername")).toString();
        settings.endGroup();

        // Ignorer les entrees corrompues (nom vide).
        if (!p.name.isEmpty() && p.type == Profile::Type::Remote) {
            remoteProfiles.append(p);
        }
    }
    settings.endGroup();

    // 3. Trier les remote par nom (ordre alphabetique).
    std::sort(remoteProfiles.begin(), remoteProfiles.end(),
              [](const Profile& a, const Profile& b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });

    result.append(remoteProfiles);
    return result;
}

void ProfileStore::saveAll(const QList<Profile>& profiles)
{
    QSettings settings;

    // 1. Effacer toutes les entrees existantes du groupe profiles.
    settings.beginGroup(kProfilesGroup);
    settings.remove(QString());  // remove("") supprime tout le groupe
    settings.endGroup();

    // 2. Reecrire uniquement les profils Remote (le Local n'est pas persiste).
    settings.beginGroup(kProfilesGroup);
    for (const Profile& p : profiles) {
        if (p.type != Profile::Type::Remote) {
            continue;  // skip Local
        }
        settings.beginGroup(p.name);
        settings.setValue(QStringLiteral("name"),         p.name);
        settings.setValue(QStringLiteral("type"),         typeToString(p.type));
        settings.setValue(QStringLiteral("baseUrl"),      p.baseUrl);
        settings.setValue(QStringLiteral("lastUsername"), p.lastUsername);
        settings.endGroup();
    }
    settings.endGroup();
    settings.sync();
}

QString ProfileStore::loadActiveProfileName()
{
    QSettings settings;
    return settings.value(kActiveProfileKey).toString();
}

void ProfileStore::saveActiveProfileName(const QString& name)
{
    QSettings settings;
    settings.setValue(kActiveProfileKey, name);
    settings.sync();
}