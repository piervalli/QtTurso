/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtSql module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsql_turso_p.h"

#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qvariant.h>
#include <qsqlerror.h>
#include <qsqlfield.h>
#include <qsqlindex.h>
#include <qsqlquery.h>
#include <QtSql/private/qsqlcachedresult_p.h>
#include <QtSql/private/qsqldriver_p.h>
#include <qstringlist.h>
#include <qvector.h>
#include <qdebug.h>
#include <QUuid>
#if QT_CONFIG(regularexpression)
#include <qcache.h>
#include <qregularexpression.h>
#endif
#include <QScopedValueRollback>

#if defined Q_OS_WIN
# include <qt_windows.h>
#else
# include <unistd.h>
#endif

#define SUPPORT_SQLITE3_CONTEXT 0
#define SQLITE_OPEN_READONLY         0x00000001  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_READWRITE        0x00000002  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_CREATE           0x00000004  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_URI              0x00000040  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_MEMORY           0x00000080  /* Ok for sqlite3_open_v2() */

#define SQLITE_OPEN_NOMUTEX          0x00008000  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_FULLMUTEX        0x00010000  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_SHAREDCACHE      0x00020000  /* Ok for sqlite3_open_v2() */
#define SQLITE_OPEN_PRIVATECACHE     0x00040000  /* Ok for sqlite3_open_v2() */


#ifndef SQLITE_CONSTRAINT
#define SQLITE_CONSTRAINT     19   /* Abort due to constraint violation */
#endif

#ifndef SQLITE_UTF8
#define SQLITE_UTF8           1
#endif
#include <sqlite3.h>

#define TURSO_STATIC      ((void*)0)
#define TURSO_TRANSIENT   ((void*)-1)
Q_DECLARE_OPAQUE_POINTER(sqlite3*)
Q_DECLARE_METATYPE(sqlite3*)

Q_DECLARE_OPAQUE_POINTER(sqlite3_stmt*)
Q_DECLARE_METATYPE(sqlite3_stmt*)

QT_BEGIN_NAMESPACE
static int sqlite3_busy_timeoutTemp(sqlite3 *_db, int _ms){
    return 0;
    //return sqlite3_busy_timeout(_db,_ms);
}
static QString _q_escapeIdentifier(const QString &identifier, QSqlDriver::IdentifierType type)
{
    QString res = identifier;
    // If it contains [ and ] then we assume it to be escaped properly already as this indicates
    // the syntax is exactly how it should be
    if (identifier.contains(QLatin1Char('[')) && identifier.contains(QLatin1Char(']')))
        return res;
    if (!identifier.isEmpty() && !identifier.startsWith(QLatin1Char('"')) && !identifier.endsWith(QLatin1Char('"'))) {
        res.replace(QLatin1Char('"'), QLatin1String("\"\""));
        res.prepend(QLatin1Char('"')).append(QLatin1Char('"'));
        if (type == QSqlDriver::TableName)
            res.replace(QLatin1Char('.'), QLatin1String("\".\""));
    }
    return res;
}

static QVariant::Type qGetColumnType(const QString &tpName)
{
    const QString typeName = tpName.toLower();

    if (typeName == QLatin1String("integer")
        || typeName == QLatin1String("int"))
        return QVariant::Int;
    if (typeName == QLatin1String("double")
        || typeName == QLatin1String("float")
        || typeName == QLatin1String("real")
        || typeName.startsWith(QLatin1String("numeric")))
        return QVariant::Double;
    if (typeName == QLatin1String("blob"))
        return QVariant::ByteArray;
    if (typeName == QLatin1String("boolean")
        || typeName == QLatin1String("bool"))
        return QVariant::Bool;
    return QVariant::String;
}

static QSqlError qMakeError(sqlite3 *access, const QString &descr, QSqlError::ErrorType type,
                            int errorCode)
{
    return QSqlError(descr,
                     QString::fromUtf8(sqlite3_errmsg(access)),
                     type, QString::number(errorCode));
}

class QTursoResultPrivate;

class QTursoResult : public QSqlCachedResult
{
    Q_DECLARE_PRIVATE(QTursoResult)
    friend class QTursoDriver;

public:
    explicit QTursoResult(const QTursoDriver* db);
    ~QTursoResult();
    QVariant handle() const override;

protected:
    bool gotoNext(QSqlCachedResult::ValueCache& row, int idx) override;
    bool reset(const QString &query) override;
    bool prepare(const QString &query) override;
    bool execBatch(bool arrayBind) override;
    bool exec() override;
    int size() override;
    int numRowsAffected() override;
    QVariant lastInsertId() const override;
    QSqlRecord record() const override;
    void detachFromResultSet() override;
    void virtual_hook(int id, void *data) override;
};

