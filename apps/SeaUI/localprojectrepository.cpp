#include "localprojectrepository.h"

#include "yaml/yaml_schema_parser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <filesystem>
#include <stdexcept>
#include <utility>

LocalProjectRepository::LocalProjectRepository(QString configsDir)
    : _configsDir(std::move(configsDir))
{
}

QString LocalProjectRepository::yamlPathFor(const QString& projectName) const
{
    return _configsDir + "/" + projectName + ".yaml";
}

IProjectRepository::ListResult LocalProjectRepository::listProjects()
{
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
    return result;
}

QString LocalProjectRepository::readRawYaml(const QString& projectName)
{
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
    return content;
}

void LocalProjectRepository::writeRawYaml(const QString& projectName,
                                          const QString& content)
{
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
}

bool LocalProjectRepository::projectExists(const QString& projectName)
{
    return QFileInfo::exists(yamlPathFor(projectName));
}

void LocalProjectRepository::removeProject(const QString& projectName)
{
    const QString path = yamlPathFor(projectName);
    if (!QFileInfo::exists(path)) {
        throw std::runtime_error(
            "Project YAML file not found: " + path.toStdString());
    }
    if (!QFile::remove(path)) {
        throw std::runtime_error(
            "Failed to remove YAML file: " + path.toStdString());
    }
}