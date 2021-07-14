/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef CONNECTION_H
#define CONNECTION_H

#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QApplication>

/*
    This file defines a helper function to open a connection to an
    in-memory SQLITE database and to create a test table.
*/

static bool createConnection()
{

    QDir dbDir = QDir(QCoreApplication::applicationDirPath());

#if 0// defined(Q_OS_WIN)
    if (dbDir.dirName().toLower() == "debug" || dbDir.dirName().toLower() == "release") {
        dbDir.cdUp();
        dbDir.cdUp();
        dbDir.cdUp();
    }
#endif
    //dbDir.cd("priv");

    //QString dbPath = dbDir.toNativeSeparators(dbDir.absoluteFilePath("test.db"));

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName("test.db");
    if (!db.open()) {
        QMessageBox::critical(nullptr, QObject::tr("Cannot open database"),
            QObject::tr("Unable to establish a database connection.\n"
                        "This example needs SQLite support. Please read "
                        "the Qt SQL driver documentation for information how "
                        "to build it.\n\n"
                        "Click Cancel to exit."), QMessageBox::Cancel);
        return false;
    }

/*
    qDebug(db.databaseName().toStdString().c_str());
    auto list = db.tables();
    char temp[2];
    _itoa_s(int(list.count()), temp, 2);
    qDebug(temp);
    for(QString& table : list){
        qDebug(table.toStdString().c_str());
    }

    query.exec("select * from stock");
    qDebug(query.lastError().text().toStdString().c_str());
*/
/*
    QSqlQuery query;
    query.exec("create table articles (code int primary key, "
               "description varchar(100), brand varchar(100), category varchar(50), "
               "sellPrice double, buyPrice double, stock int, stockMin int, "
               "impInt int, vat double, buyDate date, modDate date)");
    query.exec("insert into articles values(1, 'SANDWICH COCIDO Y QUESO', null, 'COMIDA', 14, 4.5, 4, 2, 0, 21, null,'2014/02/17')");

    query.exec("insert into stock values(100212422323, 'Canned Tomatoes 400g', 742, 500, 1000, 0.24, 'TV', 'BW')");
    query.exec("insert into stock values(102313538763, 'Frozen Peas 1lb', 41, 30, 50, 0.89, 'FR', 'FV')");
    query.exec("insert into stock values(403244976252, 'Freshly Squeezed Orange 1L', 32, 10, 25, 2.59, 'JU', 'JL')");
*/
    return true;
}

#endif
