/***************************************************************************
 *   file klflib.cpp
 *   This file is part of the KLatexFormula Project.
 *   Copyright (C) 2010 by Philippe Faist
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

#include <QDebug>
#include <QString>
#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QColor>

#include "klflib_p.h"
#include "klflib.h"



// ----------------------


QDataStream& operator<<(QDataStream& stream, const KLFStyle& style)
{
  return stream << style.name << (quint32)style.fg_color << (quint32)style.bg_color
		<< style.mathmode << style.preamble << (quint16)style.dpi;
}
QDataStream& operator>>(QDataStream& stream, KLFStyle& style)
{
  quint32 fg, bg;
  quint16 dpi;
  stream >> style.name;
  stream >> fg >> bg >> style.mathmode >> style.preamble >> dpi;
  style.fg_color = fg;
  style.bg_color = bg;
  style.dpi = dpi;
  return stream;
}

bool operator==(const KLFStyle& a, const KLFStyle& b)
{
  return a.name == b.name &&
    a.fg_color == b.fg_color &&
    a.bg_color == b.bg_color &&
    a.mathmode == b.mathmode &&
    a.preamble == b.preamble &&
    a.dpi == b.dpi;
}

QString prettyPrintStyle(const KLFStyle& sty)
{
  QString s = "";
  if (sty.name != QString::null)
    s = QObject::tr("<b>Style Name</b>: %1<br>").arg(sty.name);
  return s + QObject::tr("<b>Math Mode</b>: %1<br>"
			 "<b>DPI Resolution</b>: %2<br>"
			 "<b>Foreground Color</b>: %3 <font color=\"%4\"><b>[SAMPLE]</b></font><br>"
			 "<b>Background is Transparent</b>: %5<br>"
			 "<b>Background Color</b>: %6 <font color=\"%7\"><b>[SAMPLE]</b></font><br>"
			 "<b>LaTeX Preamble:</b><br><pre>%8</pre>")
    .arg(sty.mathmode)
    .arg(sty.dpi)
    .arg(QColor(sty.fg_color).name()).arg(QColor(sty.fg_color).name())
    .arg( (qAlpha(sty.bg_color) != 255) ? QObject::tr("YES") : QObject::tr("NO") )
    .arg(QColor(sty.bg_color).name()).arg(QColor(sty.bg_color).name())
    .arg(sty.preamble)
    ;
}



// ----------------------


KLFLibEntry::KLFLibEntry(const QString& latex, const QDateTime& dt, const QImage& preview,
			 const QString& category, const QString& tags, const KLFStyle& style)
  : KLFPropertizedObject("KLFLibEntry")
{
  initRegisteredProperties();
  setLatex(latex);
  setDateTime(dt);
  setPreview(preview);
  setCategory(category);
  setTags(tags);
  setStyle(style);
}
KLFLibEntry::KLFLibEntry(const QString& latex, const QDateTime& dt, const QImage& preview,
			 const KLFStyle& style)
  : KLFPropertizedObject("KLFLibEntry")
{
  initRegisteredProperties();
  QString latexonly = stripCategoryTagsFromLatex(latex);
  QString category = categoryFromLatex(latex);
  QString tags = tagsFromLatex(latex);
  setLatex(latexonly);
  setDateTime(dt);
  setPreview(preview);
  setCategory(category);
  setTags(tags);
  setStyle(style);
}
KLFLibEntry::KLFLibEntry(const KLFLibEntry& copy)
  : KLFPropertizedObject("KLFLibEntry")
{
  initRegisteredProperties();
  setAllProperties(copy.allProperties());
}
KLFLibEntry::~KLFLibEntry()
{
}


int KLFLibEntry::setEntryProperty(const QString& propName, const QVariant& value)
{
  int propId = propertyIdForName(propName);
  if (propId < 0) {
    // register the property
    propId = registerProperty(propName);
    if (propId < 0)
      return -1;
  }
  // and set the property
  setProperty(propId, value);
  return propId;
}

// private
void KLFLibEntry::initRegisteredProperties()
{
  KLF_FUNC_SINGLE_RUN ;
  
  registerBuiltInProperty(Latex, "Latex");
  registerBuiltInProperty(DateTime, "DateTime");
  registerBuiltInProperty(Preview, "Preview");
  registerBuiltInProperty(Category, "Category");
  registerBuiltInProperty(Tags, "Tags");
  registerBuiltInProperty(Style, "Style");
}


// static
QString KLFLibEntry::categoryFromLatex(const QString& latex)
{
  QString s = latex.section('\n', 0, 0, QString::SectionSkipEmpty);
  if (s[0] == '%' && s[1] == ':') {
    return s.mid(2).trimmed();
  }
  return QString::null;
}
// static
QString KLFLibEntry::tagsFromLatex(const QString& latex)
{
  QString s = latex.section('\n', 0, 0, QString::SectionSkipEmpty);
  if (s[0] == '%' && s[1] == ':') {
    // category is s.mid(2);
    s = latex.section('\n', 1, 1, QString::SectionSkipEmpty);
  }
  if (s[0] == '%') {
    return s.mid(1).trimmed();
  }
  return QString::null;
}

// static
QString KLFLibEntry::stripCategoryTagsFromLatex(const QString& latex)
{
  int k = 0;
  while (k < latex.length() && latex[k].isSpace())
    ++k;
  if (k == latex.length()) return "";
  if (latex[k] == '%') {
    ++k;
    if (k == latex.length()) return "";
    //strip category and/or tag:
    if (latex[k] == ':') {
      // strip category
      while (k < latex.length() && latex[k] != '\n')
	++k;
      ++k;
      if (k >= latex.length()) return "";
      if (latex[k] != '%') {
	// there isn't any tags, just category; return rest of string
	return latex.mid(k);
      }
      ++k;
      if (k >= latex.length()) return "";
    }
    // strip tag:
    while (k < latex.length() && latex[k] != '\n')
      ++k;
    ++k;
    if (k >= latex.length()) return "";
  }
  // k is the beginnnig of the latex string
  return latex.mid(k);
}


// ---------------------------------------------------

KLFLibResourceEngine::KLFLibResourceEngine(const QUrl& url, uint featureflags,
					   QObject *parent)
  : QObject(parent), KLFPropertizedObject("KLFLibResourceEngine"), pUrl(url),
    pFeatureFlags(featureflags), pReadOnly(false), pDefaultSubResource(QString())
{
  initRegisteredProperties();

  //  qDebug()<<"KLFLibResourceEngine::KLFLibResourceEngine("<<url<<","<<pFeatureFlags<<","
  //	  <<parent<<")";

  QStringList rdonly = pUrl.allQueryItemValues("klfReadOnly");
  if (rdonly.size() && rdonly.last() == "true") {
    if (pFeatureFlags & FeatureReadOnly)
      pReadOnly = true;
  }
  pUrl.removeAllQueryItems("klfReadOnly");

}
KLFLibResourceEngine::~KLFLibResourceEngine()
{
}

void KLFLibResourceEngine::initRegisteredProperties()
{
  KLF_FUNC_SINGLE_RUN

  registerBuiltInProperty(PropTitle, "Title");
  registerBuiltInProperty(PropLocked, "Locked");
  registerBuiltInProperty(PropViewType, "ViewType");
  registerBuiltInProperty(PropAccessShared, "AccessShared");
}

bool KLFLibResourceEngine::canModifyData(ModifyType /*modifytype*/) const
{
  return !isReadOnly() && !locked();
}
bool KLFLibResourceEngine::canModifyProp(int propId) const
{
  if (propId == PropLocked)
    return !isReadOnly() && (pFeatureFlags & FeatureLocked);

  return !isReadOnly() && !locked();
}
bool KLFLibResourceEngine::canRegisterProperty(const QString& /*propName*/) const
{
  return false;
}

