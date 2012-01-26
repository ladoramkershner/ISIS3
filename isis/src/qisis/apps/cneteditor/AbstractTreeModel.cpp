#include "IsisDebug.h"

#include "AbstractTreeModel.h"

#include <algorithm>
#include <iostream>

#include <QFutureWatcher>
#include <QList>
#include <QModelIndex>
#include <QMutex>
#include <QPair>
#include <QStack>
#include <QString>
#include <QtConcurrentFilter>

#include <QtConcurrentMap>

#include <QFlags>
#include <QtGlobal>
#include <QVariant>

#include "BusyLeafItem.h"
#include "TreeView.h"
#include "ControlMeasure.h"
#include "ControlNet.h"
#include "ControlPoint.h"
#include "iException.h"

#include "AbstractTreeItem.h"
#include "FilterWidget.h"
#include "RootItem.h"

using std::cerr;


namespace Isis
{
  namespace CnetViz
  {
    AbstractTreeModel::AbstractTreeModel(ControlNet * controlNet, TreeView * v,
        QObject * parent) : QObject(parent), view(v), cNet(controlNet)
    {
      ASSERT(cNet);

      filterWatcher = NULL;
      rebuildWatcher = NULL;
      busyItem = NULL;
      rootItem = NULL;
      expandedState = NULL;
      selectedState = NULL;
      guisFilterWidget = NULL;
      localFilterWidgetCopy = NULL;
      mutex = NULL;

      busyItem = new BusyLeafItem(NULL);
      rootItem = new RootItem;
      expandedState = new QList< QPair< QString, QString > >;
      selectedState = new QList< QPair< QString, QString > >;
      mutex = new QMutex;

      filterWatcher = new QFutureWatcher< QAtomicPointer< AbstractTreeItem > >;
      rebuildWatcher = new QFutureWatcher< QAtomicPointer< RootItem > >;

      connect(filterWatcher, SIGNAL(finished()), this, SLOT(applyFilterDone()));
      connect(rebuildWatcher, SIGNAL(finished()), this, SLOT(rebuildItemsDone()));

      connect(filterWatcher, SIGNAL(progressValueChanged(int)),
          this, SIGNAL(filterProgressChanged(int)));
      connect(filterWatcher, SIGNAL(progressRangeChanged(int, int)),
          this, SIGNAL(filterProgressRangeChanged(int, int)));
      connect(rebuildWatcher, SIGNAL(progressValueChanged(int)),
          this, SIGNAL(rebuildProgressChanged(int)));
      connect(rebuildWatcher, SIGNAL(progressRangeChanged(int, int)),
          this, SIGNAL(rebuildProgressRangeChanged(int, int)));

      drivable = false;
      filterAgain = false;
      filterRunning = false;
      rebuildRunning = false;
      frozen = false;
      rebuildPending = false;
    }


    AbstractTreeModel::~AbstractTreeModel()
    {
      delete filterWatcher;
      filterWatcher = NULL;

      delete rebuildWatcher;
      rebuildWatcher = NULL;

      delete busyItem;
      busyItem = NULL;

      delete rootItem;
      rootItem = NULL;

      delete expandedState;
      expandedState = NULL;

      delete selectedState;
      selectedState = NULL;

      delete mutex;
      mutex = NULL;

      delete localFilterWidgetCopy;
      localFilterWidgetCopy = NULL;

      guisFilterWidget = NULL;
      cNet = NULL;
      view = NULL;
    }


