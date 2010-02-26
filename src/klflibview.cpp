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

#include <stack>

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QString>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <QModelIndex>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QComboBox>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QListView>

#include <ui_klflibopenresourcedlg.h>
#include <ui_klflibrespropeditor.h>

#include "klflibview.h"

#include "klflibview_p.h"


template<class T>
const T& passingDebug(const T& t, QDebug& dbg)
{
  dbg << " : " << t;
  return t;
}



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
  connect(pResourceEngine, SIGNAL(dataChanged()), this, SLOT(updateResourceData()));
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

QList<KLFAbstractLibViewFactory*> KLFAbstractLibViewFactory::pRegisteredFactories =
	 QList<KLFAbstractLibViewFactory*>();


KLFAbstractLibViewFactory::KLFAbstractLibViewFactory(const QStringList& viewTypeIdentifiers,
						     QObject *parent)
  : QObject(parent), pViewTypeIdentifiers(viewTypeIdentifiers)
{
  registerFactory(this);
}
KLFAbstractLibViewFactory::~KLFAbstractLibViewFactory()
{
  unRegisterFactory(this);
}


QString KLFAbstractLibViewFactory::defaultViewTypeIdentifier()
{
  if (pRegisteredFactories.size() > 0)
    return pRegisteredFactories[0]->pViewTypeIdentifiers.first();
  return QString();
}

KLFAbstractLibViewFactory *KLFAbstractLibViewFactory::findFactoryFor(const QString& viewTypeIdentifier)
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

QStringList KLFAbstractLibViewFactory::allSupportedViewTypeIdentifiers()
{
  QStringList list;
  int k;
  for (k = 0; k < pRegisteredFactories.size(); ++k)
    list << pRegisteredFactories[k]->viewTypeIdentifiers();
  return list;
}


void KLFAbstractLibViewFactory::registerFactory(KLFAbstractLibViewFactory *factory)
{
  if (factory == NULL) {
    qWarning("KLFAbstractLibViewFactory::registerFactory: Attempt to register NULL factory!");
    return;
  }
  // WARNING: THIS FUNCTION IS CALLED FROM CONSTRUCTOR. NO VIRTUAL METHOD CALLS!
  if (factory->pViewTypeIdentifiers.size() == 0) {
    qWarning("KLFAbstractLibViewFactory::registerFactory: factory must provide at least one view type!");
    return;
  }
  if (pRegisteredFactories.indexOf(factory) != -1) // already registered
    return;
  pRegisteredFactories.append(factory);
}

