/***************************************************************************
 *   file klfsidewidget.h
 *   This file is part of the KLatexFormula Project.
 *   Copyright (C) 2011 by Philippe Faist
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
/* $Id$ */

#ifndef KLFSIDEWIDGET_H
#define KLFSIDEWIDGET_H


#include <QObject>
#include <QWidget>


#include <klfdefs.h>


class KLFSideWidgetManagerPrivate;
class KLFSideWidgetManager : public QObject
{
  Q_OBJECT
public:
  KLFSideWidgetManager(QWidget *sideWidget = NULL, QObject *parent = NULL);
  virtual ~KLFSideWidgetManager();

  inline QWidget * sideWidget() { return pSideWidget; }

  void setSideWidget(QWidget *widget);

public slots:
  virtual void showSideWidget(bool show) = 0;
  void showSideWidget() { showSideWidget(true); }
  void hideSideWidget(bool hide = true) { showSideWidget(!hide); }

protected:
  virtual void newSideWidgetSet(QWidget *oldSideWidget, QWidget *newSideWidget)
  {  Q_UNUSED(oldSideWidget); Q_UNUSED(newSideWidget);  }

private:

  QWidget *pSideWidget;

  KLF_DECLARE_PRIVATE(KLFSideWidgetManager);
};


class KLFShowHideSideWidgetManagerPrivate;
class KLFShowHideSideWidgetManager : public KLFSideWidgetManager
{
  Q_OBJECT

  Q_PROPERTY(Qt::Orientation orientation READ orientation WRITE setOrientation) ;
  Q_PROPERTY(int calcSpacing READ calcSpacing WRITE setCalcSpacing) ;
  KLF_PROPERTY_GETSET(Qt::Orientation, orientation, Orientation) ;
  KLF_PROPERTY_GETSET(int, calcSpacing, CalcSpacing) ;
public:
  KLFShowHideSideWidgetManager(QWidget *sideWidget = NULL, QObject *parent = NULL);
  virtual ~KLFShowHideSideWidgetManager();

  virtual bool eventFilter(QObject *obj, QEvent *event);

public slots:
  virtual void showSideWidget(bool show);

protected:
  virtual bool event(QEvent *event);

  virtual void newSideWidgetSet(QWidget *oldSideWidget, QWidget *newSideWidget);

private slots:
  void resizeParentWidget(const QSize& size);

private:
  KLF_DECLARE_PRIVATE(KLFShowHideSideWidgetManager);
};







#endif
