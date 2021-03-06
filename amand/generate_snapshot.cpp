// generate_snapshot.cpp
//
// Generate a MySQL Snapshot
//
//   (C) Copyright 2012-2013 Fred Gleason <fredg@paravelsystems.com>
//
//      $Id: generate_snapshot.cpp,v 1.5 2013/11/19 00:14:40 cvs Exp $
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License version 2 as
//   published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public
//   License along with this program; if not, write to the Free Software
//   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//

#include <errno.h>
#include <syslog.h>
#include <unistd.h>

#include <QtCore/QProcess>
#include <QtCore/QVariant>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

#include "amand.h"

bool MainObject::GenerateMysqlSnapshot(const QString &filename)
{
  QString sql;
  QSqlQuery *q;
  Config::Address addr;
  bool ret=true;
  FILE *f=NULL;

  //
  // Generate Working Directory
  //
  QString tempdir=MakeTempDir();
  if(tempdir.isNull()) {
    return false;
  }

  //
  // Open Mysql
  //
  addr=Config::PublicAddress;
  if(!OpenMysql(Am::This,addr)) {
    addr=Config::PublicAddress;
    if(!OpenMysql(Am::This,addr)) {
      return false;
    }
  }

  //
  // Start a New Binlog
  //
  sql="flush logs";
  q=new QSqlQuery(sql,Db());
  if(!q->isActive()) {
    syslog(LOG_ERR,"cannot flush logs in mysql at %s [%s]",
	   (const char *)main_config->address(Am::This,addr).toString().
	   toAscii(),
	   (const char *)q->lastError().text().toAscii());
    delete q;
    CloseMysql();
    return false;
  }
  delete q;

  //
  // Lock Tables
  //
  QTime start=QTime::currentTime();
  sql="flush tables with read lock";
  q=new QSqlQuery(sql,Db());
  if(!q->isActive()) {
    syslog(LOG_ERR,"cannot lock tables in mysql at %s [%s]",
	   (const char *)main_config->address(Am::This,addr).toString().
	   toAscii(),
	   (const char *)q->lastError().text().toAscii());
    delete q;
    CloseMysql();
    return false;
  }
  delete q;

  //
  // Copy Database
  //
  QProcess *proc=new QProcess(this);
  QStringList args;
  args.push_back("-C");
  args.push_back(main_config->mysqlDataDirectory(Am::This));
  args.push_back("-cf");
  args.push_back(tempdir+"/sql.tar");
  args.push_back(main_config->globalMysqlDatabase());
  proc->start("tar",args);
  proc->waitForFinished(-1);
  if(proc->exitCode()!=0) {
    syslog(LOG_ERR,"mysql snapshot copy failed [%s]",
	   (const char *)proc->readAllStandardError());
    unlink(filename.toAscii());
    ret=false;
  }
  delete proc;

  //
  // Generate Binlog Pointer
  //
  sql="show master status";
  q=new QSqlQuery(sql,Db());
  if(!q->first()) {
    syslog(LOG_ERR,"unable to get master status in mysql at %s [%s]",
	   (const char *)main_config->address(Am::This,addr).toString().
	   toAscii(),
	   (const char *)q->lastError().text().toAscii());
    delete q;
    sql="unlock tables";
    q=new QSqlQuery(sql,Db());
    delete q;
    CloseMysql();
    return false;
  }
  if((f=fopen((tempdir+"/metadata.ini").toAscii(),"w"))==NULL) {
    syslog(LOG_ERR,"unable to create temporary file at %s",
	   (const char *)(tempdir+"/metadata.ini").toAscii());
    sql="unlock tables";
    q=new QSqlQuery(sql,Db());
    delete q;
    CloseMysql();
    return false;
  }
  fprintf(f,"[Master]\n");
  fprintf(f,"MysqlDbname=%s\n",
	  (const char *)main_config->globalMysqlDatabase().toAscii());
  fprintf(f,"BinlogFilename=%s\n",
	  (const char *)q->value(0).toString().toAscii());
  fprintf(f,"BinlogPosition=%u\n",q->value(1).toUInt());
  fclose(f);
  delete q;

  //
  // Unlock Database
  //
  sql="unlock tables";
  q=new QSqlQuery(sql,Db());
  delete q;
  QTime finish=QTime::currentTime();
  CloseMysql();

  //
  // Generate Archive
  //
  proc=new QProcess(this);
  args.clear();
  args.push_back("-C");
  args.push_back(tempdir);
  args.push_back("-jcf");
  args.push_back(filename);
  args.push_back("sql.tar");
  args.push_back("metadata.ini");
  proc->start("tar",args);
  proc->waitForFinished(-1);
  if(proc->exitCode()!=0) {
    syslog(LOG_ERR,"mysql snapshot archive creation failed [%s]",
	   (const char *)proc->readAllStandardError());
    unlink(filename.toAscii());
    ret=false;
  }
  delete proc;
  syslog(LOG_INFO,
	 "generated MySQL snapshot in \"%s\", db was locked for %6.2lf sec",
	 (const char *)filename.toAscii(),(double)start.msecsTo(finish)/1000.0);

  //
  // Push Snapshot to Remote Systems
  //
  if(!PushFile(filename,main_config->address(Am::That,Config::PrivateAddress).
	       toString(),filename)) {
    if(!PushFile(filename,main_config->address(Am::That,Config::PublicAddress).
		 toString(),filename)) {
      syslog(LOG_ERR,"unable to push snapshot to \"%s\"",
	     (const char *)main_config->hostname(Am::That).toAscii());
    }
  }

  //
  // Clean Up
  //
  unlink((tempdir+"/sql.tar").toAscii());
  unlink((tempdir+"/metadata.ini").toAscii());
  rmdir(tempdir.toAscii());

  return ret;
}