class QTursoDriverPrivate : public QSqlDriverPrivate
{
    Q_DECLARE_PUBLIC(QTursoDriver)

public:
    inline QTursoDriverPrivate() : QSqlDriverPrivate(QSqlDriver::SQLite) {}
    sqlite3 *access = nullptr;
    QVector<QTursoResult *> results;
    QStringList notificationid;
    bool singleWriter=true;
};


class QTursoResultPrivate : public QSqlCachedResultPrivate
{
    Q_DECLARE_PUBLIC(QTursoResult)

public:
    Q_DECLARE_SQLDRIVER_PRIVATE(QTursoDriver)
    using QSqlCachedResultPrivate::QSqlCachedResultPrivate;
    void cleanup();
    bool fetchNext(QSqlCachedResult::ValueCache &values, int idx, bool initialFetch);
    // initializes the recordInfo and the cache
    void initColumns(bool emptyResultset);
    void finalize();

    sqlite3_stmt *stmt = nullptr;
    QSqlRecord rInf;
    QVector<QVariant> firstRow;
    bool skippedStatus = false; // the status of the fetchNext() that's skipped
    bool skipRow = false; // skip the next fetchNext()?
};

void QTursoResultPrivate::cleanup()
{
    Q_Q(QTursoResult);
    finalize();
    rInf.clear();
    skippedStatus = false;
    skipRow = false;
    q->setAt(QSql::BeforeFirstRow);
    q->setActive(false);
    q->cleanup();
}

void QTursoResultPrivate::finalize()
{
    if (!stmt)
        return;

    sqlite3_finalize(stmt);
    stmt = 0;
}

void QTursoResultPrivate::initColumns(bool emptyResultset)
{
    Q_Q(QTursoResult);
    int nCols = sqlite3_column_count(stmt);
    if (nCols <= 0)
        return;

    q->init(nCols);

    for (int i = 0; i < nCols; ++i) {
        QString colName = QString::fromUtf8(sqlite3_column_name(stmt, i));
        const QString tableName = QString::fromUtf8(sqlite3_column_table_name(stmt, 0));
        // must use typeName for resolving the type to match QTursoDriver::record
        QString typeName = QString::fromUtf8(sqlite3_column_decltype(stmt, i));
        // sqlite3_column_type is documented to have undefined behavior if the result set is empty
        int stp = emptyResultset ? -1 : sqlite3_column_type(stmt, i);

        QVariant::Type fieldType;

        if (!typeName.isEmpty()) {
            fieldType = qGetColumnType(typeName);
        } else {
            // Get the proper type for the field based on stp value
            switch (stp) {
            case SQLITE_INTEGER:
                fieldType = QVariant::Int;
                break;
            case SQLITE_FLOAT:
                fieldType = QVariant::Double;
                break;
            case SQLITE_BLOB:
                fieldType = QVariant::ByteArray;
                break;
            case SQLITE_TEXT:
                fieldType = QVariant::String;
                break;
            case SQLITE_NULL:
            default:
                fieldType = QVariant::Invalid;
                break;
            }
        }
        qDebug() << colName << fieldType << tableName;
        QSqlField fld(colName, fieldType, tableName);
        fld.setSqlType(stp);
        rInf.append(fld);
    }
}

