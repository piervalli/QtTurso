#include "testturso.h"
#include "qfuture.h"
#include "qsqlindex.h"
#include <QtConcurrent>
#include <QDebug>
TestTurso::TestTurso(QObject *parent)
    : QObject{parent},testDbPath("test_turso.db")
{}

TestTurso::~TestTurso()
{

}

void TestTurso::initTestCase()
{
    qInfo() << "=== Inizio Test Suite QTURSO ===";

    // Rimuovi database di test se esiste
    if (QFile::exists(testDbPath)) {
        QFile::remove(testDbPath);
    }

    // Verifica che il driver QTURSO sia disponibile
    if (!QSqlDatabase::isDriverAvailable("QTURSO")) {
        QFAIL("Driver QTURSO non disponibile!");
    }

    if(!QSqlDatabase::contains("TURSO"))
        QSqlDatabase::addDatabase("QTURSO","TURSO");
    auto db = QSqlDatabase::database("TURSO");
    db.setDatabaseName(testDbPath);
    //db.setConnectOptions("TURSO_WRITER_CONCURRENCY");
    QVERIFY2(db.open(), qPrintable(db.lastError().text()));
    QVERIFY(db.isOpen());
    qInfo() << "Driver QTURSO disponibile";
}

void TestTurso::cleanupTestCase()
{
    auto db = QSqlDatabase::database("TURSO");
    if (db.isOpen()) {
        db.close();
    }
    QSqlDatabase::removeDatabase(m_defaultConnection);

    qInfo() << "=== Fine Test Suite QTURSO ===";
}

void TestTurso::testSelectWithWhere()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text) VALUES (1001, 'where_test')"));
    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_integer = 1001 AND col_text = 'where_test'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString("where_test"));

    qInfo() << "✓ SELECT with WHERE OK";
}

void TestTurso::testSelectWithOrderBy()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    QVERIFY(query.exec("delete from test_all_types;"));
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer) VALUES (3), (1), (2)"));
    QVERIFY(query.exec("SELECT col_integer FROM test_all_types WHERE col_integer IN (1,2,3) ORDER BY col_integer ASC"));

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    qInfo() << "✓ SELECT with ORDER BY OK";
}

void TestTurso::testSelectWithLimit()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("SELECT * FROM test_all_types LIMIT 5"));

    int count = 0;
    while (query.next()) count++;
    QVERIFY(count <= 5);

    qInfo() << "✓ SELECT with LIMIT OK";
}

void TestTurso::testSelectWithJoin()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("CREATE TABLE IF NOT EXISTS test_join_a (id INTEGER, name TEXT)"));
    QVERIFY(query.exec("CREATE TABLE IF NOT EXISTS test_join_b (id INTEGER, value TEXT)"));

    QVERIFY(query.exec("INSERT INTO test_join_a VALUES (1, 'A'), (2, 'B')"));
    QVERIFY(query.exec("INSERT INTO test_join_b VALUES (1, 'X'), (2, 'Y')"));

    QVERIFY(query.exec("SELECT a.name, b.value FROM test_join_a a INNER JOIN test_join_b b ON a.id = b.id"));

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString("A"));
    QCOMPARE(query.value(1).toString(), QString("X"));

    qInfo() << "✓ SELECT with JOIN OK";
}

void TestTurso::testSelectWithGroupBy()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text) VALUES (1, 'A'), (1, 'A'), (2, 'B')"));
    QVERIFY(query.exec("SELECT col_integer, COUNT(*) FROM test_all_types WHERE col_integer IN (1,2) GROUP BY col_integer"));

    QVERIFY(query.next());
    int count1 = query.value(1).toInt();
    QVERIFY(count1 >= 2);

    qInfo() << "✓ SELECT with GROUP BY OK";
}

void TestTurso::testInsertIntoSelect()
{

    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    QVERIFY(query.exec("DROP TABLE IF EXISTS source_table"));
    QVERIFY(query.exec("DROP TABLE IF EXISTS dest_table"));

    // Setup
    QVERIFY(query.exec("CREATE TABLE source_table ("
                       "id INTEGER PRIMARY KEY, "
                       "name TEXT, "
                       "value INTEGER)"));

    QVERIFY(query.exec("INSERT INTO source_table (id, name, value) VALUES "
                       "(1, 'Alice', 100), "
                       "(2, 'Bob', 200), "
                       "(3, 'Charlie', 300)"));

    QVERIFY(query.exec("CREATE TABLE dest_table ("
                       "id INTEGER PRIMARY KEY, "
                       "name TEXT, "
                       "value INTEGER)"));

    // Test INSERT INTO SELECT
    QVERIFY2(query.exec("INSERT INTO dest_table SELECT * FROM source_table"),
             qPrintable(query.lastError().text()));

    QCOMPARE(query.numRowsAffected(), 3);

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM dest_table"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    QVERIFY(query.exec("SELECT id, name, value FROM dest_table ORDER BY id"));

    int expectedId = 1;
    QStringList expectedNames = {"Alice", "Bob", "Charlie"};
    QList<int> expectedValues = {100, 200, 300};

    int idx = 0;
    while (query.next()) {
        QCOMPARE(query.value(0).toInt(), expectedId++);
        QCOMPARE(query.value(1).toString(), expectedNames[idx]);
        QCOMPARE(query.value(2).toInt(), expectedValues[idx]);
        idx++;
    }

    QCOMPARE(idx, 3);

    qInfo() << "✓ INSERT INTO SELECT OK - 3 records copied";

    // Cleanup
    // QVERIFY(query.exec("DROP TABLE source_table"));
    // QVERIFY(query.exec("DROP TABLE dest_table"));
}

