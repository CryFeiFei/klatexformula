/***************************************************************************
 *   file klflibview.cpp
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


#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QString>
#include <QDataStream>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <QModelIndex>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QComboBox>
#include <QHeaderView>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QListView>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>

#include <ui_klflibopenresourcedlg.h>
#include <ui_klflibrespropeditor.h>

#include "klfconfig.h"
#include "klflibview.h"

#include "klflibview_p.h"


template<class T>
const T& passingDebug(const T& t, QDebug& dbg)
{
  dbg << " : " << t;
  return t;
}


QDebug& operator<<(QDebug& dbg, const KLFLibModel::NodeId& en);
QDebug& operator<<(QDebug& dbg, const KLFLibModel::EntryNode& en);
QDebug& operator<<(QDebug& dbg, const KLFLibModel::CategoryLabelNode& en);



// -------------------------------------------------------

KLFAbstractLibView::KLFAbstractLibView(QWidget *parent)
  : QWidget(parent), pResourceEngine(NULL)
{
}

void KLFAbstractLibView::updateView()
{
  updateResourceView();
}

void KLFAbstractLibView::setResourceEngine(KLFLibResourceEngine *resource)
{
  if (pResourceEngine)
    disconnect(pResourceEngine, 0, this, 0);
  pResourceEngine = resource;
  connect(pResourceEngine, SIGNAL(dataChanged(const QList<KLFLib::entryId>&)),
	  this, SLOT(updateResourceData(const QList<KLFLib::entryId>&)));
  connect(pResourceEngine, SIGNAL(resourcePropertyChanged(int)),
	  this, SLOT(updateResourceProp(int)));
  updateResourceView();
}

void KLFAbstractLibView::wantMoreCategorySuggestions()
{
  emit moreCategorySuggestions(getCategorySuggestions());
}

QList<QAction*> KLFAbstractLibView::addContextMenuActions(const QPoint&)
{
  return QList<QAction*>();
}



// -------------------------------------------------------

QList<KLFLibViewFactory*> KLFLibViewFactory::pRegisteredFactories =
	 QList<KLFLibViewFactory*>();


KLFLibViewFactory::KLFLibViewFactory(const QStringList& viewTypeIdentifiers,
						     QObject *parent)
  : QObject(parent), pViewTypeIdentifiers(viewTypeIdentifiers)
{
  registerFactory(this);
}
KLFLibViewFactory::~KLFLibViewFactory()
{
  unRegisterFactory(this);
}


QString KLFLibViewFactory::defaultViewTypeIdentifier()
{
  if (pRegisteredFactories.size() > 0)
    return pRegisteredFactories[0]->pViewTypeIdentifiers.first();
  return QString();
}

KLFLibViewFactory *KLFLibViewFactory::findFactoryFor(const QString& viewTypeIdentifier)
{
  if (viewTypeIdentifier.isEmpty()) {
    if (pRegisteredFactories.size() > 0)
      return pRegisteredFactories[0]; // first registered factory is default
    return NULL;
  }
  int k;
  // walk registered factories, and return the first that supports this scheme.
  for (k = 0; k < pRegisteredFactories.size(); ++k) {
    if (pRegisteredFactories[k]->viewTypeIdentifiers().contains(viewTypeIdentifier))
      return pRegisteredFactories[k];
  }
  // no factory found
  return NULL;
}

QStringList KLFLibViewFactory::allSupportedViewTypeIdentifiers()
{
  QStringList list;
  int k;
  for (k = 0; k < pRegisteredFactories.size(); ++k)
    list << pRegisteredFactories[k]->viewTypeIdentifiers();
  return list;
}


void KLFLibViewFactory::registerFactory(KLFLibViewFactory *factory)
{
  KLF_ASSERT_NOT_NULL( factory, "Attempt to register NULL factory!", return )
    ;
  // WARNING: THIS FUNCTION IS CALLED FROM CONSTRUCTOR. NO VIRTUAL METHOD CALLS!
  if (factory->pViewTypeIdentifiers.size() == 0) {
    qWarning("KLFLibViewFactory::registerFactory: factory must provide at least one view type!");
    return;
  }
  if (pRegisteredFactories.indexOf(factory) != -1) // already registered
    return;
  pRegisteredFactories.append(factory);
}

void KLFLibViewFactory::unRegisterFactory(KLFLibViewFactory *factory)
{
  if (pRegisteredFactories.indexOf(factory) == -1)
    return;
  pRegisteredFactories.removeAll(factory);
}








// -------------------------------------------------------





KLFLibModel::KLFLibModel(KLFLibResourceEngine *engine, uint flavorFlags, QObject *parent)
  : QAbstractItemModel(parent), pFlavorFlags(flavorFlags), lastSortColumn(1),
    lastSortOrder(Qt::AscendingOrder)
{
  setResource(engine);
  // DON'T CONNECT SIGNALs FROM RESOURCE ENGINE HERE, we are informed from
  // the view. This is because of the KLFAbstractLibView API.
  //  connect(engine, SIGNAL(dataChanged(...)), this, SLOT(updateData(...)));
}

KLFLibModel::~KLFLibModel()
{
}

void KLFLibModel::setResource(KLFLibResourceEngine *resource)
{
  pResource = resource;
  updateCacheSetupModel();
  sort(lastSortColumn, lastSortOrder);
}


void KLFLibModel::setFlavorFlags(uint flags, uint modify_mask)
{
  if ( (flags & modify_mask) == (pFlavorFlags & modify_mask) )
    return; // no change
  uint other_flags = pFlavorFlags & ~modify_mask;
  pFlavorFlags = flags | other_flags;

  // stuff that needs update
  if (modify_mask & DisplayTypeMask) {
    updateCacheSetupModel();
  }
  if (modify_mask & GroupSubCategories) {
    sort(lastSortColumn, lastSortOrder);
  }
}
uint KLFLibModel::flavorFlags() const
{
  return pFlavorFlags;
}

QVariant KLFLibModel::data(const QModelIndex& index, int role) const
{
  NodeId p = getNodeForIndex(index);
  if (!p.valid() || p.isRoot())
    return QVariant();

  if (role == ItemKindItemRole)
    return QVariant::fromValue<int>(p.kind);

  if (p.kind == EntryKind) {
    // --- GET ENTRY DATA ---
    EntryNode ep = getEntryNode(p);
    const KLFLibEntry& entry = ep.entry;

    if (role == Qt::ToolTipRole) // current contents
      role = entryItemRole(entryColumnContentsPropertyId(index.column()));

    if (role == EntryContentsTypeItemRole)
      return entryColumnContentsPropertyId(index.column());
    if (role == FullEntryItemRole)
      return QVariant::fromValue(entry);
    if (role == EntryIdItemRole)
      return QVariant::fromValue<KLFLib::entryId>(ep.entryid);

    if (role == entryItemRole(KLFLibEntry::Latex))
      return entry.latex();
    if (role == entryItemRole(KLFLibEntry::DateTime))
      return entry.dateTime();
    if (role == entryItemRole(KLFLibEntry::Preview))
      return QVariant::fromValue<QImage>(entry.preview());
    if (role == entryItemRole(KLFLibEntry::Category))
      return entry.category();
    if (role == entryItemRole(KLFLibEntry::Tags))
      return entry.tags();
    if (role == entryItemRole(KLFLibEntry::Style))
      return QVariant::fromValue(entry.style());
    // by default
    return QVariant();
  }
  else if (p.kind == CategoryLabelKind) {
    // --- GET CATEGORY LABEL DATA ---
    const CategoryLabelNode& cp = pCategoryLabelCache[p.index];
    if (role == Qt::ToolTipRole)
      return cp.fullCategoryPath;
    if (role == CategoryLabelItemRole)
      return cp.categoryLabel;
    if (role == FullCategoryPathItemRole)
      return cp.fullCategoryPath;
    // by default
    return QVariant();
  }

  // default
  return QVariant();
}
Qt::ItemFlags KLFLibModel::flags(const QModelIndex& index) const
{
  Qt::ItemFlags flagdropenabled = 0;
  if ( index.column() == 0 &&
       (pResource->canModifyData(KLFLibResourceEngine::InsertData) ||
	pResource->canModifyData(KLFLibResourceEngine::ChangeData)) )
    flagdropenabled = Qt::ItemIsDropEnabled;

  if (!index.isValid())
    return flagdropenabled;

  NodeId p = getNodeForIndex(index);
  if (!p.valid())
    return 0; // ?!!?
  if (p.kind == EntryKind)
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
  if (p.kind == CategoryLabelKind)
    return Qt::ItemIsEnabled | flagdropenabled;

  // by default (should never happen)
  return 0;
}
bool KLFLibModel::hasChildren(const QModelIndex &parent) const
{
  NodeId p = getNodeForIndex(parent);
  if (!p.valid() || p.isRoot()) {
    // invalid index -> interpret as root node
    p = NodeId::rootNode();
  }

  return getNode(p).children.size() > 0;
}
QVariant KLFLibModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (role == Qt::FontRole) {
    return qApp->font();
  }
  if (role == Qt::SizeHintRole && orientation == Qt::Horizontal) {
    switch (entryColumnContentsPropertyId(section)) {
    case KLFLibEntry::Preview:
      return QSize(280,30);
    case KLFLibEntry::Latex:
      return QSize(350,30);
    case KLFLibEntry::Category:
    case KLFLibEntry::Tags:
      return QSize(200,30);
    default:
      return QVariant();
    }
  }
  if (role == Qt::DisplayRole) {
    if (orientation == Qt::Horizontal)
      return KLFLibEntry().propertyNameForId(entryColumnContentsPropertyId(section));
    return QString("-");
  }
  return QVariant();
}
bool KLFLibModel::hasIndex(int row, int column, const QModelIndex &parent) const
{
  // better implementation in the future
  return index(row, column, parent).isValid();
}

QModelIndex KLFLibModel::index(int row, int column, const QModelIndex &parent) const
{
  CategoryLabelNode p = getCategoryLabelNode(NodeId::rootNode()); // root node
  if (parent.isValid()) {
    NodeId pp = NodeId::fromUID(parent.internalId());
    if (pp.valid() && pp.kind == CategoryLabelKind)
      p = getCategoryLabelNode(pp);
  }
  if (row < 0 || row >= p.children.size())
    return QModelIndex();
  return createIndexFromId(p.children[row], row, column);
}
QModelIndex KLFLibModel::parent(const QModelIndex &index) const
{
  NodeId p = getNodeForIndex(index);
  if ( !p.valid() ) // invalid index
    return QModelIndex();
  Node n = getNode(p);
  if ( ! n.parent.valid() ) // invalid parent (should never happen!)
    return QModelIndex();
  return createIndexFromId(n.parent, -1 /* figure out row */, 0);
}
int KLFLibModel::rowCount(const QModelIndex &parent) const
{
  NodeId p = getNodeForIndex(parent);
  if (!p.valid())
    p = NodeId::rootNode();

  return getNode(p).children.size();
}

int KLFLibModel::columnCount(const QModelIndex & /*parent*/) const
{
  return 5;
}
int KLFLibModel::entryColumnContentsPropertyId(int column) const
{
  // WARNING: ANY CHANGES MADE HERE MUST BE REPEATED IN THE NEXT FUNCTION
  switch (column) {
  case 0:
    return KLFLibEntry::Preview;
  case 1:
    return KLFLibEntry::Tags;
  case 2:
    return KLFLibEntry::Category;
  case 3:
    return KLFLibEntry::Latex;
  case 4:
    return KLFLibEntry::DateTime;
  default:
    return -1;
  }
}
int KLFLibModel::columnForEntryPropertyId(int entryPropertyId) const
{
  // WARNING: ANY CHANGES MADE HERE MUST BE REPEATED IN THE PREVIOUS FUNCTION
  switch (entryPropertyId) {
  case KLFLibEntry::Preview:
    return 0;
  case KLFLibEntry::Tags:
    return 1;
  case KLFLibEntry::Category:
    return 2;
  case KLFLibEntry::Latex:
    return 3;
  case KLFLibEntry::DateTime:
    return 4;
  default:
    return -1;
  }
}


Qt::DropActions KLFLibModel::supportedDropActions() const
{
  return Qt::CopyAction|Qt::MoveAction;
}