bool QTursoResultPrivate::fetchNext(QSqlCachedResult::ValueCache &values, int idx, bool initialFetch)
{
    Q_Q(QTursoResult);
    int res;
    int i;
    qDebug() << "fetchNext";
    if (skipRow) {
        // already fetched
        Q_ASSERT(!initialFetch);
        skipRow = false;
        for(int i=0;i<firstRow.count();i++)
            values[i]=firstRow[i];
        return skippedStatus;
    }
    skipRow = initialFetch;

    if(initialFetch) {
        firstRow.clear();
        firstRow.resize(sqlite3_column_count(stmt));
    }

    if (!stmt) {
        q->setLastError(QSqlError(QCoreApplication::translate("QTursoResult", "Unable to fetch row"),
                                  QCoreApplication::translate("QTursoResult", "No query"), QSqlError::ConnectionError));
        q->setAt(QSql::AfterLastRow);
        return false;
    }
    res = sqlite3_step(stmt);

    switch(res) {
    case SQLITE_ROW:
        // check to see if should fill out columns
        if (rInf.isEmpty())
            // must be first call.
            initColumns(false);
        if (idx < 0 && !initialFetch)
            return true;
        for (i = 0; i < rInf.count(); ++i) {
            const auto iType = sqlite3_column_type(stmt, i);
            switch (iType) {
            case SQLITE_BLOB:
            {
                const void* blob = sqlite3_column_blob(stmt, i);
                const int size = sqlite3_column_bytes(stmt, i);
                if (!blob || size == 0) {
                    values[i + idx]=QByteArray{};
                }else {
                    values[i + idx] = QByteArray(static_cast<const char *>(blob),size);
                }
                break;
            }
            case SQLITE_INTEGER:
                values[i + idx] = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                switch(q->numericalPrecisionPolicy()) {
                    case QSql::LowPrecisionInt32:
                        values[i + idx] = sqlite3_column_int64(stmt, i);
                        break;
                    case QSql::LowPrecisionInt64:
                        values[i + idx] = sqlite3_column_int64(stmt, i);
                        break;
                    case QSql::LowPrecisionDouble:
                    case QSql::HighPrecision:
                    default:
                        values[i + idx] = sqlite3_column_double(stmt, i);
                        break;
                };
                break;
            case SQLITE_NULL:
                values[i + idx] = QVariant();
                break;
            case SQLITE_TEXT:
            {
                const auto rawValue=reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                int bytes = sqlite3_column_bytes(stmt, i);
                if (!rawValue) {
                    values[i + idx] = QString();
                }
                else if (bytes <= 0) {
                    values[i + idx] = QString("");  // Stringa vuota valida
                }
                else {
                    values[i + idx] = QString::fromUtf8(rawValue,bytes);
                }
                break;
            }
            default:
                qCritical() << "unsupported type " <<iType;
                break;
            }
        }
        return true;
    case SQLITE_DONE:
        if (rInf.isEmpty())
            // must be first call.
            initColumns(true);
        q->setAt(QSql::AfterLastRow);
        sqlite3_reset(stmt);
        return false;
    case SQLITE_CONSTRAINT:
    case SQLITE_ERROR:
        // SQLITE_ERROR is a generic error code and we must call sqlite3_reset()
        // to get the specific error message.
        res = sqlite3_reset(stmt);
        q->setLastError(qMakeError(drv_d_func()->access, QCoreApplication::translate("QTursoResult",
                        "Unable to fetch row"), QSqlError::ConnectionError, res));
        q->setAt(QSql::AfterLastRow);
        return false;
    case SQLITE_MISUSE:
    case SQLITE_BUSY:
    default:
        // something wrong, don't get col info, but still return false
        q->setLastError(qMakeError(drv_d_func()->access, QCoreApplication::translate("QTursoResult",
                        "Unable to fetch row"), QSqlError::ConnectionError, res));
        sqlite3_reset(stmt);
        q->setAt(QSql::AfterLastRow);
        return false;
    }
    return false;
}

QTursoResult::QTursoResult(const QTursoDriver* db)
    : QSqlCachedResult(*new QTursoResultPrivate(this, db))
{
    Q_D(QTursoResult);
    const_cast<QTursoDriverPrivate*>(d->drv_d_func())->results.append(this);
    qDebug() << "QTursoResult";
}

QTursoResult::~QTursoResult()
{
    Q_D(QTursoResult);
    if (d->drv_d_func())
        const_cast<QTursoDriverPrivate*>(d->drv_d_func())->results.removeOne(this);
    d->cleanup();
    qDebug() << "~QTursoResult";
}

void QTursoResult::virtual_hook(int id, void *data)
{
    QSqlCachedResult::virtual_hook(id, data);
}

bool QTursoResult::reset(const QString &query)
{
    if (!prepare(query))
        return false;
    return exec();
}

bool QTursoResult::prepare(const QString &query)
{
    Q_D(QTursoResult);
    qDebug() << "prepare";
    if (!driver() || !driver()->isOpen() || driver()->isOpenError())
        return false;

    d->cleanup();

    setSelect(false);

    const char *pzTail = NULL;
    int res=SQLITE_ERROR;

    res = sqlite3_prepare_v2(d->drv_d_func()->access, query.toUtf8().constData(), -1,
                                   &d->stmt, &pzTail);
    if (res != SQLITE_OK) {
        setLastError(qMakeError(d->drv_d_func()->access, QCoreApplication::translate("QTursoResult",
                     "Unable to execute statement"), QSqlError::StatementError, res));
        d->finalize();
        return false;
    } else if (pzTail && !QString(reinterpret_cast<const QChar *>(pzTail)).trimmed().isEmpty()) {
        setLastError(qMakeError(d->drv_d_func()->access, QCoreApplication::translate("QTursoResult",
            "Unable to execute multiple statements at a time"), QSqlError::StatementError, SQLITE_MISUSE));
        d->finalize();
        return false;
    }
    return true;
}