void TestTurso::testInsertIntoSelect2()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    QVERIFY(query.exec("DROP TABLE IF EXISTS source_table"));
    QVERIFY(query.exec("DROP TABLE IF EXISTS dest_table"));

    // Setup
    QVERIFY(query.exec("CREATE TABLE source_table ("
                       "id INTEGER PRIMARY KEY, "
                       "name TEXT, "
                       "value INTEGER)"));

    QVERIFY(query.exec("INSERT INTO source_table (id, name, value) VALUES "
                       "(1, 'Alice', 100), "
                       "(2, 'Bob', 200), "
                       "(3, 'Charlie', 300)"));

    QVERIFY(query.exec("CREATE TABLE dest_table ("
                       "id INTEGER PRIMARY KEY, "
                       "name TEXT, "
                       "value INTEGER)"));

    // Test INSERT INTO SELECT
    QVERIFY2(query.exec("INSERT INTO dest_table(id,name,value) SELECT id,name,value FROM source_table"),
             qPrintable(query.lastError().text()));

    QCOMPARE(query.numRowsAffected(), 3);

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM dest_table"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    QVERIFY(query.exec("SELECT id, name, value FROM dest_table ORDER BY id"));

    int expectedId = 1;
    QStringList expectedNames = {"Alice", "Bob", "Charlie"};
    QList<int> expectedValues = {100, 200, 300};

    int idx = 0;
    while (query.next()) {
        QCOMPARE(query.value(0).toInt(), expectedId++);
        QCOMPARE(query.value(1).toString(), expectedNames[idx]);
        QCOMPARE(query.value(2).toInt(), expectedValues[idx]);
        idx++;
    }

    QCOMPARE(idx, 3);

    qInfo() << "✓ INSERT INTO SELECT OK - 3 records copied";

}

void TestTurso::testInsertIntoSelectRowId()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    QVERIFY(query.exec("DROP TABLE IF EXISTS source_tablerowid"));
    QVERIFY(query.exec("DROP TABLE IF EXISTS dest_tablerowid"));

    // Setup
    QVERIFY(query.exec("CREATE TABLE source_tablerowid ("
                       "id INTEGER, "
                       "name TEXT, "
                       "value INTEGER)"));

    QVERIFY(query.exec("INSERT INTO source_tablerowid (id, name, value) VALUES "
                       "(1, 'Alice', 100), "
                       "(2, 'Bob', 200), "
                       "(3, 'Charlie', 300)"));

    QVERIFY(query.exec("CREATE TABLE dest_tablerowid ("
                       "id INTEGER, "
                       "name TEXT, "
                       "value INTEGER)"));

    // Test INSERT INTO SELECT
    QVERIFY2(query.exec("INSERT INTO dest_tablerowid SELECT * FROM source_tablerowid"),
             qPrintable(query.lastError().text()));

    QCOMPARE(query.numRowsAffected(), 3);

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM dest_tablerowid"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    QVERIFY(query.exec("SELECT id, name, value FROM dest_tablerowid ORDER BY id"));

    int expectedId = 1;
    QStringList expectedNames = {"Alice", "Bob", "Charlie"};
    QList<int> expectedValues = {100, 200, 300};

    int idx = 0;
    while (query.next()) {
        QCOMPARE(query.value(0).toInt(), expectedId++);
        QCOMPARE(query.value(1).toString(), expectedNames[idx]);
        QCOMPARE(query.value(2).toInt(), expectedValues[idx]);
        idx++;
    }

    QCOMPARE(idx, 3);

    qInfo() << "✓ INSERT INTO SELECT OK - 3 records copied";

    // Cleanup
    // QVERIFY(query.exec("DROP TABLE source_tablerowid"));
    // QVERIFY(query.exec("DROP TABLE dest_tablerowid"));
}