QStringList KLFLibModel::mimeTypes() const
{
  return QStringList() << "application/x-klf-internal-lib-move-entries"
		       << "application/x-klf-libentries" << "text/html" << "text/plain";
}
QMimeData *KLFLibModel::mimeData(const QModelIndexList& indlist) const
{
  /** \page appxMimeLib Appendix: KLF's Own Mime Formats for Library Entries
   *
   * \section appxMimeLib_elist The application/x-klf-libentries data format
   *
   * Is basically a Qt-\ref QDataStream saved data of a \ref KLFLibEntryList (ie. a
   * \ref QList<KLFLibEntry>), saved with Qt \ref QDataStream version \ref QDataStream::Qt_4_4.
   *
   * There is also a property map for including arbitrary parameters to the list (eg.
   * originating URL, etc.), stored in a \ref QVariantMap. Standard properties as of now:
   *  - \c "Url" (type QUrl) : originating URL
   *
   * Example code:
   * \code
   *  KLFLibEntryList entries = ...;
   *  QByteArray data;
   *  QDataStream stream(&data, QIODevice::WriteOnly);
   *  stream << QVariantMap() << data;
   *  // now data contains the exact data for the application/x-klf-libentries mimetype.
   * \endcode
   * Other example: see the source code of \ref KLFLibModel::mimeData() in \ref klfview.cpp .
   *
   * \section appxMimeLib_internal The INTERNAL <tt>application/x-klf-internal-lib-move-entries</tt>
   * * Used for internal move within the same resource only.
   * * Basic difference with <tt>application/x-klf-libentries</tt>: the latter contains
   *   only the IDs of the entries (for reference for deletion for example) and the url
   *   of the open resource for identification.
   * * Internal Format: \ref QDataStream, stream version \ref QDataStream::Qt_4_4, in the
   *   following order:
   *   \code stream << QVariantMap(<i>property-map</i>)
   *        << QList<KLFLib::entryId>(<i>entry-id-list</i>);
   *   \endcode
   * * The <tt><i>property-map</i></tt> contains properties relative to the mime data, such
   *   as the originating URL (in property \c "Url" of type QUrl)
   * .
   */

  // in the future, this will serve when dragging a category label, to redifine the index
  // list as containing all the child entries.
  QModelIndexList indexes = indlist;

  // get all entries
  KLFLibEntryList entries;
  QList<KLFLib::entryId> entryids;
  QList<NodeId> entriesnodeids;
  int k;
  for (k = 0; k < indexes.size(); ++k) {
    NodeId n = getNodeForIndex(indexes[k]);
    if (!n.valid() || n.isRoot())
      continue;
    if (n.kind != EntryKind)
      continue;
    if (entriesnodeids.contains(n))
      continue; // skip duplicates (for ex. for other model column indexes)
    EntryNode en = getEntryNode(n);
    entries << pResource->entry(en.entryid);
    entryids << en.entryid;
    entriesnodeids << n;
  }

  //  qDebug()<<"KLFLibModel::mimeData: Encoding entries "<<entries;

  QMimeData *mime = new QMimeData;
  QByteArray data;
  QByteArray internalmovedata;
  QByteArray htmldata;
  QByteArray textdata;

  // prepare the data for filling in the mime object
  {
    // application/x-klf-libentries
    QDataStream str(&data, QIODevice::WriteOnly);
    QVariantMap vprop;
    vprop["Url"] = QUrl(pResource->url());
    str.setVersion(QDataStream::Qt_4_4);
    // now dump the list in the bytearray
    str << vprop << entries;

    // application/x-klf-internal-lib-move-entries
    QDataStream imstr(&internalmovedata, QIODevice::WriteOnly);
    imstr.setVersion(QDataStream::Qt_4_4);
    imstr << vprop << entryids;
  }
  {
    QTextStream htmlstr(&htmldata, QIODevice::WriteOnly);
    QTextStream textstr(&textdata, QIODevice::WriteOnly);
    // a header for HTML
    htmlstr << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
	    << "<html>\n"
	    << "  <head>\n"
      	    << "    <title>KLatexFormula Library Entry List</title>\n"
      // 	    << "    <style>\n"
      // 	    << "      div.klfpobj_entry { margin-bottom: 15px; }\n"
      // 	    << "      div.klfpobj_name { font-weight: bold; }\n"
      // 	    << "      div.klfpobj_propname { display: inline; }\n"
      // 	    << "      div.klfpobj_propvalue { display: inline; font-style: italic;\n"
      //	    << "        padding-left: 10px; }\n"
      // 	    << "    </style>\n"
	    << "  </head>\n"
	    << "\n"
	    << "  <body>\n"
	    << "\n";

    // a header for text
    textstr << " *** KLFLibEntryList ***\n\n";
    // now dump the data in the appropriate streams

    KLFLibEntry de; // dummy entry for property querying
    QList<int> entryprops = de.registeredPropertyIdList();
    for (k = 0; k < entries.size(); ++k) {
      htmlstr << entries[k].toString(KLFLibEntry::ToStringUseHtml);
      textstr << entries[k].toString(/*KLFLibEntry::ToStringQuoteValues*/);
    }
    // HTML footer
    htmlstr << "\n"
	    << "  </body>\n"
	    << "</html>\n";
  }

  // and set the data
  mime->setData("application/x-klf-libentries", data);
  mime->setData("application/x-klf-internal-lib-move-entries", internalmovedata);
  mime->setData("text/html", htmldata);
  mime->setData("text/plain", textdata);
  return mime;
}

// private
bool KLFLibModel::dropCanInternal(const QMimeData *mimedata)
{
  if ( ! mimedata->hasFormat("application/x-klf-internal-lib-move-entries") ||
       ! pResource->canModifyData(KLFLibResourceEngine::ChangeData))
    return false;

  QByteArray imdata = mimedata->data("application/x-klf-internal-lib-move-entries");
  QDataStream imstr(imdata);
  imstr.setVersion(QDataStream::Qt_4_4);
  QVariantMap vprop;
  imstr >> vprop;
  QUrl url = vprop.value("Url").toUrl();
  //  qDebug()<<"KLFLibModel::dropCanInternal: drag originated from "<<url<<"; we are "<<pResource->url();
  return (url == pResource->url());
}

bool KLFLibModel::dropMimeData(const QMimeData *mimedata, Qt::DropAction action, int row,
			       int column, const QModelIndex& parent)
{
  qDebug() << "Drop data: action="<<action<<" row="<<row<<" col="<<column
	   << " parent="<<parent;

  if (action == Qt::IgnoreAction)
    return true;
  if (action != Qt::CopyAction)
    return false;

  if ( ! (mimedata->hasFormat("application/x-klf-internal-lib-move-entries") &&
	  pResource->canModifyData(KLFLibResourceEngine::ChangeData)) &&
       ! (mimedata->hasFormat("application/x-klf-libentries") &&
	  pResource->canModifyData(KLFLibResourceEngine::InsertData)) ) {
    // cannot (change items with application/x-klf-internal-lib-move-entries) AND
    // cannot (insert items with application/x-klf-libentries)
    return false;
  }

  if (column > 0)
    return false;

  // imdata, imstr : "*i*nternal *m*ove" ; ldata, lstr : "entry *l*ist"
  bool useinternalmove = dropCanInternal(mimedata);
  if (useinternalmove) {
    qDebug() << "Dropping application/x-klf-internal-lib-move-entries";
    if ( !(pFlavorFlags & CategoryTree) )
      return false;

    QByteArray imdata = mimedata->data("application/x-klf-internal-lib-move-entries");
    QDataStream imstr(imdata);
    imstr.setVersion(QDataStream::Qt_4_4);
    QList<KLFLib::entryId> idlist;
    QVariantMap vprop;
    imstr >> vprop >> idlist;
    // get the category we moved to
    NodeId pn = getNodeForIndex(parent);
    if (!pn.valid()) {
      pn = NodeId::rootNode();
    }
    if (pn.kind != CategoryLabelKind) {
      qWarning()<<"Dropped in a non-category index! (kind="<<pn.kind<<")";
      return false;
    }
    const CategoryLabelNode& cn = pCategoryLabelCache[pn.index];
    // move, not copy: change the selected entries to the new category.
    QString newcategory = cn.fullCategoryPath;
    bool r = pResource->changeEntries(idlist, QList<int>() << KLFLibEntry::Category,
				      QList<QVariant>() << QVariant(newcategory));
    qDebug()<<"Accepted drop of type application/x-klf-internal-lib-move-entries. Res="<<r<<"\n"
	    <<"ID list is "<<idlist<<" new category is "<<newcategory;
    if (!r) {
      return false;
    }
    // dataChanged() is emitted in changeEntries()
    return true;
  }

  qDebug()<<"Dropping entry list.";
  QByteArray ldata = mimedata->data("application/x-klf-libentries");
  QDataStream lstr(ldata);
  lstr.setVersion(QDataStream::Qt_4_4);
  KLFLibEntryList elist;
  QVariantMap vprop;
  lstr >> vprop >> elist;
  if ( elist.isEmpty() )
    return true;

  // insert list, regardless of parent (no category change)
  bool res = insertEntries(elist);
  qDebug()<<"Dropped entry list "<<elist<<". result="<<res;
  if (!res)
    return false;

  // dataChanged() is emitted in insertEntries()
  return true;
}

uint KLFLibModel::dropFlags(QDragMoveEvent *event)
{
  const QMimeData *mdata = event->mimeData();
  if (dropCanInternal(mdata)) {
    if ( !(pFlavorFlags & CategoryTree) ) {
      return 0; // will NOT accept an internal move not in a category tree.
      //           (note: icon moves not handled in KLFLibModel; intercepted in view.)
    }
    // we're doing an internal drop in category tree:
    if (pResource->canModifyData(KLFLibResourceEngine::ChangeData))
      return DropWillAccept|DropWillMove|DropWillCategorize;
    return 0;
  }
  if (pResource->canModifyData(KLFLibResourceEngine::InsertData))
    return DropWillAccept;
  return 0;
}

QImage KLFLibModel::dragImage(const QModelIndexList& indexes)
{
  if (indexes.isEmpty())
    return QImage();

  const int MAX = 5;
  const QSize s1 = 0.8*klfconfig.UI.labelOutputFixedSize;
  const QPointF delta(18.0, 20.0);
  QList<QImage> previewlist;
  QList<NodeId> alreadydone;
  int k, j;
  int iS = indexes.size();
  int n = qMin(1+MAX, iS);
  for (j = 0, k = 0; k < iS && j < n; ++k) {
    NodeId n = getNodeForIndex(indexes[iS-k-1]); // reverse order
    if (!n.valid() || n.kind != EntryKind)
      continue;
    if (alreadydone.contains(n))
      continue;
    alreadydone << n;
    previewlist << pEntryCache[n.index].entry.preview().scaled(s1, Qt::KeepAspectRatio,
							       Qt::SmoothTransformation);
    ++j;
  }
  if (previewlist.isEmpty())
    return QImage();

  int N = qMin(MAX, previewlist.size());
  QSize s2 = (s1 + (N-1)*pointToSizeF(delta)).toSize();
  QImage image(s2, QImage::Format_ARGB32_Premultiplied);
  image.fill(qRgba(0,0,0,0));
  {
    QPainter p(&image);
    QPointF P(0,0);
    QPointF lastimgbr;
    for (k = 0; k < N; ++k) {
      // and add this image
      QImage i = transparentify_image(previewlist[k], 0.7);
      p.drawImage(P, i);
      // p.drawRect(QRectF(P, s1));
      lastimgbr = P+sizeToPointF(i.size());
      P += delta;
    }
    if (k == MAX) {
      p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
      p.setPen(QPen(QColor(0,0,0),2.0));
      QPointF br = lastimgbr;
      p.drawPoint(br - QPointF(5,5));
      p.drawPoint(br - QPointF(10,5));
      p.drawPoint(br - QPointF(15,5));
    }
  }

  return autocrop_image(image);
}


bool KLFLibModel::isDesendantOf(const QModelIndex& child, const QModelIndex& ancestor)
{
  if (!child.isValid())
    return false;

  return child.parent() == ancestor || isDesendantOf(child.parent(), ancestor);
}

QStringList KLFLibModel::categoryList() const
{
  QStringList catlist;
  // walk category tree and collect all categories
  int k;

  // skip root category (hence k=1 instead of k=0)
  for (k = 1; k < pCategoryLabelCache.size(); ++k) {
    catlist << pCategoryLabelCache[k].fullCategoryPath;
  }
  return catlist;
}

void KLFLibModel::updateData(const QList<KLFLib::entryId>& /*entryIdList*/)
{
  /** \todo ................ OPTIMIZE THIS METHOD, don't rebuild full tree each time */
  updateCacheSetupModel();
}

QModelIndex KLFLibModel::walkNextIndex(const QModelIndex& cur)
{
  NodeId nextnode = nextNode(getNodeForIndex(cur));
  //  qDebug()<<"KLFLibModel::walkNextIndex: got next node=NodeId("<<nextnode.kind<<","<<nextnode.index<<")";
  return createIndexFromId(nextnode, -1, 0);
}
QModelIndex KLFLibModel::walkPrevIndex(const QModelIndex& cur)
{
  NodeId prevnode = prevNode(getNodeForIndex(cur));
  return createIndexFromId(prevnode, -1, 0);
}

