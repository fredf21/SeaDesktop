#include "test_in_memory_generic_repository.h"

#include "persistence/memory/in_memory_generic_repository.h"
#include "runtime/dynamic_record.h"
#include "runtime/dynamic_value.h"

#include <QtTest>

#include <seastar/core/future.hh>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using sea::infrastructure::persistence::InMemoryGenericRepository;
using sea::infrastructure::persistence::PageRequest;
using sea::infrastructure::persistence::OffsetRequest;
using sea::infrastructure::persistence::CursorRequest;

using sea::infrastructure::runtime::DynamicRecord;

namespace {

DynamicRecord makeUser(const std::string& id,
                       const std::string& name,
                       std::int32_t age)
{
    DynamicRecord record;
    record["id"] = id;
    record["name"] = name;
    record["age"] = age;
    return record;
}

std::string getString(const DynamicRecord& record, const std::string& key)
{
    return std::get<std::string>(record.at(key));
}

std::int32_t getInt32(const DynamicRecord& record, const std::string& key)
{
    return std::get<std::int32_t>(record.at(key));
}

} // namespace

void TestInMemoryGenericRepository::create_withValidId_shouldStoreRecord()
{
    InMemoryGenericRepository repo;

    auto record = makeUser("u1", "Alice", 30);

    auto created = await_ready(repo.create("User", record));

    QVERIFY(created.has_value());
    QCOMPARE(getString(*created, "id"), std::string("u1"));
    QCOMPARE(getString(*created, "name"), std::string("Alice"));
    QCOMPARE(getInt32(*created, "age"), std::int32_t(30));

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("Alice"));
}

void TestInMemoryGenericRepository::create_withoutId_shouldReturnNullopt()
{
    InMemoryGenericRepository repo;

    DynamicRecord record;
    record["name"] = std::string("Alice");
    record["age"] = std::int32_t(30);

    auto created = await_ready(repo.create("User", record));

    QVERIFY(!created.has_value());
}

void TestInMemoryGenericRepository::create_duplicateId_shouldReturnNullopt()
{
    InMemoryGenericRepository repo;

    auto first = makeUser("u1", "Alice", 30);
    auto duplicate = makeUser("u1", "Bob", 40);

    auto created1 = await_ready(repo.create("User", first));
    auto created2 = await_ready(repo.create("User", duplicate));

    QVERIFY(created1.has_value());
    QVERIFY(!created2.has_value());

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("Alice"));
}

void TestInMemoryGenericRepository::create_shouldIgnoreManyToManyVectorString()
{
    InMemoryGenericRepository repo;

    DynamicRecord record;
    record["id"] = std::string("u1");
    record["name"] = std::string("Alice");
    record["roles"] = std::vector<std::string>{ "admin", "editor" };

    auto created = await_ready(repo.create("User", record));

    QVERIFY(created.has_value());
    QVERIFY(created->contains("id"));
    QVERIFY(created->contains("name"));
    QVERIFY(!created->contains("roles"));
}

void TestInMemoryGenericRepository::findById_existingRecord_shouldReturnRecord()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "id"), std::string("u1"));
    QCOMPARE(getString(*found, "name"), std::string("Alice"));
}

void TestInMemoryGenericRepository::findById_unknownRecord_shouldReturnNullopt()
{
    InMemoryGenericRepository repo;

    auto found = await_ready(repo.find_by_id("User", "unknown"));

    QVERIFY(!found.has_value());
}

void TestInMemoryGenericRepository::findAll_existingEntity_shouldReturnAllRecords()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    auto all = await_ready(repo.find_all("User"));

    QCOMPARE(all.size(), std::size_t(2));
}

void TestInMemoryGenericRepository::findOneByField_matchingString_shouldReturnRecord()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    auto found = await_ready(repo.find_one_by_field("User", "name", "Bob"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "id"), std::string("u2"));
    QCOMPARE(getString(*found, "name"), std::string("Bob"));
}

void TestInMemoryGenericRepository::update_existingRecord_shouldModifyFields()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    DynamicRecord patch;
    patch["name"] = std::string("Alice Updated");
    patch["age"] = std::int32_t(31);

    auto response = await_ready(repo.update("User", "u1", patch));

    QVERIFY(response.status);
    QCOMPARE(getString(response.record, "name"), std::string("Alice Updated"));
    QCOMPARE(getInt32(response.record, "age"), std::int32_t(31));

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("Alice Updated"));
}

