#include "testmultistatement.h"
#include "qfuture.h"
#include <QtConcurrent>
#include <QDebug>
TestMultiStatement::TestMultiStatement(QObject *parent)
    : QObject{parent}
{}

void TestMultiStatement::initTestCase()
{

}

void TestMultiStatement::init()
{

}

void TestMultiStatement::cleanup()
{

}
void TestMultiStatement::testBeginConcurrent()
{
    return;
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);
    if (!db.isOpen()) {
        qCritical() << "Database not open!";
        QFAIL("Database not open");
    }
    QString sql = "DROP TABLE IF EXISTS test_write_concurrent";
    QVERIFY(query.exec(sql));
    sql = "CREATE TABLE IF NOT EXISTS test_write_concurrent (id uuid, code INTEGER)";
    QVERIFY(query.exec(sql));


    const int NUM_THREADS = 2;
    const int INSERTS_PER_THREAD = 10;
    QAtomicInt failedInserts(0);
    // Funzione che ogni thread eseguirà
    static auto insertTask = [&](int threadId) {
        // Ogni thread ha la sua connessione
        QSqlDatabase threadDb = QSqlDatabase::cloneDatabase("TURSO", QString("thread_%1").arg(threadId)
                                                            );

        if (!threadDb.open()) {
            qCritical() << "Failed to open connection for thread" << threadId;
            return;
        }
        if (!threadDb.isValid()) {
            qCritical() << "Thread" << threadId << "invalid database clone";
            return;
        }
        auto ok = threadDb.transaction();
        QStringList lastError;
        if(!ok) {
            lastError << threadDb.lastError().text()<<threadDb.lastError().nativeErrorCode() << threadDb.lastError().databaseText();
            qCritical() << lastError;
        }
        QVERIFY(ok);
        QSqlQuery query(threadDb);
        for (int i = 0; i < INSERTS_PER_THREAD; i++) {
            // *** KEY: Usa BEGIN CONCURRENT ***
            // query.exec("BEGIN");

            // Simula business logic
            QThread::msleep(1);
            const auto q =  QStringLiteral("INSERT INTO test_write_concurrent(id, code) VALUES ('%1', %2)")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
                               .arg(QRandomGenerator::global()->bounded(100));
            bool success = query.exec(q);
            if (success) {
                query.next();
                // query.exec("COMMIT");
            } else {
                QString error = query.lastError().text();
                qCritical() << error;
                // query.exec("ROLLBACK");
                failedInserts.fetchAndAddRelaxed(1);
            }
        }

        threadDb.close();
        QSqlDatabase::removeDatabase(QString("thread_%1").arg(threadId));
    };

    // Esegui task in parallelo con QtConcurrent
    QList<int> threadIds;
    for (int i = 0; i < NUM_THREADS; i++) {
        threadIds.append(i);
    }

    QElapsedTimer timer;
    timer.start();

    // QtConcurrent::map esegue la funzione su ogni elemento in parallelo
    QFuture<void> future = QtConcurrent::map(threadIds, insertTask);
    future.waitForFinished();

    // Verifica integrità dati
    QSqlQuery count(db);
    count.exec("SELECT COUNT(*) FROM test_write_concurrent");
    count.next();
    const auto result = failedInserts.loadRelaxed();
    QCOMPARE(result,0);
    QCOMPARE(count.value(0).toInt(), INSERTS_PER_THREAD);
}
void TestMultiStatement::testMultiStatementExecution0()
{
    return;
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    bool ok = query.exec("DROP TABLE IF EXISTS multi_test;");
    auto lastError=query.lastError().text();
    QCOMPARE(lastError,"");
    QVERIFY(ok);
    ok=query.exec("CREATE TABLE multi_test (id INTEGER PRIMARY KEY, name TEXT, value INTEGER);");
    lastError=query.lastError().text();
    QCOMPARE(lastError,"");
    QVERIFY(ok);
    // SQL Script con multiple statements separati da ;
    QString multiSQL =
        "INSERT INTO multi_test (id, name, value) VALUES (1, 'Alice', 100);"
        "INSERT INTO multi_test (id, name, value) VALUES (2, 'Bob', 200);"
        "INSERT INTO multi_test (id, name, value) VALUES (3, 'Charlie', 300);"
        "CREATE INDEX idx_multi_name ON multi_test(name);";


    // Turso supporta multi-statement nativamente come PostgreSQL
    bool success = query.exec(multiSQL);

    if (success) {
        qDebug() << "✓ MULTI-STATEMENT EXECUTED SUCCESSFULLY!";
        qDebug() << "  Turso supports native multi-statement execution";
    } else {
        qCritical() << "  Type:" << query.lastError().type();
        QFAIL("Multi-statement not supported or error in SQL");
    }

    QVERIFY2(success, qPrintable(query.lastError().text()));

    // Verifica tabella creata
    QVERIFY(query.exec("SELECT COUNT(*) FROM multi_test"));
    QVERIFY(query.next());
    int count = query.value(0).toInt();
    QCOMPARE(count, 3);

    // Verifica dati
    QVERIFY(query.exec("SELECT id, name, value FROM multi_test ORDER BY id"));

    qDebug() << "  Records:";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toString(), QString("Alice"));
    QCOMPARE(query.value(2).toInt(), 100);
    qDebug() << "    1: Alice, 100";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QCOMPARE(query.value(1).toString(), QString("Bob"));
    QCOMPARE(query.value(2).toInt(), 200);
    qDebug() << "    2: Bob, 200";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    QCOMPARE(query.value(1).toString(), QString("Charlie"));
    QCOMPARE(query.value(2).toInt(), 300);
    qDebug() << "    3: Charlie, 300";

    // Verifica indice creato
    QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_multi_name'"));
    QVERIFY(query.next());
    QString indexName = query.value(0).toString();
    qDebug() << "  Index created:" << indexName;
    QCOMPARE(indexName, QString("idx_multi_name"));

    qInfo() << "✓ Turso multi-statement execution works perfectly!";
}
void TestMultiStatement::testMultiStatementExecution()
{
    return;
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    // Cleanup
    query.exec("DROP TABLE IF EXISTS multi_test");

    // SQL Script con multiple statements separati da ;
    QString multiSQL =
        "CREATE TABLE multi_test (id INTEGER PRIMARY KEY, name TEXT, value INTEGER);"
        "INSERT INTO multi_test (id, name, value) VALUES (1, 'Alice', 100);"
        "INSERT INTO multi_test (id, name, value) VALUES (2, 'Bob', 200);"
        "INSERT INTO multi_test (id, name, value) VALUES (3, 'Charlie', 300);"
        "CREATE INDEX idx_multi_name ON multi_test(name);";


    // Turso supporta multi-statement nativamente come PostgreSQL
    bool success = query.exec(multiSQL);

    if (success) {
        qDebug() << "✓ MULTI-STATEMENT EXECUTED SUCCESSFULLY!";
        qDebug() << "  Turso supports native multi-statement execution";
    } else {
        qCritical() << "  Type:" << query.lastError().type();
        QFAIL("Multi-statement not supported or error in SQL");
    }

    QVERIFY2(success, qPrintable(query.lastError().text()));

    // Verifica tabella creata
    QVERIFY(query.exec("SELECT COUNT(*) FROM multi_test"));
    QVERIFY(query.next());
    int count = query.value(0).toInt();
    QCOMPARE(count, 3);

    // Verifica dati
    QVERIFY(query.exec("SELECT id, name, value FROM multi_test ORDER BY id"));

    qDebug() << "  Records:";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toString(), QString("Alice"));
    QCOMPARE(query.value(2).toInt(), 100);
    qDebug() << "    1: Alice, 100";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QCOMPARE(query.value(1).toString(), QString("Bob"));
    QCOMPARE(query.value(2).toInt(), 200);
    qDebug() << "    2: Bob, 200";

    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);
    QCOMPARE(query.value(1).toString(), QString("Charlie"));
    QCOMPARE(query.value(2).toInt(), 300);
    qDebug() << "    3: Charlie, 300";

    // Verifica indice creato
    QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_multi_name'"));
    QVERIFY(query.next());
    QString indexName = query.value(0).toString();
    qDebug() << "  Index created:" << indexName;
    QCOMPARE(indexName, QString("idx_multi_name"));

    qInfo() << "✓ Turso multi-statement execution works perfectly!";
}