KLFLib::entryId KLFLibModel::entryIdForIndex(const QModelIndex& index) const
{
  NodeId node = getNodeForIndex(index);
  if (!node.valid())
    return -1;
  if (node.kind != EntryKind)
    return -1;
  return getEntryNode(node).entryid;
}

QModelIndex KLFLibModel::findEntryId(KLFLib::entryId eid) const
{
  // walk entry list
  int k;
  for (k = 0; k < pEntryCache.size(); ++k) {
    if (pEntryCache[k].entryid == eid)
      return createIndexFromId(NodeId(EntryKind, k), -1, 0);
  }
  return QModelIndex();
}


QModelIndex KLFLibModel::searchFind(const QString& queryString, bool forward)
{
  pSearchString = queryString;
  pSearchCurNode = NodeId();
  return searchFindNext(forward);
}
QModelIndex KLFLibModel::searchFindNext(bool forward)
{
  if (pSearchString.isEmpty())
    return QModelIndex();

  // search nodes
  NodeId (KLFLibModel::*stepfunc)(NodeId) = forward ? &KLFLibModel::nextNode : &KLFLibModel::prevNode;
  bool found = false;
  while ( ! found &&
	  (pSearchCurNode = (this->*stepfunc)(pSearchCurNode)).valid() ) {
    if ( nodeValue(pSearchCurNode, KLFLibEntry::Latex).contains(pSearchString, Qt::CaseInsensitive) ||
    	 nodeValue(pSearchCurNode, KLFLibEntry::Tags).contains(pSearchString, Qt::CaseInsensitive) ) {
      found = true;
    }
  }
  if (found)
    return createIndexFromId(pSearchCurNode, -1, 0);
  return QModelIndex();
}

KLFLibModel::NodeId KLFLibModel::nextNode(NodeId n)
{
  //  qDebug()<<"KLFLibModel::nextNode("<<n<<"): full tree dump:";
  //  dumpNodeTree(NodeId::rootNode());
  //  qDebug()<<"\tFull Entry Cache is:\n\t"<<pEntryCache
  //	  <<"\n\tAnd full catlabl cache is:\n\t"<<pCategoryLabelCache;
    
  if (!n.valid()) {
    // start with root category (but don't return the root category itself)
    n = NodeId::rootNode();
  }
  // walk children, if any
  Node nn = getNode(n);
  if (nn.children.size() > 0) {
    return nn.children[0];
  }
  // no children, find next sibling.
  NodeId parentid;
  while ( (parentid = nn.parent).valid() ) {
    Node parent = getNode(parentid);
    int row = getNodeRow(n);
    if (row+1 < parent.children.size()) {
      // there is a next sibling: return it
      return parent.children[row+1];
    }
    // no next sibling, so go up the tree
    n = parentid;
    nn = parent;
  }
  // last entry passed
  return NodeId();
}

KLFLibModel::NodeId KLFLibModel::prevNode(NodeId n)
{
  if (!n.valid() || n.isRoot()) {
    // look for last node in tree.
    return lastNode(NodeId()); // NodeId() is accepted by lastNode.
  }
  // find previous sibling
  NodeId parentId = getNode(n).parent;
  Node parent = getNode(parentId);
  int row = getNodeRow(n);
  if (row > 0) {
    // there is a previous sibling: find its last node
    return lastNode(parent.children[row-1]);
  }
  // there is no previous sibling: so we can return the parent
  // except if parent is the root node, in which case we return an invalid NodeId.
  if (parentId == NodeId::rootNode())
    return NodeId();

  return parentId;
}

KLFLibModel::NodeId KLFLibModel::lastNode(NodeId n)
{
  if (!n.valid())
    n = NodeId::rootNode(); // root category

  Node nn = getNode(n);
  if (nn.children.size() == 0)
    return n; // no children: n is itself the "last node"

  // children: return last node of last child.
  return lastNode(nn.children[nn.children.size()-1]);
}


// private
QList<KLFLib::entryId> KLFLibModel::entryIdList(const QModelIndexList& indexlist) const
{
  QList<KLFLib::entryId> idList;
  int k;
  QList<NodeId> nodeIds;
  // walk all indexes and get their IDs
  for (k = 0; k < indexlist.size(); ++k) {
    NodeId n = getNodeForIndex(indexlist[k]);
    if ( !n.valid() || n.kind != EntryKind)
      continue;
    if ( nodeIds.contains(n) ) // duplicate, probably for another column
      continue;
    nodeIds << n;
    idList << getEntryNode(n).entryid;
  }
  return idList;
}

bool KLFLibModel::changeEntries(const QModelIndexList& indexlist, int propId, const QVariant& value)
{
  if ( indexlist.size() == 0 )
    return true; // no change...

  QList<KLFLib::entryId> idList = entryIdList(indexlist);
  bool r = pResource->changeEntries(idList, QList<int>() << propId, QList<QVariant>() << value);

  //  if (r)
  //    for (k = 0; k < nodePtrs.size(); ++k)
  //      nodePtrs[k]->entry.setProperty(propId, value);

  // will be automatically called via the call to resoruce->(modify-data)()!
  //  updateCacheSetupModel();
  return r;
}

bool KLFLibModel::insertEntries(const KLFLibEntryList& entries)
{
  if (entries.size() == 0)
    return true; // nothing to do...

  KLFLibEntryList elist = entries;
  int k;
  for (k = 0; k < entries.size(); ++k) {
    // strip any position property from the entry
    if (elist[k].property("IconView_IconPosition").isValid())
      elist[k].setEntryProperty("IconView_IconPosition", QVariant());
  }

  QList<KLFLib::entryId> list = pResource->insertEntries(elist);

  // will be automatically called via the call to resoruce->(modify-data)()!
  //  updateCacheSetupModel();

  if ( list.size() == 0 || list.contains(-1) )
    return false; // error for at least one entry
  return true;
}

bool KLFLibModel::deleteEntries(const QModelIndexList& indexlist)
{
  if ( indexlist.size() == 0 )
    return true; // no change...

  QList<KLFLib::entryId> idList = entryIdList(indexlist);
  if ( pResource->deleteEntries(idList) ) {
    // will be automatically called via the call to resoruce->(modify-data)()!
    //    updateCacheSetupModel();
    return true;
  }
  return false;
}

QString KLFLibModel::nodeValue(NodeId n, int entryProperty)
{
  // return an internal string representation of the value of the property 'entryProperty' in node ptr.
  // or the category title if node is a category

  if (!n.valid() || n.isRoot())
    return QString();
  if (n.kind == EntryKind) {
    if (entryProperty == KLFLibEntry::Preview)
      entryProperty = KLFLibEntry::DateTime; // user friendliness. sort by date when selecting preview.

    EntryNode en = getEntryNode(n);
    if (entryProperty == KLFLibEntry::DateTime) {
      return en.entry.property(KLFLibEntry::DateTime).toDateTime().toString("yyyy-MM-dd+hh:mm:ss.zzz");
    } else {
      return en.entry.property(entryProperty).toString();
    }
  }
  if (n.kind == CategoryLabelKind)
    return getCategoryLabelNode(n).categoryLabel;

  qWarning("nodeValue(): Bad Item Kind: %d", n.kind);
  return QString();
}

bool KLFLibModel::KLFLibModelSorter::operator()(const NodeId& a, const NodeId& b)
{
  if ( ! a.valid() || ! b.valid() )
    return false;

  if ( groupCategories ) {
    if ( a.kind != EntryKind && b.kind == EntryKind ) {
      // category kind always smaller than entry kind in category grouping mode
      return true;
    } else if ( a.kind == EntryKind && b.kind != EntryKind ) {
      return false;
    }
    if ( a.kind != EntryKind || b.kind != EntryKind ) {
      if ( ! (a.kind == CategoryLabelKind && b.kind == CategoryLabelKind) ) {
	qWarning("KLFLibModelSorter::operator(): Bad node kinds!! a: %d and b: %d",
		 a.kind, b.kind);
	return false;
      }
      // when grouping sub-categories, always sort ascending
      return QString::localeAwareCompare(model->getCategoryLabelNode(a).categoryLabel,
					 model->getCategoryLabelNode(b).categoryLabel)  < 0;
    }
  }

  QString nodevaluea = model->nodeValue(a, entryProp);
  QString nodevalueb = model->nodeValue(b, entryProp);
  return sortOrderFactor*QString::localeAwareCompare(nodevaluea, nodevalueb) < 0;
}

// private
void KLFLibModel::sortCategory(NodeId category, int column, Qt::SortOrder order)
{
  int k;
  // sort this category's children

  // This works both in LinearList and in CategoryTree display types. (In LinearList
  // display type, call this function on root category node)

  if (category.kind != CategoryLabelKind)
    return;
  if (category.index < 0 || category.index >= pCategoryLabelCache.size())
    return;

  KLFLibModelSorter s =
    KLFLibModelSorter(this, entryColumnContentsPropertyId(column), order, pFlavorFlags & GroupSubCategories);

  QList<NodeId> childlist = pCategoryLabelCache[category.index].children;
  qSort(childlist.begin(), childlist.end(), s);
  pCategoryLabelCache[category.index].children = childlist;

  // and sort all children's children
  for (k = 0; k < childlist.size(); ++k)
    if (childlist[k].kind == CategoryLabelKind)
      sortCategory(childlist[k], column, order);

}
void KLFLibModel::sort(int column, Qt::SortOrder order)
{
  // first notify anyone who may be interested
  emit layoutAboutToBeChanged();
  // save persistent indexes
  QModelIndexList pindexes = persistentIndexList();
  QList<PersistentId> pids = persistentIdList(pindexes);
  // and then sort our model
  sortCategory(NodeId::rootNode(), column, order);
  // change relevant persistent indexes
  QModelIndexList newpindexes = newPersistentIndexList(pids);
  changePersistentIndexList(pindexes, newpindexes);
  // and notify of layout change
  emit layoutChanged();
  lastSortColumn = column;
  lastSortOrder = order;
}



KLFLibModel::NodeId KLFLibModel::getNodeForIndex(const QModelIndex& index) const
{
  if ( ! index.isValid() )
    return NodeId();
  return NodeId::fromUID(index.internalId());
}
KLFLibModel::Node KLFLibModel::getNode(NodeId nodeid) const
{
  if (!nodeid.valid()) {
    qWarning()<<"KLFLibModel::getNode: Invalid Node Id: "<<nodeid;
    return EntryNode();
  }
  if (nodeid.kind == EntryKind) {
    if (nodeid.index < 0 || nodeid.index >= pEntryCache.size()) {
      qWarning()<<"KLFLibModel::getNode: Invalid Entry Node Id: "<<nodeid<<" : index out of range!";
      return EntryNode();
    }
    return pEntryCache[nodeid.index];
  } else if (nodeid.kind == CategoryLabelKind) {
    if (nodeid.index < 0 || nodeid.index >= pCategoryLabelCache.size()) {
      qWarning()<<"KLFLibModel::getNode: Invalid Category Label Node Id: "<<nodeid
		<<" : index out of range!";
      return CategoryLabelNode();
    }
    return pCategoryLabelCache[nodeid.index];
  }
  qWarning("KLFLibModel::getNode(): Invalid kind: %d (index=%d)\n", nodeid.kind, nodeid.index);
  return EntryNode();
}
KLFLibModel::EntryNode KLFLibModel::getEntryNode(NodeId nodeid) const
{
  if (!nodeid.valid() || nodeid.kind != EntryKind ||
      nodeid.index < 0 || nodeid.index >= pEntryCache.size()) {
    qWarning()<<"KLFLibModel::getEntryNode: Invalid Entry Node "<<nodeid<<"!";
    return EntryNode();
  }
  return pEntryCache[nodeid.index];
}

KLFLibModel::CategoryLabelNode KLFLibModel::getCategoryLabelNode(NodeId nodeid) const
{
  if (!nodeid.valid() || nodeid.kind != CategoryLabelKind ||
      nodeid.index < 0 || nodeid.index >= pCategoryLabelCache.size()) {
    qWarning()<<"KLFLibModel::getCat.LabelNode: Invalid Category Label Node "<<nodeid<<"!";
    return CategoryLabelNode();
  }
  return pCategoryLabelCache[nodeid.index];
}