QString KLFLibResourceEngine::defaultSubResource()
{
  return pDefaultSubResource;
}

QVariant KLFLibResourceEngine::subResourceProperty(const QString& /*subResource*/, int /*propId*/)
{
  return QVariant();
}

bool KLFLibResourceEngine::setTitle(const QString& title)
{
  return setResourceProperty(PropTitle, title);
}
bool KLFLibResourceEngine::setLocked(bool setlocked)
{
  // if locked feature is supported, setResourceProperty().
  // immediately return FALSE otherwise.
  if (pFeatureFlags & FeatureLocked) {
    return setResourceProperty(PropLocked, setlocked);
  }
  return false;
}

bool KLFLibResourceEngine::setViewType(const QString& viewType)
{
  return setResourceProperty(PropViewType, viewType);
}

bool KLFLibResourceEngine::setReadOnly(bool readonly)
{
  if ( !(pFeatureFlags & FeatureReadOnly) )
    return false;

  pReadOnly = readonly;
  return true;
}


void KLFLibResourceEngine::setDefaultSubResource(const QString& subResource)
{
  pDefaultSubResource = subResource;
}

bool KLFLibResourceEngine::setSubResourceProperty(const QString& /*subResource*/, int /*propId*/,
						  const QVariant& /*value*/)
{
  return false;
}