void TestInMemoryGenericRepository::update_unknownRecord_shouldFail()
{
    InMemoryGenericRepository repo;

    DynamicRecord patch;
    patch["name"] = std::string("Ghost");

    auto response = await_ready(repo.update("User", "unknown", patch));

    QVERIFY(!response.status);
    QVERIFY(response.record.empty());
}

void TestInMemoryGenericRepository::update_shouldNotModifyId()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    DynamicRecord patch;
    patch["id"] = std::string("u2");
    patch["name"] = std::string("Alice Updated");

    auto response = await_ready(repo.update("User", "u1", patch));

    QVERIFY(response.status);
    QVERIFY(!response.record.empty());

    QCOMPARE(getString(response.record, "id"), std::string("u1"));
    QCOMPARE(getString(response.record, "name"), std::string("Alice Updated"));

    auto foundOld = await_ready(repo.find_by_id("User", "u1"));
    auto foundNew = await_ready(repo.find_by_id("User", "u2"));

    QVERIFY(foundOld.has_value());
    QVERIFY(!foundNew.has_value());
}

void TestInMemoryGenericRepository::update_shouldIgnoreManyToManyVectorString()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    DynamicRecord patch;
    patch["roles"] = std::vector<std::string>{ "admin", "editor" };

    auto response = await_ready(repo.update("User", "u1", patch));

    QVERIFY(response.status);
    QVERIFY(!response.record.empty());
    QVERIFY(!response.record.contains("roles"));
}

void TestInMemoryGenericRepository::remove_existingRecord_shouldReturnTrue()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool removed = await_ready(repo.remove("User", "u1"));

    QVERIFY(removed);

    auto found = await_ready(repo.find_by_id("User", "u1"));
    QVERIFY(!found.has_value());
}

void TestInMemoryGenericRepository::remove_unknownRecord_shouldReturnFalse()
{
    InMemoryGenericRepository repo;

    const bool removed = await_ready(repo.remove("User", "unknown"));

    QVERIFY(!removed);
}

void TestInMemoryGenericRepository::count_existingEntity_shouldReturnNumberOfRecords()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    const auto count = await_ready(repo.count("User"));

    QCOMPARE(count, std::size_t(3));
}

void TestInMemoryGenericRepository::listPage_shouldReturnRequestedPage()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    PageRequest request;
    request.page = 2;
    request.page_size = 1;
    request.sort_field = std::string("name");
    request.sort_desc = false;

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.total, std::size_t(3));
    QCOMPARE(result.items.size(), std::size_t(1));
    QCOMPARE(getString(result.items[0], "name"), std::string("Bob"));
}

void TestInMemoryGenericRepository::listOffset_shouldReturnRequestedSlice()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    OffsetRequest request;
    request.offset = 1;
    request.limit = 2;
    request.sort_field = std::string("name");
    request.sort_desc = false;

    auto result = await_ready(repo.list_offset("User", request));

    QCOMPARE(result.total, std::size_t(3));
    QCOMPARE(result.items.size(), std::size_t(2));
    QCOMPARE(getString(result.items[0], "name"), std::string("Bob"));
    QCOMPARE(getString(result.items[1], "name"), std::string("Charlie"));
}

void TestInMemoryGenericRepository::listCursor_shouldReturnItemsAfterCursor()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    CursorRequest request;
    request.cursor_field = "name";
    request.after = std::string("Alice");
    request.limit = 1;
    request.sort_desc = false;

    auto result = await_ready(repo.list_cursor("User", request));

    QCOMPARE(result.items.size(), std::size_t(1));
    QCOMPARE(getString(result.items[0], "name"), std::string("Bob"));
    QVERIFY(result.next_cursor.has_value());
    QCOMPARE(*result.next_cursor, std::string("Bob"));
}

void TestInMemoryGenericRepository::incrementField_int32_shouldIncrementValue()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool incremented = await_ready(repo.increment_field("User", "u1", "age", 5));

    QVERIFY(incremented);

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getInt32(*found, "age"), std::int32_t(35));
}