int KLFLibModel::getNodeRow(NodeId node) const
{
  if ( ! node.valid() )
    return -1;
  if ( node == NodeId::rootNode() )
    return 0;

  Node n = getNode(node);

  NodeId pparentid = n.parent;
  if ( !pparentid.valid() ) {
    // shouldn't happen (!?!), only parentless item should be root node !
    qWarning()<<"Found parentless non-root node !";
    return 0;
  }
  Node pparent = getNode(pparentid);
  // find child in parent
  int k;
  for (k = 0; k < pparent.children.size(); ++k)
    if (pparent.children[k] == node)
      return k;

  // search failed
  qWarning("KLFLibModel: Unable to get node row: parent-child one-way broken!!");
  return -1;
}

QModelIndex KLFLibModel::createIndexFromId(NodeId nodeid, int row, int column) const
{
  if ( ! nodeid.valid() )
    return QModelIndex();
  if (nodeid == NodeId::rootNode())
    return QModelIndex();

  // if row is -1, then we need to find the row of the item
  if ( row < 0 ) {
    row = getNodeRow(nodeid);
  }
  return createIndex(row, column, nodeid.universalId());
}

QList<KLFLibModel::PersistentId> KLFLibModel::persistentIdList(const QModelIndexList& persistentIndexes)
{
  // save persistent indexes
  QList<PersistentId> persistentIndexIds;
  int k;
  for (k = 0; k < persistentIndexes.size(); ++k) {
    PersistentId id;
    NodeId n = getNodeForIndex(persistentIndexes[k]);
    if (!n.valid()) {
      id.kind = (ItemKind)-1;
    } else {
      id.kind = n.kind;
      if (id.kind == EntryKind)
	id.entry_id = getEntryNode(n).entryid;
      else if (id.kind == CategoryLabelKind)
	id.categorylabel_fullpath = getCategoryLabelNode(n).fullCategoryPath;
      else
	qWarning("KLFLibModel::persistentIdList: Bad Node kind: %d!!", id.kind);
    }
    id.column = persistentIndexes[k].column();
    persistentIndexIds << id;
  }
  return persistentIndexIds;
}
QModelIndexList KLFLibModel::newPersistentIndexList(const QList<PersistentId>& persistentIndexIds)
{
  QModelIndexList newPersistentIndexes;
  int k;
  for (k = 0; k < persistentIndexIds.size(); ++k) {
    // find node in new layout
    PersistentId id = persistentIndexIds[k];
    QModelIndex index;
    int j;
    if (id.kind == EntryKind) {
      // walk entry cache to find this item
      for (j = 0; j < pEntryCache.size() && pEntryCache[j].entryid != id.entry_id; ++j)
	;
      if (j < pEntryCache.size())
	index = createIndexFromId(NodeId(EntryKind, j), -1, id.column);
    } else if (id.kind == CategoryLabelKind) {
      // walk entry cache to find this item
      for (j = 0; j < pCategoryLabelCache.size() &&
	     pCategoryLabelCache[j].fullCategoryPath != id.categorylabel_fullpath; ++j)
	;
      if (j < pCategoryLabelCache.size())
	index = createIndexFromId(NodeId(CategoryLabelKind, j), -1, id.column);
    } else {
      qWarning("newPersistentIndexList: bad persistent id node kind! :%d", id.kind);
    }

    newPersistentIndexes << index;
  }
  return newPersistentIndexes;
}

  
void KLFLibModel::updateCacheSetupModel()
{
  int k;
  qDebug("updateCacheSetupModel(); pFlavorFlags=%#010x", (uint)pFlavorFlags);

  emit layoutAboutToBeChanged();

  QModelIndexList persistentIndexes = persistentIndexList();
  QList<PersistentId> persistentIndexIds = persistentIdList(persistentIndexes);

  // clear cache first
  pEntryCache.clear();
  pCategoryLabelCache.clear();
  CategoryLabelNode root = CategoryLabelNode();
  root.fullCategoryPath = "/";
  root.categoryLabel = "/";
  // root category label MUST ALWAYS (in every display flavor) occupy index 0 in category
  // label cache
  pCategoryLabelCache.append(root);

  QList<KLFLibResourceEngine::KLFLibEntryWithId> everything = pResource->allEntries();
  NodeId entryindex;
  entryindex.kind = EntryKind;
  for (k = 0; k < everything.size(); ++k) {
    qDebug()<<"KLFLibModel::updateC.S.M.: Adding entry id="<<everything[k].id<<"; entry="
	    <<everything[k].entry;
    EntryNode e;
    e.entryid = everything[k].id;
    e.entry = everything[k].entry;
    entryindex.index = pEntryCache.size();
    pEntryCache.append(e);

    if (displayType() == LinearList) {
      // add as child of root element
      pCategoryLabelCache[0].children.append(entryindex);
      pEntryCache[entryindex.index].parent = NodeId(CategoryLabelKind, 0);
    } else if (displayType() == CategoryTree) {
      // create category label node (creating the full tree if needed)
      IndexType catindex = cacheFindCategoryLabel(e.entry.category(), true);
      pCategoryLabelCache[catindex].children.append(entryindex);
      pEntryCache[entryindex.index].parent = NodeId(CategoryLabelKind, catindex);
    } else {
      qWarning("Flavor Flags have unknown display type! pFlavorFlags=%#010x", (uint)pFlavorFlags);
    }
  }

  QModelIndexList newPersistentIndexes = newPersistentIndexList(persistentIndexIds);
  // and refresh all persistent indexes
  changePersistentIndexList(persistentIndexes, newPersistentIndexes);

  dumpNodeTree(NodeId::rootNode()); // DEBUG
  emit dataChanged(QModelIndex(),QModelIndex());
  emit layoutChanged();

  // and sort
  sort(lastSortColumn, lastSortOrder);

  qDebug()<<"KLFLibModel::updateCacheSetupModel: end of func";
}
KLFLibModel::IndexType KLFLibModel::cacheFindCategoryLabel(QString category, bool createIfNotExists)
{
  // fix category: remove any double-/ to avoid empty sections. (goal: ensure that join(split(c))==c )
  category = category.split('/', QString::SkipEmptyParts).join("/");
  
  //  qDebug() << "cacheFindCategoryLabel(category="<<category<<", createIfNotExists="<<createIfNotExists<<")";

  int i;
  for (i = 0; i < pCategoryLabelCache.size(); ++i)
    if (pCategoryLabelCache[i].fullCategoryPath == category)
      return i;
  if (category.isEmpty() || category == "/")
    return 0; // index of root category label

  // if we haven't found the correct category, we may need to create it if requested by
  // caller. If not, return failure immediately
  if ( ! createIfNotExists )
    return -1;

  if (displayType() == CategoryTree) {
    QStringList catelements = category.split('/', QString::SkipEmptyParts);
    QString path;
    int last_index = 0; // root node
    int this_index = -1;
    // create full tree
    for (i = 0; i < catelements.size(); ++i) {
      path = QStringList(catelements.mid(0, i+1)).join("/");
      // find this parent category (eg. "Math" for a "Math/Vector Analysis" category)
      this_index = cacheFindCategoryLabel(path, false);
      if (this_index == -1) {
	// and create the parent category if needed
	this_index = pCategoryLabelCache.size();
	CategoryLabelNode c;
	c.fullCategoryPath = path;
	c.categoryLabel = catelements[i];
	pCategoryLabelCache.append(c);
	pCategoryLabelCache[last_index].children.append(NodeId(CategoryLabelKind, this_index));
	pCategoryLabelCache[this_index].parent = NodeId(CategoryLabelKind, last_index);
      }
      last_index = this_index;
    }
    return last_index;
  } else {
    qWarning("cacheFindCategoryLabel: but not in a category tree display type (flavor flags=%#010x)",
	     pFlavorFlags);
  }

  return 0;
}

QDebug& operator<<(QDebug& dbg, const KLFLibModel::NodeId& n)
{
  const char * skind =
    ( (n.kind == KLFLibModel::EntryKind) ? "EntryKind" :
      ((n.kind == KLFLibModel::CategoryLabelKind) ? "CategoryLabelKind" :
       "*InvalidKind*") );
  return dbg.nospace() << "NodeId("<<skind<<"+"<<n.index<<")";
}
QDebug& operator<<(QDebug& dbg, const KLFLibModel::EntryNode& en)
{
  return dbg << "EntryNode(entryid="<<en.entryid<<"; entry/latex="<<en.entry.latex()<<"; parent="
	     <<en.parent<<")";
}
QDebug& operator<<(QDebug& dbg, const KLFLibModel::CategoryLabelNode& cn)
{
  return dbg << "CategoryLabelNode(label="<<cn.categoryLabel<<";fullCategoryPath="<<cn.fullCategoryPath
	     <<"; parent="<<cn.parent<<")";
}


void KLFLibModel::dumpNodeTree(NodeId node, int indent) const
{
  char sindent[] = "                                                          ";
  if (indent < strlen(sindent))
    sindent[indent] = '\0';

  if (!node.valid())
    qDebug() << sindent << "(Invalid Node)";

  EntryNode en;
  CategoryLabelNode cn;
  switch (node.kind) {
  case EntryKind:
    en = getEntryNode(node);
    qDebug() << sindent << node <<"\n"<<sindent<<"\t\t"<<en;
    break;
  case CategoryLabelKind:
    cn = getCategoryLabelNode(node);
    qDebug() << sindent << node << "\n"<<sindent<<"\t\t"<<cn;
    break;
  default:
    qDebug() << sindent << node << "\n"<<sindent<<"\t\t*InvalidNodeKind*(kind="<<node.kind<<")";
  }

  if (!node.valid())
    return;

  Node n = getNode(node);

  int k;
  for (k = 0; k < n.children.size(); ++k) {
    dumpNodeTree(getNode(node).children[k], indent+4);
  }
}


// --------------------------------------------------------



KLFLibViewDelegate::KLFLibViewDelegate(QObject *parent)
  : QAbstractItemDelegate(parent), pSelModel(NULL)
{
}
KLFLibViewDelegate::~KLFLibViewDelegate()
{
}