void KLFAbstractLibViewFactory::unRegisterFactory(KLFAbstractLibViewFactory *factory)
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
  Node *p = getNodeForIndex(index);
  if (p == NULL)
    return QVariant();

  if (role == ItemKindItemRole)
    return (int)p->nodeKind();

  if (p->nodeKind() == EntryKind) {
    // --- GET ENTRY DATA ---
    EntryNode *ep = (EntryNode*) p;
    KLFLibEntry *entry = & ep->entry;

    if (role == Qt::ToolTipRole) // current contents
      role = entryItemRole(entryColumnContentsPropertyId(index.column()));

    if (role == EntryContentsTypeItemRole)
      return entryColumnContentsPropertyId(index.column());
    if (role == FullEntryItemRole)
      return QVariant::fromValue(*entry);

    if (role == entryItemRole(KLFLibEntry::Latex))
      return entry->latex();
    if (role == entryItemRole(KLFLibEntry::DateTime))
      return entry->dateTime();
    if (role == entryItemRole(KLFLibEntry::Preview))
      return QVariant::fromValue<QImage>(entry->preview());
    if (role == entryItemRole(KLFLibEntry::Category))
      return entry->category();
    if (role == entryItemRole(KLFLibEntry::Tags))
      return entry->tags();
    if (role == entryItemRole(KLFLibEntry::Style))
      return QVariant::fromValue(entry->style());
    // by default
    return QVariant();
  }
  else if (p->nodeKind() == CategoryLabelKind) {
    // --- GET CATEGORY LABEL DATA ---
    CategoryLabelNode *cp = (CategoryLabelNode*) p;
    if (role == Qt::ToolTipRole)
      return cp->fullCategoryPath;
    if (role == CategoryLabelItemRole)
      return cp->categoryLabel;
    if (role == FullCategoryPathItemRole)
      return cp->fullCategoryPath;
    // by default
    return QVariant();
  }

  // default
  return QVariant();
}
Qt::ItemFlags KLFLibModel::flags(const QModelIndex& index) const
{
  const Node * p = getNodeForIndex(index);
  if (p == NULL)
    return 0;
  if (p->nodeKind() == EntryKind)
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  if (p->nodeKind() == CategoryLabelKind)
    return Qt::ItemIsEnabled;

  // by default (should never happen)
  return 0;
}
bool KLFLibModel::hasChildren(const QModelIndex &parent) const
{
  const Node * p = getNodeForIndex(parent);
  if (p == NULL) {
    // invalid index -> interpret as root node
    p = pCategoryLabelCache.data() + 0;
  }

  return p->children.size() > 0;
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
  const CategoryLabelNode *p = pCategoryLabelCache.data() + 0; // root node
  if (parent.isValid()) {
    Node *pp = (Node*) parent.internalPointer();
    if (pp != NULL && pp->nodeKind() == CategoryLabelKind)
      p = (const CategoryLabelNode*)pp;
  }
  if (row < 0 || row >= p->children.size())
    return QModelIndex();
  return createIndexFromId(p->children[row], row, column);
}
QModelIndex KLFLibModel::parent(const QModelIndex &index) const
{
  Node *p = getNodeForIndex(index);
  if ( p == NULL ) // invalid index
    return QModelIndex();
  if ( ! p->parent.valid() ) // invalid parent (should never happen!)
    return QModelIndex();
  return createIndexFromId(p->parent, -1 /* figure out row */, 0);
}
int KLFLibModel::rowCount(const QModelIndex &parent) const
{
  const Node *p = getNodeForIndex(parent);
  if (p == NULL)
    p = pCategoryLabelCache.data() ; // ROOT NODE (index 0 in category label cache)

  return p->children.size();
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

void KLFLibModel::updateData()
{
  updateCacheSetupModel();
}

QModelIndex KLFLibModel::searchFind(const QString& queryString, bool forward)
{
  pSearchString = queryString;
  pSearchCurNode = NULL;
  return searchFindNext(forward);
}
QModelIndex KLFLibModel::searchFindNext(bool forward)
{
  if (pSearchString.isEmpty())
    return QModelIndex();

  // sanity check on searchcurptr: in case vectors have been re-alloced in the meantime
  if ( pSearchCurNode != NULL &&
       (pSearchCurNode < pCategoryLabelCache.data()+0 ||
	pSearchCurNode >= pCategoryLabelCache.data()+pCategoryLabelCache.size()) &&
       (pSearchCurNode < pEntryCache.data()+0 ||
	pSearchCurNode >= pEntryCache.data()+pEntryCache.size()) ) {
    // pointer is out of bounds; reset it
    qWarning("Search Pointer out of bounds! Forcing reset.");
    pSearchCurNode = NULL;
  }

  // search nodes
  Node * (KLFLibModel::*stepfunc)(Node *) = forward ? &KLFLibModel::nextNode : &KLFLibModel::prevNode;
  bool found = false;
  while ( ! found &&
	  (pSearchCurNode = (this->*stepfunc)(pSearchCurNode)) != NULL ) {
    if ( nodeValue(pSearchCurNode, KLFLibEntry::Latex).contains(pSearchString, Qt::CaseInsensitive) ||
    	 nodeValue(pSearchCurNode, KLFLibEntry::Tags).contains(pSearchString, Qt::CaseInsensitive) ) {
      found = true;
    }
  }
  if (found)
    return createIndexFromPtr(pSearchCurNode, -1, 0);
  return QModelIndex();
}

KLFLibModel::Node * KLFLibModel::nextNode(Node *n)
{
  if (n == NULL) {
    // start with root category (but don't return the root category itself)
    n = (pCategoryLabelCache.data()+0);
  }
  // walk children, if any
  if (n->children.size() > 0) {
    return getNode(n->children[0]);
  }
  // no children, find next sibling.
  Node * parent;
  while ( (parent = getNode(n->parent)) != NULL ) {
    int row = getNodeRow(n);
    if (row+1 < parent->children.size()) {
      // there is a next sibling: return it
      return getNode(parent->children[row+1]);
    }
    // no next sibling, so go up the tree
    n = parent;
  }
  // last entry passed
  return NULL;
}

KLFLibModel::Node * KLFLibModel::prevNode(Node *n)
{
  if (n == NULL) {
    // look for last node in tree.
    return lastNode(NULL); // NULL is accepted by lastNode.
  }
  // find previous sibling
  Node * parent = getNode(n->parent);
  int row = getNodeRow(n);
  if (row > 0) {
    // there is a previous sibling: find its last node
    return lastNode(getNode(parent->children[row-1]));
  }
  // there is no previous sibling: so we can return the parent
  // except if parent is the root node, in which case we return NULL.
  if (parent == pCategoryLabelCache.data()+0)
    return NULL;
  return parent;
}

KLFLibModel::Node * KLFLibModel::lastNode(Node *n)
{
  if (n == NULL)
    n = pCategoryLabelCache.data()+0;

  if (n->children.size() == 0)
    return n; // no children: n is itself the "last node"

  // children: return last node of last child.
  return lastNode(getNode(n->children[n->children.size()-1]));
}


// private
QList<KLFLibResourceEngine::entryId> KLFLibModel::entryIdList(const QModelIndexList& indexlist) const
{
  QList<KLFLibResourceEngine::entryId> idList;
  int k;
  QList<EntryNode*> nodePtrs;
  // walk all indexes and get their IDs
  for (k = 0; k < indexlist.size(); ++k) {
    Node *ptr = getNodeForIndex(indexlist[k]);
    if ( ptr == NULL )
      continue;
    if (ptr->nodeKind() != EntryKind)
      continue;
    EntryNode *n = (EntryNode*)ptr;
    if ( nodePtrs.contains(n) ) // duplicate, probably for another column
      continue;
    nodePtrs << n;
    idList << n->entryid;
  }
  return idList;
}

bool KLFLibModel::changeEntries(const QModelIndexList& indexlist, int propId, const QVariant& value)
{
  if ( indexlist.size() == 0 )
    return true; // no change...

  QList<KLFLibResourceEngine::entryId> idList = entryIdList(indexlist);
  if ( pResource->changeEntries(idList, QList<int>() << propId, QList<QVariant>() << value) ) {
    //    for (k = 0; k < nodePtrs.size(); ++k)
    //      nodePtrs[k]->entry.setProperty(propId, value);
    updateCacheSetupModel();
    return true;
  }
  return false;
}

bool KLFLibModel::insertEntries(const KLFLibEntryList& entries)
{
  if (entries.size() == 0)
    return true; // nothing to do...

  QList<KLFLibResourceEngine::entryId> list = pResource->insertEntries(entries);
  updateCacheSetupModel();

  if ( list.size() == 0 || list.contains(-1) )
    return false; // error for at least one entry
  return true;
}

bool KLFLibModel::deleteEntries(const QModelIndexList& indexlist)
{
  if ( indexlist.size() == 0 )
    return true; // no change...

  QList<KLFLibResourceEngine::entryId> idList = entryIdList(indexlist);
  if ( pResource->deleteEntries(idList) ) {
    updateCacheSetupModel();
    return true;
  }
  return false;
}

QString KLFLibModel::nodeValue(Node *ptr, int entryProperty)
{
  // return an internal string representation of the value of the property 'entryProperty' in node ptr.
  // or the category title if node is a category
  if (ptr == NULL)
    return QString();
  int kind = ptr->nodeKind();
  if (kind == EntryKind) {
    if (entryProperty == KLFLibEntry::Preview)
      entryProperty = KLFLibEntry::DateTime; // user friendliness. sort by date when selecting preview.

    EntryNode *eptr = (EntryNode*)ptr;
    if (entryProperty == KLFLibEntry::DateTime) {
      return eptr->entry.property(KLFLibEntry::DateTime).toDateTime().toString("yyyy-MM-dd+hh:mm:ss.zzz");
    } else {
      return ((EntryNode*)ptr)->entry.property(entryProperty).toString();
    }
  }
  if (kind == CategoryLabelKind)
    return ((CategoryLabelNode*)ptr)->categoryLabel;

  qWarning("nodeValue(): Bad Item Kind: %d", kind);
  return QString();
}

bool KLFLibModel::KLFLibModelSorter::operator()(const NodeId& a, const NodeId& b)
{
  Node *pa = model->getNode(a);
  Node *pb = model->getNode(b);
  if ( ! pa || ! pb )
    return false;

  if ( groupCategories ) {
    if ( pa->nodeKind() != EntryKind && pb->nodeKind() == EntryKind ) {
      // category kind always smaller than entry kind in category grouping mode
      return true;
    } else if ( pa->nodeKind() == EntryKind && pb->nodeKind() != EntryKind ) {
      return false;
    }
    if ( pa->nodeKind() != EntryKind || pb->nodeKind() != EntryKind ) {
      if ( ! (pa->nodeKind() == CategoryLabelKind && pb->nodeKind() == CategoryLabelKind) ) {
	qWarning("KLFLibModelSorter::operator(): Bad node kinds!! a: %d and b: %d",
		 pa->nodeKind(), pb->nodeKind());
	return false;
      }
      // when grouping sub-categories, always sort ascending
      return QString::localeAwareCompare(((CategoryLabelNode*)pa)->categoryLabel,
					 ((CategoryLabelNode*)pb)->categoryLabel)  < 0;
    }
  }

  QString nodevaluea = model->nodeValue(pa, entryProp);
  QString nodevalueb = model->nodeValue(pb, entryProp);
  return sortOrderFactor*QString::localeAwareCompare(nodevaluea, nodevalueb) < 0;
}

// private
void KLFLibModel::sortCategory(CategoryLabelNode *category, int column, Qt::SortOrder order)
{
  int k;
  // sort this category's children

  // This works both in LinearList and in CategoryTree display types. (In LinearList
  // display type, call this function on root category node)

  KLFLibModelSorter s =
    KLFLibModelSorter(this, entryColumnContentsPropertyId(column), order, pFlavorFlags & GroupSubCategories);

  qSort(category->children.begin(), category->children.end(), s);

  // and sort all children's children
  for (k = 0; k < category->children.size(); ++k)
    if (category->children[k].kind == CategoryLabelKind)
      sortCategory((CategoryLabelNode*)getNode(category->children[k]), column, order);

}
void KLFLibModel::sort(int column, Qt::SortOrder order)
{
  // first notify anyone who may be interested
  emit layoutAboutToBeChanged();
  // and then sort our model
  sortCategory( pCategoryLabelCache.data()+0, // root node
		column, order );
  // change relevant persistent indexes
  QModelIndexList pindexes = persistentIndexList();
  int k;
  for (k = 0; k < pindexes.size(); ++k) {
    QModelIndex index = createIndexFromPtr((Node*)pindexes[k].internalPointer(), -1, pindexes[k].column());
    changePersistentIndex(pindexes[k], index);
  }
  // and notify of layout change
  emit layoutChanged();
  lastSortColumn = column;
  lastSortOrder = order;
}



KLFLibModel::Node * KLFLibModel::getNodeForIndex(const QModelIndex& index) const
{
  if ( ! index.isValid() )
    return NULL;
  return (Node *) index.internalPointer();
}
KLFLibModel::Node * KLFLibModel::getNode(NodeId nodeid) const
{
  if (nodeid.kind == EntryKind) {
    if (nodeid.index < 0 || nodeid.index >= pEntryCache.size())
      return NULL;
    return (Node*) (pEntryCache.data() + nodeid.index);
  } else if (nodeid.kind == CategoryLabelKind) {
    if (nodeid.index < 0 || nodeid.index >= pCategoryLabelCache.size())
      return NULL;
    return (Node*) (pCategoryLabelCache.data() + nodeid.index);
  }
  qWarning("KLFLibModel::getNode(): Invalid kind: %d (index=%d)\n", nodeid.kind, nodeid.index);
  return NULL;
}
KLFLibModel::NodeId KLFLibModel::getNodeId(Node *node) const
{
  if (node == NULL)
    return NodeId();

  NodeId id;
  id.kind = (ItemKind)node->nodeKind();
  if (id.kind == EntryKind)
    id.index =  ( (EntryNode*)node - pEntryCache.data() );
  else if (id.kind == CategoryLabelKind)
    id.index =  ( (CategoryLabelNode*)node - pCategoryLabelCache.data() );
  else
    qWarning("KLFLibModel::getNodeId(%p): bad node kind `%d'!", (void*)node, id.kind);

  return id;
}

int KLFLibModel::getNodeRow(NodeId nodeid, Node *nodeptr) const
{
  if (nodeptr == NULL)
    nodeptr = getNode(nodeid);
  if ( ! nodeid.valid() || nodeptr == NULL)
    return -1;

  NodeId pparentid = nodeptr->parent;
  Node *pparent = getNode(pparentid);
  if (  pparent == NULL ) {
    // this is root element
    return 0;
  }
  // find child in parent
  int k;
  for (k = 0; k < pparent->children.size(); ++k)
    if (pparent->children[k].kind == nodeid.kind &&
	pparent->children[k].index == nodeid.index)
      return k;

  // search failed
  qWarning("KLFLibModel: Unable to get node row!!");
  return -1;
}

QModelIndex KLFLibModel::createIndexFromId(NodeId nodeid, int row, int column) const
{
  if ( ! nodeid.valid() )
    return QModelIndex();
  if (nodeid.kind == CategoryLabelKind && nodeid.index == 0) // root node
    return QModelIndex();

  Node *p = getNode(nodeid);
  void *intp = (void*) p;
  // if row is -1, then we need to find the row of the item
  if ( row < 0 ) {
    row = getNodeRow(nodeid);
    if ( row < 0 ) {
      qWarning("Unable to find row of item (kind=%d,index=%d)", (int)nodeid.kind, (int)nodeid.index);
    }
  }
  return createIndex(row, column, intp);
}
QModelIndex KLFLibModel::createIndexFromPtr(Node *n, int row, int column) const
{
  return createIndexFromId(getNodeId(n), row, column);
}
  
void KLFLibModel::updateCacheSetupModel()
{
  int k;
  //  qDebug("updateCacheSetupModel(); pFlavorFlags=%#010x", (uint)pFlavorFlags);

  emit layoutAboutToBeChanged();

  // save persistent indexes
  QModelIndexList persistentIndexes = persistentIndexList();
  QList<PersistentId> persistentIndexIds;
  for (k = 0; k < persistentIndexes.size(); ++k) {
    PersistentId id;
    Node * n = getNodeForIndex(persistentIndexes[k]);
    if (n == NULL) {
      id.kind = (ItemKind)-1;
    } else {
      id.kind = (ItemKind)n->nodeKind();
      if (id.kind == EntryKind)
	id.entry_id = ((EntryNode*)n)->entryid;
      else if (id.kind == CategoryLabelKind)
	id.categorylabel_fullpath = ((CategoryLabelNode*)n)->fullCategoryPath;
      else
	qWarning("KLFLibModel::updateCacheSetupModel: Bad Node kind: %d!!", id.kind);
    }
    persistentIndexIds << id;
  }

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

  QModelIndexList newPersistentIndexes;
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
	index = createIndexFromId(NodeId(EntryKind, j), -1, persistentIndexes[k].column());
    } else if (id.kind == CategoryLabelKind) {
      // walk entry cache to find this item
      for (j = 0; j < pCategoryLabelCache.size() &&
	     pCategoryLabelCache[j].fullCategoryPath != id.categorylabel_fullpath; ++j)
	;
      if (j < pCategoryLabelCache.size())
	index = createIndexFromId(NodeId(CategoryLabelKind, j), -1, persistentIndexes[k].column());
    } else {
      qWarning("updateCacheSetupModel: bad persistent id node kind! :%d", id.kind);
    }

    newPersistentIndexes << index;
  }

  changePersistentIndexList(persistentIndexes, newPersistentIndexes);

  //  dumpNodeTree(pCategoryLabelCache.data()+0); // DEBUG

  emit dataChanged(QModelIndex(),QModelIndex());
  emit layoutChanged();
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



