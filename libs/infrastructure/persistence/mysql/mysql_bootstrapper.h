#pragma once

#include "database_config.h"
#include "schema.h"
#include "mysql_introspector.h"
#include "mysqlconnexionpool.h"
#include "thread_pool_execution/i_blocking_executor.h"

#include <memory>
#include <vector>
#include <string>

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>

namespace sea::infrastructure::persistence::mysql {

// ─────────────────────────────────────────────────────────────
// Resultat d'un bootstrap
// ─────────────────────────────────────────────────────────────
struct BootstrapResult {
    bool success = false;

    bool database_was_created = false;
    std::vector<std::string> tables_created;
    std::vector<std::string> columns_added; // forma~/Qt/Tools/QtCreator/bin/qtcreatort "Table.column"
    std::vector<std::string> columns_modified;
    std::vector<std::string> columns_renamed;
    std::vector<std::string> indexes_changed;
    std::vector<std::string> pivots_created;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;



};

// ─────────────────────────────────────────────────────────────
// MysqlBootstrapper
//
// Orchestre le bootstrap automatique de la base de donnees au boot
// du serveur. Compare le schema YAML avec l'etat actuel de MySQL
// et applique les changements necessaires.
//
// Flow :
// 1. ensure_database_exists() : connecte sans DB, CREATE DATABASE si manquante
// 2. introspect() : lit l'etat actuel (apres reconnexion avec la DB)
// 3. compute_and_apply_diff() : compare YAML vs MySQL, applique les changements
//
// Modes de migration :
// - Conservative (defaut) : CREATE TABLE + ADD COLUMN. Jamais DROP.
// - Modified (V2) : + MODIFY COLUMN
// - Aggressive (V3) : tout, y compris DROP
// ─────────────────────────────────────────────────────────────
class MysqlBootstrapper {
public:
    MysqlBootstrapper(
        const sea::domain::DatabaseConfig& config,
        const sea::domain::Schema& schema,
        seastar::sharded<MysqlConnexionPool>& pool,
        std::shared_ptr<IBlockingExecutor> executor
        );

    // Methode principale : execute le bootstrap complet.
    seastar::future<BootstrapResult> bootstrap();

    // Cree la database si elle n'existe pas.
    // Cette methode utilise une connexion SANS specifier la DB (root@host)
    // car la DB peut ne pas exister.
    seastar::future<bool> ensure_database_exists();

    // ─────────────────────────────────────────────────────────
    // S'assure que la table système `sea_files` existe.
    //
    // Cette table stocke les metadata de tous les fichiers
    // uploadés (cf. SeaFilesTable). Elle est créée AVANT toutes
    // les tables d'entités du YAML, car celles-ci peuvent y
    // référer via des FK (champs de type File).
    //
    // Idempotent : utilise CREATE TABLE IF NOT EXISTS. Peut
    // être appelée à chaque boot sans effet de bord.
    //
    // Note : on ne modifie pas la table existante si son schéma
    // a évolué — cela suivra le pattern de migration des entités
    // utilisateur si on évolue le schéma de sea_files plus tard.
    //
    // Retour : true si la table existe (créée ou déjà présente).
    //         false en cas d'erreur SQL.
    // ─────────────────────────────────────────────────────────
    seastar::future<bool> ensure_sea_files_table();

private:
    // Compare le schema YAML avec l'etat actuel et applique les changements.
    // Utilise pool/executor pour les operations bloquantes.
    seastar::future<>
    compute_and_apply_diff(const SchemaSnapshot& snapshot, BootstrapResult& result);

    // Execute un SQL statement (sans fetch).
    // Utilise pour CREATE TABLE, ALTER TABLE, etc.
    seastar::future<bool> execute_sql(const std::string& sql);

    // Helper : cree directement une connexion sans DB (pour CREATE DATABASE).
    // Cette connexion est UTILISEE UNE FOIS puis fermee.
    seastar::future<bool> execute_sql_without_database(const std::string& sql);

    const sea::domain::DatabaseConfig& _config;
    const sea::domain::Schema& _schema;
    seastar::sharded<MysqlConnexionPool>& _pool;
    std::shared_ptr<IBlockingExecutor> _executor;
};

} // namespace sea::infrastructure::persistence::mysql