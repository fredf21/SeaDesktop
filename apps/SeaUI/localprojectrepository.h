#pragma once

#include "IProjectRepository.h"

#include <QFuture>
#include <QString>

/**
 * @brief Implementation locale de IProjectRepository.
 *
 * Lit et ecrit les projets directement dans le dossier configs/ du
 * filesystem local. C'est le mode historique de SeaUI : on suppose
 * que SeaUI tourne sur la meme machine que le backend, et que les
 * deux partagent le meme dossier configs/.
 *
 * Bien que l'interface soit asynchrone (QFuture), les operations sont
 * executees de maniere synchrone en interne -- le filesystem local
 * est suffisamment rapide pour qu'aucun travail en arriere-plan ne
 * soit utile. Le QFuture retourne est deja resolu au moment ou
 * l'appelant en prend possession (via QtFuture::makeReadyFuture).
 *
 * En cas d'erreur, le QFuture porte une exception std::runtime_error
 * qui peut etre capturee par .onFailed() cote appelant.
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

    QFuture<ListResult> listProjects() override;
    QFuture<QString>    readRawYaml(const QString& projectName) override;
    QFuture<bool>       writeRawYaml(const QString& projectName,
                               const QString& content) override;
    QFuture<bool>       projectExists(const QString& projectName) override;
    QFuture<bool>       removeProject(const QString& projectName) override;

private:
    /**
     * Construit le chemin absolu d'un YAML projet a partir de son
     * nom logique. Ajoute l'extension .yaml.
     */
    [[nodiscard]] QString yamlPathFor(const QString& projectName) const;

    QString _configsDir;
};