void KLFLibModel::dumpNodeTree(Node *node, int indent) const
{
  QString sindent; sindent.fill(' ', indent);

  if (node == NULL)
    qDebug() << sindent << "(NULL)";

  EntryNode *en;
  CategoryLabelNode *cn;
  switch (node->nodeKind()) {
  case EntryKind:
    en = (EntryNode*)node;
    qDebug() << sindent << "EntryNode(tags=" << en->entry.tags() <<";latex="<<en->entry.latex()<<")";
    break;
  case CategoryLabelKind:
    cn = (CategoryLabelNode*)node;
    qDebug() << sindent << "CategoryNode(categoryLabel=" << cn->categoryLabel << "; fullcatpath="
	       << cn->fullCategoryPath << ")";
    break;
  default:
    qDebug() << sindent << "InvalidKindNode(kind="<<node->nodeKind()<<")";
  }

  if (node == NULL)
    return;
  int k;
  for (k = 0; k < node->children.size(); ++k) {
    dumpNodeTree(getNode(node->children[k]), indent+4);
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
static QImage transparentify_image(const QImage& img, qreal factor)
{
  QImage img2 = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  int k, j;
  for (k = 0; k < img.height(); ++k) {
    for (j = 0; j < img.width(); ++j) {
      QRgb c = img2.pixel(j, k);
      img2.setPixel(j, k, qRgba(qRed(c),qGreen(c),qBlue(c),(int)(factor*qAlpha(c))));
    }
  }
  return img2;
}
void KLFLibViewDelegate::paint(QPainter *painter, const QStyleOptionViewItem& option,
			       const QModelIndex& index) const
{
  PaintPrivate pp;
  pp.p = painter;
  pp.option = &option;
  pp.innerRectImage = QRect(option.rect.topLeft()+QPoint(2,2), option.rect.size()-QSize(4,4));
  pp.innerRectText = QRect(option.rect.topLeft()+QPoint(4,2), option.rect.size()-QSize(8,4));

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
  if (1) //indexHasSelectedChild(index))
    fl |= PTF_SelUnderline;

  // paint Category Label
  paintText(p, index.data(KLFLibModel::CategoryLabelItemRole).toString(), fl);
}

QDebug& operator<<(QDebug& d, const KLFLibViewDelegate::ColorRegion& c)
{
  return d << "ColorRegion["<<c.start<<"->+"<<c.len<<"]";
}

void KLFLibViewDelegate::paintText(PaintPrivate *p, const QString& text, uint flags) const
{
  if ( pSearchString.isEmpty() || !(flags&PTF_HighlightSearch) ||
       text.indexOf(pSearchString, 0, Qt::CaseInsensitive) == -1 ) {
    // no formatting required, use fast method.
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
    textDocument.setDefaultFont(qApp->font());
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

    //     QString html = 
    //       "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\""
    //       " \"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
    //       "<html><head><meta name=\"qrichtext\" content=\"1\" /><style type=\"text/css\">\n"
    //       "p, li { white-space: nowrap; }\n"
    //       "</style></head><body style=\"\">\n"
    //       "<p style=\"margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; "
    //       "-qt-block-indent:0; text-indent:0px;\">"+Qt::escape(text)+"</p></body></html>";
    //     textDocument.setHtml(html);
    QSizeF s = textDocument.size();
    if (s.width() > p->innerRectText.width()) { // sorry, too large
      s.setWidth(p->innerRectText.width());
      // draw small arrow indicating more text
      QColor c = p->option->palette.color(QPalette::Text);
      c.setAlpha(80);
      p->p->save();
      p->p->translate(p->innerRectText.right(), p->innerRectText.bottom()-2);
      p->p->setPen(c);
      p->p->drawLine(0, 0, -16, 0);
      p->p->drawLine(0, 0, -2, +2);
      p->p->restore();
    }
    p->p->save();
    p->p->setClipRect(p->innerRectText);
    p->p->translate(p->innerRectText.topLeft());
    p->p->translate( -4, 0 ); // some offset because of qt's rich text drawing ..?
    p->p->translate( QPointF( 0, //p->innerRectText.width() - s.width(),
			      p->innerRectText.height() - s.height()) / 2.f );
    textDocument.drawContents(p->p);
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
QSize KLFLibViewDelegate::sizeHint(const QStyleOptionViewItem& /*option*/, const QModelIndex& index) const
{
  int kind = index.data(KLFLibModel::ItemKindItemRole).toInt();
  if (kind == KLFLibModel::EntryKind) {
    int prop = -1;
    switch (index.data(KLFLibModel::EntryContentsTypeItemRole).toInt()) {
    case KLFLibEntry::Latex: prop = KLFLibEntry::Latex;
    case KLFLibEntry::Category: prop = (prop > 0) ? prop : KLFLibEntry::Category;
    case KLFLibEntry::Tags: prop = (prop > 0) ? prop : KLFLibEntry::Tags;
      return QFontMetrics(qApp->font())
	.size(0, index.data(KLFLibModel::entryItemRole(prop)).toString())+QSize(4,2);
    case KLFLibEntry::DateTime:
      return QFontMetrics(qApp->font())
	.size(0, index.data(KLFLibModel::entryItemRole(KLFLibEntry::DateTime)).toDateTime()
	      .toString(Qt::DefaultLocaleLongDate) )+QSize(4,2);
    case KLFLibEntry::Preview:
      return index.data(KLFLibModel::entryItemRole(KLFLibEntry::Preview)).value<QImage>().size()+QSize(4,4);
    default:
      return QSize();
    }
  } else if (kind == KLFLibModel::CategoryLabelKind) {
    return QFontMetrics(qApp->font())
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



bool KLFLibViewDelegate::indexHasSelectedChild(const QModelIndex& index) const
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

  pStateMousePress = false;

  QVBoxLayout *lyt = new QVBoxLayout(this);
  lyt->setMargin(0);
  lyt->setSpacing(0);
  pDelegate = new KLFLibViewDelegate(this);

  QTreeView *treeView = NULL;
  QListView *listView = NULL;
  switch (pViewType) {
  case IconView:
    listView = new QListView(this);
    listView->setViewMode(QListView::IconMode);
    listView->setSpacing(15);
    listView->setResizeMode(QListView::Adjust);
    pView = listView;
    break;
  case CategoryTreeView:
  case ListTreeView:
  default:
    treeView = new QTreeView(this);
    treeView->setSortingEnabled(true);
    treeView->setIndentation(16);
    pView = treeView;
    break;
  };

  lyt->addWidget(pView);

  pView->setSelectionBehavior(QAbstractItemView::SelectRows);
  pView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  pView->setItemDelegate(pDelegate);
  pView->viewport()->installEventFilter(this);

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
    //    qDebug() << "selection list: adding item [latex="<<selectedindexes[k].data(KLFLibModel::FullEntryItemRole).value<KLFLibEntry>().latex()<<"]";
    elist << selectedindexes[k].data(KLFLibModel::FullEntryItemRole).value<KLFLibEntry>();
  }
  return elist;
}

QList<QAction*> KLFLibDefaultView::addContextMenuActions(const QPoint& /*pos*/)
{
  /// \todo SELECT ALL ...............TODO....................

  return pShowColumnActions;
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

  // delegate wants to know more about selections...
  pDelegate->setSelectionModel(s);

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
    for (col = 0; col < pModel->columnCount(); ++col) {
      QString title = pModel->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
      QAction *a;
      a = new QAction(title, this);
      a->setProperty("klfModelColumn", col);
      a->setCheckable(true);
      a->setChecked(!treeView->isColumnHidden(col));
      connect(a, SIGNAL(toggled(bool)), this, SLOT(slotShowColumnSenderAction(bool)));
      pShowColumnActions << a;
    }
    // expand root items if they contain little number of children
    for (k = 0; k < pModel->rowCount(QModelIndex()); ++k) {
      QModelIndex i = pModel->index(k, 0, QModelIndex());
      if (pModel->rowCount(i) < 6)
	treeView->expand(i);
    }
  }

  wantMoreCategorySuggestions();
}

void KLFLibDefaultView::updateResourceData()
{
  pModel->updateData();
}
void KLFLibDefaultView::updateResourceProp()
{
  // nothing to do
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
  pView->repaint(); // repaint widget to update search underline

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
  pView->repaint();
}


void KLFLibDefaultView::slotViewSelectionChanged(const QItemSelection& /*selected*/,
						 const QItemSelection& /*deselected*/)
{
  emit entriesSelected(selectedEntries());
}

QItemSelection KLFLibDefaultView::fixSelection(const QModelIndexList& selidx)
{
  // Callers of this function garantee that no signals will be emitted upon changing the
  // selection.

  //  qDebug()<<"KLFLibDef.View::fixSelection("<<selidx<<",isbasesel="<<isbasesel<<")";

  QModelIndexList moreselidx;
  QItemSelection moresel;

  int k, j;
  for (k = 0; k < selidx.size(); ++k) {
    //    qDebug()<<"testing "<<k<<"th index "<<selidx[k];
    if (selidx[k].isValid() &&
	selidx[k].data(KLFLibModel::ItemKindItemRole).toInt() == KLFLibModel::CategoryLabelKind &&
	selidx[k].column() == 0) {
      //      qDebug()<<"is a category!";
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
      //      moresel.append(QItemSelectionRange(selidx[k].child(0, 0), selidx[k].child(nrows-1, ncols-1)));
      //      pTreeView->selectionModel()->select(QItemSelection(selidx[k].child(0,0),
      //							 selidx[k].child(nrows-1,ncols-1)),
      //					  QItemSelectionModel::Select);
    }
  }

  //  qDebug()<<"moresel is "<<moresel;

  if (moresel.isEmpty()) {
    //    qDebug()<<"No moresel.";
    return QItemSelection();
  }

  //  return moresel;

  QItemSelection s = moresel;
  QItemSelection morefixed = fixSelection(moreselidx);
  //  qDebug()<<"## before merge: selection is "<<s;
  //  qDebug()<<"more="<<moresel<<"; more/fixed="<<morefixed;
  s.merge(morefixed, QItemSelectionModel::Select);
  //  qDebug()<<"merged: selection is "<<s;
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
  : KLFAbstractLibViewFactory(defaultViewTypeIds, parent)
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
  QStringList schemeList = KLFAbstractLibEngineFactory::allSupportedSchemes();
  int k;
  for (k = 0; k < schemeList.size(); ++k) {
    KLFAbstractLibEngineFactory *factory
      = KLFAbstractLibEngineFactory::findFactoryFor(schemeList[k]);
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
  KLFAbstractLibEngineFactory *factory
    = KLFAbstractLibEngineFactory::findFactoryFor(scheme);
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
  QStringList schemeList = KLFAbstractLibEngineFactory::allSupportedSchemes();
  int k;
  for (k = 0; k < schemeList.size(); ++k) {
    KLFAbstractLibEngineFactory *factory
      = KLFAbstractLibEngineFactory::findFactoryFor(schemeList[k]);
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

KLFAbstractLibEngineFactory::Parameters KLFLibCreateResourceDlg::getCreateParameters() const
{
  int k = pUi->cbxType->currentIndex();
  QString scheme = pUi->cbxType->itemData(k).toString();
  KLFAbstractLibEngineFactory *factory
    = KLFAbstractLibEngineFactory::findFactoryFor(scheme);
  Parameters p = factory->retrieveCreateParametersFromWidget(scheme, pUi->stkOpenWidgets->widget(k));
  p["klfScheme"] = scheme;
  return p;
}


void KLFLibCreateResourceDlg::accept()
{
  const Parameters p = getCreateParameters();
  if (p == Parameters() || p["cancel"].toBool() == true) {
    QDialog::reject();
    return;
  }
  if (p["retry"].toBool() == true)
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

  KLFAbstractLibEngineFactory * factory = KLFAbstractLibEngineFactory::findFactoryFor(scheme);
  if (factory == NULL) {
    qWarning()<<"Couldn't find factory for scheme "<<scheme<<" ?!?";
    return NULL;
  }

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

  pUi->txtTitle->setText(res->title());
  pUi->txtUrl->setText(res->url().toString());
  pUi->chkLocked->setChecked(res->locked());

  qDebug()<<"res->supp.F.Flags()="<<res->supportedFeatureFlags();
  if ( !(res->supportedFeatureFlags() & KLFLibResourceEngine::FeatureLocked) ) {
    pUi->chkLocked->setEnabled(false);
  }

  pUi->frmAdvanced->setShown(pUi->btnAdvanced->isChecked());
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

// --

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

#include <klfbackend.h>
#include <klflibbrowser.h>


void klf___temp___test_newlib()
{

  // initialize and register some factories
  (void)new KLFLibDBEngineFactory(qApp);
  (void)new KLFLibDefaultViewFactory(qApp);

  /*
    QUrl url("klf+sqlite:///home/philippe/temp/klf_sqlite_test");
    KLFAbstractLibEngineFactory *factory = KLFAbstractLibEngineFactory::findFactoryFor(url.scheme());
    qDebug("%s: Got factory: %p", qPrintable(url.toString()), factory);
    KLFLibResourceEngine * resource = factory->openResource(url);
    qDebug("%s: Got resource: %p", qPrintable(url.toString()), resource);
    
    qDebug()<<"All entries: "<< resource->allEntries();
    
    KLFAbstractLibViewFactory *viewfactory =
    KLFAbstractLibViewFactory::findFactoryFor(resource->suggestedViewTypeIdentifier());
    qDebug("Got view factory: %p", viewfactory);
    KLFAbstractLibView * view = viewfactory->createLibView(NULL, resource);
    qDebug("Got view: %p", view);
    
    view->resize(800,600);
    view->show();*/
  KLFLibBrowser * w = new KLFLibBrowser(NULL);
  w->setAttribute(Qt::WA_DeleteOnClose, true);
  w->openResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_sqlite_test")),
		  KLFLibBrowser::NoCloseRoleFlag);
  w->openResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_another_resource.klf.db")));

  w->show();


  // create a new entry
  srand(time(NULL));
  KLFBackend::klfInput input;
  input.latex = "\\mathcal{"+QString()+QChar(rand()%26+65)+"}-"+QChar(rand()%26+97)+"";
  input.mathmode = "\\[ ... \\]";
  input.preamble = "";
  input.fg_color = qRgb(rand()%256, rand()%256, rand()%256);
  input.bg_color = qRgba(255, 255, 255, 0);
  input.dpi = 300;
  KLFBackend::klfSettings settings;
  settings.tempdir = "/tmp";
  settings.latexexec = "latex";
  settings.dvipsexec = "dvips";
  settings.gsexec = "gs";
  settings.epstopdfexec = "epstopdf";
  settings.lborderoffset = 1;
  settings.tborderoffset = 1;
  settings.rborderoffset = 1;
  settings.bborderoffset = 1;
  //  KLFBackend::klfOutput output = KLFBackend::getLatexFormula(input,settings);
  //  QImage preview = output.result.scaled(350, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  //  KLFLibEntry e(input.latex,QDateTime::currentDateTime(),preview,"Junk/Random Chars",input.latex[9]+QString("-")+input.latex[12],
  //  		KLFStyle(QString(), input.fg_color, input.bg_color, input.mathmode,
  //			 input.preamble, input.dpi));
  //  w->getOpenResource(QUrl(QLatin1String("klf+sqlite:///home/philippe/temp/klf_sqlite_test")))
  //    ->insertEntry(e);


  /*
  view = new QTableView(0);
  delegate = new KLFLibViewDelegate(view);
  view->setModel(model);
  view->setItemDelegate(delegate);
  view->show();
  view->resize(800,600);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  ((QTableView*)view)->resizeColumnsToContents();
  ((QTableView*)view)->resizeRowsToContents();

  view = new QTreeView(0);
  delegate = new KLFLibViewDelegate(view);
  view->setModel(model);
  view->setItemDelegate(delegate);
  view->resize(800,600);
  view->show();
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  ((QTreeView*)view)->setSortingEnabled(true);
  ((QTreeView*)view)->resizeColumnToContents(0);
  ((QTreeView*)view)->resizeColumnToContents(1);
  ((QTreeView*)view)->resizeColumnToContents(2);
  ((QTreeView*)view)->resizeColumnToContents(3);

  view = new QListView(0);
  delegate = new KLFLibViewDelegate(view);
  view->setModel(model);
  view->setItemDelegate(delegate);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->show();
  view->resize(800,600);

  view = new QListView(0);
  delegate = new KLFLibViewDelegate(view);
  view->setModel(model);
  view->setItemDelegate(delegate);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  ((QListView*)view)->setViewMode(QListView::IconMode);
  ((QListView*)view)->setSpacing(15);
  view->show();
  view->resize(800,600);
  */
}