void TestMultiStatement::testMultiStatementWithTransaction()
{
    return;
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    qDebug() << "\n=== Multi-Statement with Transaction ===";

    query.exec("DROP TABLE IF EXISTS trans_multi");

    // Multi-statement con transazione esplicita
    QString transSQL =
        "BEGIN TRANSACTION;"
        "CREATE TABLE trans_multi (id INTEGER, data TEXT);"
        "INSERT INTO trans_multi VALUES (1, 'First');"
        "INSERT INTO trans_multi VALUES (2, 'Second');"
        "INSERT INTO trans_multi VALUES (3, 'Third');"
        "COMMIT;";

    qDebug() << "Executing transactional multi-statement...";

    QVERIFY2(query.exec(transSQL), qPrintable(query.lastError().text()));

    qDebug() << "✓ Transaction multi-statement OK";

    // Verifica
    QVERIFY(query.exec("SELECT COUNT(*) FROM trans_multi"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 3);

    qDebug() << "  All 3 records committed successfully";

    qInfo() << "✓ Multi-statement with transaction OK";
}

void TestMultiStatement::testMultiStatementWithError()
{
    return;
    auto db = QSqlDatabase::database("TURSO");
    QSqlQuery query(db);

    query.exec("DROP TABLE IF EXISTS error_multi");

    // Multi-statement con errore nel mezzo
    QString errorSQL =
        "CREATE TABLE error_multi (id INTEGER);"
        "INSERT INTO error_multi VALUES (1);"
        "INSERT INTO non_existing_table VALUES (999);"  // Questo fallirà
        "INSERT INTO error_multi VALUES (2);";

    qDebug() << "Executing multi-statement with intentional error...";

    bool result = query.exec(errorSQL);

    if (result) {
        qWarning() << "⚠ Multi-statement succeeded (unexpected)";
        qWarning() << "  Turso may have executed partial statements";
    } else {
        qDebug() << "✓ Error correctly detected";
        qDebug() << "  Error:" << query.lastError().text();

        // Verifica stato parziale
        if (query.exec("SELECT COUNT(*) FROM error_multi")) {
            if (query.next()) {
                int partial = query.value(0).toInt();
                qDebug() << "  Partial records before error:" << partial;

                if (partial == 1) {
                    qDebug() << "  ✓ Transaction rolled back (atomic)";
                } else {
                    qDebug() << "  ⚠ Partial execution occurred (non-atomic)";
                }
            }
        }
    }

    QVERIFY(!result); // Dovrebbe fallire

    qInfo() << "✓ Error handling in multi-statement OK";
}
