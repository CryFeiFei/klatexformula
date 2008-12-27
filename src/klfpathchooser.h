/***************************************************************************
 *   file klfpathchooser.h
 *   This file is part of the KLatexFormula Project.
 *   Copyright (C) 2008 by Philippe Faist
 *   philippe.faist at bluewin.ch
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


#ifndef KLFPATHCHOOSER_H
#define KLFPATHCHOOSER_H

#include <QFrame>
#include <QPushButton>
#include <QLineEdit>


class KLFPathChooser : public QFrame
{
  Q_OBJECT

  Q_PROPERTY(int mode READ mode WRITE setMode)
  Q_PROPERTY(QString caption READ caption WRITE setCaption)
  Q_PROPERTY(QString filter READ filter WRITE setFilter)
  Q_PROPERTY(QString path READ path WRITE setPath USER true)

public:
  KLFPathChooser(QWidget *parent);
  ~KLFPathChooser();

  int mode() const { return _mode; }
  QString caption() const { return _caption; }
  QString filter() const { return _filter; }
  QString path() const;

public slots:
  void setMode(int mode);
  void setCaption(const QString& caption);
  void setFilter(const QString& filter);
  void setPath(const QString& path);

  void requestBrowse();

private:
  int _mode;
  QString _caption;
  QString _filter;

  QLineEdit *txtPath;
  QPushButton *btnBrowse;

  QString _selectedfilter;
};

#endif