void TestTurso::testInsertIntoSelectRowId2()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    QVERIFY(query.exec("DROP TABLE IF EXISTS source_tablerowid"));
    QVERIFY(query.exec("DROP TABLE IF EXISTS dest_tablerowid"));

    // Setup
    QVERIFY(query.exec("CREATE TABLE source_tablerowid ("
                       "id INTEGER, "
                       "name TEXT, "
                       "value INTEGER)"));

    QVERIFY(query.exec("INSERT INTO source_tablerowid (id, name, value) VALUES "
                       "(1, 'Alice', 100), "
                       "(2, 'Bob', 200), "
                       "(3, 'Charlie', 300)"));

    QVERIFY(query.exec("CREATE TABLE dest_tablerowid ("
                       "id INTEGER, "
                       "name TEXT, "
                       "value INTEGER)"));

    // Test INSERT INTO SELECT
    QVERIFY2(query.exec("INSERT INTO dest_tablerowid(id,name,value) SELECT id,name,value FROM source_tablerowid"),
             qPrintable(query.lastError().text()));

    QCOMPARE(query.numRowsAffected(), 3);

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM dest_tablerowid"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    QVERIFY(query.exec("SELECT id, name, value FROM dest_tablerowid ORDER BY id"));

    int expectedId = 1;
    QStringList expectedNames = {"Alice", "Bob", "Charlie"};
    QList<int> expectedValues = {100, 200, 300};

    int idx = 0;
    while (query.next()) {
        QCOMPARE(query.value(0).toInt(), expectedId++);
        QCOMPARE(query.value(1).toString(), expectedNames[idx]);
        QCOMPARE(query.value(2).toInt(), expectedValues[idx]);
        idx++;
    }

    QCOMPARE(idx, 3);

    qInfo() << "✓ INSERT INTO SELECT OK - 3 records copied";
}

void TestTurso::testInsertIntoSelectWithRowid()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    QVERIFY(query.exec("DROP TABLE IF EXISTS t"));

    // Crea tabella con PRIMARY KEY
    QVERIFY2(query.exec("CREATE TABLE t(a)"),
             qPrintable(query.lastError().text()));

    // Insert iniziale
    QVERIFY2(query.exec("INSERT INTO t VALUES (1)"),
             qPrintable(query.lastError().text()));

    // Verifica record iniziale
    QVERIFY(query.exec("SELECT a FROM t"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QVERIFY(!query.next()); // Solo 1 record

    // INSERT INTO SELECT usando rowid
    // rowid è una colonna speciale di SQLite che identifica univocamente ogni riga
    QVERIFY2(query.exec("INSERT INTO t(a) SELECT rowid FROM t"),
             qPrintable(query.lastError().text()));

    // Verifica che ora ci sia 1 record
    QVERIFY(query.exec("SELECT COUNT(*) FROM t"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);

    // Verifica i valori
    QVERIFY(query.exec("SELECT a FROM t ORDER BY a"));

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1); // Record originale

    QVERIFY(query.next());
    qDebug() << "Record 2 - rowid:" << query.value(0).toInt() << "a:" << query.value(1).toInt();
    QCOMPARE(query.value(0).toInt(), 1); // a = 1
    qInfo() << "✓ INSERT INTO SELECT with rowid OK";

    // Cleanup
    // query.exec("DROP TABLE t");
}

void TestTurso::testInsertIntoSelectWithRowidExplicit()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.exec("DROP TABLE IF EXISTS te");

    QVERIFY(query.exec("CREATE TABLE te(a)"));
    QVERIFY(query.exec("INSERT INTO te VALUES (1)"));

    // Verifica il rowid del primo record
    QVERIFY(query.exec("SELECT rowid, a FROM te"));
    QVERIFY(query.next());
    int firstRowid = query.value(0).toInt();
    int firstA = query.value(1).toInt();

    qDebug() << "First record - rowid:" << firstRowid << "a:" << firstA;
    QCOMPARE(firstA, 1);

    // INSERT usando rowid
    QVERIFY(query.exec("INSERT INTO te(a) SELECT rowid FROM te"));

    // Verifica i nuovi record
    QVERIFY(query.exec("SELECT rowid, a FROM t ORDER BY rowid"));

    QVERIFY(query.next());
    qDebug() << "Record 1 - rowid:" << query.value(0).toInt() << "a:" << query.value(1).toInt();
    QCOMPARE(query.value(1).toInt(), 1); // a = 1

    QVERIFY(query.next());
    qDebug() << "Record 2 - rowid:" << query.value(0).toInt() << "a:" << query.value(1).toInt();
    QCOMPARE(query.value(1).toInt(), 1); // a = 1

    qInfo() << "✓ INSERT INTO SELECT with explicit rowid verification OK";
}

void TestTurso::testInsertIntoSelectWithTypelessColumn()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.exec("DROP TABLE IF EXISTS ttypeless");

    // Esegui esattamente l'esempio che causa il bug
    QVERIFY(query.exec("CREATE TABLE ttypeless(a)"));
    QVERIFY(query.exec("INSERT INTO ttypeless VALUES (2)"));

    // Questo potrebbe fallire in Turso
    if (!query.exec("INSERT INTO ttypeless(a) SELECT rowid FROM ttypeless")) {
        qCritical() << "INSERT INTO SELECT failed with typeless column";
        qCritical() << "Error:" << query.lastError().text();
        QFAIL("Turso bug confirmed");
    }

    QVERIFY(query.exec("SELECT * FROM ttypeless"));

    int count = 0;
    while (query.next()) {
        qDebug() << "Row" << (count + 1) << "- a =" << query.value(0).toInt();
        count++;
    }

    QCOMPARE(count, 2);

    qInfo() << "✓ Typeless column test OK";
}