KLFLibEntry KLFLibResourceEngine::entry(entryId id)
{
  if ((pFeatureFlags & FeatureSubResources) && pDefaultSubResource.isNull())
    qWarning("KLFLibResourceEngine::entry(id): sub-resources are supported feature but"
	     " no default sub-resource is specified!");
  return etnry(pDefaultSubResource, id);
}
bool KLFLibResourceEngine::hasEntry(entryId id)
{
  if ((pFeatureFlags & FeatureSubResources) && pDefaultSubResource.isNull())
    qWarning("KLFLibResourceEngine::hasEntry(id): sub-resources are supported feature but"
	     " no default sub-resource is specified!");
  return hasEtnry(pDefaultSubResource, id);
}
QList<KLFLibEntryWithId> KLFLibResourceEngine::entries(const QList<KLFLib::entryId>& idList)
{
  if ((pFeatureFlags & FeatureSubResources) && pDefaultSubResource.isNull())
    qWarning("KLFLibResourceEngine::entries(idList): sub-resources are supported feature but"
	     " no default sub-resource is specified!");
  return entries(pDefaultSubResource, idList);
}

QList<KLFLibEntryWithId> KLFLibResourceEngine::allEntries()
{
  if ((pFeatureFlags & FeatureSubResources) && pDefaultSubResource.isNull())
    qWarning("KLFLibResourceEngine::allEntries(): sub-resources are supported feature but"
	     " no default sub-resource is specified!");
  return allEntries(pDefaultSubResource);
}




KLFLibResourceEngine::entryId KLFLibResourceEngine::insertEntry(const QString& subResource,
								const KLFLibEntry& entry)
{
  QList<entryId> ids = insertEntries(subResource, KLFLibEntryList() << entry);
  if (ids.size() == 0)
    return -1;

  return ids[0];
}
entryId KLFLibResourceEngine::insertEntry(const KLFLibEntry& entry)
{
  if (pFeatureFlags & FeatureSubResources)
    qWarning("KLFLibResourceEngine::insertEntry(entry): sub-resources are supported feature but"
	     " no sub-resource is specified!");
  return insertEntry(QString(), entry);
}
QList<entryId> KLFLibResourceEngine::insertEntries(const KLFLibEntryList& entrylist)
{
  if (pFeatureFlags & FeatureSubResources)
    qWarning("KLFLibResourceEngine::insertEntries(entrylist): sub-resources are supported feature but"
	     " no sub-resource is specified!");
  return insertEntry(QString(), entrylist);
}



bool KLFLibResourceEngine::saveAs(const QUrl&)
{
  // not permitted by default. Subclasses must reimplement
  // to support this feature.
  return false;
}

QVariant KLFLibResourceEngine::resourceProperty(const QString& name) const
{
  return KLFPropertizedObject::property(name);
}

bool KLFLibResourceEngine::loadResourceProperty(const QString& propName, const QVariant& value)
{
  int propId = propertyIdForName(propName);
  if (propId < 0) {
    if (!canRegisterProperty(propName))
      return false;
    // register the property
    propId = registerProperty(propName);
    if (propId < 0)
      return false;
  }
  // finally set the property
  return setResourceProperty(propId, value);
}

