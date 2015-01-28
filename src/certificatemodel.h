/****************************************************************************
 *   Copyright (C) 2015 by Savoir-Faire Linux                               *
 *   Author : Emmanuel Lepage Vallee <emmanuel.lepage@savoirfairelinux.com> *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Lesser General Public             *
 *   License as published by the Free Software Foundation; either           *
 *   version 2.1 of the License, or (at your option) any later version.     *
 *                                                                          *
 *   This library is distributed in the hope that it will be useful,        *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU General Public License      *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/
#ifndef CERTIFICATE_MODEL_H
#define CERTIFICATE_MODEL_H
#include <QtCore/QAbstractItemModel>
#include "typedefs.h"

class CertificateModelPrivate;

class LIB_EXPORT CertificateModel : public QAbstractItemModel
{
   Q_OBJECT
public:

   CertificateModel(QObject* parent);
   virtual ~CertificateModel();

   //Model implementation
   virtual bool          setData      ( const QModelIndex& index, const QVariant &value, int role   ) override;
   virtual QVariant      data         ( const QModelIndex& index, int role = Qt::DisplayRole        ) const override;
   virtual int           rowCount     ( const QModelIndex& parent = QModelIndex()                   ) const override;
   virtual Qt::ItemFlags flags        ( const QModelIndex& index                                    ) const override;
   virtual int           columnCount  ( const QModelIndex& parent = QModelIndex()                   ) const override;
   virtual QModelIndex   parent       ( const QModelIndex& index                                    ) const override;
   virtual QModelIndex   index        ( int row, int column, const QModelIndex& parent=QModelIndex()) const override;
   virtual QVariant      headerData   ( int section, Qt::Orientation orientation, int role = Qt::DisplayRole ) const override;
   virtual QStringList   mimeTypes    (                                                             ) const override;
   virtual QMimeData*    mimeData     ( const QModelIndexList &indexes                              ) const override;
   virtual bool          dropMimeData ( const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent ) override;

private:
   CertificateModelPrivate* d_ptr;
   Q_DECLARE_PRIVATE(CertificateModelPrivate);
};

#endif