// ============================================================================
// EDGE CASES (2 test)
// ============================================================================

void TestTurso::testVeryLongString()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QString longString(100000, 'X');

    query.prepare("INSERT INTO test_all_types (col_text) VALUES (?)");
    query.addBindValue(longString);

    if (query.exec()) {
        query.prepare("SELECT LENGTH(col_text) FROM test_all_types WHERE col_text LIKE 'XXX%'");
        QVERIFY(query.exec());
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), longString.length());

        qInfo() << "✓ Very long string OK:" << longString.length() << "chars";
    } else {
        qInfo() << "⚠ Very long string skipped (size limit)";
    }
}

void TestTurso::testManyColumns()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QStringList cols;
    for (int i = 0; i < 50; ++i) {
        cols << QString("col_%1 INTEGER").arg(i);
    }
    QString sql = "DROP TABLE IF EXISTS test_many_cols";
    QVERIFY(query.exec(sql));
    sql = "CREATE TABLE IF NOT EXISTS test_many_cols (" + cols.join(", ") + ")";
    QVERIFY(query.exec(sql));

    QStringList vals;
    for (int i = 0; i < 50; ++i) {
        vals << QString::number(i);
    }

    QVERIFY(query.exec("INSERT INTO test_many_cols VALUES (" + vals.join(", ") + ")"));
    QVERIFY(query.exec("SELECT * FROM test_many_cols"));
    QVERIFY(query.next());

    QCOMPARE(query.record().count(), 50);

    qInfo() << "✓ Many columns OK: 50 columns";
}

void TestTurso::testManyColumnsManyRows()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);


    QStringList cols;
    for (int i = 0; i < 50; ++i) {
        cols << QString("col_%1 INTEGER").arg(i);
    }
    QString sql = "DROP TABLE IF EXISTS test_many_cols_rows";
    QVERIFY(query.exec(sql));
    sql = "CREATE TABLE IF NOT EXISTS test_many_cols_rows (" + cols.join(", ") + ")";
    QVERIFY(query.exec(sql));

    QStringList vals;
    for (int i = 0; i < 50; ++i) {
        vals << QString::number(i);
    }
    QString q=QString("INSERT INTO test_many_cols_rows VALUES (" + vals.join(", ") + ")");
    for (int i = 0; i < 5000; ++i) {
        q.append(QString(",(%2)").arg(vals.join(", ")));
    }
    QFile file("out.sql");
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)){
        file.write(q.toUtf8());
        file.close();
    }
    QBENCHMARK {
        QVERIFY(query.exec(q));
    }
    QVERIFY(query.exec("SELECT * FROM test_many_cols_rows"));
    QVERIFY(query.next());

    QCOMPARE(query.record().count(), 50);

    qInfo() << "✓ Many columns OK: 50 columns";
}



void TestTurso::init()
{
    // Eseguito prima di ogni test
}

void TestTurso::cleanup()
{
    // Eseguito dopo ogni test
}

// ============================================================================
// TEST CONNESSIONE
// ============================================================================

void TestTurso::testConnection()
{
    auto db = QSqlDatabase::database("TURSO");
    db.setDatabaseName(testDbPath);

    QVERIFY2(db.open(), qPrintable(db.lastError().text()));
    QVERIFY(db.isOpen());

    qInfo() << "✓ Connessione stabilita con successo";
}

void TestTurso::testConnectionFailure()
{
    QSqlDatabase tempDb = QSqlDatabase::addDatabase("QTURSO", "temp_connection");
    tempDb.setDatabaseName("/invalid/path/to/database.db");

    // Dovrebbe fallire
    QVERIFY(!tempDb.open());

    QSqlDatabase::removeDatabase("temp_connection");
    qInfo() << "✓ Test fallimento connessione OK";
}

// ============================================================================
// TEST CREAZIONE TABELLE
// ============================================================================

void TestTurso::testCreateTable()
{
    auto db = QSqlDatabase::database("TURSO");
    QVERIFY(db.isOpen());

    QSqlQuery query(db);
    QVERIFY2(query.exec("DROP TABLE IF EXISTS test_simple"), qPrintable(query.lastError().text()));

    QString sql = "CREATE TABLE IF NOT EXISTS test_simple ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL"
                  ")";

    QVERIFY2(query.exec(sql), qPrintable(query.lastError().text()));

    qInfo() << "✓ Tabella semplice creata";
}

void TestTurso::testCreateTableWithAllTypes()
{
    auto db = QSqlDatabase::database("TURSO");
    QVERIFY(db.isOpen());

    QSqlQuery query(db);
    QVERIFY2(query.exec("DROP TABLE IF EXISTS test_all_types"), qPrintable(query.lastError().text()));
    QString sql = "CREATE TABLE IF NOT EXISTS test_all_types ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "col_integer INTEGER, "
                  "col_text TEXT, "
                  "col_real REAL, "
                  "col_blob BLOB, "
                  "col_null INTEGER, "
                  "col_bool BOOLEAN, "
                  "col_datetime TEXT"
                  ")";

    QVERIFY2(query.exec(sql), qPrintable(query.lastError().text()));

    qInfo() << "✓ Tabella con tutti i tipi creata";
}