bool QTursoResult::execBatch(bool arrayBind)
{
    Q_UNUSED(arrayBind);
    Q_D(QSqlResult);
    QScopedValueRollback<QVector<QVariant>> valuesScope(d->values);
    QVector<QVariant> values = d->values;
    if (values.count() == 0)
        return false;

    for (int i = 0; i < values.at(0).toList().count(); ++i) {
        d->values.clear();
        QScopedValueRollback<QHash<QString, QVector<int>>> indexesScope(d->indexes);
        QHash<QString, QVector<int>>::const_iterator it = d->indexes.constBegin();
        while (it != d->indexes.constEnd()) {
            bindValue(it.key(), values.at(it.value().first()).toList().at(i), QSql::In);
            ++it;
        }
        if (!exec())
            return false;
    }
    return true;
}

bool QTursoResult::exec()
{
    Q_D(QTursoResult);
    qDebug() << "exec";
    QVector<QVariant> values = boundValues();
    d->skippedStatus = false;
    d->skipRow = false;
    d->rInf.clear();
    clearValues();
    setLastError(QSqlError());

    int res = sqlite3_reset(d->stmt);
    if (res != SQLITE_OK) {
        setLastError(qMakeError(d->drv_d_func()->access, QCoreApplication::translate("QTursoResult",
                     "Unable to reset statement"), QSqlError::StatementError, res));
        d->finalize();
        return false;
    }

    int paramCount = sqlite3_bind_parameter_count(d->stmt);
    bool paramCountIsValid = paramCount == values.count();


    // In the case of the reuse of a named placeholder
    // We need to check explicitly that paramCount is greater than or equal to 1, as sqlite
    // can end up in a case where for virtual tables it returns 0 even though it
    // has parameters
    qDebug() <<"paramCount" << "values" << values.count();
    if (paramCount >= 1 && paramCount < values.count())
    {
        const auto countIndexes = [](int counter, const QVector<int> &indexList) {
                                      return counter + indexList.length();
                                  };

        const int bindParamCount = std::accumulate(d->indexes.cbegin(),
                                                   d->indexes.cend(),
                                                   0,
                                                   countIndexes);

        paramCountIsValid = bindParamCount == values.count();
        // When using named placeholders, it will reuse the index for duplicated
        // placeholders. So we need to ensure the QVector has only one instance of
        // each value as SQLite will do the rest for us.
        QVector<QVariant> prunedValues;
        QVector<int> handledIndexes;
        for (int i = 0, currentIndex = 0; i < values.size(); ++i) {
            if (handledIndexes.contains(i))
                continue;
            const char *parameterName = sqlite3_bind_parameter_name(d->stmt, currentIndex + 1);
            if (!parameterName) {
                paramCountIsValid = false;
                continue;
            }
            const auto placeHolder = QString::fromUtf8(parameterName);
            const auto &indexes = d->indexes.value(placeHolder);
            handledIndexes << indexes;
            prunedValues << values.at(indexes.first());
            ++currentIndex;
        }
        values = prunedValues;
    }


    //qCritical() << "paramCount"<<paramCount<<values;
    if (paramCountIsValid) {
        for (int i = 0; i < paramCount; ++i) {
            res = SQLITE_OK;
            const QVariant &value = values.at(i);

            if (value.isNull()) {
                res = sqlite3_bind_null(d->stmt, i + 1);
            } else {
                qDebug() << "userType" << value.userType() <<value.typeName();
                switch (value.userType()) {
                case QVariant::ByteArray: {
                    const QByteArray *ba = static_cast<const QByteArray*>(value.constData());
                    const unsigned char* blob_data = reinterpret_cast<const unsigned char*>(ba->constData());
                    int blob_size = ba->size();
                    res = sqlite3_bind_blob(d->stmt, i + 1, blob_data,
                                            blob_size, TURSO_STATIC);
                    break; }
                case QVariant::Int:
                case QVariant::Bool:
                    res = sqlite3_bind_int64(d->stmt, i + 1, value.toInt());
                    break;
                case QVariant::Double:
                    res = sqlite3_bind_double(d->stmt, i + 1, value.toDouble());
                    break;
                case QVariant::UInt:
                case QVariant::LongLong:
                    res = sqlite3_bind_int64(d->stmt, i + 1, value.toLongLong());
                    break;
                case QVariant::DateTime: {
                    const QDateTime dateTime = value.toDateTime();
                    const auto bytea = dateTime.toString(Qt::ISODateWithMs).toUtf8();
                    res = sqlite3_bind_text(d->stmt, i + 1, bytea.constData(),bytea.size(), TURSO_TRANSIENT);//SQLITE_TRANSIENT
                    break;
                }
                case QVariant::Time: {
                    const QTime time = value.toTime();
                    const auto bytea = time.toString(u"hh:mm:ss.zzz").toUtf8();
                    res = sqlite3_bind_text(d->stmt, i + 1, bytea.constData(),bytea.size(), TURSO_TRANSIENT);//SQLITE_TRANSIENT
                    break;
                }
                case QVariant::String: {
                    // lifetime of string == lifetime of its qvariant
                    const auto bytea = value.toString().toUtf8();
                    int bytea_size = bytea.size();
                    res = sqlite3_bind_text(d->stmt, i + 1, bytea.constData(),
                                              bytea_size, TURSO_TRANSIENT);//was SQLITE_STATIC
                    break; }
                default: {
                    const auto bytea = value.toString().toUtf8();
                    // SQLITE_TRANSIENT makes sure that sqlite buffers the data
                    res = sqlite3_bind_text(d->stmt, i + 1, bytea.constData(),
                                              (bytea.size()), TURSO_TRANSIENT);//SQLITE_TRANSIENT
                    break; }
                }
            }
            if (res != SQLITE_OK) {
                setLastError(qMakeError(d->drv_d_func()->access, QCoreApplication::translate("QTursoResult",
                             "Unable to bind parameters"), QSqlError::StatementError, res));
                d->finalize();
                return false;
            }
        }
    } else {
        setLastError(QSqlError(QCoreApplication::translate("QTursoResult",
                        "Parameter count mismatch"), QString(), QSqlError::StatementError));
        return false;
    }
    d->skippedStatus = d->fetchNext(d->firstRow, 0, true);
    if (lastError().isValid()) {
        setSelect(false);
        setActive(false);
        return false;
    }
    setSelect(!d->rInf.isEmpty());
    setActive(true);
    return true;
}