QWidget * KLFLibViewDelegate::createEditor(QWidget */*parent*/,
					   const QStyleOptionViewItem& /*option*/,
					   const QModelIndex& /*index*/) const
{
  return 0;
}
bool KLFLibViewDelegate::editorEvent(QEvent */*event*/, QAbstractItemModel */*model*/,
				     const QStyleOptionViewItem& /*option*/,
				     const QModelIndex& /*index*/)
{
  return false;
}
void KLFLibViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem& option,
			       const QModelIndex& index) const
{
  PaintPrivate pp;
  pp.p = painter;
  pp.option = &option;
  pp.innerRectImage = QRect(option.rect.topLeft()+QPoint(2,2), option.rect.size()-QSize(4,4));
  pp.innerRectText = QRect(option.rect.topLeft()+QPoint(4,2), option.rect.size()-QSize(8,4));

  painter->save();

  QPen pen = painter->pen();
  pp.isselected = (option.state & QStyle::State_Selected);
  QPalette::ColorGroup cg = option.state & QStyle::State_Enabled
    ? QPalette::Normal : QPalette::Disabled;
  if (cg == QPalette::Normal && !(option.state & QStyle::State_Active))
    cg = QPalette::Inactive;
  if (pp.isselected) {
    painter->fillRect(option.rect, option.palette.brush(cg, QPalette::Highlight));
    painter->setPen(option.palette.color(cg, QPalette::HighlightedText));
  } else {
    painter->setPen(option.palette.color(cg, QPalette::Text));
  }

  int kind = index.data(KLFLibModel::ItemKindItemRole).toInt();
  if (kind == KLFLibModel::EntryKind)
    paintEntry(&pp, index);
  else if (kind == KLFLibModel::CategoryLabelKind)
    paintCategoryLabel(&pp, index);

  if (option.state & QStyle::State_HasFocus) {
    QStyleOptionFocusRect o;
    o.QStyleOption::operator=(option);
    o.rect = option.rect;
    o.state |= QStyle::State_KeyboardFocusChange;
    o.state |= QStyle::State_Item;
    QPalette::ColorGroup cg = (option.state & QStyle::State_Enabled)
                              ? QPalette::Normal : QPalette::Disabled;
    o.backgroundColor = option.palette.color(cg, (option.state & QStyle::State_Selected)
                                             ? QPalette::Highlight : QPalette::Window);
    const QWidget *w = 0;
    if (const QStyleOptionViewItemV3 *v3 = qstyleoption_cast<const QStyleOptionViewItemV3 *>(&option))
      w = v3->widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_FrameFocusRect, &o, painter, w);
  }

  painter->restore();
}
void KLFLibViewDelegate::paintEntry(PaintPrivate *p, const QModelIndex& index) const
{
  uint fl = PTF_HighlightSearch;
  if (index.parent() == pSearchIndex.parent() && index.row() == pSearchIndex.row())
    fl |= PTF_HighlightSearchCurrent;

  switch (index.data(KLFLibModel::EntryContentsTypeItemRole).toInt()) {
  case KLFLibEntry::Latex:
    // paint Latex String
    paintText(p, index.data(KLFLibModel::entryItemRole(KLFLibEntry::Latex)).toString(), fl);
    break;
  case KLFLibEntry::Preview:
    // paint Latex Equation
    {
      QImage img = index.data(KLFLibModel::entryItemRole(KLFLibEntry::Preview)).value<QImage>();
      QImage img2 = img.scaled(p->innerRectImage.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      if (p->isselected)
	img2 = transparentify_image(img2, 0.85);
      QPoint pos = p->innerRectImage.topLeft() + QPoint(0, (p->innerRectImage.height()-img2.height()) / 2);
      p->p->drawImage(pos, img2);
      break;
    }
  case KLFLibEntry::Category:
    // paint Category String
    paintText(p, index.data(KLFLibModel::entryItemRole(KLFLibEntry::Category)).toString(), fl);
    break;
  case KLFLibEntry::Tags:
    // paint Tags String
    paintText(p, index.data(KLFLibModel::entryItemRole(KLFLibEntry::Tags)).toString(), fl);
    break;
  case KLFLibEntry::DateTime:
    // paint DateTime String
    paintText(p, index.data(KLFLibModel::entryItemRole(KLFLibEntry::DateTime)).toDateTime()
	      .toString(Qt::DefaultLocaleLongDate), fl);
    break;
  default:
    qDebug("KLFLibViewDelegate::paintEntry(): Got bad contents type %d !",
	   index.data(KLFLibModel::EntryContentsTypeItemRole).toInt());
    // nothing to show !
    //    painter->fillRect(option.rect, QColor(128,0,0));
  }
}
void KLFLibViewDelegate::paintCategoryLabel(PaintPrivate *p, const QModelIndex& index) const
{
  if (index.column() > 0)  // paint only on first column
    return;

  //  qDebug()<<"index="<<index<<"; pSearchIndex="<<pSearchIndex;

  uint fl = PTF_HighlightSearch;
  if (index.parent() == pSearchIndex.parent() && index.row() == pSearchIndex.row())
    fl |= PTF_HighlightSearchCurrent;
  if ( (!pExpandedIndexes.contains(index) || !pExpandedIndexes.value(index)) &&
       indexHasSelectedDescendant(index) ) {
    // not expanded, and has selected child
    fl |= PTF_SelUnderline;
  }
  //   if ( sizeHint(*p->option, index).width() > p->option->rect.width() ) {
  //     // force rich text edit to handle too-wide texts
  //     fl |= PTF_ForceRichTextRender;
  //   }

  // paint Category Label
  paintText(p, index.data(KLFLibModel::CategoryLabelItemRole).toString(), fl);
}

QDebug& operator<<(QDebug& d, const KLFLibViewDelegate::ColorRegion& c)
{
  return d << "ColorRegion["<<c.start<<"->+"<<c.len<<"]";
}

void KLFLibViewDelegate::paintText(PaintPrivate *p, const QString& text, uint flags) const
{
  int drawnTextWidth;
  int drawnBaseLineY;
  if ( (pSearchString.isEmpty() || !(flags&PTF_HighlightSearch) ||
	text.indexOf(pSearchString, 0, Qt::CaseInsensitive) == -1) &&
       !(flags & PTF_SelUnderline) &&
       !(flags & PTF_ForceRichTextRender) ) {
    // no formatting required, use fast method.
    QSize s = QFontMetrics(p->option->font).size(0, text);
    drawnTextWidth = s.width();
    drawnBaseLineY = (int)(p->innerRectText.bottom() - 0.5f*(p->innerRectText.height()-s.height()));
    p->p->drawText(p->innerRectText, Qt::AlignLeft|Qt::AlignVCenter, text);
  } else {
    // formatting required.
    // build a list of regions to highlight
    QList<ColorRegion> c;
    QTextCharFormat f_highlight;
    if (flags & PTF_HighlightSearchCurrent)
      f_highlight.setBackground(QColor(0,255,0,80));
    f_highlight.setForeground(QColor(128,0,0));
    f_highlight.setFontItalic(true);
    f_highlight.setFontWeight(QFont::Bold);
    int i = -1, ci, j;
    if (!pSearchString.isEmpty()) {
      while ((i = text.indexOf(pSearchString, i+1, Qt::CaseInsensitive)) != -1)
	c << ColorRegion(f_highlight, i, pSearchString.length());
    }
    qSort(c);
    //    qDebug()<<"searchstr="<<pSearchString<<"; label "<<text<<": c is "<<c;
    QTextDocument textDocument;
    textDocument.setDefaultFont(p->option->font);
    QTextCursor cur(&textDocument);
    QList<ColorRegion> appliedfmts;
    for (i = ci = 0; i < text.length(); ++i) {
      //      qDebug()<<"Processing char "<<text[i]<<"; i="<<i<<"; ci="<<ci;
      if (ci >= c.size() && appliedfmts.size() == 0) {
	// insert all remaining text (no more formatting needed)
	cur.insertText(Qt::escape(text.mid(i)), QTextCharFormat());
	break;
      }
      while (ci < c.size() && c[ci].start == i) {
	appliedfmts.append(c[ci]);
	++ci;
      }
      QTextCharFormat f;
      for (j = 0; j < appliedfmts.size(); ++j) {
	if (i >= appliedfmts[j].start + appliedfmts[j].len) {
	  // this format no longer applies
	  appliedfmts.removeAt(j);
	  --j;
	  continue;
	}
	f.merge(appliedfmts[j].fmt);
      }
      cur.insertText(Qt::escape(text[i]), f);
    }

    QSizeF s = textDocument.size();
    QRectF textRect = p->innerRectText;
    // note: setLeft changes width, not right.
    textRect.setLeft(textRect.left()-4); // some offset because of qt's rich text drawing ..?
    // textRect is now correct
    // draw a selection underline, if needed
    if (flags & PTF_SelUnderline) {
       QColor h1 = p->option->palette.color(QPalette::Highlight);
       QColor h2 = h1;
       h1.setAlpha(0);
       h2.setAlpha(220);
       QLinearGradient gr(0.f, 0.f, 0.f, 1.f);
       gr.setCoordinateMode(QGradient::ObjectBoundingMode);
       gr.setColorAt(0.f, h1);
       gr.setColorAt(1.f, h2);
       QBrush selbrush(gr);
       p->p->save();
       //       p->p->setClipRect(p->option->rect);
       p->p->fillRect(QRectF(textRect.left(), textRect.bottom()-10,
			     textRect.width(), 10), selbrush);
       p->p->restore();
    }
    // check the text size
    drawnTextWidth = s.width();
    if (s.width() > textRect.width()) { // sorry, too large
      s.setWidth(textRect.width());
    }
    p->p->save();
    p->p->setClipRect(textRect);
    p->p->translate(textRect.topLeft());
    p->p->translate( QPointF( 0, //textRect.width() - s.width(),
			      textRect.height() - s.height()) / 2.f );
    textDocument.drawContents(p->p);
    p->p->restore();
    drawnBaseLineY = (int)(textRect.bottom() - (textRect.height() - s.height()) / 2.f);
  }

  // draw arrow if text too large

  if (drawnTextWidth > p->innerRectText.width()) {
    // draw small arrow indicating more text
    QColor c = p->option->palette.color(QPalette::Text);
    //      c.setAlpha(80);
    p->p->save();
    p->p->translate(p->option->rect.right()-2, drawnBaseLineY-2);
    p->p->setPen(c);
    p->p->drawLine(0, 0, -16, 0);
    p->p->drawLine(0, 0, -2, +2);
    p->p->restore();
  }
}

void KLFLibViewDelegate::setEditorData(QWidget */*editor*/, const QModelIndex& /*index*/) const
{
}
void KLFLibViewDelegate::setModelData(QWidget */*editor*/, QAbstractItemModel */*model*/,
				      const QModelIndex& /*index*/) const
{
}
QSize KLFLibViewDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
  int kind = index.data(KLFLibModel::ItemKindItemRole).toInt();
  if (kind == KLFLibModel::EntryKind) {
    int prop = -1;
    switch (index.data(KLFLibModel::EntryContentsTypeItemRole).toInt()) {
    case KLFLibEntry::Latex: prop = KLFLibEntry::Latex;
    case KLFLibEntry::Category: prop = (prop > 0) ? prop : KLFLibEntry::Category;
    case KLFLibEntry::Tags: prop = (prop > 0) ? prop : KLFLibEntry::Tags;
      return QFontMetrics(option.font)
	.size(0, index.data(KLFLibModel::entryItemRole(prop)).toString())+QSize(4,2);
    case KLFLibEntry::DateTime:
      return QFontMetrics(option.font)
	.size(0, index.data(KLFLibModel::entryItemRole(KLFLibEntry::DateTime)).toDateTime()
	      .toString(Qt::DefaultLocaleLongDate) )+QSize(4,2);
    case KLFLibEntry::Preview:
      return index.data(KLFLibModel::entryItemRole(KLFLibEntry::Preview)).value<QImage>().size()+QSize(4,4);
    default:
      return QSize();
    }
  } else if (kind == KLFLibModel::CategoryLabelKind) {
    return QFontMetrics(option.font)
      .size(0, index.data(KLFLibModel::CategoryLabelItemRole).toString())+QSize(4,2);
  } else {
    qWarning("KLFLibItemViewDelegate::sizeHint(): Bad Item kind: %d\n", kind);
    return QSize();
  }
}
void KLFLibViewDelegate::updateEditorGeometry(QWidget */*editor*/,
					      const QStyleOptionViewItem& /*option*/,
					      const QModelIndex& /*index*/) const
{
}



bool KLFLibViewDelegate::indexHasSelectedDescendant(const QModelIndex& index) const
{
  // simple, dumb way: walk selection list.
  int k;
  QModelIndexList sel = pSelModel->selectedIndexes();
  KLFLibModel *model = (KLFLibModel*)index.model();
  for (k = 0; k < sel.size(); ++k) {
    if (model->isDesendantOf(sel[k], index)) {
      // this selected item is child of our index
      return true;
    }
  }
  return false;
}




// -------------------------------------------------------

QDebug& operator<<(QDebug& dbg, const KLFLibResourceEngine::KLFLibEntryWithId& e)
{
  return dbg <<"KLFLibEntryWithId(id="<<e.id<<";"<<e.entry.category()<<","<<e.entry.tags()<<","
	     <<e.entry.latex()<<")";
}



KLFLibDefaultView::KLFLibDefaultView(QWidget *parent, ViewType view)
  : KLFAbstractLibView(parent), pViewType(view)
{
  pModel = NULL;

  pIconViewLockAction = NULL;
  pIconViewRelayoutAction = NULL;

  pEventFilterNoRecurse = false;

  QVBoxLayout *lyt = new QVBoxLayout(this);
  lyt->setMargin(0);
  lyt->setSpacing(0);
  pDelegate = new KLFLibViewDelegate(this);

  KLFLibDefTreeView *treeView = NULL;
  KLFLibDefListView *listView = NULL;
  switch (pViewType) {
  case IconView:
    listView = new KLFLibDefListView(this);
    qDebug()<<"Created list view.";
    listView->setViewMode(QListView::IconMode);
    listView->setSpacing(15);
    listView->setMovement(QListView::Free);
    listView->setFlow(QListView::LeftToRight);
    listView->setResizeMode(QListView::Fixed);
    qDebug()<<"prepared list view.";
    pView = listView;
    break;
  case CategoryTreeView:
  case ListTreeView:
  default:
    treeView = new KLFLibDefTreeView(this);
    treeView->setSortingEnabled(true);
    treeView->setIndentation(16);
    treeView->setAllColumnsShowFocus(true);
    connect(treeView, SIGNAL(expanded(const QModelIndex&)),
	    this, SLOT(slotExpanded(const QModelIndex&)));
    connect(treeView, SIGNAL(collapsed(const QModelIndex&)),
	    this, SLOT(slotCollapsed(const QModelIndex&)));
    pView = treeView;
    break;
  };

  lyt->addWidget(pView);

  pView->setSelectionBehavior(QAbstractItemView::SelectRows);
  pView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  pView->setDragEnabled(true);
  pView->setDragDropMode(QAbstractItemView::DragDrop);
  pView->setDragDropOverwriteMode(false);
  pView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  pView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  pView->setItemDelegate(pDelegate);
  pView->viewport()->installEventFilter(this);
  pView->installEventFilter(this);
  //  installEventFilter(this);

  connect(pView, SIGNAL(clicked(const QModelIndex&)),
	  this, SLOT(slotViewItemClicked(const QModelIndex&)));
  connect(pView, SIGNAL(doubleClicked(const QModelIndex&)),
	  this, SLOT(slotEntryDoubleClicked(const QModelIndex&)));
}
KLFLibDefaultView::~KLFLibDefaultView()
{
}

