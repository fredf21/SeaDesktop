#include "sea_files_table.h"

#include <sstream>

namespace sea::infrastructure::persistence::mysql {

std::string SeaFilesTable::generate_create_table_sql()
{
    std::ostringstream sql;

    sql << "CREATE TABLE IF NOT EXISTS `" << TABLE_NAME << "` (\n"
        << "  `" << COL_ID              << "` BINARY(16) NOT NULL,\n"
        << "  `" << COL_ORIGINAL_NAME   << "` VARCHAR(255) NOT NULL,\n"
        << "  `" << COL_MIME_TYPE       << "` VARCHAR(100) NOT NULL,\n"
        << "  `" << COL_SIZE_BYTES      << "` BIGINT NOT NULL,\n"
        << "  `" << COL_STORAGE_PATH    << "` VARCHAR(500) NOT NULL,\n"
        << "  `" << COL_REFERENCE_COUNT << "` INT NOT NULL DEFAULT 0,\n"
        << "  `" << COL_CREATED_AT      << "` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
        << "  PRIMARY KEY (`" << COL_ID << "`)\n"
        << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci";

    return sql.str();
}

} // namespace sea::infrastructure::persistence::mysql