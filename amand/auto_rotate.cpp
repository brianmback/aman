// auto_rotate.cpp
//
// Autorotation Routines for amand(8).
//
//   (C) Copyright 2012-2013 Fred Gleason <fredg@paravelsystems.com>
//
//      $Id: auto_rotate.cpp,v 1.2 2013/11/19 00:14:40 cvs Exp $
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

#include <syslog.h>
#include <errno.h>

#include <QtCore/QDir>
#include <QtSql/QSqlQuery>
#include <QtCore/QVariant>

#include "amand.h"

void MainObject::autoRotateData()
{
  AutoRotate();
}


void MainObject::ScheduleAutoRotation()
{
  int msecs=0;

  if(main_config->globalAutoRotateBinlogs()) {
    QDateTime now=QDateTime(QDate::currentDate(),QTime::currentTime());
    if(!main_auto_rotate_state) {
      if(now.time()>main_config->globalAutoRotateTime()) {
	msecs=1000*now.secsTo(QDateTime(now.addDays(1).date(),
					main_config->globalAutoRotateTime()));
      }
      else {
	msecs=1000*now.secsTo(QDateTime(now.date(),
					main_config->globalAutoRotateTime()));
      }
      main_auto_rotate_state=true;
    }
    else {
      msecs=3600000;
      main_auto_rotate_state=false;
    }
    main_auto_rotate_timer->start(msecs);
    syslog(LOG_DEBUG,"next auto rotation scheduled for %s",
	   (const char *)now.addSecs(msecs/1000).
	   toString("hh:mm:ss").toAscii());
  }
}


void MainObject::AutoRotate()
{
  if(main_auto_rotate_state) {
    //
    // Generate Snapshot
    //
    QString snapshot=MakeSnapshotName();

    if(GenerateMysqlSnapshot(QString(AM_SNAPSHOT_DIR)+"/"+snapshot)) {
      main_state->setCurrentSnapshot(Am::This,snapshot);
      main_monitor->setThisSnapshotName(snapshot);
    }
  }
  else {
    //
    // Purge Snapshots
    //
    main_state->purgeSnapshots();

    //
    // Purge Bin Logs
    //
    if(main_config->globalAutoPurgeBinlogs()) {
      PurgeBinlogs();
    }
  }
  ScheduleAutoRotation();
}


void MainObject::PurgeBinlogs()
{
  QString sql;
  QSqlQuery *q;
  QString log1;
  QString log2;
  QStringList f1;
  QStringList f2;
  bool ok1=false;
  bool ok2=false;
  unsigned v1=0;
  unsigned v2=0;

  if((main_monitor->dbState(Am::This)==State::StateMaster)&&
     (main_monitor->dbState(Am::That)==State::StateSlave)) {
    if(OpenMysql(Am::This,Config::PrivateAddress)) {
      sql="show master status";
      q=new QSqlQuery(sql,Db());
      if(q->first()) {
	log1=q->value(0).toString();
	delete q;
	CloseMysql();
	if(OpenMysql(Am::That,Config::PublicAddress)) {
	  sql="show slave status";
	  q=new QSqlQuery(sql,Db());
	  if(q->first()) {
	    log2=q->value(5).toString();   // Field: 'Master_Log_File'
	  }
	  f1=log1.split(".");
	  f2=log2.split(".");
	  if((f1.size()==2)&&(f2.size()==2)) {
	    v1=f1[1].toUInt(&ok1);
	    v2=f2[1].toUInt(&ok2);
	    if(ok1&&ok2) {
	      if(v1<=v2) {
		DeleteBinlogSequence(f1[0],v1-1);
	      }
	      else {
		DeleteBinlogSequence(f2[0],v2-1);
	      }
	    }
	  }
	}
      }
      delete q;
      CloseMysql();
    }
  }
  if((main_monitor->dbState(Am::This)==State::StateSlave)&&
     (main_monitor->dbState(Am::That)==State::StateMaster)) {
    if(OpenMysql(Am::This,Config::PrivateAddress)) {
      sql="show master status";
      q=new QSqlQuery(sql,Db());
      if(q->first()) {
	log1=q->value(0).toString();      // Field: 'File'
	delete q;
	CloseMysql();
	f1=log1.split(".");
	if(f1.size()==2) {
	  v1=f1[1].toUInt(&ok1);
	  if(ok1) {
	    DeleteBinlogSequence(f1[0],v1-1);
	  }
	}
	if(OpenMysql(Am::This,Config::PrivateAddress)) {
	  sql="show slave status";
	  q=new QSqlQuery(sql,Db());
	  if(q->first()) {
	    log1=q->value(7).toString();   // Field: 'Relay_Log_File'
	  }
	  f1=log1.split(".");
	  if(f1.size()) {
	    v1=f1[1].toUInt(&ok1);
	    if(ok1) {
	      DeleteBinlogSequence(f1[0],v1-2);
	    }
	  }
	}
      }
      delete q;
      CloseMysql();
    }
  }
}


void MainObject::DeleteBinlogSequence(const QString &basename,
				      unsigned last) const
{
  //printf("delete up to %s.%06u\n",(const char *)basename.toUtf8(),last);

  QStringList f1;
  bool ok=false;
  unsigned n;
  QDir *dir=new QDir(AM_MYSQL_DATADIR,basename+".*");
  QStringList binlogs=dir->entryList();

  for(int i=0;i<binlogs.size();i++) {
    f1=binlogs[i].split(".");
    if(f1.size()==2) {
      n=f1[1].toUInt(&ok);
      if(ok&&n<=last) {
	dir->remove(binlogs[i]);
	syslog(LOG_INFO,"purged mysql binlog \"%s\"",
	       (const char *)binlogs[i].toUtf8());
      }
    }
  }
  delete dir;
}