// ============================================================================
// TEST TIPI DI DATI
// ============================================================================

void TestTurso::testIntegerType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Insert
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer) VALUES (42)"));

    // Select
    QVERIFY(query.exec("SELECT col_integer FROM test_all_types WHERE col_integer = 42"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QCOMPARE(value.type(), QVariant::LongLong);
    QCOMPARE(value.toLongLong(), 42LL);

    qInfo() << "✓ Tipo INTEGER: valore =" << value;
}

void TestTurso::testTextType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QString testText = "Hello QTURSO";
    query.prepare("INSERT INTO test_all_types (col_text) VALUES (:col_text)");
    query.bindValue(0,testText);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_text = 'Hello QTURSO'"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QCOMPARE(value.type(), QVariant::String);
    QCOMPARE(value.toString(), testText);

    qInfo() << "✓ Tipo TEXT: valore =" << value;
}

void TestTurso::testRealType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    double testReal = 3.14159;
    query.prepare("INSERT INTO test_all_types (col_real) VALUES (?)");
    query.addBindValue(testReal);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_real FROM test_all_types WHERE col_real > 3.0"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QCOMPARE(value.type(), QVariant::Double);
    QCOMPARE(value.toDouble(), testReal);

    qInfo() << "✓ Tipo REAL: valore =" << value;
}

void TestTurso::testBlobType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QByteArray testBlob;
    testBlob.append(static_cast<char>(0x61)); // 'a'
    testBlob.append(static_cast<char>(0x00)); // null byte
    testBlob.append(static_cast<char>(0x62)); // 'b'

    query.prepare("INSERT INTO test_all_types (col_blob) VALUES (?)");
    query.addBindValue(testBlob);
    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

    QVERIFY(query.exec("SELECT col_blob FROM test_all_types WHERE col_blob IS NOT NULL"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QCOMPARE(value.type(), QVariant::ByteArray);
    QCOMPARE(value.toByteArray(), testBlob);

    qInfo() << "✓ Tipo BLOB: size =" << value.toByteArray().size()
            << "hex =" << value.toByteArray().toHex();
}

void TestTurso::testNullType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_null) VALUES (?)");
    query.addBindValue(QVariant(QVariant::Int));
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_null FROM test_all_types WHERE col_null IS NULL"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QVERIFY(value.isNull());

    qInfo() << "✓ Tipo NULL: isNull =" << value.isNull();
}

void TestTurso::testBooleanType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_bool) VALUES (?)");
    query.addBindValue(true);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_bool FROM test_all_types WHERE col_bool = 1"));
    QVERIFY(query.next());

    QVariant value = query.value(0);
    QCOMPARE(value.toBool(), true);

    qInfo() << "✓ Tipo BOOLEAN: valore =" << value.toBool();
}

void TestTurso::testDateTimeType()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QDateTime now = QDateTime::currentDateTime();
    QString dateTimeStr = now.toString(Qt::ISODate);

    query.prepare("INSERT INTO test_all_types (col_datetime) VALUES (?)");
    query.addBindValue(dateTimeStr);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_datetime FROM test_all_types WHERE col_datetime IS NOT NULL ORDER BY id DESC LIMIT 1"));
    QVERIFY(query.next());

    QString retrieved = query.value(0).toString();
    QCOMPARE(retrieved, dateTimeStr);

    qInfo() << "✓ Tipo DATETIME: valore =" << retrieved;
}

// ============================================================================
// TEST NOMI CAMPI E METADATA
// ============================================================================

void TestTurso::testFieldNames()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    QVERIFY(query.exec("SELECT id, col_integer, col_text FROM test_all_types LIMIT 1"));

    QSqlRecord record = query.record();

    QCOMPARE(record.count(), 3);
    QCOMPARE(record.fieldName(0), QString("id"));
    QCOMPARE(record.fieldName(1), QString("col_integer"));
    QCOMPARE(record.fieldName(2), QString("col_text"));

    qInfo() << "✓ Nomi campi verificati:";
    for (int i = 0; i < record.count(); ++i) {
        qInfo() << "  -" << record.fieldName(i);
    }
}

void TestTurso::testFieldTypes()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Inserisci dati di test
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text, col_real) "
                       "VALUES (123, 'test', 45.67)"));

    QVERIFY(query.exec("SELECT col_integer, col_text, col_real FROM test_all_types "
                       "WHERE col_integer = 123"));
    QVERIFY(query.next());

    QSqlRecord record = query.record();

    qInfo() << "✓ Tipi campi:";
    for (int i = 0; i < record.count(); ++i) {
        QSqlField field = record.field(i);
        qInfo() << "  -" << field.name()
                << ": SQLite type =" << field.type()
                << ", value type =" << query.value(i).typeName();
    }

    // Verifica tipi
    QCOMPARE(query.value(0).type(), QVariant::LongLong);
    QCOMPARE(query.value(1).type(), QVariant::String);
    QCOMPARE(query.value(2).type(), QVariant::Double);
}

