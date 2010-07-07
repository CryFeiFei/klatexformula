/***************************************************************************
 *   file klflib_p.h
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

/** \file
 * This header contains (in principle private) auxiliary classes for
 * library routines defined in klflib.cpp */

#ifndef KLFLIB_P_H
#define KLFLIB_P_H

#include "klflib.h"



/** \internal */
class KLFLibEntryMimeEncoder : public KLFAbstractLibEntryMimeEncoder
{
public:
  KLFLibEntryMimeEncoder() { }
  virtual ~KLFLibEntryMimeEncoder() { }

  virtual QStringList supportedEncodingMimeTypes() const
  {
    return QStringList() << "application/x-klf-libentries"
			 << "text/html"
			 << "text/plain"
			 << "image/png";
  }
  virtual QStringList supportedDecodingMimeTypes() const
  {
    return QStringList() << "application/x-klf-libentries";
  }

  virtual QByteArray encodeMime(const KLFLibEntryList& entryList, const QVariantMap& metaData,
				const QString& mimeType) const
  {
    int k;
    QByteArray data;
    if (mimeType == "application/x-klf-libentries") {
      // prepare the data through the stream in a separate block
      { QDataStream str(&data, QIODevice::WriteOnly);
	str.setVersion(QDataStream::Qt_4_4);
	// now dump the list in the bytearray
	str << metaData << entryList;
      }
      // now the data is prepared, return it
      return data;
    }
    if (mimeType == "text/html") {
      // prepare the data through the stream in a separate block
      { QTextStream htmlstr(&data, QIODevice::WriteOnly);
	// a header for HTML
	htmlstr
	  << "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n"
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
	KLFLibEntry de; // dummy entry for property querying
	QList<int> entryprops = de.registeredPropertyIdList();
	for (k = 0; k < entryList.size(); ++k) {
	  htmlstr << entryList[k].toString(KLFLibEntry::ToStringUseHtml);
	}
	// HTML footer
	htmlstr << "\n"
		<< "  </body>\n"
		<< "</html>\n";
      }
      return data;
    }
    if (mimeType == "text/plain") {
      // prepare the data through the stream in a separate block
      { QTextStream textstr(&data, QIODevice::WriteOnly);
	// a header for text
	textstr << " *** KLFLibEntryList ***\n\n";
	// now dump the data in the appropriate streams
	KLFLibEntry de; // dummy entry for property querying
	QList<int> entryprops = de.registeredPropertyIdList();
	for (k = 0; k < entryList.size(); ++k) {
	  textstr << entryList[k].toString(/*KLFLibEntry::ToStringQuoteValues*/);
	}
      }
      return data;
    }
    if (mimeType == "image/png") {
      if (entryList.size() != 1) {
	klfDbg("Can only encode image/png for an entry list of size ONE (!)");
	return QByteArray();
      }
      { // format data: write the PNG data for the preview
	QBuffer buf(&data);
	entryList[0].preview().save(&buf, "PNG");
      }
      return data;
    }
    qWarning()<<KLF_FUNC_NAME<<": unsupported mime type requested: "<<mimeType;
    return QByteArray();
  }

  virtual bool decodeMime(const QByteArray& data, const QString& mimeType,
			  KLFLibEntryList *entryList, QVariantMap *metaData) const
  {
    if (mimeType == "application/x-klf-libentries") {
      QDataStream str(data);
      str.setVersion(QDataStream::Qt_4_4);
      str >> *metaData >> *entryList;
      return true;
    }
    qWarning()<<KLF_FUNC_NAME<<": Unsupported mime type: "<<mimeType;
    return false;
  }

};





/** \page appxMimeLib Appendix: KLF's Own Mime Formats for Library Entries
 *
 * \section appxMimeLib_elist The application/x-klf-libentries data format
 *
 * Is basically a Qt-\ref QDataStream saved data of a \ref KLFLibEntryList (ie. a
 * \ref QList "QList<KLFLibEntry>"), saved with Qt \ref QDataStream version "QDataStream::Qt_4_4" .
 *
 * There is also a property map for including arbitrary parameters to the list (eg.
 * originating URL, etc.), stored in a QVariantMap, ie a \ref QMap "QMap<QString,QVariant>". Standard
 * properties as of now:
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
 * Other example: see the source code of \ref KLFLibModel::mimeData() in file
 * <a href="klflibview_8cpp.html">klfview.cpp</a> .
 *
 * \section appxMimeLib_internal The INTERNAL <tt>application/x-klf-internal-lib-move-entries</tt>
 * * Used for internal move within the same resource only.
 * * Basic difference with <tt>application/x-klf-libentries</tt>: the latter contains
 *   only the IDs of the entries (for reference for deletion for example) and the url
 *   of the open resource for identification.
 * * Internal Format: \ref QDataStream, stream version QDataStream::Qt_4_4, in the
 *   following order:
 *   \code stream << QVariantMap(<i>property-map</i>)
 *        << QList<KLFLib::entryId>(<i>entry-id-list</i>);
 *   \endcode
 * * The <tt><i>property-map</i></tt> contains properties relative to the mime data, such
 *   as the originating URL (in property \c "Url" of type QUrl)
 */

#endif