    // If a negative end is passed in, grabs all items from start to the end of
    // the tree. No busy leaf items will be inserted.
    QList< AbstractTreeItem * > AbstractTreeModel::getItems(int start, int end,
        InterestingItemsFlag flags, bool ignoreExpansion)
    {
      QList< AbstractTreeItem * > foundItems;
      int rowCount = end - start;
      const AbstractTreeItem * lastVisibleFilteredItem =
        rootItem->getLastVisibleFilteredItem();

      bool grabToEnd = (start >= 0 && end < 0);

      if (lastVisibleFilteredItem && (rowCount > 0 || grabToEnd) &&
          rootItem->childCount())
      {
        int row = 0;
        AbstractTreeItem * currentItem = rootItem->getFirstVisibleChild();

        if (currentItem && !itemIsInteresting(currentItem, flags))
        {
          currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }

        bool listStillValid = true;

        while (row < start && listStillValid && currentItem)
        {
          row++;
          listStillValid = (currentItem != lastVisibleFilteredItem ||
              currentItem == currentItem->parent()->getLastVisibleChild());

          if (listStillValid)
            currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }

        while ((row < end || grabToEnd) && listStillValid && currentItem)
        {
          ASSERT(currentItem);
          foundItems.append(currentItem);
          listStillValid = (currentItem != lastVisibleFilteredItem ||
              currentItem == currentItem->parent()->getLastVisibleChild());
          row++;

          if (listStillValid)
            currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }

        // Fill in the rest with busy items if needed. If we are grabbing all
        // items to the end of the visible tree, we do not want any busy items
        // added to our found items list.
        while (!grabToEnd && isFiltering() && foundItems.size() < rowCount)
        {
          foundItems.append(busyItem);
        }
      }

      return foundItems;
    }


    QList< AbstractTreeItem * > AbstractTreeModel::getItems(
        AbstractTreeItem * item1, AbstractTreeItem * item2,
        InterestingItemsFlag flags, bool ignoreExpansion)
    {
      QList< AbstractTreeItem * > foundItems;

      if (rootItem->childCount())
      {
        AbstractTreeItem * start = NULL;

        AbstractTreeItem * curItem = rootItem->getFirstVisibleChild();

        while (!start && curItem)
        {
          if (curItem == item1)
            start = item1;
          else
            if (curItem == item2)
              start = item2;

          if (!start)
            curItem = nextItem(curItem, flags, ignoreExpansion);
        }

        if (!start)
        {
          iString msg = "The first item passed to getItems(AbstractTreeItem*, "
              "AbstractTreeItem*) is not visible in this model's tree";
          throw iException::Message(iException::Programmer, msg, _FILEINFO_);
        }

        AbstractTreeItem * end = item2;
        
        // Sometimes we need to build the list forwards and sometimes backwards.
        // This is accomplished by using either append or prepend.  We abstract
        // away which of these we should use (why should we care) by using the
        // variable "someKindaPend" to store the appropriate method.
        void (QList<AbstractTreeItem*>::*someKindaPend)(
            AbstractTreeItem * const &);
        
        someKindaPend = &QList<AbstractTreeItem *>::append;
        if (start == item2)
        {
          end = item1;
          someKindaPend = &QList<AbstractTreeItem *>::prepend;
        }

        while (curItem && curItem != end)
        {
          (foundItems.*someKindaPend)(curItem);
          curItem = nextItem(curItem, flags, ignoreExpansion);
        }

        if (!curItem)
        {
          iString msg = "The second item passed to getItems(AbstractTreeItem*, "
              "AbstractTreeItem*) is not visible in this model's tree";
          throw iException::Message(iException::Programmer, msg, _FILEINFO_);
        }

        (foundItems.*someKindaPend)(end);
      }

      return foundItems;
    }


    QList< AbstractTreeItem * > AbstractTreeModel::getSelectedItems(
        InterestingItemsFlag flags, bool ignoreExpansion)
    {
      QList< AbstractTreeItem * > selectedItems;

      ASSERT(rootItem);

      if (!isFiltering())
      {
        AbstractTreeItem * currentItem = rootItem->getFirstVisibleChild();

        if (currentItem && !itemIsInteresting(currentItem, flags))
          currentItem = nextItem(currentItem, flags, ignoreExpansion);

        while (currentItem)
        {
          if (currentItem->isSelected())
            selectedItems.append(currentItem);

          currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }
      }

      return selectedItems;
    }


    QMutex * AbstractTreeModel::getMutex() const
    {
      return mutex;
    }


    int AbstractTreeModel::getItemCount(InterestingItemsFlag flags) const
    {
      return getItemCount(rootItem, flags);
    }


    int AbstractTreeModel::getTopLevelItemCount() const
    {
      return rootItem->childCount();
    }