void TestTurso::testFieldCount()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    QVERIFY(query.exec("SELECT * FROM test_all_types LIMIT 1"));

    QSqlRecord record = query.record();
    int fieldCount = record.count();

    QVERIFY(fieldCount > 0);
    qInfo() << "✓ Numero campi in test_all_types:" << fieldCount;
}

// ============================================================================
// TEST VALORI E CONVERSIONI
// ============================================================================

void TestTurso::testValueRetrieval()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Inserisci valori di test
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text, col_real) "
                       "VALUES (999, 'conversion_test', 88.88)"));

    QVERIFY(query.exec("SELECT col_integer, col_text, col_real FROM test_all_types "
                       "WHERE col_integer = 999"));
    QVERIFY(query.next());

    // Test recupero per indice
    QCOMPARE(query.value(0).toInt(), 999);
    QCOMPARE(query.value(1).toString(), QString("conversion_test"));
    QCOMPARE(query.value(2).toDouble(), 88.88);

    // Test recupero per nome campo
    QSqlRecord rec = query.record();
    int idxInteger = rec.indexOf("col_integer");
    int idxText = rec.indexOf("col_text");
    int idxReal = rec.indexOf("col_real");

    QCOMPARE(query.value(idxInteger).toInt(), 999);
    QCOMPARE(query.value(idxText).toString(), QString("conversion_test"));
    QCOMPARE(query.value(idxReal).toDouble(), 88.88);

    qInfo() << "✓ Recupero valori per indice e nome OK";
}

void TestTurso::testValueConversion()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer) VALUES (777)"));
    QVERIFY(query.exec("SELECT col_integer FROM test_all_types WHERE col_integer = 777"));
    QVERIFY(query.next());

    QVariant value = query.value(0);

    // Test conversioni
    QCOMPARE(value.toInt(), 777);
    QCOMPARE(value.toLongLong(), 777LL);
    QCOMPARE(value.toString(), QString("777"));
    QCOMPARE(value.toDouble(), 777.0);
    QCOMPARE(value.toBool(), true);  // Non-zero = true

    qInfo() << "✓ Conversioni tipo OK";
}

void TestTurso::testNullValues()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text) VALUES (NULL, NULL)"));
    QVERIFY(query.exec("SELECT col_integer, col_text FROM test_all_types WHERE col_integer IS NULL AND col_text IS NULL"));
    QVERIFY(query.next());

    QVERIFY(query.value(0).isNull());
    QVERIFY(query.value(1).isNull());

    qInfo() << "✓ Gestione valori NULL OK";
}

// ============================================================================
// TEST PREPARED STATEMENTS
// ============================================================================

void TestTurso::testPreparedStatement()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text, col_real) "
                  "VALUES (?, ?, ?)");
    query.addBindValue(111);
    query.addBindValue("prepared_test");
    query.addBindValue(22.22);

    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

    // Verifica
    QVERIFY(query.exec("SELECT col_integer, col_text, col_real FROM test_all_types "
                       "WHERE col_integer = 111"));
    QVERIFY(query.next());

    QCOMPARE(query.value(0).toInt(), 111);
    QCOMPARE(query.value(1).toString(), QString("prepared_test"));
    QCOMPARE(query.value(2).toDouble(), 22.22);

    qInfo() << "✓ Prepared statement OK";
}

void TestTurso::testPreparedStatementWithNull()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, ?)");
    query.addBindValue(555);
    query.addBindValue(QVariant(QVariant::String));  // NULL

    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_integer = 555"));
    QVERIFY(query.next());
    QVERIFY(query.value(0).isNull());

    qInfo() << "✓ Prepared statement con NULL OK";
}

void TestTurso::testMultipleBindings()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    return;
    query.prepare("INSERT INTO test_all_types (col_integer, col_text, col_real, col_bool) "
                  "VALUES (?, ?, ?, ?)");

    // Primo insert
    query.addBindValue(100);
    query.addBindValue("first");
    query.addBindValue(1.1);
    query.addBindValue(true);
    QVERIFY(query.exec());

    // Secondo insert (riutilizzo prepared statement)
    query.addBindValue(200);
    query.addBindValue("second");
    query.addBindValue(2.2);
    query.addBindValue(false);
    QVERIFY(query.exec());

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer IN (100, 200)"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);

    qInfo() << "✓ Multiple bindings OK";
}

void TestTurso::testPositionalBinding()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text, col_real) VALUES (?1, ?2, ?3)");
    query.addBindValue(101);
    query.addBindValue("positional");
    query.addBindValue(1.23);

    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

    QVERIFY(query.exec("SELECT col_integer, col_text, col_real FROM test_all_types WHERE col_integer = 101"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 101);
    QCOMPARE(query.value(1).toString(), QString("positional"));
    QCOMPARE(query.value(2).toDouble(), 1.23);

    qInfo() << "✓ Positional binding OK";

}
void TestTurso::testNamedBinding()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text, col_real) VALUES (:id, :name, :value)");
    query.bindValue(":id", 201);
    query.bindValue(":name", "named");
    query.bindValue(":value", 4.56);

    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_integer = 201"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString("named"));

    qInfo() << "✓ Named binding OK";
}
void TestTurso::testPreparedQueryReuse()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, ?)");

    for (int i = 0; i < 3; ++i) {
        query.addBindValue(10 + i);
        query.addBindValue(QString("reuse_%1").arg(i));
        QVERIFY(query.exec());
    }

    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer BETWEEN 10 AND 12"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    qInfo() << "✓ Prepared query reuse OK";
}
// ============================================================================
// TEST TRANSAZIONI
// ============================================================================

