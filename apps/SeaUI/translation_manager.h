#ifndef TRANSLATION_MANAGER_H
#define TRANSLATION_MANAGER_H

#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVector>

/**
 * @brief Gere l'internationalisation de l'application SeaUI.
 *
 * Cette classe encapsule :
 *  - le chargement des fichiers de traduction compiles (.qm) : les chaines
 *    propres a SeaUI (embarquees sous ":/i18n/") et les chaines standard
 *    de Qt (qtbase) ;
 *  - l'installation / desinstallation des QTranslator sur l'application ;
 *  - la persistance de la langue choisie via QSettings ;
 *  - la liste des langues disponibles.
 *
 * Le changement de langue est applique a chaud : apres installation des
 * nouveaux traducteurs, Qt envoie un evenement QEvent::LanguageChange a
 * tous les widgets, qui se retraduisent alors via retranslateUi().
 */
class TranslationManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Decrit une langue proposee a l'utilisateur.
     */
    struct Language
    {
        QString code;         ///< Code locale, ex. "en_US", "fr_FR".
        QString displayName;  ///< Nom affiche dans le menu, ex. "Francais".
    };

    explicit TranslationManager(QObject* parent = nullptr);

    /**
     * @brief Retourne la liste des langues supportees par l'application.
     */
    [[nodiscard]] const QVector<Language>& availableLanguages() const;

    /**
     * @brief Retourne le code de la langue actuellement active.
     */
    [[nodiscard]] QString currentLanguage() const;

    /**
     * @brief Charge la langue persistee dans QSettings et l'applique.
     *
     * A appeler une fois au demarrage, avant la creation de la fenetre
     * principale. Si aucune preference n'est enregistree, la langue de
     * la locale systeme est tentee, puis l'anglais en repli.
     */
    void loadPersistedLanguage();

    /**
     * @brief Change la langue active et persiste le choix.
     *
     * @param code Code locale ("en_US", "fr_FR").
     * @return true si la langue a ete appliquee, false si code inconnu.
     */
    bool applyLanguage(const QString& code);

signals:
    /**
     * @brief Emis apres qu'une nouvelle langue a ete appliquee avec succes.
     *
     * @param code Code de la langue desormais active.
     */
    void languageChanged(const QString& code);

private:
    /**
     * @brief Installe les fichiers .qm (SeaUI + qtbase) pour le code donne.
     *
     * @param code Code locale.
     * @return true si au moins le traducteur SeaUI a ete charge et installe.
     */
    bool installTranslatorsFor(const QString& code);

    /**
     * @brief Retire les deux traducteurs de l'application.
     */
    void removeInstalledTranslators();

    /**
     * @brief Persiste le code de langue dans QSettings.
     */
    void persistLanguage(const QString& code);

    /**
     * @brief Verifie qu'un code de langue fait partie des langues supportees.
     */
    [[nodiscard]] bool isSupported(const QString& code) const;

    QVector<Language> _languages;     ///< Langues supportees.
    QTranslator       _appTranslator; ///< Traducteur des chaines de SeaUI.
    QTranslator       _qtTranslator;  ///< Traducteur des chaines standard Qt.
    QString           _currentCode;   ///< Langue active courante.
};

#endif // TRANSLATION_MANAGER_H