void TestInMemoryGenericRepository::incrementField_missingField_shouldReturnFalse()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool incremented = await_ready(repo.increment_field("User", "u1", "unknown", 5));

    QVERIFY(!incremented);
}

void TestInMemoryGenericRepository::incrementField_nonNumericField_shouldReturnFalse()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool incremented = await_ready(repo.increment_field("User", "u1", "name", 5));

    QVERIFY(!incremented);

    auto found = await_ready(repo.find_by_id("User", "u1"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("Alice"));
}

// ═════════════════════════════════════════════════════════════
// COUVERTURE ADDITIONNELLE
// ═════════════════════════════════════════════════════════════

// ── count / find_all sur entité inexistante ──────────────────

void TestInMemoryGenericRepository::count_unknownEntity_shouldReturnZero()
{
    InMemoryGenericRepository repo;

    // Aucune entité "User" n'a jamais été touchée.
    const auto count = await_ready(repo.count("User"));

    QCOMPARE(count, std::size_t(0));
}

void TestInMemoryGenericRepository::count_emptyAfterRemovingAll_shouldReturnZero()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    await_ready(repo.remove("User", "u1"));
    await_ready(repo.remove("User", "u2"));

    const auto count = await_ready(repo.count("User"));
    QCOMPARE(count, std::size_t(0));
}

void TestInMemoryGenericRepository::findAll_unknownEntity_shouldReturnEmpty()
{
    InMemoryGenericRepository repo;

    auto all = await_ready(repo.find_all("Ghost"));

    QVERIFY(all.empty());
}

// ── create avec id entier ────────────────────────────────────

void TestInMemoryGenericRepository::create_withInt64Id_shouldStoreRecord()
{
    // extract_id supporte un id de type std::int64_t : il est
    // converti en chaîne ("42") pour servir de clé de stockage.
    InMemoryGenericRepository repo;

    DynamicRecord record;
    record["id"] = std::int64_t(42);
    record["name"] = std::string("NumericId");

    auto created = await_ready(repo.create("User", record));
    QVERIFY(created.has_value());

    // La clé de stockage est la représentation décimale de l'id.
    auto found = await_ready(repo.find_by_id("User", "42"));
    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("NumericId"));
}

// ── find_one_by_field : cas non couverts ─────────────────────

void TestInMemoryGenericRepository::findOneByField_noMatch_shouldReturnNullopt()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    auto found = await_ready(repo.find_one_by_field("User", "name", "Zoe"));

    QVERIFY(!found.has_value());
}

void TestInMemoryGenericRepository::findOneByField_unknownEntity_shouldReturnNullopt()
{
    InMemoryGenericRepository repo;

    auto found = await_ready(repo.find_one_by_field("Ghost", "name", "Alice"));

    QVERIFY(!found.has_value());
}

void TestInMemoryGenericRepository::findOneByField_numericField_shouldMatchViaStringConversion()
{
    // find_one_by_field compare via dynamic_value_to_string : un
    // champ numérique se cherche donc avec sa représentation décimale.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    auto found = await_ready(repo.find_one_by_field("User", "age", "40"));

    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "id"), std::string("u2"));
}

// ── increment_field : cas non couverts ───────────────────────

void TestInMemoryGenericRepository::incrementField_unknownEntity_shouldReturnFalse()
{
    InMemoryGenericRepository repo;

    const bool incremented =
        await_ready(repo.increment_field("Ghost", "u1", "age", 5));

    QVERIFY(!incremented);
}

void TestInMemoryGenericRepository::incrementField_unknownRecord_shouldReturnFalse()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool incremented =
        await_ready(repo.increment_field("User", "unknown", "age", 5));

    QVERIFY(!incremented);
}

void TestInMemoryGenericRepository::incrementField_negativeDelta_shouldDecrement()
{
    // Un delta négatif décrémente le champ (v + delta).
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));

    const bool incremented =
        await_ready(repo.increment_field("User", "u1", "age", -12));
    QVERIFY(incremented);

    auto found = await_ready(repo.find_by_id("User", "u1"));
    QVERIFY(found.has_value());
    QCOMPARE(getInt32(*found, "age"), std::int32_t(18));
}

// ── in_transaction ───────────────────────────────────────────