    int AbstractTreeModel::getVisibleItemCount(InterestingItemsFlag flags,
        bool ignoreExpansion) const
    {
      AbstractTreeItem * currentItem = rootItem->getFirstVisibleChild();
      int count = -1;

      if (!isFiltering())
      {
        count = 0;

        while (currentItem)
        {
          if (itemIsInteresting(currentItem, flags))
          {
            count++;
          }

          currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }
      }

      return count;
    }


    int AbstractTreeModel::getVisibleTopLevelItemCount() const
    {
      AbstractTreeItem * currentItem = rootItem->getFirstVisibleChild();
      int count = -1;

      if (!isFiltering())
      {
        count = 0;

        while (currentItem)
        {
          count++;
          currentItem = currentItem->getNextVisiblePeer();
        }
      }

      return count;
    }
    
    
    int AbstractTreeModel::indexOfVisibleItem(AbstractTreeItem const * item,
        InterestingItemsFlag flags, bool ignoreExpansion) const
    {
      AbstractTreeItem * currentItem = rootItem->getFirstVisibleChild();
      int index = -1;

      if (!isFiltering())
      {
        while (currentItem && currentItem != item)
        {
          if (itemIsInteresting(currentItem, flags))
            index++;

          currentItem = nextItem(currentItem, flags, ignoreExpansion);
        }
        
        index++;
        
        if (!currentItem)
          index = -1;
      }

      return index;
    }
    
    
    void AbstractTreeModel::setFrozen(bool newFrozenState)
    {
      frozen = newFrozenState;
      if (!frozen)
      {
        if (rebuildPending)
        {
          rebuildItems();
          rebuildPending = false;
        }
        else
        {
          applyFilter();
        }
      }
    }
    
    
    bool AbstractTreeModel::isFrozen() const
    {
      return frozen;
    }


    void AbstractTreeModel::queueRebuild()
    {
      rebuildPending = true;
    }


    bool AbstractTreeModel::isFiltering() const
    {
      return filterRunning;
    }
    
    
    bool AbstractTreeModel::isRebuilding() const
    {
      return rebuildRunning;
    }
    
    
    void AbstractTreeModel::setFilter(FilterWidget * fw)
    {
      guisFilterWidget = fw;
      if (fw)
      {
        connect(guisFilterWidget, SIGNAL(filterChanged()),
            this, SLOT(applyFilter()));
        applyFilter();
      }
    }


    void AbstractTreeModel::clear()
    {
      ASSERT(rootItem);

      delete rootItem;
      rootItem = NULL;
      rootItem = new RootItem;
    }


    ControlNet * AbstractTreeModel::getControlNetwork() const
    {
      return cNet;
    }


  //   FilterWidget * AbstractTreeModel::getFilterWidget() const
  //   {
  //     return filter;
  //   }


    QFutureWatcher< QAtomicPointer< RootItem > > *
    AbstractTreeModel::getRebuildWatcher() const
    {
      return rebuildWatcher;
    }


    RootItem * AbstractTreeModel::getRootItem() const
    {
      return rootItem;
    }


    TreeView * AbstractTreeModel::getView() const
    {
      return view;
    }


    void AbstractTreeModel::stopWorking()
    {
      filterWatcher->cancel();
      filterWatcher->waitForFinished();
      rebuildWatcher->cancel();
      rebuildWatcher->waitForFinished();
    }


    //! indentation is in pixels
    QSize AbstractTreeModel::getVisibleSize(int indentation) const
    {
      QSize size;

      if (!isFiltering())
      {
        int visibleRowCount = 0;
        int maxWidth = 0;

        if (rootItem && rootItem->getFirstVisibleChild())
        {
          AbstractTreeItem * current = rootItem->getFirstVisibleChild();

          while (current != NULL)
          {
            int depth = current->getDepth();

            visibleRowCount++;
            maxWidth = qMax(maxWidth,
                current->getDataWidth() + indentation * depth);
            current = nextItem(current, AllItems, false);
          }
        }

        size = QSize(maxWidth, visibleRowCount);
      }

      return size;
    }