bool KLFLibDefaultView::event(QEvent *event)
{
  return KLFAbstractLibView::event(event);
}
bool KLFLibDefaultView::eventFilter(QObject *object, QEvent *event)
{
  return KLFAbstractLibView::eventFilter(object, event);
}

KLFLibEntryList KLFLibDefaultView::selectedEntries() const
{
  QModelIndexList selectedindexes = selectedEntryIndexes();
  KLFLibEntryList elist;
  // fill the entry list with our selected entries
  int k;
  for (k = 0; k < selectedindexes.size(); ++k) {
    if ( selectedindexes[k].data(KLFLibModel::ItemKindItemRole) != KLFLibModel::EntryKind )
      continue;
    KLFLibEntry entry = selectedindexes[k].data(KLFLibModel::FullEntryItemRole).value<KLFLibEntry>();
    qDebug() << "selection list: adding item [latex="<<entry.latex()<<"; tags="<<entry.tags()<<"]";

    elist << entry;
  }
  return elist;
}

QList<QAction*> KLFLibDefaultView::addContextMenuActions(const QPoint& /*pos*/)
{
  QList<QAction*> actionList = QList<QAction*>() << pCommonActions;
  if (pViewType == IconView) {
    actionList << pIconViewActions;
  }
  if (pViewType == CategoryTreeView || pViewType == ListTreeView) {
    actionList << pShowColumnActions;
  }
  return actionList;
}

bool KLFLibDefaultView::canMoveIcons() const
{
  KLFLibResourceEngine *e = resourceEngine();
  if (e == NULL)
    return false;
  if (e->propertyNameRegistered("IconView_IconPositionsLocked") &&
      e->resourceProperty("IconView_IconPositionsLocked").toBool())
    return false;
  if (!e->accessShared() && e->locked())
    return false;
  return true;
}


QVariantMap KLFLibDefaultView::saveGuiState() const
{
  QVariantMap vst;
  if (pViewType == CategoryTreeView || pViewType == ListTreeView) {
    QTreeView *tv = qobject_cast<QTreeView*>(pView);
    KLF_ASSERT_NOT_NULL( tv, "Tree View is NULL in view type "<<pViewType<<" !!", return QVariantMap() )
      ;
    vst["ColumnsState"] = QVariant::fromValue<QByteArray>(tv->header()->saveState());
  }
  if (pViewType == IconView) {
    KLFLibDefListView *lv = qobject_cast<KLFLibDefListView*>(pView);
    KLF_ASSERT_NOT_NULL( lv, "KLFLibDefListView List View is NULL in view type "<<pViewType<<" !!",
			 return QVariantMap() )
      ;
    if ( resourceEngine()->accessShared() ) {
      // access is shared, so we store icon position info in Gui state instead
      // of in the resource data itself.
      QMap<KLFLibResourceEngine::entryId,QPoint> iconpositions = lv->allIconPositions();
      QMap<KLFLibResourceEngine::entryId,QPoint>::const_iterator it;
      QVariantList vEntryIds; // will hold entryId's in qint32 format
      QVariantList vPositions; // will hold QPoint's
      for (it = iconpositions.begin(); it != iconpositions.end(); ++it) {
	vEntryIds << QVariant::fromValue<qint32>(it.key());
	vPositions << QVariant::fromValue<QPoint>(it.value());
      }
      vst["IconPositionsEntryIdList"] = QVariant::fromValue<QVariantList>(vEntryIds);
      vst["IconPositionsPositionList"] = QVariant::fromValue<QVariantList>(vPositions);
    }
  }
  return vst;
}
bool KLFLibDefaultView::restoreGuiState(const QVariantMap& vstate)
{
  if (pViewType == CategoryTreeView || pViewType == ListTreeView) {
    QByteArray colstate = vstate["ColumnsState"].toByteArray();
    QTreeView *tv = qobject_cast<QTreeView*>(pView);
    KLF_ASSERT_NOT_NULL( tv, "Tree View is NULL in view type"<<pViewType<<"!!", return false )
      ;
    tv->header()->restoreState(colstate);
  }
  if (pViewType == IconView) {
    KLFLibDefListView *lv = qobject_cast<KLFLibDefListView*>(pView);
    KLF_ASSERT_NOT_NULL( lv, "KLFLibDefListView List View is NULL in view type "<<pViewType<<" !!",
			 return false )
      ;
    if ( resourceEngine()->accessShared() ) {
      // access is shared, so we store icon position info in Gui state instead
      // of in the resource data itself.
      QVariantList vEntryIds = vstate["IconPositionsEntryIdList"].toList();
      QVariantList vPositions = vstate["IconPositionsPositionList"].toList();
      QMap<KLFLibResourceEngine::entryId,QPoint> iconpositions;
      int k;
      for (k = 0; k < vEntryIds.size() && k < vPositions.size(); ++k) {
	iconpositions[vEntryIds[k].value<qint32>()] = vPositions[k].value<QPoint>();
      }
      lv->loadIconPositions(iconpositions);
    }
  }
  return true;
}


void KLFLibDefaultView::updateResourceView()
{
  int k;
  KLFLibResourceEngine *resource = resourceEngine();
  if (resource == NULL) {
    pModel = NULL;
    pView->setModel(NULL);
    for (k = 0; k < pShowColumnActions.size(); ++k)
      delete pShowColumnActions[k];
    pShowColumnActions.clear();
    if (pIconViewLockAction)
      delete pIconViewLockAction;
    if (pIconViewRelayoutAction)
      delete pIconViewRelayoutAction;
  }

  //  qDebug() << "KLFLibDefaultView::updateResourceView: All items:\n"<<resource->allEntries();
  uint model_flavor = 0;
  switch (pViewType) {
  case IconView:
  case ListTreeView:
    model_flavor = KLFLibModel::LinearList;
    break;
  case CategoryTreeView:
  default:
    model_flavor = KLFLibModel::CategoryTree;
    break;
  };
  pModel = new KLFLibModel(resource, model_flavor|KLFLibModel::GroupSubCategories, this);
  pView->setModel(pModel);
  // get informed about selections
  QItemSelectionModel *s = pView->selectionModel();
  connect(s, SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)),
	  this, SLOT(slotViewSelectionChanged(const QItemSelection&, const QItemSelection&)));

  connect(pModel, SIGNAL(dataChanged(const QModelIndex&, const QModelIndex&)),
	  this, SLOT(slotResourceDataChanged(const QModelIndex&, const QModelIndex&)));

  // delegate wants to know more about selections...
  pDelegate->setSelectionModel(s);

  QAction *selectAllAction = new QAction(tr("Select All"), this);
  connect(selectAllAction, SIGNAL(activated()), this, SLOT(slotSelectAll()));
  pCommonActions = QList<QAction*>() << selectAllAction;

  if (pViewType == IconView) {
    qDebug()<<"About to prepare iconview.";
    //    KLFLibDefListView *lv = qobject_cast<KLFLibDefListView*>(pView);
    if ( ! resource->propertyNameRegistered("IconView_IconPositionsLocked") )
      resource->loadResourceProperty("IconView_IconPositionsLocked", false);

    pIconViewRelayoutAction = new QAction(tr("Relayout All Icons"), this);
    connect(pIconViewRelayoutAction, SIGNAL(activated()), this, SLOT(slotRelayoutIcons()));
    pIconViewLockAction = new QAction(tr("Lock Icon Positions"), this);
    pIconViewLockAction->setCheckable(true);
    connect(pIconViewLockAction, SIGNAL(toggled(bool)), this, SLOT(slotLockIconPositions(bool)));
    connect(pIconViewLockAction, SIGNAL(toggled(bool)),
	    pIconViewRelayoutAction, SLOT(setDisabled(bool)));
    bool iconposlocked
      = resource->resourceProperty("IconView_IconPositionsLocked").toBool();
    pIconViewLockAction->setChecked(iconposlocked);
    bool iconposlockenabled
      = resource->propertyNameRegistered("IconView_IconPositionsLocked") &&
      /* */ resource->canModifyProp(resource->propertyIdForName("IconView_IconPositionsLocked"));
    pIconViewLockAction->setEnabled(iconposlockenabled);
    pIconViewRelayoutAction->setEnabled(canMoveIcons());

    pIconViewActions = QList<QAction*>() << pIconViewRelayoutAction << pIconViewLockAction;
  }
  if (pViewType == CategoryTreeView || pViewType == ListTreeView) {
    QTreeView *treeView = qobject_cast<QTreeView*>(pView);
    // select some columns to show
    treeView->setColumnHidden(pModel->columnForEntryPropertyId(KLFLibEntry::Preview), false);
    treeView->setColumnHidden(pModel->columnForEntryPropertyId(KLFLibEntry::Latex), true);
    treeView->setColumnHidden(pModel->columnForEntryPropertyId(KLFLibEntry::Tags), false);
    treeView->setColumnHidden(pModel->columnForEntryPropertyId(KLFLibEntry::Category), true);
    treeView->setColumnHidden(pModel->columnForEntryPropertyId(KLFLibEntry::DateTime), true);
    // optimize column sizes
    for (k = 0; k < pModel->columnCount(); ++k)
      treeView->resizeColumnToContents(k);
    treeView->setColumnWidth(0, 35+treeView->columnWidth(0));
    // and provide a menu to show/hide these columns
    int col;
    QMenu *colMenu = new QMenu(this);
    for (col = 0; col < pModel->columnCount(); ++col) {
      QString title = pModel->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
      QAction *a;
      a = new QAction(title, this);
      a->setProperty("klfModelColumn", col);
      a->setCheckable(true);
      a->setChecked(!treeView->isColumnHidden(col));
      connect(a, SIGNAL(toggled(bool)), this, SLOT(slotShowColumnSenderAction(bool)));
      colMenu->addAction(a);
    }
    QAction *menuAction = new QAction(tr("Show/Hide Columns"), this);
    menuAction->setMenu(colMenu);
    pShowColumnActions = QList<QAction*>() << menuAction;
    // expand root items if they contain little number of children
    for (k = 0; k < pModel->rowCount(QModelIndex()); ++k) {
      QModelIndex i = pModel->index(k, 0, QModelIndex());
      if (pModel->rowCount(i) < 6)
	treeView->expand(i);
    }
  }

  updateResourceProp(-1); // update all with respect to resource changes..
  updateResourceOwnData(QList<KLFLib::entryId>());

  wantMoreCategorySuggestions();
}

void KLFLibDefaultView::updateResourceData(const QList<KLFLib::entryId>& entryIdList)
{
  KLF_ASSERT_NOT_NULL( pModel , "Model is NULL!" , return )
    ;
  pModel->updateData(entryIdList);
  // update our own data (icon positions)
  updateResourceOwnData(entryIdList);
}
void KLFLibDefaultView::updateResourceOwnData(const QList<KLFLib::entryId>& /*entryIdList*/)
{
  /** \todo ..................... OPTIMIZE THIS FUNCTION not to do a full refresh */
  qDebug()<<KLF_FUNC_NAME;
  if (pViewType == IconView) {
    KLFLibDefListView *lv = qobject_cast<KLFLibDefListView*>(pView);
    KLF_ASSERT_NOT_NULL( lv, "KLFLibDefListView List View is NULL in view type "<<pViewType<<" !!",
			 return )
      ;
    if ( ! lv->isSavingIconPositions() ) {
      // Important: do NOT update if the update was generated because an icon position was
      // saved by the view!

      // load the icon positions stored in the resource's entries as custom properties.
      int k;
      QList<KLFLibResourceEngine::KLFLibEntryWithId> allentries = pModel->resource()->allEntries();
      QMap<KLFLib::entryId, QPoint> readIconPositions;
      for (k = 0; k < allentries.size(); ++k) {
	QVariant pos = allentries[k].entry.property("IconView_IconPosition");
	if (pos.isValid() && pos.canConvert<QPoint>())
	  readIconPositions[allentries[k].id] = pos.value<QPoint>();
      }
      // update the view
      // call this function now. It will detect and possibly delay the actual icon position loading
      // until widget polishing time, if needed.
      lv->loadIconPositions(readIconPositions);
    }
  }
}
void KLFLibDefaultView::updateResourceProp(int propId)
{
  //  qDebug()<<"KLFLibDefView::updateR.Prop("<<propId<<")";

  // if locked property was modified, refresh
  // Lock Icon Positions & Relayout Icons according to whether we store that info
  // in resource or in view state...

  KLF_ASSERT_NOT_NULL( resourceEngine() , "Resource Engine is NULL, skipping !" , return )
    ;

  if ( propId == -1 || propId == KLFLibResourceEngine::PropLocked ) {
    if (pIconViewRelayoutAction != NULL)
      pIconViewRelayoutAction->setEnabled(canMoveIcons());
    if (pIconViewLockAction != NULL) {
      int propId = resourceEngine()->propertyIdForName("IconView_IconPositionsLocked");
      bool canmodify = resourceEngine()->canModifyProp(propId);
      if ( ! canmodify )
	pIconViewLockAction->setChecked(!canMoveIcons());
      pIconViewLockAction->setEnabled(canmodify);
      if (canmodify) {
	bool locked = pModel->resource()->resourceProperty("IconView_IconPositionsLocked").toBool();
	pIconViewLockAction->setChecked(locked);
      }
    }
  }
}