void TestInMemoryGenericRepository::inTransaction_workReturnsTrue_shouldCommit()
{
    InMemoryGenericRepository repo;

    auto result = await_ready(repo.in_transaction([]() {
        return seastar::make_ready_future<bool>(true);
    }));

    QVERIFY(result.committed);
    QVERIFY(result.error_message.empty());
}

void TestInMemoryGenericRepository::inTransaction_workReturnsFalse_shouldNotCommit()
{
    // work renvoie false : pas de commit, message d'erreur renseigné.
    InMemoryGenericRepository repo;

    auto result = await_ready(repo.in_transaction([]() {
        return seastar::make_ready_future<bool>(false);
    }));

    QVERIFY(!result.committed);
    QVERIFY(!result.error_message.empty());
}

void TestInMemoryGenericRepository::inTransaction_workThrows_shouldNotCommit()
{
    // Une exception dans work est attrapée : committed=false,
    // message d'erreur renseigné, aucune exception ne remonte.
    InMemoryGenericRepository repo;

    auto result = await_ready(repo.in_transaction([]() -> seastar::future<bool> {
        throw std::runtime_error("boom");
    }));

    QVERIFY(!result.committed);
    QVERIFY(!result.error_message.empty());
}

void TestInMemoryGenericRepository::inTransaction_canMutateRepository()
{
    // Le backend mémoire n'a pas de vrai rollback : les écritures
    // faites dans work persistent même si work réussit. On vérifie
    // simplement qu'une mutation effectuée dans la transaction est
    // bien visible ensuite.
    InMemoryGenericRepository repo;

    auto result = await_ready(repo.in_transaction([&repo]() {
        auto created = await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
        return seastar::make_ready_future<bool>(created.has_value());
    }));

    QVERIFY(result.committed);

    auto found = await_ready(repo.find_by_id("User", "u1"));
    QVERIFY(found.has_value());
    QCOMPARE(getString(*found, "name"), std::string("Alice"));
}

// ── insert_pivot ─────────────────────────────────────────────

void TestInMemoryGenericRepository::insertPivot_shouldStoreLink()
{
    // insert_pivot stocke le lien M2M ; il renvoie toujours true
    // pour le backend mémoire.
    InMemoryGenericRepository repo;

    DynamicRecord link;
    link["user_id"] = std::string("u1");
    link["role_id"] = std::string("r1");

    const bool inserted = await_ready(repo.insert_pivot("user_roles", link));
    QVERIFY(inserted);

    // La table pivot est une "entité" comme une autre dans storage_.
    const auto count = await_ready(repo.count("user_roles"));
    QCOMPARE(count, std::size_t(1));
}

void TestInMemoryGenericRepository::insertPivot_sameKeyTwice_shouldOverwrite()
{
    // La clé synthétique d'un pivot dépend de ses valeurs : insérer
    // deux fois le même couple ne crée qu'une seule entrée.
    InMemoryGenericRepository repo;

    DynamicRecord link;
    link["user_id"] = std::string("u1");
    link["role_id"] = std::string("r1");

    await_ready(repo.insert_pivot("user_roles", link));
    await_ready(repo.insert_pivot("user_roles", link));

    const auto count = await_ready(repo.count("user_roles"));
    QCOMPARE(count, std::size_t(1));
}

// ── pagination : cas limites ─────────────────────────────────

void TestInMemoryGenericRepository::listPage_pageZero_shouldBehaveAsFirstPage()
{
    // page == 0 est traité défensivement comme page 1.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    PageRequest request;
    request.page = 0;
    request.page_size = 1;
    request.sort_field = std::string("name");
    request.sort_desc = false;

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.total, std::size_t(2));
    QCOMPARE(result.items.size(), std::size_t(1));
    QCOMPARE(getString(result.items[0], "name"), std::string("Alice"));
}

void TestInMemoryGenericRepository::listPage_pageSizeZero_shouldReturnEmpty()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    PageRequest request;
    request.page = 1;
    request.page_size = 0;
    request.sort_field = std::string("name");

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.total, std::size_t(2));
    QVERIFY(result.items.empty());
}

void TestInMemoryGenericRepository::listPage_offsetBeyondTotal_shouldReturnEmptyButKeepTotal()
{
    // Une page au-delà du total : items vide, mais total reste correct.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    PageRequest request;
    request.page = 99;
    request.page_size = 10;
    request.sort_field = std::string("name");

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.total, std::size_t(2));
    QVERIFY(result.items.empty());
}

