/***************************************************************************
 *   file klfblockprocess.h
 *   This file is part of the KLatexFormula Project.
 *   Copyright (C) 2007 by Philippe Faist
 *   philippe.faist@bluewin.ch
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef KLFBLOCKPROCESS_H
#define KLFBLOCKPROCESS_H

//! Defines the KLFBlockProcess class
/** \file
 * Defines the KLFBlockProcess class */

#include <qprocess.h>
#include <qstring.h>
#ifdef KLFBACKEND_QT4
#include <QByteArray>
#else
#include <qcstring.h>
#include <qmemarray.h>
#endif

//! A QProcess subclass for code-blocking process execution
/** A Code-blocking (but not GUI-blocking) process executor
 *
 * Use for example like:
 * \code
 *   KLFBlockProcess proc;
 *   QStringList args;
 *   args << "ls" << "/dev";
 *   proc.startProcess(args);
 *   QString alldevices = proc.readStdoutString();
 * \endcode
 *
 * This class provides functionality for passing data to STDIN and
 * getting data from STDOUT and STDERR afterwards.
 *
 * \author Philippe Faist &lt;philippe.faist@bluewin.ch&gt;
 */
class KLFBlockProcess : public QProcess
{
  Q_OBJECT
public:
  /** Normal constructor, like QProcess constructor */
  KLFBlockProcess(QObject *parent = 0);
  /** Normal destructor */
  ~KLFBlockProcess();

  /** Returns all standard error output as a QByteArray. This function is to standardize the
   * readStderr() and readAllStandardError() functions in QT 3 or QT 4 respectively */
  QByteArray getAllStderr() {
#ifdef KLFBACKEND_QT4
    return readAllStandardError();
#else
    return readStderr();
#endif
  }

  /** Returns all standard output as a QByteArray. This function is to standardize the
   * readStdout() and readAllStandardOutput() functions in QT 3 or QT 4 respectively */
  QByteArray getAllStdout() {
#ifdef KLFBACKEND_QT4
    return readAllStandardOutput();
#else
    return readStdout();
#endif
  }

#ifdef KLFBACKEND_QT4
  /** Tweak here to change new QT4 api to old QT3 api */
  bool normalExit() const {
    return QProcess::exitStatus() == NormalExit;
  }
  /** Tweak here to change new QT4 api to old QT3 api */
  int exitStatus() const {
    return exitCode();
  }
#endif


public slots:
  /** Starts cmd (which is a list of arguments, the first being the
   * program itself) and blocks until process stopped. The QT event
   * loop is updated regularly so that the GUI doesn't freeze.
   *
   * Read result with QProcess::readStdout() and QProcess::readStderr(),
   * get process exit info with QProcess::normalExit() and QProcess::exitStatus().
   *
   * \returns TRUE upon success, FALSE upon failure.
   */
  bool startProcess(QStringList cmd, QByteArray stdindata, QStringList env = QStringList());
#ifndef KLFBACKEND_QT4
  /** Convenient function if you want to pass string input to program
   * This mostly truncates the last '\\0' because programs don't like it.
   *
   * \warning this function is only available with QT 3. */
  bool startProcess(QStringList cmd, QCString str, QStringList env = QStringList());
#endif
  /** Convenient function to be used in the case where program doesn't
   * expect stdin data or if you chose to directly close stdin without writing
   * anything to it. */
  bool startProcess(QStringList cmd, QStringList env = QStringList());

#ifdef KLFBACKEND_QT4
  /** Same as getAllStderr(), except result is returned here as QString. */
  QString readStderrString() {
    return QString::fromLocal8Bit(getAllStderr());
  }
  /** Same as getAllStdout(), except result is returned here as QString. */
  QString readStdoutString() {
    return QString::fromLocal8Bit(getAllStdout());
  }
#else
  /** Like QProcess::readStdout(), except returns a QString instead of a QByteArray */
  QString readStdoutString() {
    QCString sstdout = "";
    QByteArray stdout = readStdout();
    if (stdout.size() > 0 && stdout.data() != 0)
      sstdout = QCString(stdout.data(), stdout.size());
    return QString(sstdout);
  }
  /** Like QProcess::readStderr(), except returns a QString instead of a QByteArray */
  QString readStderrString() {
    QCString sstderr = "";
    QByteArray stderr = readStderr();
    if (stderr.size() > 0 && stderr.data() != 0)
      sstderr = QCString(stderr.data(), stderr.size());
    return QString(sstderr);
  }
#endif


private slots:
  void ourProcExited();
  void ourProcGotOurStdinData();

private:
  bool _runstatus;
};

#endif