bool QTursoResult::gotoNext(QSqlCachedResult::ValueCache& row, int idx)
{
    Q_D(QTursoResult);

    return d->fetchNext(row, idx, false);
}

int QTursoResult::size()
{
    return -1;
}

int QTursoResult::numRowsAffected()
{
    Q_D(const QTursoResult);
    return sqlite3_changes(d->drv_d_func()->access);
}

QVariant QTursoResult::lastInsertId() const
{
    Q_D(const QTursoResult);
    if (isActive()) {
        qint64 id = sqlite3_last_insert_rowid(d->drv_d_func()->access);
        if (id)
            return id;
    }
    return QVariant();
}

QSqlRecord QTursoResult::record() const
{
    Q_D(const QTursoResult);
    if (!isActive() || !isSelect())
        return QSqlRecord();
    return d->rInf;
}

void QTursoResult::detachFromResultSet()
{
    Q_D(QTursoResult);
    if (d->stmt)
        sqlite3_reset(d->stmt);
}

QVariant QTursoResult::handle() const
{
    Q_D(const QTursoResult);
    return QVariant::fromValue(d->stmt);
}

/////////////////////////////////////////////////////////

#if QT_CONFIG(regularexpression)
#if SUPPORT_SQLITE3_CONTEXT == 1
static void _q_regexp(void* context, int argc, void** argv)
{
    if (Q_UNLIKELY(argc != 2)) {
        sqlite3_result_int64(context, 0);
        return;
    }

    const QString pattern = QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_value_text(argv[0])));
    const QString subject = QString::fromUtf8(
        reinterpret_cast<const char*>(sqlite3_value_text(argv[1])));

    auto cache = static_cast<QCache<QString, QRegularExpression>*>(sqlite3_user_data(context));
    auto regexp = cache->object(pattern);
    const bool wasCached = regexp;

    if (!wasCached)
        regexp = new QRegularExpression(pattern, QRegularExpression::DontCaptureOption);

    const bool found = subject.contains(*regexp);

    if (!wasCached)
        cache->insert(pattern, regexp);

    sqlite3_result_int64(context, int(found));
}
static void sqliteCreateUUid4(void *ctx, int argc, void **argv)
{
    Q_UNUSED(argc)
    Q_UNUSED(argv)
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QByteArray uuidUtf8 = uuid.toUtf8();
    sqlite3_result_text(ctx, uuidUtf8.constData(), uuidUtf8.length(), NULL);
}
static void _q_regexp_cleanup(void *cache)
{
    delete static_cast<QCache<QString, QRegularExpression>*>(cache);
}
#endif
#endif
QTursoDriver::QTursoDriver(QObject * parent)
    : QSqlDriver(*new QTursoDriverPrivate, parent)
{
#ifdef SQLITE_ENABLE_WAL2
    qDebug()<<"SQLITE_ENABLE_WAL2 is enabled.";
#endif
}