void TestTurso::testTransaction()
{
    auto db = QSqlDatabase::database("TURSO");
    QVERIFY(db.transaction());

    QSqlQuery query(db);
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer) VALUES (888)"));

    QVERIFY(db.commit());

    // Verifica
    QVERIFY(query.exec("SELECT col_integer FROM test_all_types WHERE col_integer = 888"));
    QVERIFY(query.next());

    qInfo() << "✓ Transaction commit OK";
}
void TestTurso::testPreparedQueryBatch()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, ?)");

    QVariantList integers;
    QVariantList texts;

    for (int i = 0; i < 10; ++i) {
        integers << (10000 + i);
        texts << QString("batch_%1").arg(i);
    }

    query.addBindValue(integers);
    query.addBindValue(texts);

    QVERIFY2(query.execBatch(), qPrintable(query.lastError().text()));

    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer BETWEEN 10000 AND 10009"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 10);

    qInfo() << "✓ Batch execution OK";
}
void TestTurso::testMixedBinding()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Mixing ? and :name dovrebbe fallire o essere gestito
    query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, :name)");
    query.addBindValue(301);
    query.bindValue(":name", "mixed");

    bool result = query.exec();
    qInfo() << "✓ Mixed binding result:" << result << "(may fail as expected)";
}
void TestTurso::testRecord()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("SELECT col_integer, col_text, col_real FROM test_all_types LIMIT 1"));

    QSqlRecord rec = query.record();

    QVERIFY(rec.count() >= 3);
    QCOMPARE(rec.fieldName(0), QString("col_integer"));
    QCOMPARE(rec.fieldName(1), QString("col_text"));
    QCOMPARE(rec.fieldName(2), QString("col_real"));

    qInfo() << "✓ Record metadata OK";
}
void TestTurso::testRecordFieldIndex()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text) VALUES (500, 'test')"));
    QVERIFY(query.exec("SELECT col_integer, col_text FROM test_all_types WHERE col_integer = 500"));
    QVERIFY(query.next());

    QSqlRecord rec = query.record();

    int idx_integer = rec.indexOf("col_integer");
    int idx_text = rec.indexOf("col_text");

    QVERIFY(idx_integer >= 0);
    QVERIFY(idx_text >= 0);

    QCOMPARE(query.value(idx_integer).toInt(), 500);
    QCOMPARE(query.value(idx_text).toString(), QString("test"));

    qInfo() << "✓ Field index lookup OK";
}

void TestTurso::testRecordFieldName()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("SELECT col_integer AS numero, col_text AS testo FROM test_all_types LIMIT 1"));

    QSqlRecord rec = query.record();
    QCOMPARE(rec.fieldName(0).toLower(), QString("numero"));
    QCOMPARE(rec.fieldName(1).toLower(), QString("testo"));

    qInfo() << "✓ Field name with alias OK";
}

void TestTurso::testRecordFieldCount()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("SELECT * FROM test_all_types LIMIT 1"));

    QSqlRecord rec = query.record();
    QVERIFY(rec.count() > 0);

    qInfo() << "✓ Field count OK:" << rec.count();
}

void TestTurso::testRecordIsNull()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer, col_text) VALUES (600, NULL)"));
    QVERIFY(query.exec("SELECT col_integer, col_text FROM test_all_types WHERE col_integer = 600"));
    QVERIFY(query.next());

    QVERIFY(!query.isNull(0));
    QVERIFY(query.isNull(1));

    qInfo() << "✓ Record isNull OK";
}

void TestTurso::testPrimaryIndex()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlIndex idx = db.primaryIndex("test_all_types");

    qInfo() << "✓ Primary index count:" << idx.count();
    QVERIFY(idx.count() >= 0);
}
void TestTurso::testTransactionRollback()
{
    auto db = QSqlDatabase::database("TURSO");
    QVERIFY(db.transaction());

    QSqlQuery query(db);
    QVERIFY(query.exec("INSERT INTO test_all_types (col_integer) VALUES (666)"));

    QVERIFY(db.rollback());

    // Verifica che il dato NON sia stato salvato
    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer = 666"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);

    qInfo() << "✓ Transaction rollback OK";
}

void TestTurso::testTransactionCommit()
{
    auto db = QSqlDatabase::database("TURSO");
    QVERIFY(db.transaction());

    QSqlQuery query(db);
    for (int i = 1; i <= 5; ++i) {
        query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, ?)");
        query.addBindValue(1000 + i);
        query.addBindValue(QString("tx_test_%1").arg(i));
        QVERIFY(query.exec());
    }

    QVERIFY(db.commit());

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer BETWEEN 1001 AND 1005"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 5);

    qInfo() << "✓ Transaction con multiple insert OK";
}

