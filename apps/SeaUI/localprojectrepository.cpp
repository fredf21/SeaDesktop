#include "localprojectrepository.h"

#include "yaml/yaml_schema_parser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPromise>
#include <QTextStream>
#include <QFuture>

#include <exception>
#include <filesystem>
#include <stdexcept>
#include <utility>
// QtFuture::makeReadyValueFuture a ete introduit en Qt 6.6.
// Avant, on utilise QtFuture::makeReadyFuture (meme semantique).
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
namespace QtFuture {
template <typename T>
inline auto makeReadyValueFuture(T&& value) {
    return makeReadyFuture(std::forward<T>(value));
}
}
#endif
namespace {

/**
 * Cree un QFuture<T> deja resolu portant l'exception passee. Utilise
 * pour propager les erreurs synchrones via le mecanisme QFuture.
 */
template <typename T>
QFuture<T> makeFailedFuture(std::exception_ptr ex)
{
    QPromise<T> promise;
    promise.start();
    promise.setException(ex);
    promise.finish();
    return promise.future();
}

} // namespace anonyme


LocalProjectRepository::LocalProjectRepository(QString configsDir)
    : _configsDir(std::move(configsDir))
{
}

QString LocalProjectRepository::yamlPathFor(const QString& projectName) const
{
    return _configsDir + "/" + projectName + ".yaml";
}

QFuture<IProjectRepository::ListResult> LocalProjectRepository::listProjects()
{
    try {
        ListResult result;

        QDir().mkpath(_configsDir);
        QDir dir(_configsDir);

        const QStringList files =
            dir.entryList(QStringList() << "*.yaml" << "*.yml", QDir::Files);

        sea::infrastructure::yaml::YamlSchemaParser parser;
        for (const QString& file : files) {
            const std::filesystem::path path =
                std::filesystem::path(_configsDir.toStdString()) /
                file.toStdString();
            try {
                result.projects.push_back(
                    parser.parse_project_file(path.string()));
            } catch (const std::exception& e) {
                result.errors.append(
                    QStringLiteral("%1: %2").arg(file, QString::fromUtf8(e.what())));
            }
        }
        return QtFuture::makeReadyValueFuture(result);
    } catch (...) {
        return makeFailedFuture<ListResult>(std::current_exception());
    }
}

QFuture<QString> LocalProjectRepository::readRawYaml(const QString& projectName)
{
    try {
        const QString path = yamlPathFor(projectName);
        if (!QFileInfo::exists(path)) {
            throw std::runtime_error(
                "Project YAML file not found: " + path.toStdString());
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            throw std::runtime_error(
                "Unable to open YAML file for reading: " + path.toStdString());
        }

        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        const QString content = in.readAll();
        file.close();

        return QtFuture::makeReadyValueFuture(content);
    } catch (...) {
        return makeFailedFuture<QString>(std::current_exception());
    }
}

QFuture<bool> LocalProjectRepository::writeRawYaml(const QString& projectName,
                                                   const QString& content)
{
    try {
        QDir().mkpath(_configsDir);
        const QString path = yamlPathFor(projectName);

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            throw std::runtime_error(
                "Unable to open YAML file for writing: " + path.toStdString());
        }

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << content;
        file.close();

        if (file.error() != QFile::NoError) {
            throw std::runtime_error(
                "Write to YAML file failed: " + path.toStdString());
        }

        return QtFuture::makeReadyValueFuture(true);
    } catch (...) {
        return makeFailedFuture<bool>(std::current_exception());
    }
}

QFuture<bool> LocalProjectRepository::projectExists(const QString& projectName)
{
    try {
        const bool exists = QFileInfo::exists(yamlPathFor(projectName));
        return QtFuture::makeReadyValueFuture(exists);
    } catch (...) {
        return makeFailedFuture<bool>(std::current_exception());
    }
}

QFuture<bool> LocalProjectRepository::removeProject(const QString& projectName)
{
    try {
        const QString path = yamlPathFor(projectName);
        if (!QFileInfo::exists(path)) {
            throw std::runtime_error(
                "Project YAML file not found: " + path.toStdString());
        }
        if (!QFile::remove(path)) {
            throw std::runtime_error(
                "Failed to remove YAML file: " + path.toStdString());
        }
        return QtFuture::makeReadyValueFuture(true);
    } catch (...) {
        return makeFailedFuture<bool>(std::current_exception());
    }
}