QTursoDriver::QTursoDriver(sqlite3 *connection, QObject *parent)
    : QSqlDriver(*new QTursoDriverPrivate, parent)
{
    Q_D(QTursoDriver);
    d->access = connection;
    setOpen(true);
    setOpenError(false);

#ifdef SQLITE_ENABLE_WAL2
    qDebug()<<"SQLITE_ENABLE_WAL2 is enabled.";
#endif
}


QTursoDriver::~QTursoDriver()
{
    close();
}

bool QTursoDriver::hasFeature(DriverFeature f) const
{
    switch (f) {
    // case BLOB:TODO
    case Transactions:
    case Unicode:
    case LastInsertId:
    case PreparedQueries:
    case PositionalPlaceholders:
    case SimpleLocking:
    case FinishQuery:
    case LowPrecisionNumbers:
    case EventNotifications:
        return true;
    case QuerySize:
    case BatchOperations:
    case MultipleResultSets:
    case CancelQuery:
        return false;
    case NamedPlaceholders:
        return true;
    case QSqlDriver::BLOB:
        break;
    }
    return false;
}

/*
   SQLite dbs have no user name, passwords, hosts or ports.
   just file names.
*/
bool QTursoDriver::open(const QString & db, const QString &, const QString &, const QString &, int, const QString &conOpts)
{
    Q_D(QTursoDriver);
    if (isOpen())
        close();


    int timeOut = 5000;
    bool sharedCache = false;
    bool openReadOnlyOption = false;
    bool openUriOption = false;
#if QT_CONFIG(regularexpression)
    static const QLatin1String regexpConnectOption = QLatin1String("QSQLITE_ENABLE_REGEXP");
    bool defineRegexp = false;
    int regexpCacheSize = 25;
#endif

    const auto opts = conOpts.splitRef(QLatin1Char(';'));
    for (auto option : opts) {
        option = option.trimmed();
        if (option.startsWith(QLatin1String("QSQLITE_BUSY_TIMEOUT"))) {
            option = option.mid(20).trimmed();
            if (option.startsWith(QLatin1Char('='))) {
                bool ok;
                const int nt = option.mid(1).trimmed().toInt(&ok);
                if (ok)
                    timeOut = nt;
            }
        } else if (option == QLatin1String("QSQLITE_OPEN_READONLY")) {
            openReadOnlyOption = true;
        } else if (option == QLatin1String("QSQLITE_OPEN_URI")) {
            openUriOption = true;
        } else if (option == QLatin1String("QSQLITE_ENABLE_SHARED_CACHE")) {
            sharedCache = true;
        }else if (option == QLatin1String("TURSO_WRITER_CONCURRENCY")) {
            d->singleWriter = false;
        }
#if QT_CONFIG(regularexpression)
        else if (option.startsWith(regexpConnectOption)) {
            option = option.mid(regexpConnectOption.size()).trimmed();
            if (option.isEmpty()) {
                defineRegexp = true;
            } else if (option.startsWith(QLatin1Char('='))) {
                bool ok = false;
                const int cacheSize = option.mid(1).trimmed().toInt(&ok);
                if (ok) {
                    defineRegexp = true;
                    if (cacheSize > 0)
                        regexpCacheSize = cacheSize;
                }
            }
        }
#endif
    }

    int openMode = (openReadOnlyOption ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE));
    openMode |= (sharedCache ? SQLITE_OPEN_SHAREDCACHE : SQLITE_OPEN_PRIVATECACHE);
    if (openUriOption)
        openMode |= SQLITE_OPEN_URI;

    openMode |= SQLITE_OPEN_NOMUTEX;

    const int res = sqlite3_open_v2(db.toUtf8().constData(), &d->access, openMode, NULL);

    if (res == SQLITE_OK) {
        sqlite3_busy_timeoutTemp(d->access, timeOut);
        setOpen(true);
        setOpenError(false);
#if QT_CONFIG(regularexpression)
        if (defineRegexp) {
            auto cache = new QCache<QString, QRegularExpression>(regexpCacheSize);
#if SUPPORT_SQLITE3_CONTEXT==1
            sqlite3_create_function_v2(d->access, "regexp", 2, SQLITE_UTF8, cache, reinterpret_cast<void(*)(void)>(&_q_regexp), NULL,
                                       NULL,NULL ); //&_q_regexp_cleanup
#endif
        }
#endif
		// Register custom function uuid_generate_v4
#if SUPPORT_SQLITE3_CONTEXT==1
        int rc =0;
        rc = sqlite3_create_function_v2(d->access, "uuid_generate_v4", 0, SQLITE_UTF8, nullptr,nullptr,
                                         reinterpret_cast<void(*)(void)>(&sqliteCreateUUid4), nullptr,nullptr);
		if (rc != SQLITE_OK) {
			qWarning("Failed to register uuid_generate_v4: %s", sqlite3_errmsg(d->access));
		}

        return true;
#endif
    } else {
        setLastError(qMakeError(d->access, tr("Error opening database"),
                     QSqlError::ConnectionError, res));
        setOpenError(true);

        if (d->access) {
            sqlite3_close(d->access);
            d->access = 0;
        }

        return false;
    }
    return true;
}

