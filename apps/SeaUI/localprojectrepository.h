#pragma once

#include "IProjectRepository.h"

#include <QString>

/**
 * @brief Implementation locale de IProjectRepository.
 *
 * Lit et ecrit les projets directement dans le dossier configs/ du
 * filesystem local. C'est le mode historique de SeaUI : on suppose
 * que SeaUI tourne sur la meme machine que le backend, et que les
 * deux partagent le meme dossier configs/.
 *
 * Le dossier configs/ est resolu via appConfigsDir() (defini dans
 * mainwindow.cpp), qui suit la priorite :
 *   1. Variable d'environnement SEA_DESKTOP_CONFIGS_DIR
 *   2. Dossier 'configs' a cote du repo source (mode dev)
 *   3. AppDataLocation/configs (mode release / install systeme)
 *
 * Le parsing des YAML utilise YamlSchemaParser de la lib
 * sea_infrastructure, embarquee statiquement dans le binaire SeaUI.
 */
class LocalProjectRepository : public IProjectRepository
{
public:
    /**
     * @param configsDir Chemin absolu du dossier contenant les YAML.
     *                   Typiquement le retour de appConfigsDir().
     */
    explicit LocalProjectRepository(QString configsDir);

    ListResult listProjects() override;
    QString    readRawYaml(const QString& projectName) override;
    void       writeRawYaml(const QString& projectName,
                      const QString& content) override;
    bool       projectExists(const QString& projectName) override;
    void       removeProject(const QString& projectName) override;

private:
    /**
     * Construit le chemin absolu d'un YAML projet a partir de son
     * nom logique. Ajoute l'extension .yaml.
     */
    [[nodiscard]] QString yamlPathFor(const QString& projectName) const;

    QString _configsDir;
};