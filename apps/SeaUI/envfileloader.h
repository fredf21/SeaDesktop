#pragma once

#include <QMap>
#include <QString>

/**
 * @brief Helper de chargement de fichiers .env (format KEY=VALUE).
 *
 * Le fichier .env de SeaDesktop est lu par SeaUI au moment de
 * lancer le backend, et ses variables sont injectees dans le
 * QProcessEnvironment du processus enfant. Le parser YAML du
 * backend utilise std::getenv() pour resoudre ${VAR:-default}
 * a partir de cet environnement.
 *
 * Format supporte :
 *   - Lignes 'KEY=VALUE' ou 'KEY="VALUE"' (les guillemets sont
 *     retires si presents en debut et fin)
 *   - Lignes vides ignorees
 *   - Lignes commencant par '#' (commentaires) ignorees
 *   - Espaces autour de '=' ignores
 *
 * Convention de l'emplacement du .env (par defaut) :
 *
 *   <parent>/configs/...        choisi par l'utilisateur
 *   <parent>/environment/       deduit automatiquement
 *           /seadesktop.env     fichier lu par cette classe
 *
 * Le parent est le dossier contenant configs/.
 */
class EnvFileLoader
{
public:
    /**
     * Charge un fichier .env et retourne ses entrees sous forme
     * d'un QMap. Si le fichier n'existe pas, retourne une map vide
     * (pas une erreur : SeaUI peut decider de demander a l'utilisateur
     * de creer le .env via un dialog).
     */
    [[nodiscard]] static QMap<QString, QString> load(const QString& filePath);

    /**
     * Deduit le chemin standard du fichier .env a partir du
     * configsDir choisi par l'utilisateur.
     *
     * Exemple :
     *   configsDir = ~/.local/share/SeaDesktop/SeaUI/configs
     *   -> ~/.local/share/SeaDesktop/SeaUI/environment/seadesktop.env
     */
    [[nodiscard]] static QString envFilePathFor(const QString& configsDir);
};