void QTursoDriver::close()
{
    Q_D(QTursoDriver);
    qDebug() << "close";
    if (isOpen()) {
        for (QTursoResult *result : qAsConst(d->results))
            result->d_func()->finalize();

        if (d->access && (d->notificationid.count() > 0)) {
            d->notificationid.clear();
            // sqlite3_update_hook(d->access, NULL, NULL);TODO
        }

        const int res = sqlite3_close(d->access);
        qDebug() << "sqlite3_close" << res;
        if (res != SQLITE_OK)
            setLastError(qMakeError(d->access, tr("Error closing database"), QSqlError::ConnectionError, res));
        d->access = 0;
        setOpen(false);
        setOpenError(false);
    }
}

QSqlResult *QTursoDriver::createResult() const
{
    return new QTursoResult(this);
}

bool QTursoDriver::beginTransaction()
{
    Q_D(QTursoDriver);
    if (!isOpen() || isOpenError())
        return false;
    qDebug() << "beginTransaction singleWriter"<<d->singleWriter;
    QSqlQuery q(createResult());
    if (!q.exec(QLatin1String(d->singleWriter?"BEGIN":"BEGIN CONCURRENT TRANSACTION; --experimental-mvcc"))) {
        setLastError(QSqlError(tr("Unable to begin transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

bool QTursoDriver::commitTransaction()
{
    if (!isOpen() || isOpenError())
        return false;
    qCritical() << "commitTransaction";
    QSqlQuery q(createResult());
    if (!q.exec(QLatin1String("COMMIT"))) {
        setLastError(QSqlError(tr("Unable to commit transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

bool QTursoDriver::rollbackTransaction()
{
    if (!isOpen() || isOpenError())
        return false;
    qCritical() << "rollbackTransaction";
    QSqlQuery q(createResult());
    if (!q.exec(QLatin1String("ROLLBACK"))) {
        setLastError(QSqlError(tr("Unable to rollback transaction"),
                               q.lastError().databaseText(), QSqlError::TransactionError));
        return false;
    }

    return true;
}

QStringList QTursoDriver::tables(QSql::TableType type) const
{
    QStringList res;
    if (!isOpen())
        return res;
    qDebug() << "tables" << type;
    QSqlQuery q(createResult());
    q.setForwardOnly(true);

    QString sql = QLatin1String("SELECT name FROM sqlite_master WHERE %1 "
                                "UNION ALL SELECT name FROM sqlite_temp_master WHERE %1");
    if ((type & QSql::Tables) && (type & QSql::Views))
        sql = sql.arg(QLatin1String("type='table' OR type='view'"));
    else if (type & QSql::Tables)
        sql = sql.arg(QLatin1String("type='table'"));
    else if (type & QSql::Views)
        sql = sql.arg(QLatin1String("type='view'"));
    else
        sql.clear();

    if (!sql.isEmpty() && q.exec(sql)) {
        while(q.next())
            res.append(q.value(0).toString());
    }

    if (type & QSql::SystemTables) {
        // there are no internal tables beside this one:
        res.append(QLatin1String("sqlite_master"));
    }

    return res;
}

static QSqlIndex qGetTableInfo(QSqlQuery &q, const QString &tableName, bool onlyPIndex = false)
{
    QString schema;
    QString table(tableName);
    const int indexOfSeparator = tableName.indexOf(QLatin1Char('.'));
    if (indexOfSeparator > -1) {
        const int indexOfCloseBracket = tableName.indexOf(QLatin1Char(']'));
        if (indexOfCloseBracket != tableName.size() - 1) {
            // Handles a case like databaseName.tableName
            schema = tableName.left(indexOfSeparator + 1);
            table = tableName.mid(indexOfSeparator + 1);
        } else {
            const int indexOfOpenBracket = tableName.lastIndexOf(QLatin1Char('['), indexOfCloseBracket);
            if (indexOfOpenBracket > 0) {
                // Handles a case like databaseName.[tableName]
                schema = tableName.left(indexOfOpenBracket);
                table = tableName.mid(indexOfOpenBracket);
            }
        }
    }
    q.exec(QLatin1String("PRAGMA ") + schema + QLatin1String("table_info (") +
           _q_escapeIdentifier(table, QSqlDriver::TableName) + QLatin1Char(')'));
    QSqlIndex ind;
    while (q.next()) {
        bool isPk = q.value(5).toInt();
        if (onlyPIndex && !isPk)
            continue;
        QString typeName = q.value(2).toString().toLower();
        QString defVal = q.value(4).toString();
        if (!defVal.isEmpty() && defVal.at(0) == QLatin1Char('\'')) {
            const int end = defVal.lastIndexOf(QLatin1Char('\''));
            if (end > 0)
                defVal = defVal.mid(1, end - 1);
        }

        QSqlField fld(q.value(1).toString(), qGetColumnType(typeName), tableName);
        if (isPk && (typeName == QLatin1String("integer")))
            // INTEGER PRIMARY KEY fields are auto-generated in sqlite
            // INT PRIMARY KEY is not the same as INTEGER PRIMARY KEY!
            fld.setAutoValue(true);
        fld.setRequired(q.value(3).toInt() != 0);
        fld.setDefaultValue(defVal);
        ind.append(fld);
    }
    return ind;
}

QSqlIndex QTursoDriver::primaryIndex(const QString &tblname) const
{
    if (!isOpen())
        return QSqlIndex();

    QString table = tblname;
    if (isIdentifierEscaped(table, QSqlDriver::TableName))
        table = stripDelimiters(table, QSqlDriver::TableName);

    QSqlQuery q(createResult());
    q.setForwardOnly(true);
    return qGetTableInfo(q, table, true);
}

QSqlRecord QTursoDriver::record(const QString &tbl) const
{
    if (!isOpen())
        return QSqlRecord();
    qDebug() << "record"<<tbl;
    QString table = tbl;
    if (isIdentifierEscaped(table, QSqlDriver::TableName))
        table = stripDelimiters(table, QSqlDriver::TableName);

    QSqlQuery q(createResult());
    q.setForwardOnly(true);
    return qGetTableInfo(q, table);
}

QVariant QTursoDriver::handle() const
{
    Q_D(const QTursoDriver);
    return QVariant::fromValue(d->access);
}

QString QTursoDriver::escapeIdentifier(const QString &identifier, IdentifierType type) const
{
    return _q_escapeIdentifier(identifier, type);
}

static void handle_sqlite_callback(void *qobj,int aoperation, char const *adbname, char const *atablename,
                                   sqlite3_int64 arowid)
{
    Q_UNUSED(aoperation);
    Q_UNUSED(adbname);
    QTursoDriver *driver = static_cast<QTursoDriver *>(qobj);
    if (driver) {
        QMetaObject::invokeMethod(driver, "handleNotification", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromUtf8(atablename)), Q_ARG(qint64, arowid));
    }
}

bool QTursoDriver::subscribeToNotification(const QString &name)
{
    Q_D(QTursoDriver);
    if (!isOpen()) {
        qWarning("Database not open.");
        return false;
    }

    if (d->notificationid.contains(name)) {
        qWarning("Already subscribing to '%s'.", qPrintable(name));
        return false;
    }

    //sqlite supports only one notification callback, so only the first is registered
    d->notificationid << name;
    if (d->notificationid.count() == 1) {
        // sqlite3_update_hook(d->access, &handle_sqlite_callback, reinterpret_cast<void *> (this)); TODO
    }

    return true;
}

bool QTursoDriver::unsubscribeFromNotification(const QString &name)
{
    Q_D(QTursoDriver);
    if (!isOpen()) {
        qWarning("Database not open.");
        return false;
    }

    if (!d->notificationid.contains(name)) {
        qWarning("Not subscribed to '%s'.", qPrintable(name));
        return false;
    }

    d->notificationid.removeAll(name);
    if (d->notificationid.isEmpty()) {
        // sqlite3_update_hook(d->access, NULL, NULL); TODO
    }

    return true;
}

QStringList QTursoDriver::subscribedToNotifications() const
{
    Q_D(const QTursoDriver);
    return d->notificationid;
}

void QTursoDriver::handleNotification(const QString &tableName, qint64 rowid)
{
    Q_D(const QTursoDriver);
    if (d->notificationid.contains(tableName)) {
#if QT_DEPRECATED_SINCE(5, 15)
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
        emit notification(tableName);
QT_WARNING_POP
#endif
        emit notification(tableName, QSqlDriver::UnknownSource, QVariant(rowid));
    }
}

QT_END_NAMESPACE

#include "moc_qsql_turso_p.cpp"
