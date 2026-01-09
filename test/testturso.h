#ifndef TESTTURSO_H
#define TESTTURSO_H

#include <QObject>
#include <QTest>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlField>
#include <QDebug>
#include <QTest>
#include <QVariant>

class TestTurso : public QObject
{
    Q_OBJECT
public:
    explicit TestTurso(QObject *parent = nullptr);
    ~TestTurso();
private slots:
    // Setup e cleanup
    void initTestCase();      // Eseguito una volta all'inizio

    void init();              // Eseguito prima di ogni test
    void cleanup();           // Eseguito dopo ogni test

    // Test connessione
    void testConnection();
    void testConnectionFailure();

    // Test creazione tabelle
    void testCreateTable();
    void testCreateTableWithAllTypes();

    // Test tipi di dati
    void testIntegerType();
    void testTextType();
    void testRealType();
    void testBlobType();
    void testNullType();
    void testBooleanType();
    void testDateTimeType();

    // Test operazioni CRUD
    // void testInsert();
    // void testSelect();
    // void testUpdate();
    // void testDelete();

    // Test prepared statements
    void testPreparedStatement();
    void testPreparedStatementWithNull();
    void testMultipleBindings();

    // Prepared Statements (5 test)
    void testPositionalBinding();
    void testNamedBinding();
    void testPreparedQueryReuse();
    void testPreparedQueryBatch();
    void testMixedBinding();

    // Record e Metadata (6 test)
    void testRecord();
    void testRecordFieldIndex();
    void testRecordFieldName();
    void testRecordFieldCount();
    void testRecordIsNull();
    void testPrimaryIndex();

    // Test nomi campi
    void testFieldNames();
    void testFieldTypes();
    void testFieldCount();

    // Test valori e conversioni
    void testValueRetrieval();
    void testValueConversion();
    void testNullValues();

    // Test transazioni
    void testTransaction();
    void testTransactionRollback();
    void testTransactionCommit();

    // Test casi limite
    void testEmptyString();
    void testUnicodeStrings();
    void testLargeBlob();
    void testSpecialCharacters();

    // Test performance
    void testBulkInsert();
    void testMultipleQueries();
    void cleanupTestCase();   // Eseguito una volta alla fine

    // Query Complesse (5 test)
    void testSelectWithWhere();
    void testSelectWithOrderBy();
    void testSelectWithLimit();
    void testSelectWithJoin();
    void testSelectWithGroupBy();
    void testInsertIntoSelect();
    void testInsertIntoSelect2();
    void testInsertIntoSelectRowId();
    void testInsertIntoSelectRowId2();
    void testInsertIntoSelectWithRowid();
    void testInsertIntoSelectWithRowidExplicit();
    void testInsertIntoSelectWithTypelessColumn();
    // Edge Cases (2 test)
    void testVeryLongString();
    void testManyColumns();
    void testManyColumnsManyRows();
    void testMvcc();
    void testLibraryVersion();

signals:
private:

    QString testDbPath;
    QString m_defaultConnection{"TURSO"};
    // Helper methods
    bool executeQuery(const QString &sql);
    void createTestTable();
    void dropTestTable();
    void verifyFieldMetadata(const QString &tableName);
    void printQueryResults(QSqlQuery &query);
};

#endif // TESTTURSO_H