void TestInMemoryGenericRepository::listPage_sortDescending_shouldReverseOrder()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    PageRequest request;
    request.page = 1;
    request.page_size = 3;
    request.sort_field = std::string("name");
    request.sort_desc = true;

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.items.size(), std::size_t(3));
    QCOMPARE(getString(result.items[0], "name"), std::string("Charlie"));
    QCOMPARE(getString(result.items[1], "name"), std::string("Bob"));
    QCOMPARE(getString(result.items[2], "name"), std::string("Alice"));
}

void TestInMemoryGenericRepository::listOffset_offsetBeyondTotal_shouldReturnEmpty()
{
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));

    OffsetRequest request;
    request.offset = 50;
    request.limit = 10;
    request.sort_field = std::string("name");

    auto result = await_ready(repo.list_offset("User", request));

    QCOMPARE(result.total, std::size_t(2));
    QVERIFY(result.items.empty());
}

void TestInMemoryGenericRepository::listCursor_noCursor_shouldReturnFromStart()
{
    // Sans 'after', le curseur démarre au tout début de la liste triée.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    CursorRequest request;
    request.cursor_field = "name";
    request.limit = 2;
    request.sort_desc = false;
    // request.after laissé à nullopt

    auto result = await_ready(repo.list_cursor("User", request));

    QCOMPARE(result.items.size(), std::size_t(2));
    QCOMPARE(getString(result.items[0], "name"), std::string("Alice"));
    QCOMPARE(getString(result.items[1], "name"), std::string("Bob"));
    QVERIFY(result.next_cursor.has_value());
    QCOMPARE(*result.next_cursor, std::string("Bob"));
}

void TestInMemoryGenericRepository::listCursor_lastPage_shouldHaveNoNextCursor()
{
    // Quand la page atteint la fin de la liste, next_cursor est absent.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("u1", "Alice", 30)));
    await_ready(repo.create("User", makeUser("u2", "Bob", 40)));
    await_ready(repo.create("User", makeUser("u3", "Charlie", 50)));

    CursorRequest request;
    request.cursor_field = "name";
    request.after = std::string("Bob");   // reste seulement Charlie
    request.limit = 10;
    request.sort_desc = false;

    auto result = await_ready(repo.list_cursor("User", request));

    QCOMPARE(result.items.size(), std::size_t(1));
    QCOMPARE(getString(result.items[0], "name"), std::string("Charlie"));
    QVERIFY(!result.next_cursor.has_value());
}

void TestInMemoryGenericRepository::listPage_emptyEntity_shouldReturnEmpty()
{
    InMemoryGenericRepository repo;

    PageRequest request;
    request.page = 1;
    request.page_size = 10;
    request.sort_field = std::string("name");

    auto result = await_ready(repo.list_page("User", request));

    QCOMPARE(result.total, std::size_t(0));
    QVERIFY(result.items.empty());
}

// ── isolation entre entités ──────────────────────────────────

void TestInMemoryGenericRepository::storage_distinctEntities_shouldNotInterfere()
{
    // storage_ est indexé par entity_name : deux entités portant le
    // même id ne se télescopent pas.
    InMemoryGenericRepository repo;

    await_ready(repo.create("User", makeUser("shared-id", "Alice", 30)));

    DynamicRecord product;
    product["id"] = std::string("shared-id");
    product["name"] = std::string("Widget");
    await_ready(repo.create("Product", product));

    QCOMPARE(await_ready(repo.count("User")), std::size_t(1));
    QCOMPARE(await_ready(repo.count("Product")), std::size_t(1));

    auto user = await_ready(repo.find_by_id("User", "shared-id"));
    auto prod = await_ready(repo.find_by_id("Product", "shared-id"));

    QVERIFY(user.has_value());
    QVERIFY(prod.has_value());
    QCOMPARE(getString(*user, "name"), std::string("Alice"));
    QCOMPARE(getString(*prod, "name"), std::string("Widget"));

    // Supprimer dans une entité n'affecte pas l'autre.
    await_ready(repo.remove("User", "shared-id"));
    QCOMPARE(await_ready(repo.count("User")), std::size_t(0));
    QCOMPARE(await_ready(repo.count("Product")), std::size_t(1));
}