    void AbstractTreeModel::applyFilter()
    {
  //     cerr << "AbstractTreeModel::applyFilter\n";
      // If filterAgain is true, then this method will be recalled later
      // with filterAgain = false.
      if (!frozen && !filterAgain && guisFilterWidget &&
          rebuildWatcher->isFinished())
      {
        emit cancelSort();
        QFuture< QAtomicPointer< AbstractTreeItem> > futureRoot;

        if (filterRunning)
        {
          filterAgain = true;
          futureRoot = filterWatcher->future();
          futureRoot.cancel();
        }
        else
        {
  //         cerr << "rootItem->getFirstVisibleChild : " << rootItem->getFirstVisibleChild  ( )<< "\n";
          // filterCounts are unknown and invalid and this fact is shared to
          // users of this class by emitting invalid (negative) information.
          emit filterCountsChanged(-1, getTopLevelItemCount());

          // update our local copy of the gui widget
          if (localFilterWidgetCopy)
          {
            delete localFilterWidgetCopy;
            localFilterWidgetCopy = NULL;
          }
            
          localFilterWidgetCopy = new FilterWidget(*guisFilterWidget);

          // using the local copy (NOT the GUI's FilterWidget!!!) apply then
          // the filter using qtconcurrent's filteredReduced.  ApplyFilterDone()
          // will get called when the filtering is finished.
          filterRunning = true;
          rootItem->setLastVisibleFilteredItem(NULL);
          futureRoot = QtConcurrent::filteredReduced(rootItem->getChildren(),
              FilterFunctor(localFilterWidgetCopy),
              &FilterFunctor::updateTopLevelLinks,
              QtConcurrent::OrderedReduce | QtConcurrent::SequentialReduce);

          filterWatcher->setFuture(futureRoot);
        }
      }
  //     cerr << "/AbstractTreeModel::applyFilter\n";
    }


    void AbstractTreeModel::setGlobalSelection(bool selected,
        InterestingItemsFlag flags)
    {
      selectItems(rootItem, selected, flags);
    }


    void AbstractTreeModel::selectItems(
        AbstractTreeItem * item, bool selected, InterestingItemsFlag flags)
    {
      if (item && itemIsInteresting(item, flags))
      {
        item->setSelected(selected);
      }

      if (item->childCount())
      {
        foreach(AbstractTreeItem * childItem, item->getChildren())
        {
          selectItems(childItem, selected, flags);
        }
      }
    }


    bool AbstractTreeModel::itemIsInteresting(AbstractTreeItem * item,
        InterestingItemsFlag flags)
    {
      AbstractTreeItem::InternalPointerType pointerType = item->getPointerType();

      if ((pointerType == AbstractTreeItem::Point &&
          !flags.testFlag(PointItems)) ||
          (pointerType == AbstractTreeItem::Measure &&
          !flags.testFlag(MeasureItems)) ||
          (pointerType == AbstractTreeItem::CubeGraphNode &&
          !flags.testFlag(SerialItems)))
      {
        return false;
      }
      else
      {
        return true;
      }
    }


    int AbstractTreeModel::getItemCount(AbstractTreeItem * item,
        InterestingItemsFlag flags) const
    {
      int count = 0;

      if (item && itemIsInteresting(item, flags))
      {
        count++;
      }

      if (item->childCount())
      {
        foreach(AbstractTreeItem * childItem, item->getChildren())
        {
          count += getItemCount(childItem, flags);
        }
      }

      return count;
    }


    AbstractTreeItem * AbstractTreeModel::nextItem(AbstractTreeItem * current,
        InterestingItemsFlag flags, bool ignoreExpansion) const
    {
      if (current)
      {
        do
        {
          if ((ignoreExpansion || current->isExpanded()) &&
              current->getFirstVisibleChild())
            current = current->getFirstVisibleChild();
          else if (current->getNextVisiblePeer())
            current = current->getNextVisiblePeer();
          else if (current->parent())
            current = current->parent()->getNextVisiblePeer();
          else
            current = NULL;
        }
        while (current && !itemIsInteresting(current, flags));
      }

      return current;
    }


