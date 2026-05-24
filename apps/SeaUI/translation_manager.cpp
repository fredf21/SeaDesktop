#include "translation_manager.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace {

/// Cle QSettings sous laquelle la langue choisie est persistee.
constexpr auto kSettingsLanguageKey = "ui/language";

/// Langue de repli si rien n'est persiste et que la locale systeme
/// n'est pas supportee.
constexpr auto kFallbackLanguage = "en_US";

} // namespace

/**
 * @brief Construit le gestionnaire et declare les langues supportees.
 */
TranslationManager::TranslationManager(QObject* parent)
    : QObject(parent)
{
    // Langue de reference (anglais) : pas de fichier .qm, les chaines
    // sources sont deja en anglais. Le code "en_US" sert de marqueur.
    _languages.push_back({QStringLiteral("en_US"), tr("English")});
    _languages.push_back({QStringLiteral("fr_FR"), tr("Francais")});

    _currentCode = QString::fromLatin1(kFallbackLanguage);
}

const QVector<TranslationManager::Language>&
TranslationManager::availableLanguages() const
{
    return _languages;
}

QString TranslationManager::currentLanguage() const
{
    return _currentCode;
}

bool TranslationManager::isSupported(const QString& code) const
{
    for (const Language& lang : _languages) {
        if (lang.code == code) {
            return true;
        }
    }
    return false;
}

void TranslationManager::loadPersistedLanguage()
{
    QSettings settings;
    const QString persisted =
        settings.value(QString::fromLatin1(kSettingsLanguageKey)).toString();

    if (!persisted.isEmpty() && isSupported(persisted)) {
        applyLanguage(persisted);
        return;
    }

    // Aucune preference : tenter la locale systeme.
    const QString systemCode = QLocale::system().name(); // ex. "fr_FR"
    if (isSupported(systemCode)) {
        applyLanguage(systemCode);
        return;
    }

    applyLanguage(QString::fromLatin1(kFallbackLanguage));
}

bool TranslationManager::applyLanguage(const QString& code)
{
    if (!isSupported(code)) {
        return false;
    }

    // Retirer les anciens traducteurs avant d'installer les nouveaux.
    removeInstalledTranslators();

    // L'anglais est la langue source : aucun .qm a charger.
    if (code != QStringLiteral("en_US")) {
        if (!installTranslatorsFor(code)) {
            return false;
        }
    }

    _currentCode = code;
    persistLanguage(code);

    emit languageChanged(code);
    return true;
}

void TranslationManager::removeInstalledTranslators()
{
    qApp->removeTranslator(&_appTranslator);
    qApp->removeTranslator(&_qtTranslator);
}

bool TranslationManager::installTranslatorsFor(const QString& code)
{
    // 1. Traducteur des chaines propres a SeaUI.
    //    Convention de nommage alignee sur les ressources Qt :
    //    ":/i18n/SeaUI_<locale>.qm" (ex. ":/i18n/SeaUI_fr_FR.qm").
    const QString seaUiBase = QStringLiteral(":/i18n/SeaUI_") + code;

    if (!_appTranslator.load(seaUiBase)) {
        return false;
    }
    qApp->installTranslator(&_appTranslator);

    // 2. Traducteur des chaines standard de Qt (boutons natifs des
    //    dialogues, etc.). On tente le code complet puis le code court.
    //    Echec non bloquant : l'UI SeaUI reste traduite meme si le
    //    qtbase_<locale>.qm n'est pas disponible sur le systeme.
    const QString qtDir =
        QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    const QString shortCode = code.left(code.indexOf(QLatin1Char('_')));

    if (_qtTranslator.load(QStringLiteral("qtbase_") + code, qtDir) ||
        _qtTranslator.load(QStringLiteral("qtbase_") + shortCode, qtDir)) {
        qApp->installTranslator(&_qtTranslator);
    }

    return true;
}

void TranslationManager::persistLanguage(const QString& code)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingsLanguageKey), code);
}