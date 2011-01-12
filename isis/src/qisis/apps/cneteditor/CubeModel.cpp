#include "CubeModel.h"

#include <iostream>

#include <QAbstractItemModel>
#include <QMap>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <QVector>

#include "ControlMeasure.h"
#include "ControlNet.h"
#include "ControlPoint.h"

using namespace Isis;
using std::cerr;

namespace Qisis
{


  CubeModel::CubeModel(ControlNet* cNet, QObject * parent) :
      QAbstractItemModel(parent), controlNet(cNet), cubeStructure(NULL)
  {
    // cubeIdToIndexHash provides a way of assigning unique sequential indices
    // to all of the Cube Serial numbers in the ControlNet, while
    // cubeIndexToIdHash prvides a way to get the Cube Serial numbers back.
    cubeIdToIndexHash = new QHash< QString, qlonglong >();
    cubeIndexToIdHash = new QHash< qlonglong, QString >();
    cubeIdToIndexHash->reserve(controlNet->Size() / 5);
    cubeIndexToIdHash->reserve(controlNet->Size() / 5);

    populateCubeStructure();
  }


  CubeModel::~CubeModel()
  {
    if (cubeIdToIndexHash)
    {
      delete cubeIdToIndexHash;
      cubeIdToIndexHash = NULL;
    }

    if (cubeIndexToIdHash)
    {
      delete cubeIndexToIdHash;
      cubeIndexToIdHash = NULL;
    }
  }


  void CubeModel::populateCubeStructure()
  {
    int cubeIndex = -1;
    cubeStructure = new QMap< QString, QVector< int > >;

    for (int cpIndex = 0; cpIndex < controlNet->Size(); cpIndex++) 
    {
      // use a reference for the current ControlPoint for clearity
      const ControlPoint & curCtrlPoint = (*controlNet)[cpIndex];

      for (int cmIndex = 0; cmIndex < curCtrlPoint.Size(); cmIndex++) 
      {
        // get current cube's serial number and hash if new
        QString curCube = curCtrlPoint[cmIndex].GetCubeSerialNumber();

        if (!cubeStructure->contains(curCube)) 
        {
          QVector< int > newPointList;
          newPointList.append(cpIndex);
          cubeStructure->insert(curCube, newPointList);
          cubeIdToIndexHash->insert(curCube, ++cubeIndex);
          cubeIndexToIdHash->insert(cubeIndex, curCube);
        }
        else
        {
          (*cubeStructure)[curCube].append(cpIndex);
        }
      } // of for all measures in cp
    } // of for all ControlPoints in net
  }


  QVariant CubeModel::data(const QModelIndex & index, int role) const
  {
    if (role != Qt::DisplayRole)
      return QVariant();

    if (!index.isValid())
      return QVariant();

    if (index.column() != 0)
      return QVariant();


//    cerr << "CubeModel::data called \n";
//    cerr << "  row: " << index.row() << "\tcol: " << index.column() << "\n";
    
    // is a point
    if (index.parent().isValid())
    {
//      cerr << "  index.parent().isValid() is true! (is a point)\n";
      int parentsRow = index.parent().row();
//      cerr << "  parents row: " << parentsRow << "\n";
      QString cubeId = cubeIndexToIdHash->value(parentsRow);
      int pointIndex = cubeStructure->value(cubeId)[index.row()];

      QString id = (*controlNet)[pointIndex].GetId();
//      cerr << "  returning \"" << qPrintable(id) << "\"\n";
//      cerr << "CubeModel::data done\n\n";
      return QVariant(id);
    }
    else // is a cube
    {
//      cerr << "  index.parent().isValid() is false! (is a cube)\n";
//      cerr << "  returning \""
//           << qPrintable(cubeIndexToIdHash->value(index.row())) << "\"\n";
//      cerr << "CubeModel::data done\n\n";
      return QVariant(cubeIndexToIdHash->value(index.row()));
    }
  }
  
  
  int CubeModel::rowCount(const QModelIndex & parent) const
  {
//    cerr << "CubeModel::rowCount called...\n";
    int rowCount = 0;
    
    // is a control Point
    if (parent.isValid())
    {
//      cerr << "  parent is valid! (is a point)\n";
      QString cubeId = parent.data().toString();
      rowCount = cubeStructure->value(cubeId).size();
    }
    else // is a cube
    {
//     cerr << "  parent not valid (is a cube)\n";
      rowCount = cubeStructure->size();
    }
    
//    cerr << "CubeModel::rowCount done... returning " << rowCount << "\n\n";
    return rowCount;
  }
  
  
  int CubeModel::columnCount(const QModelIndex & parent) const
  {
//    cerr << "CubeModel::columnCount called and done\n\n";
    return 1;
  }
  
  
  Qt::ItemFlags CubeModel::flags(const QModelIndex &index) const
  {
//    cerr << "CubeModel::flags called\n";
    
    Qt::ItemFlags flags;
    if (index.isValid())
    {
//      cerr << "\tindex is valid!\n";
      flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    else
    {
      flags = 0;
    }
    
//    cerr << "CubeModel::flags done\n\n";
    
    return flags;
  }
  
  
  QModelIndex CubeModel::parent(const QModelIndex & index) const
  {
    if (!index.isValid())
      return QModelIndex();
    
    
//    cerr << "CubeModel::parent called \n";
//    cerr << "  row: " << index.row() << "\tcol: " << index.column() << "\n";
    void * ip = index.internalPointer();
//    cerr << "  ip: " << ip << "\n";
//      if (ip != -1)
//    {
      /*
      QString * cubeId = (QString *) ip;
      int row = (*cubeIdtoIndexHash)[*cubeId];
      */
    qlonglong id = (qlonglong) ip;
//    cerr << "  id: " << (int) id << "\n";
    if (id != -1)
    {
//      cerr << "CubeModel::parent done\n\n";
      return createIndex((int) id, 0, 0);
    }
//    }
    
//    cerr << "  returning QModelIndex()\n";
//    cerr << "CubeModel::parent done\n\n";
    return QModelIndex();
  }
  
  
  QModelIndex CubeModel::index(int row, int column,
      const QModelIndex & parent) const
  {
//    cerr << "CubeModel::index called \n";
//    cerr << "  row: " << row << "\tcol: " << column << "\n";
    QModelIndex modelIndex;
    
    if (!parent.isValid())
    {
//      cerr << "  parent.isValid() is false! (is a cube)\n";
/*      QHash< int, QString > * mutableHash =
          const_cast< QHash< int, QString > >(cubeIndexToIdHash);
      QString * cubeId = &((*mutableHash)[row]);
*/
      modelIndex = createIndex(row, column, row);
    }
    else
    {
//      cerr << "  parent.isValid() is true! (is a point)\n";
      modelIndex = createIndex(row, column, -1);
    }
    
    
//    cerr << "CubeModel::index done\n\n";
    return modelIndex;
  }
  
  
  void CubeModel::printCubeStructure()
  {
    QMapIterator< QString, QVector< int > > i(*cubeStructure);
    while (i.hasNext())
    {
      i.next();
      cerr << qPrintable(i.key()) << "\n";
      for (int j = 0; j < i.value().size(); j++)
        cerr << "  " << i.value()[j] << "\n";
      cerr << "\n";
    }
  }
  
}

