QStringList KLFLibDefaultView::getCategorySuggestions()
{
  return pModel->categoryList();
}

bool KLFLibDefaultView::writeEntryProperty(int property, const QVariant& value)
{
  return pModel->changeEntries(selectedEntryIndexes(), property, value);
}
bool KLFLibDefaultView::deleteSelected(bool requireConfirm)
{
  QModelIndexList sel = selectedEntryIndexes();
  if (sel.size() == 0)
    return true;
  if (requireConfirm) {
    QMessageBox::StandardButton res
      = QMessageBox::question(this, tr("Delete?"),
			      tr("Delete %n selected item(s) from resource \"%1\"?", "", sel.size())
			      .arg(pModel->resource()->title()),
			      QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Cancel);
    if (res != QMessageBox::Yes)
      return false; // abort action
  }
  return pModel->deleteEntries(sel);
}
bool KLFLibDefaultView::insertEntries(const KLFLibEntryList& entries)
{
  return pModel->insertEntries(entries);
}



void KLFLibDefaultView::restore(uint restoreflags)
{
  QModelIndexList sel = selectedEntryIndexes();
  if (sel.size() != 1) {
    qWarning("KLFLibDefaultView::restoreWithStyle(): Cannot restore: %d items selected.", sel.size());
    return;
  }

  KLFLibEntry e = sel[0].data(KLFLibModel::FullEntryItemRole).value<KLFLibEntry>();
  
  emit requestRestore(e, restoreflags);
}

void KLFLibDefaultView::slotSelectAll(const QModelIndex& parent)
{
  int k;
  QModelIndex topLeft = pModel->index(0, 0, parent);
  QModelIndex bottomRight = pModel->index(pModel->rowCount(parent)-1,
					  pModel->columnCount(parent)-1, parent);
  pView->selectionModel()->select(QItemSelection(topLeft, bottomRight), QItemSelectionModel::Select);
  if ( ! pModel->hasChildren(parent) )
    return;
  for (k = 0; k < pModel->rowCount(parent); ++k) {
    QModelIndex child = pModel->index(k, 0, parent);
    slotSelectAll(child);
  }
}

void KLFLibDefaultView::slotRelayoutIcons()
{
  if (pViewType != IconView || !pView->inherits("KLFLibDefListView")) {
    return;
  }
  if (pModel->resource()->resourceProperty("IconView_IconPositionsLocked").toBool())
    return;
  KLFLibDefListView *lv = qobject_cast<KLFLibDefListView*>(pView);
  // force a re-layout
  lv->forceRelayout();
}
void KLFLibDefaultView::slotLockIconPositions(bool locked)
{
  if (locked == pModel->resource()->resourceProperty("IconView_IconPositionsLocked").toBool())
    return; // no change

  qDebug()<<"Locking icon positions to "<<locked;
  bool r = pModel->resource()->loadResourceProperty("IconView_IconPositionsLocked",
						    QVariant::fromValue(locked));
  if (!r) {
    qWarning()<<"Can't lock icon positions!";
    return;
  }
  pIconViewLockAction->setChecked(locked);
}




bool KLFLibDefaultView::searchFind(const QString& queryString, bool forward)
{
  QModelIndex i = pModel->searchFind(queryString, forward);
  pDelegate->setSearchString(queryString);
  searchFound(i);
  return i.isValid();
}

bool KLFLibDefaultView::searchFindNext(bool forward)
{
  QModelIndex i = pModel->searchFindNext(forward);
  searchFound(i);
  return i.isValid();
}

void KLFLibDefaultView::searchAbort()
{
  pDelegate->setSearchString(QString());
  pView->viewport()->update(); // repaint widget to update search underline

  // don't un-select the found index...
  //  searchFound(QModelIndex());
}

// private
void KLFLibDefaultView::searchFound(const QModelIndex& i)
{
  pDelegate->setSearchIndex(i);
  if ( ! i.isValid() ) {
    pView->scrollTo(QModelIndex(), QAbstractItemView::PositionAtTop);
    // unselect all
    pView->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::Clear);
    return;
  }
  pView->scrollTo(i, QAbstractItemView::EnsureVisible);
  if (pViewType == CategoryTreeView) {
    // if tree view, expand item
    qobject_cast<QTreeView*>(pView)->expand(i);
  }
  // select the item
  pView->selectionModel()->setCurrentIndex(i,
					   QItemSelectionModel::ClearAndSelect|
					   QItemSelectionModel::Rows);
  pView->update(i);
}


void KLFLibDefaultView::slotViewSelectionChanged(const QItemSelection& /*selected*/,
						 const QItemSelection& /*deselected*/)
{
  pView->viewport()->update();
  
  emit entriesSelected(selectedEntries());
}

void KLFLibDefaultView::slotResourceDataChanged(const QModelIndex& /*topleft*/,
						const QModelIndex& /*bottomRight*/)
{
  /** \todo ............. TODO ......... optimize here, don't do full refresh */
  emit resourceDataChanged(QList<KLFLib::entryId>());
}


QItemSelection KLFLibDefaultView::fixSelection(const QModelIndexList& selidx)
{
  QModelIndexList moreselidx;
  QItemSelection moresel;

  int k, j;
  for (k = 0; k < selidx.size(); ++k) {
    if (selidx[k].isValid() &&
	selidx[k].data(KLFLibModel::ItemKindItemRole).toInt() == KLFLibModel::CategoryLabelKind &&
	selidx[k].column() == 0) {
      // a category is selected -> select all its children
      const QAbstractItemModel *model = selidx[k].model();
      int nrows = model->rowCount(selidx[k]);
      int ncols = model->columnCount(selidx[k]);
      if (pViewType == CategoryTreeView) {
	// if tree view, expand item
	qobject_cast<QTreeView*>(pView)->expand(selidx[k]);
      }
      for (j = 0; j < nrows; ++j) {
	switch (selidx[k].child(j,0).data(KLFLibModel::ItemKindItemRole).toInt()) {
	case KLFLibModel::CategoryLabelKind:
	  moreselidx.append(selidx[k].child(j,0));
	  // no break; proceed to entrykind->
	case KLFLibModel::EntryKind:
	  moresel.append(QItemSelectionRange(selidx[k].child(j, 0),
					     selidx[k].child(j, ncols-1)));
	  break;
	default: ;
	}
	
      }
    }
  }

  if (moresel.isEmpty()) {
    return QItemSelection();
  }

  QItemSelection s = moresel;
  QItemSelection morefixed = fixSelection(moreselidx);
  s.merge(morefixed, QItemSelectionModel::Select);
  return s;

}

void KLFLibDefaultView::slotViewItemClicked(const QModelIndex& index)
{
  if (index.column() != 0)
    return;
  QItemSelection fixed = fixSelection( QModelIndexList() << index);
  pView->selectionModel()->select(fixed, QItemSelectionModel::Select);
}
void KLFLibDefaultView::slotEntryDoubleClicked(const QModelIndex& index)
{
  if (index.data(KLFLibModel::ItemKindItemRole).toInt() != KLFLibModel::EntryKind)
    return;
  emit requestRestore(index.data(KLFLibModel::FullEntryItemRole).value<KLFLibEntry>(),
		      KLFLib::RestoreLatex|KLFLib::RestoreStyle);
}

void KLFLibDefaultView::slotExpanded(const QModelIndex& index)
{
  pDelegate->setIndexExpanded(index, true);
}
void KLFLibDefaultView::slotCollapsed(const QModelIndex& index)
{
  pDelegate->setIndexExpanded(index, false);
}

void KLFLibDefaultView::slotShowColumnSenderAction(bool showCol)
{
  QObject *a = sender();
  if (a == NULL)
    return;

  if ( ! pView->inherits("QTreeView") )
    return;
  int colNo = a->property("klfModelColumn").toInt();
  qobject_cast<QTreeView*>(pView)->setColumnHidden(colNo, !showCol);
}




QModelIndexList KLFLibDefaultView::selectedEntryIndexes() const
{
  QModelIndexList selection = pView->selectionModel()->selectedIndexes();
  QModelIndexList entryindexes;
  int k;
  // walk selection and strip all non-zero column number indexes
  for (k = 0; k < selection.size(); ++k) {
    if (selection[k].column() > 0)
      continue;
    entryindexes << selection[k];
  }
  return entryindexes;
}



// ------------

static QStringList defaultViewTypeIds = QStringList()<<"default"<<"default+list"<<"default+icons";


KLFLibDefaultViewFactory::KLFLibDefaultViewFactory(QObject *parent)
  : KLFLibViewFactory(defaultViewTypeIds, parent)
{
}


QString KLFLibDefaultViewFactory::viewTypeTitle(const QString& viewTypeIdent) const
{
  if (viewTypeIdent == "default")
    return tr("Category Tree View");
  if (viewTypeIdent == "default+list")
    return tr("List View");
  if (viewTypeIdent == "default+icons")
    return tr("Icon View");

  return QString();
}


KLFAbstractLibView * KLFLibDefaultViewFactory::createLibView(const QString& viewTypeIdent,
							     QWidget *parent,
							     KLFLibResourceEngine *resourceEngine)
{
  KLFLibDefaultView::ViewType v = KLFLibDefaultView::CategoryTreeView;
  if (viewTypeIdent == "default")
    v = KLFLibDefaultView::CategoryTreeView;
  else if (viewTypeIdent == "default+list")
    v = KLFLibDefaultView::ListTreeView;
  else if (viewTypeIdent == "default+icons")
    v = KLFLibDefaultView::IconView;

  KLFLibDefaultView *view = new KLFLibDefaultView(parent, v);
  view->setResourceEngine(resourceEngine);
  return view;
}



// ------------


KLFLibOpenResourceDlg::KLFLibOpenResourceDlg(const QUrl& defaultlocation, QWidget *parent)
  : QDialog(parent)
{
  pUi = new Ui::KLFLibOpenResourceDlg;
  pUi->setupUi(this);

  // add a widget for all supported schemes
  QStringList schemeList = KLFLibEngineFactory::allSupportedSchemes();
  int k;
  for (k = 0; k < schemeList.size(); ++k) {
    KLFLibEngineFactory *factory
      = KLFLibEngineFactory::findFactoryFor(schemeList[k]);
    QWidget *openWidget = factory->createPromptUrlWidget(pUi->stkOpenWidgets, schemeList[k],
							 defaultlocation);
    pUi->stkOpenWidgets->insertWidget(k, openWidget);
    pUi->cbxType->insertItem(k, factory->schemeTitle(schemeList[k]),
			     QVariant::fromValue(schemeList[k]));

    connect(openWidget, SIGNAL(readyToOpen(bool)), this, SLOT(updateReadyToOpenFromSender(bool)));
  }

  QString defaultscheme = defaultlocation.scheme();
  if (defaultscheme.isEmpty()) {
    pUi->cbxType->setCurrentIndex(0);
    pUi->stkOpenWidgets->setCurrentIndex(0);
  } else {
    pUi->cbxType->setCurrentIndex(k = schemeList.indexOf(defaultscheme));
    pUi->stkOpenWidgets->setCurrentIndex(k);
  }

  btnGo = pUi->btnBar->button(QDialogButtonBox::Open);

  connect(pUi->cbxType, SIGNAL(activated(int)), this, SLOT(updateReadyToOpen()));
  updateReadyToOpen();
}

