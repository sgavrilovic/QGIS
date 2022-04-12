/***************************************************************************
                         qgsremotecopcpointcloudindex.cpp
                         --------------------
    begin                : March 2022
    copyright            : (C) 2022 by Belgacem Nedjima
    email                : belgacem dot nedjima at gmail dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsremotecopcpointcloudindex.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QtDebug>
#include <QQueue>
#include <QTimer>

#include "qgseptdecoder.h"
#include "qgscoordinatereferencesystem.h"
#include "qgspointcloudrequest.h"
#include "qgspointcloudattribute.h"
#include "qgslogger.h"
#include "qgsfeedback.h"
#include "qgsmessagelog.h"

#include "qgstiledownloadmanager.h"
#include "qgsblockingnetworkrequest.h"
#include "qgslazdecoder.h"
#include "qgsfileutils.h"
#include "qgsapplication.h"
#include "qgscopcpointcloudblockrequest.h"
#include "qgspointcloudexpression.h"

///@cond PRIVATE

QgsRemoteCopcPointCloudIndex::QgsRemoteCopcPointCloudIndex() : QgsCopcPointCloudIndex()
{

}

QgsRemoteCopcPointCloudIndex::~QgsRemoteCopcPointCloudIndex() = default;

QList<IndexedPointCloudNode> QgsRemoteCopcPointCloudIndex::nodeChildren( const IndexedPointCloudNode &n ) const
{
  fetchNodeHierarchy( n );

  mHierarchyMutex.lock();
  Q_ASSERT( mHierarchy.contains( n ) );
  QList<IndexedPointCloudNode> lst;
  const int d = n.d() + 1;
  const int x = n.x() * 2;
  const int y = n.y() * 2;
  const int z = n.z() * 2;
  mHierarchyMutex.unlock();

  for ( int i = 0; i < 8; ++i )
  {
    int dx = i & 1, dy = !!( i & 2 ), dz = !!( i & 4 );
    const IndexedPointCloudNode n2( d, x + dx, y + dy, z + dz );
    if ( fetchNodeHierarchy( n2 ) && mHierarchy[n] > 0 )
      lst.append( n2 );
  }
  return lst;
}

void QgsRemoteCopcPointCloudIndex::load( const QString &url )
{
  mUrl = QUrl( url );
  mLazInfo.reset( new QgsLazInfo( QgsLazInfo::fromUrl( mUrl ) ) );
  mIsValid = mLazInfo->isValid();
  if ( mIsValid )
  {
    mIsValid = loadSchema( *mLazInfo.get() );
    if ( mIsValid )
    {
      loadHierarchy();
    }
  }
  if ( !mIsValid )
  {
    QgsMessageLog::logMessage( tr( "Unable to recognize %1 as a LAZ file: \"%2\"" ).arg( url ).arg( mLazInfo->error() ) );
  }
}

QgsPointCloudBlock *QgsRemoteCopcPointCloudIndex::nodeData( const IndexedPointCloudNode &n, const QgsPointCloudRequest &request )
{
  std::unique_ptr<QgsPointCloudBlockRequest> blockRequest( asyncNodeData( n, request ) );
  if ( !blockRequest )
    return nullptr;

  QEventLoop loop;
  connect( blockRequest.get(), &QgsPointCloudBlockRequest::finished, &loop, &QEventLoop::quit );
  loop.exec();

  if ( !blockRequest->block() )
  {
    QgsDebugMsg( QStringLiteral( "Error downloading node %1 data, error : %2 " ).arg( n.toString(), blockRequest->errorStr() ) );
  }

  return blockRequest->block();
}

QgsPointCloudBlockRequest *QgsRemoteCopcPointCloudIndex::asyncNodeData( const IndexedPointCloudNode &n, const QgsPointCloudRequest &request )
{
  if ( !fetchNodeHierarchy( n ) )
    return nullptr;
  QMutexLocker locker( &mHierarchyMutex );

  // we need to create a copy of the expression to pass to the decoder
  // as the same QgsPointCloudExpression object might be concurrently
  // used on another thread, for example in a 3d view
  QgsPointCloudExpression filterExpression = mFilterExpression;
  QgsPointCloudAttributeCollection requestAttributes = request.attributes();
  requestAttributes.extend( attributes(), filterExpression.referencedAttributes() );
  auto [ blockOffset, blockSize ] = mHierarchyNodePos[n];
  int pointCount = mHierarchy[n];

  return new QgsCopcPointCloudBlockRequest( n, mUrl.toString(), attributes(), requestAttributes,
         scale(), offset(), filterExpression,
         blockOffset, blockSize, pointCount, *mLazInfo.get() );
}

bool QgsRemoteCopcPointCloudIndex::hasNode( const IndexedPointCloudNode &n ) const
{
  return fetchNodeHierarchy( n );
}

bool QgsRemoteCopcPointCloudIndex::fetchNodeHierarchy( const IndexedPointCloudNode &n ) const
{
  QMutexLocker locker( &mHierarchyMutex );

  QVector<IndexedPointCloudNode> ancestors;
  IndexedPointCloudNode foundRoot = n;
  while ( !mHierarchy.contains( foundRoot ) )
  {
    ancestors.push_front( foundRoot );
    foundRoot = foundRoot.parentNode();
  }
  ancestors.push_front( foundRoot );
  for ( IndexedPointCloudNode n : ancestors )
  {
    if ( !mHierarchy.contains( n ) )
      return false;
    int nodesCount = mHierarchy[n];
    if ( nodesCount < 0 )
    {
      fetchHierarchyPage( mHierarchyNodePos[n].first, mHierarchyNodePos[n].second );
    }
  }
  return mHierarchy.contains( n );
}

bool QgsRemoteCopcPointCloudIndex::isValid() const
{
  return mIsValid;
}

void QgsRemoteCopcPointCloudIndex::fetchHierarchyPage( uint64_t offset, uint64_t byteSize ) const
{
  QNetworkRequest nr( mUrl );
  nr.setAttribute( QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork );
  nr.setAttribute( QNetworkRequest::CacheSaveControlAttribute, false );
  QByteArray queryRange = QStringLiteral( "bytes=%1-%2" ).arg( offset ).arg( offset + byteSize - 1 ).toLocal8Bit();
  nr.setRawHeader( "Range", queryRange );
  QgsBlockingNetworkRequest req;
  QgsBlockingNetworkRequest::ErrorCode errCode = req.get( nr );
  if ( errCode != QgsBlockingNetworkRequest::NoError )
  {
    QgsDebugMsg( QStringLiteral( "Request failed: " ) + mUrl.toString() );
    return;
  }
  const QgsNetworkReplyContent reply = req.reply();
  QByteArray data = reply.content();

  struct CopcVoxelKey
  {
    int32_t level;
    int32_t x;
    int32_t y;
    int32_t z;
  };

  struct CopcEntry
  {
    CopcVoxelKey key;
    uint64_t offset;
    int32_t byteSize;
    int32_t pointCount;
  };

  for ( uint64_t i = 0; i < byteSize; i += sizeof( CopcEntry ) )
  {
    CopcEntry *entry = reinterpret_cast<CopcEntry *>( data.data() + i );
    const IndexedPointCloudNode nodeId( entry->key.level, entry->key.x, entry->key.y, entry->key.z );
    mHierarchy[nodeId] = entry->pointCount;
    mHierarchyNodePos.insert( nodeId, QPair<uint64_t, int32_t>( entry->offset, entry->byteSize ) );
  }
}


///@endcond