    void AbstractTreeModel::applyFilterDone()
    {
      filterRunning = false;

      if (filterAgain)
      {
        filterAgain = false;
        applyFilter();
      }
      else
      {
        emit modelModified();
        emit filterCountsChanged(getVisibleTopLevelItemCount(),
            getTopLevelItemCount());
      }
    }


    void AbstractTreeModel::rebuildItemsDone()
    {
  //     cerr << "AbstractTreeModel::rebuildItemsDone called\n";
      clear();

      QAtomicPointer< RootItem > newRoot = rebuildWatcher->future();

      if (newRoot && newRoot->childCount())
      {
        ASSERT(rootItem);
        delete rootItem;
        rootItem = NULL;
        rootItem = newRoot;
      }
      else
      {
        delete newRoot;
        newRoot = NULL;
      }
      
      applyFilter();
      
      setRebuilding(false);
      emit modelModified();

  //     cerr << "/AbstractTreeModel::rebuildItemsDone done\n";
    }


    AbstractTreeModel::FilterFunctor::FilterFunctor(
        FilterWidget * fw) : filter(fw)
    {
    }


    AbstractTreeModel::FilterFunctor::FilterFunctor(FilterFunctor const & other)
    {
      filter = other.filter;
    }


    AbstractTreeModel::FilterFunctor::~FilterFunctor()
    {
    }


    bool AbstractTreeModel::FilterFunctor::operator()(
      AbstractTreeItem * const & item) const
    {
      filterWorker(item);
      return true;
    }


    AbstractTreeModel::FilterFunctor &
        AbstractTreeModel::FilterFunctor::operator=(FilterFunctor const & other)
    {
      if (this != &other)
        filter = other.filter;

      return *this;
    }


    void AbstractTreeModel::FilterFunctor::filterWorker(
        AbstractTreeItem * item) const
    {
      switch (item->getPointerType())
      {

        case AbstractTreeItem::Point:
          item->setVisible((!filter || filter->evaluate(
              (ControlPoint *) item->getPointer())) ? true : false);
          break;

        case AbstractTreeItem::Measure:
          item->setVisible((!filter || filter->evaluate(
              (ControlMeasure *) item->getPointer())) ? true : false);
          break;

        case AbstractTreeItem::CubeGraphNode:
          item->setVisible((!filter || filter->evaluate(
              (ControlCubeGraphNode *) item->getPointer())) ? true : false);
          break;

        case AbstractTreeItem::None:
          item->setVisible(true);
          break;
      }

      // Destroy peer link because it will need to be recreated later.
      if (item->getFirstVisibleChild())
        item->setFirstVisibleChild(NULL);

      if (item->getLastVisibleChild())
        item->setLastVisibleChild(NULL);

      item->setNextVisiblePeer(NULL);

      // Update each tree item's visible flag based on whether or not it is
      // accepted by the filter.
      if (item->childCount())
      {
        for (int i = 0; i < item->childCount(); i++)
        {
          AbstractTreeItem * child = item->childAt(i);
          filterWorker(child);

          if (child->isVisible())
          {
            if (!item->getFirstVisibleChild())
            {
              item->setFirstVisibleChild(child);
              item->setLastVisibleChild(child);
            }
            else
            {
              item->getLastVisibleChild()->setNextVisiblePeer(child);
              item->setLastVisibleChild(child);
            }
          }
        }
      }
    }


    void AbstractTreeModel::FilterFunctor::updateTopLevelLinks(
        QAtomicPointer< AbstractTreeItem > & root,
        AbstractTreeItem * const & item)
    {
      if (!root)
      {
        root = item->parent();
        root->setFirstVisibleChild(NULL);
        root->setLastVisibleChild(NULL);
        root->setLastVisibleFilteredItem(NULL);
      }

      if (item->isVisible())
      {
        if (!root->getFirstVisibleChild())
        {
          root->setFirstVisibleChild(item);
          root->setLastVisibleChild(item);
        }
        else
        {
          root->getLastVisibleChild()->setNextVisiblePeer(item);
          root->setLastVisibleChild(item);
        }

        root->setLastVisibleFilteredItem(item);
      }
    }
  }
}