KLFLibOpenResourceDlg::~KLFLibOpenResourceDlg()
{
  delete pUi;
}

    
QUrl KLFLibOpenResourceDlg::url() const
{
  int k = pUi->cbxType->currentIndex();
  QString scheme = pUi->cbxType->itemData(k).toString();
  KLFLibEngineFactory *factory
    = KLFLibEngineFactory::findFactoryFor(scheme);
  QUrl url = factory->retrieveUrlFromWidget(scheme, pUi->stkOpenWidgets->widget(k));
  if (url.isEmpty()) {
    // empty url means cancel open
    return QUrl();
  }
  if (pUi->chkReadOnly->isChecked())
    url.addQueryItem("klfReadOnly", "true");
  qDebug()<<"Got URL: "<<url;
  return url;
}
 
void KLFLibOpenResourceDlg::updateReadyToOpenFromSender(bool isready)
{
  QObject *w = sender();
  w->setProperty("readyToOpen", isready);
  updateReadyToOpen();
}
void KLFLibOpenResourceDlg::updateReadyToOpen()
{
  QWidget *w = pUi->stkOpenWidgets->currentWidget();
  if (w == NULL) return;
  QVariant v = w->property("readyToOpen");
  // and update button enabled
  btnGo->setEnabled(!v.isValid() || v.toBool());
}


// static
QUrl KLFLibOpenResourceDlg::queryOpenResource(const QUrl& defaultlocation, QWidget *parent)
{
  KLFLibOpenResourceDlg dlg(defaultlocation, parent);
  int result = dlg.exec();
  if (result != QDialog::Accepted)
    return QUrl();
  QUrl url = dlg.url();
  return url;
}



// ---------------------


KLFLibCreateResourceDlg::KLFLibCreateResourceDlg(QWidget *parent)
  : QDialog(parent)
{
  pUi = new Ui::KLFLibOpenResourceDlg;
  pUi->setupUi(this);

  pUi->lblMain->setText(tr("Create New Library Resource", "[[dialog title]]"));
  setWindowTitle(tr("Create New Library Resource", "[[dialog title]]"));
  pUi->chkReadOnly->hide();

  pUi->btnBar->setStandardButtons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
  btnGo = pUi->btnBar->button(QDialogButtonBox::Save);

  // add a widget for all supported schemes
  QStringList schemeList = KLFLibEngineFactory::allSupportedSchemes();
  int k;
  for (k = 0; k < schemeList.size(); ++k) {
    KLFLibEngineFactory *factory
      = KLFLibEngineFactory::findFactoryFor(schemeList[k]);
    QWidget *createResWidget =
      factory->createPromptCreateParametersWidget(pUi->stkOpenWidgets, schemeList[k],
						  Parameters());
    pUi->stkOpenWidgets->insertWidget(k, createResWidget);
    pUi->cbxType->insertItem(k, factory->schemeTitle(schemeList[k]),
			     QVariant::fromValue(schemeList[k]));

    connect(createResWidget, SIGNAL(readyToCreate(bool)),
	    this, SLOT(updateReadyToCreateFromSender(bool)));
  }


  pUi->cbxType->setCurrentIndex(0);
  pUi->stkOpenWidgets->setCurrentIndex(0);

  connect(pUi->cbxType, SIGNAL(activated(int)), this, SLOT(updateReadyToCreate()));
  updateReadyToCreate();
}
KLFLibCreateResourceDlg::~KLFLibCreateResourceDlg()
{
  delete pUi;
}

KLFLibEngineFactory::Parameters KLFLibCreateResourceDlg::getCreateParameters() const
{
  int k = pUi->cbxType->currentIndex();
  QString scheme = pUi->cbxType->itemData(k).toString();
  KLFLibEngineFactory *factory
    = KLFLibEngineFactory::findFactoryFor(scheme);
  Parameters p = factory->retrieveCreateParametersFromWidget(scheme, pUi->stkOpenWidgets->widget(k));
  p["klfScheme"] = scheme;
  return p;
}


void KLFLibCreateResourceDlg::accept()
{
  const Parameters p = getCreateParameters();
  if (p == Parameters() || p["klfCancel"].toBool() == true) {
    QDialog::reject();
    return;
  }
  if (p["klfRetry"].toBool() == true)
    return; // ignore accept, reprompt user

  // then by default accept the event
  // set internal parameter cache to the given parameters, they
  // will be recycled by createResource() instead of being re-queried
  pParam = p;
  QDialog::accept();
}
void KLFLibCreateResourceDlg::reject()
{
  QDialog::reject();
}
 
void KLFLibCreateResourceDlg::updateReadyToCreateFromSender(bool isready)
{
  QObject *w = sender();
  w->setProperty("readyToCreate", isready);
  updateReadyToCreate();
}
void KLFLibCreateResourceDlg::updateReadyToCreate()
{
  QWidget *w = pUi->stkOpenWidgets->currentWidget();
  if (w == NULL) return;
  QVariant v = w->property("readyToCreate");
  // and update button enabled
  btnGo->setEnabled(!v.isValid() || v.toBool());
}


// static
KLFLibResourceEngine *KLFLibCreateResourceDlg::createResource(QObject *resourceParent, QWidget *parent)
{
  KLFLibCreateResourceDlg dlg(parent);
  int result = dlg.exec();
  if (result != QDialog::Accepted)
    return NULL;

  Parameters p = dlg.pParam; // we have access to this private member
  QString scheme = p["klfScheme"].toString();

  KLFLibEngineFactory * factory = KLFLibEngineFactory::findFactoryFor(scheme);
  KLF_ASSERT_NOT_NULL( factory ,
		       qPrintable(QString("Couldn't find factory for scheme %1 ?!?").arg(scheme)),
		       return NULL )
    ;

  KLFLibResourceEngine *resource = factory->createResource(scheme, p, resourceParent);
  return resource;
}


// ---------------------


KLFLibResPropEditor::KLFLibResPropEditor(KLFLibResourceEngine *res, QWidget *parent)
  : QWidget(parent)
{
  pUi = new Ui::KLFLibResPropEditor;
  pUi->setupUi(this);

  QPalette pal = pUi->txtUrl->palette();
  pal.setColor(QPalette::Base, pal.color(QPalette::Window));
  pal.setColor(QPalette::Background, pal.color(QPalette::Window));
  pUi->txtUrl->setPalette(pal);

  if (res == NULL)
    qWarning("KLFLibResPropEditor: NULL Resource! Expect CRASH!");

  pResource = res;

  connect(pResource, SIGNAL(resourcePropertyChanged(int)),
	  this, SLOT(slotResourcePropertyChanged(int)));

  pUi->frmAdvanced->setShown(pUi->btnAdvanced->isChecked());
  slotResourcePropertyChanged(-1);
}

KLFLibResPropEditor::~KLFLibResPropEditor()
{
  delete pUi;
}

bool KLFLibResPropEditor::apply()
{
  bool res = true;

  bool lockmodified = (pResource->locked() != pUi->chkLocked->isChecked());
  bool wantunlock = lockmodified && !pUi->chkLocked->isChecked();
  bool wantlock = lockmodified && !wantunlock;
  bool titlemodified = (pResource->title() != pUi->txtTitle->text());

  if ( pResource->locked() && pUi->chkLocked->isChecked() ) {
    // no intent to modify locked state of locked resource
    if (titlemodified) {
      QMessageBox::critical(this, tr("Error"), tr("Can't rename a locked resource!"));
      return false;
    } else {
      return true; // nothing to apply
    }
  }
  if (wantunlock) {
    if ( ! pResource->setLocked(false) ) { // unlock resource before other modifications
      res = false;
      QMessageBox::critical(this, tr("Error"), tr("Failed to unlock resource."));
    }
  }

  if ( titlemodified && ! pResource->setTitle(pUi->txtTitle->text()) ) {
    res = false;
    QMessageBox::critical(this, tr("Error"), tr("Failed to rename resource."));
  }

  if (wantlock) {
    if ( ! pResource->setLocked(true) ) { // unlock resource before other modifications
      res = false;
      QMessageBox::critical(this, tr("Error"), tr("Failed to lock resource."));
    }
  }

  return res;
}


void KLFLibResPropEditor::slotResourcePropertyChanged(int /*propId*/)
{
  // perform full update

  pUi->txtTitle->setText(pResource->title());
  pUi->txtTitle->setEnabled(pResource->canModifyProp(KLFLibResourceEngine::PropTitle));
  pUi->txtUrl->setText(pResource->url().toString());
  pUi->chkLocked->setChecked(pResource->locked());
  pUi->chkLocked->setEnabled(pResource->canModifyProp(KLFLibResourceEngine::PropLocked));
}



// -------------


KLFLibResPropEditorDlg::KLFLibResPropEditorDlg(KLFLibResourceEngine *resource, QWidget *parent)
  : QDialog(parent)
{
  QVBoxLayout *lyt = new QVBoxLayout(this);

  pEditor = new KLFLibResPropEditor(resource, this);
  lyt->addWidget(pEditor);

  QDialogButtonBox *btns = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Apply|
						QDialogButtonBox::Cancel, Qt::Horizontal, this);
  lyt->addWidget(btns);

  connect(btns->button(QDialogButtonBox::Ok), SIGNAL(clicked()),
	  this, SLOT(applyAndClose()));
  connect(btns->button(QDialogButtonBox::Apply), SIGNAL(clicked()),
	  pEditor, SLOT(apply()));
  connect(btns->button(QDialogButtonBox::Cancel), SIGNAL(clicked()),
	  this, SLOT(cancelAndClose()));
  
  setModal(false);
  setWindowTitle(pEditor->windowTitle());
  setAttribute(Qt::WA_DeleteOnClose, true);
}

KLFLibResPropEditorDlg::~KLFLibResPropEditorDlg()
{
}

void KLFLibResPropEditorDlg::applyAndClose()
{
  if (pEditor->apply()) {
    accept();
  }
}

void KLFLibResPropEditorDlg::cancelAndClose()
{
  reject();
}















// -------------------------------------------------------




#include <QMessageBox>
#include <QSqlDatabase>
#include <QTreeView>
#include <QListView>
#include <QTableView>
#include <QFile>

#include <klfbackend.h>
#include <klflibdbengine.h>
#include <klflibbrowser.h>


void klf___temp___test_newlib()
{
  KLF_ASSERT_NOT_NULL( ((void*)0) , "Test Case Assert is "<<"working!" ,  )
    ;
  KLF_ASSERT_NOT_NULL( ((void*)1) , "Test Case Assert is "<<"NOT"<<" working!" ,  )
    ;

  // initialize and register some library resource engine + view factories
  (void)new KLFLibDBEngineFactory(qApp);
  (void)new KLFLibDefaultViewFactory(qApp);

  QVariantMap vm;
  bool loadstate = QFile::exists("/home/philippe/temp/klf_saved_libbrowser_state");
  if (loadstate) {
    QFile f("/home/philippe/temp/klf_saved_libbrowser_state");
    f.open(QIODevice::ReadOnly);
    QDataStream str(&f);
    str >> vm;
  }

  KLFLibBrowser * w = new KLFLibBrowser(NULL);
  w->setAttribute(Qt::WA_DeleteOnClose, true);

  if (loadstate) {
    qDebug()<<"Loading saved state!";
    w->loadGuiState(vm);
  } else {
    qDebug()<<"Creating fresh Gui State!";
    w->openResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_sqlite_test?dataTableName=klfentries")),
		    KLFLibBrowser::NoCloseRoleFlag);
    w->openResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_another_resource.klf.db?dataTableName=klfentries")));
  }

  w->show();


  // create a new entry
  //   srand(time(NULL));
  //   KLFBackend::klfInput input;
  //   input.latex = "\\mathcal{"+QString()+QChar(rand()%26+65)+"}-"+QChar(rand()%26+97)+"";
  //   input.mathmode = "\\[ ... \\]";
  //   input.preamble = "";
  //   input.fg_color = qRgb(rand()%256, rand()%256, rand()%256);
  //   input.bg_color = qRgba(255, 255, 255, 0);
  //   input.dpi = 300;
  //   KLFBackend::klfSettings settings;
  //   settings.tempdir = "/tmp";
  //   settings.latexexec = "latex";
  //   settings.dvipsexec = "dvips";
  //   settings.gsexec = "gs";
  //   settings.epstopdfexec = "epstopdf";
  //   settings.lborderoffset = 1;
  //   settings.tborderoffset = 1;
  //   settings.rborderoffset = 1;
  //   settings.bborderoffset = 1;
  //  KLFBackend::klfOutput output = KLFBackend::getLatexFormula(input,settings);
  //  QImage preview = output.result.scaled(350, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  //  KLFLibEntry e(input.latex,QDateTime::currentDateTime(),preview,"Junk/Random Chars",input.latex[9]+QString("-")+input.latex[12],
  //  		KLFStyle(QString(), input.fg_color, input.bg_color, input.mathmode,
  //			 input.preamble, input.dpi));
  //  w->getOpenResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_sqlite_test")))
  //    ->insertEntry(e);

}