bool KLFLibResourceEngine::setResourceProperty(int propId, const QVariant& value)
{
  if ( ! KLFPropertizedObject::propertyIdRegistered(propId) )
    return false;

  if ( !canModifyProp(propId) ) {
    return false;
  }

  bool result = saveResourceProperty(propId, value);
  if (!result) // operation not permitted or failed
    return false;

  // operation succeeded: set KLFPropertizedObject-based property.
  KLFPropertizedObject::setProperty(propId, value);
  emit resourcePropertyChanged(propId);
  return true;
}


// -----

QDataStream& operator<<(QDataStream& stream, const KLFLibResourceEngine::KLFLibEntryWithId& entrywid)
{
  return stream << entrywid.id << entrywid.entry;
}
QDataStream& operator>>(QDataStream& stream, KLFLibResourceEngine::KLFLibEntryWithId& entrywid)
{
  return stream >> entrywid.id >> entrywid.entry;
}



// ---------------------------------------------------

bool KLFLibResourceSimpleEngine::hasEntry(entryId id)
{
  return entry(id) == KLFLibEntry();
}

QList<KLFLibResourceEngine::KLFLibEntryWithId>
/* */ KLFLibResourceSimpleEngine::entries(const QList<KLFLib::entryId>& idList)
{
  QList<KLFLibEntryWithId> elist;
  int k;
  for (k = 0; k < idList.size(); ++k)
    elist << KLFLibEntryWithId(idList[k], entry(idList[k]));
  return elist;
}

// ---------------------------------------------------


QList<KLFLibEngineFactory*> KLFLibEngineFactory::pRegisteredFactories =
	 QList<KLFLibEngineFactory*>();

KLFLibEngineFactory::KLFLibEngineFactory(QObject *parent)
  : QObject(parent)
{
  registerFactory(this);
}
KLFLibEngineFactory::~KLFLibEngineFactory()
{
  unRegisterFactory(this);
}

bool KLFLibEngineFactory::canCreateResource(const QString& /*scheme*/) const
{
  return false;
}

QWidget * KLFLibEngineFactory::createPromptCreateParametersWidget(QWidget */*parent*/,
									  const QString& /*scheme*/,
									  const Parameters& /*par*/)
{
  return NULL;
}
KLFLibEngineFactory::Parameters
/* */ KLFLibEngineFactory::retrieveCreateParametersFromWidget(const QString& /*scheme*/,
								      QWidget */*parent*/)
{
  return Parameters();
}
KLFLibResourceEngine *KLFLibEngineFactory::createResource(const QString& /*scheme*/,
								  const Parameters& /*param*/,
								  QObject */*parent*/)
{
  return NULL;
}

bool KLFLibEngineFactory::canResourceSaveAs(const QString& /*scheme*/) const
{
  return false;
}
QWidget *KLFLibEngineFactory::createPromptSaveAsWidget(QWidget */*parent*/,
						       const QString& /*scheme*/,
						       const QUrl& /*defaultUrl*/)
{
  return NULL;
}
QUrl KLFLibEngineFactory::retrieveSaveAsUrlFromWidget(const QString& /*scheme*/,
						      QWidget */*widget*/)
{
  return QUrl();
}





KLFLibEngineFactory *KLFLibEngineFactory::findFactoryFor(const QString& urlScheme)
{
  int k;
  // walk registered factories, and return the first that supports this scheme.
  for (k = 0; k < pRegisteredFactories.size(); ++k) {
    if (pRegisteredFactories[k]->supportedSchemes().contains(urlScheme))
      return pRegisteredFactories[k];
  }
  // no factory found
  return NULL;
}

QStringList KLFLibEngineFactory::allSupportedSchemes()
{
  QStringList schemes;
  int k;
  for (k = 0; k < pRegisteredFactories.size(); ++k) {
    schemes << pRegisteredFactories[k]->supportedSchemes();
  }
  return schemes;
}

void KLFLibEngineFactory::registerFactory(KLFLibEngineFactory *factory)
{
  if (pRegisteredFactories.indexOf(factory) != -1)
    return;
  pRegisteredFactories.append(factory);
}

void KLFLibEngineFactory::unRegisterFactory(KLFLibEngineFactory *factory)
{
  if (pRegisteredFactories.indexOf(factory) == -1)
    return;
  pRegisteredFactories.removeAll(factory);
}