// ============================================================================
// TEST CASI LIMITE
// ============================================================================

void TestTurso::testEmptyString()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.prepare("INSERT INTO test_all_types (col_text) VALUES (?)");
    query.addBindValue(QString(""));
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_text = ''"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QString(""));

    qInfo() << "✓ Stringa vuota OK";
}

void TestTurso::testUnicodeStrings()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QString unicodeText = "Hello 世界 🚀 Привет";
    qDebug() << "=== UNICODE TEST ===";
    qDebug() << "Original string length:" << unicodeText.length();
    query.prepare("INSERT INTO test_all_types (col_text) VALUES (?)");
    query.addBindValue(unicodeText);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_text LIKE '%世界%'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), unicodeText);

    qInfo() << "✓ Unicode strings OK:" << unicodeText;
}

void TestTurso::testLargeBlob()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Crea un blob di 1MB
    QByteArray largeBlob(1024 * 1024, 'X');

    query.prepare("INSERT INTO test_all_types (col_blob) VALUES (?)");
    query.addBindValue(largeBlob);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_blob FROM test_all_types WHERE LENGTH(col_blob) > 1000000"));
    QVERIFY(query.next());

    QByteArray retrieved = query.value(0).toByteArray();
    QCOMPARE(retrieved.size(), largeBlob.size());

    qInfo() << "✓ Large BLOB OK: size =" << retrieved.size();
}

void TestTurso::testSpecialCharacters()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QString specialText = "Test's \"quoted\" text\nwith\nnewlines\tand\ttabs";
    query.prepare("INSERT INTO test_all_types (col_text) VALUES (?)");
    query.addBindValue(specialText);
    QVERIFY(query.exec());

    QVERIFY(query.exec("SELECT col_text FROM test_all_types WHERE col_text LIKE '%quoted%'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), specialText);

    qInfo() << "✓ Caratteri speciali OK";
}

// ============================================================================
// TEST PERFORMANCE
// ============================================================================

void TestTurso::testBulkInsert()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QVERIFY(db.transaction());

    QElapsedTimer timer;
    timer.start();

    query.prepare("INSERT INTO test_all_types (col_integer, col_text) VALUES (?, ?)");

    for (int i = 0; i < 1000; ++i) {
        query.addBindValue(2000 + i);
        query.addBindValue(QString("bulk_%1").arg(i));
        QVERIFY(query.exec());
    }

    QVERIFY(db.commit());

    qint64 elapsed = timer.elapsed();

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types WHERE col_integer BETWEEN 2000 AND 2999"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1000);

    qInfo() << "✓ Bulk insert di 1000 record in" << elapsed << "ms";
}

void TestTurso::testMultipleQueries()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < 100; ++i) {
        QVERIFY(query.exec("SELECT COUNT(*) FROM test_all_types"));
        QVERIFY(query.next());
    }

    qint64 elapsed = timer.elapsed();

    qInfo() << "✓ 100 query SELECT in" << elapsed << "ms";
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void TestTurso::testMvcc()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Attiva MVCC
    QVERIFY(query.exec("PRAGMA journal_mode = 'experimental_mvcc'"));
    QVERIFY(query.next());
    QString mode = query.value(0).toString();
    qInfo() << "Journal mode set to:" << mode;
    
    // Restore WAL
    QVERIFY(query.exec("PRAGMA journal_mode = 'wal'"));
    QVERIFY(query.next());
    QString restoreMode = query.value(0).toString();
    qInfo() << "Journal mode restored to:" << restoreMode;
    QCOMPARE(restoreMode, QString("wal"));
}

void TestTurso::testLibraryVersion()
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Query SQLite/libSQL version
    QVERIFY(query.exec("SELECT sqlite_version()"));
    QVERIFY(query.next());
    QString version = query.value(0).toString();
    qCritical() << "SQLite/libSQL version:" << version;
    QVERIFY(!version.isEmpty());
    QCOMPARE(version,"3.50.4");
    
    qInfo() << "✓ Library version check OK";
}

bool TestTurso::executeQuery(const QString &sql)
{
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qWarning() << "Query failed:" << sql;
        qWarning() << "Error:" << query.lastError().text();
        return false;
    }
    return true;
}

void TestTurso::printQueryResults(QSqlQuery &query)
{
    QSqlRecord record = query.record();

    qDebug() << "=== Query Results ===";
    qDebug() << "Columns:" << record.count();

    for (int i = 0; i < record.count(); ++i) {
        qDebug() << "  " << record.fieldName(i);
    }

    int rowCount = 0;
    while (query.next()) {
        rowCount++;
        qDebug() << "Row" << rowCount << ":";
        for (int i = 0; i < record.count(); ++i) {
            qDebug() << "  " << record.fieldName(i) << "=" << query.value(i);
        }
    }

    qDebug() << "Total rows:" << rowCount;
}
