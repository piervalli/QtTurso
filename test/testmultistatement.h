#ifndef TESTMULTISTATEMENT_H
#define TESTMULTISTATEMENT_H

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

class TestMultiStatement : public QObject
{
    Q_OBJECT
public:
    explicit TestMultiStatement(QObject *parent = nullptr);
    // Setup e cleanup
    void initTestCase();      // Eseguito una volta all'inizio

    void init();              // Eseguito prima di ogni test
    void cleanup();           // Eseguito dopo ogni test
    void testBeginConcurrent();
    void testMultiStatementExecution0();
    void testMultiStatementExecution();
    void testMultiStatementWithTransaction();
    void testMultiStatementWithError();
signals:
};

#endif // TESTMULTISTATEMENT_H
