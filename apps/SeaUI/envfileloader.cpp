#include "envfileloader.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

/**
 * Retire les guillemets entourant une valeur si presents.
 * "abc" -> abc
 * 'abc' -> abc
 * abc   -> abc
 */
QString stripQuotes(const QString& s)
{
    if (s.length() < 2) {
        return s;
    }
    const QChar first = s.front();
    const QChar last  = s.back();
    if ((first == '"'  && last == '"') ||
        (first == '\'' && last == '\'')) {
        return s.mid(1, s.length() - 2);
    }
    return s;
}

} // namespace

QMap<QString, QString> EnvFileLoader::load(const QString& filePath)
{
    QMap<QString, QString> result;

    QFile file(filePath);
    if (!file.exists()) {
        // Pas une erreur : le fichier peut etre absent au premier
        // lancement. Le code appelant gerera ce cas.
        return result;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "EnvFileLoader: cannot open" << filePath
                   << ":" << file.errorString();
        return result;
    }

    QTextStream in(&file);
    int lineNumber = 0;
    while (!in.atEnd()) {
        ++lineNumber;
        const QString rawLine = in.readLine();
        const QString line = rawLine.trimmed();

        // Lignes vides et commentaires : ignorees.
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        const int equalsIdx = line.indexOf('=');
        if (equalsIdx < 0) {
            qWarning() << "EnvFileLoader: malformed line" << lineNumber
                       << "in" << filePath << ":" << rawLine;
            continue;
        }

        const QString key   = line.left(equalsIdx).trimmed();
        const QString value = stripQuotes(line.mid(equalsIdx + 1).trimmed());

        if (key.isEmpty()) {
            qWarning() << "EnvFileLoader: empty key at line" << lineNumber
                       << "in" << filePath;
            continue;
        }

        result.insert(key, value);
    }

    return result;
}

QString EnvFileLoader::envFilePathFor(const QString& configsDir)
{
    QDir parent(configsDir);
    parent.cdUp();
    return parent.absoluteFilePath("environment/seadesktop.env");
}