/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2023 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "PerformanceCounterViewer.h"
#include "Code/QRDUtils.h"
#include "Windows/Dialogs/PerformanceCounterSelection.h"
#include "ui_PerformanceCounterViewer.h"
#include "../../3rdparty/zstd/debug.h"

static const int EIDRole = Qt::UserRole + 1;
static const int SortDataRole = Qt::UserRole + 2;

class PerformanceCounterItemModel : public QAbstractItemModel
{
public:
  PerformanceCounterItemModel(ICaptureContext &ctx, QObject *parent)
      : QAbstractItemModel(parent), m_Ctx(ctx)
  {
    m_TimeUnit = m_Ctx.Config().EventBrowser_TimeUnit;
    m_NumRows = 0;
  }

  bool UpdateDurationColumn()
  {
    if(m_TimeUnit == m_Ctx.Config().EventBrowser_TimeUnit)
      return false;

    m_TimeUnit = m_Ctx.Config().EventBrowser_TimeUnit;
    emit headerDataChanged(Qt::Horizontal, 1, columnCount());

    return true;
  }

  void refresh(const rdcarray<CounterDescription> &counterDescriptions,
               const rdcarray<CounterResult> &results)
  {
    emit beginResetModel();

    m_Descriptions = counterDescriptions;

    QMap<uint32_t, int> eventIdToRow;
    for(const CounterResult &result : results)
    {
      if(eventIdToRow.contains(result.eventId))
        continue;
      eventIdToRow.insert(result.eventId, eventIdToRow.size());
    }
    QMap<GPUCounter, int> counterToCol;
    for(int i = 0; i < counterDescriptions.count(); i++)
    {
      counterToCol[counterDescriptions[i].counter] = i;
    }

    m_NumRows = eventIdToRow.size();
    m_Data.resize(m_NumRows * (m_Descriptions.size() + 1));

    for(int i = 0; i < (int)results.size(); ++i)
    {
      int row = eventIdToRow[results[i].eventId];

      getData(row, 0).u = results[i].eventId;

      int col = counterToCol[results[i].counter];

      const CounterDescription &desc = counterDescriptions[col];

      col++;

      if(desc.resultType == CompType::UInt)
      {
        if(desc.resultByteWidth == 4)
          getData(row, col).u = results[i].value.u32;
        else
          getData(row, col).u = results[i].value.u64;
      }
      else
      {
        if(desc.resultByteWidth == 4)
          getData(row, col).f = results[i].value.f;
        else
          getData(row, col).f = results[i].value.d;
      }
    }

    emit endResetModel();
  }

  QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override
  {
    if(row < 0 || row >= rowCount())
      return QModelIndex();

    return createIndex(row, column);
  }

  QModelIndex parent(const QModelIndex &index) const override { return QModelIndex(); }
  int rowCount(const QModelIndex &parent = QModelIndex()) const override { return m_NumRows; }
  int columnCount(const QModelIndex &parent = QModelIndex()) const override
  {
    return m_Descriptions.count() + 1;
  }
  Qt::ItemFlags flags(const QModelIndex &index) const override
  {
    if(!index.isValid())
      return 0;

    return QAbstractItemModel::flags(index);
  }

  QVariant headerData(int section, Qt::Orientation orientation, int role) const override
  {
    if(orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
      if(section == 0)
        return lit("EID");

      const CounterDescription &cd = m_Descriptions[section - 1];

      QString unit = QString::null;
      switch(cd.unit)
      {
        case CounterUnit::Bytes: unit = lit("bytes"); break;

        case CounterUnit::Cycles: unit = lit("cycles"); break;

        case CounterUnit::Percentage: unit = lit("%"); break;

        case CounterUnit::Seconds: unit = UnitSuffix(m_TimeUnit); break;

        case CounterUnit::Absolute:
        case CounterUnit::Ratio: break;

        case CounterUnit::Hertz: unit = lit("Hz"); break;
        case CounterUnit::Volt: unit = lit("V"); break;
        case CounterUnit::Celsius: unit = lit("°C"); break;
      }

      if(unit.isNull())
        return cd.name;
      else
        return QFormatStr("%1 (%2)").arg(cd.name, unit);
    }

    return QVariant();
  }

  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
  {
    if(index.isValid())
    {
      int row = index.row();
      int col = index.column();

      if(role == Qt::TextAlignmentRole)
      {
        if(col == 0)
          return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        else
          return QVariant(Qt::AlignRight | Qt::AlignVCenter);
      }

      if((role == Qt::DisplayRole && col == 0) || role == EIDRole)
        return (qulonglong)getData(row, 0).u;

      const CounterDescription &desc = m_Descriptions[qMax(1, col) - 1];

      if(role == SortDataRole)
      {
        if(col == 0 || desc.resultType == CompType::UInt)
          return (qulonglong)getData(row, col).u;
        else
          return getData(row, col).f;
      }

      if(role == Qt::DisplayRole)
      {
        if(col == 0 || desc.resultType == CompType::UInt)
          return (qulonglong)getData(row, col).u;

        double val = getData(row, col).f;

        if(desc.unit == CounterUnit::Seconds)
        {
          if(m_TimeUnit == TimeUnit::Milliseconds)
            val *= 1000.0;
          else if(m_TimeUnit == TimeUnit::Microseconds)
            val *= 1000000.0;
          else if(m_TimeUnit == TimeUnit::Nanoseconds)
            val *= 1000000000.0;
        }

        return Formatter::Format(val);
      }
    }

    return QVariant();
  }

private:
  ICaptureContext &m_Ctx;

  union CounterDataVal
  {
    uint64_t u;
    double f;
  };

  const CounterDataVal &getData(int row, int col) const
  {
    return m_Data[row * (m_Descriptions.size() + 1) + col];
  }

  CounterDataVal &getData(int row, int col)
  {
    return m_Data[row * (m_Descriptions.size() + 1) + col];
  }

  TimeUnit m_TimeUnit;

  rdcarray<CounterDataVal> m_Data;
  rdcarray<CounterDescription> m_Descriptions;
  int m_NumRows;
};

class PerformanceCounterFilterModel : public QSortFilterProxyModel
{
public:
  PerformanceCounterFilterModel(ICaptureContext &ctx, QObject *parent)
      : QSortFilterProxyModel(parent), m_Ctx(ctx)
  {
  }

  void refresh(bool sync)
  {
    if(m_Sync || sync)
    {
      m_Sync = sync;
      invalidateFilter();
    }
  }

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
  {
    if(!m_Sync)
      return true;

    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);

    return m_Ctx.GetEventBrowser()->IsAPIEventVisible(idx.data(EIDRole).toUInt());
  }

  bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
  {
    return sourceModel()->data(left, SortDataRole) < sourceModel()->data(right, SortDataRole);
  }

private:
  ICaptureContext &m_Ctx;
  bool m_Sync = false;
};

PerformanceCounterViewer::PerformanceCounterViewer(ICaptureContext &ctx, QWidget *parent)
    : QFrame(parent), ui(new Ui::PerformanceCounterViewer), m_Ctx(ctx)
{
  ui->setupUi(this);

  connect(ui->captureCounters, &QToolButton::clicked, this,
          &PerformanceCounterViewer::CaptureCounters);

  ui->captureCounters->setEnabled(m_Ctx.IsCaptureLoaded());
  ui->saveCSV->setEnabled(m_Ctx.IsCaptureLoaded());

  m_ItemModel = new PerformanceCounterItemModel(m_Ctx, this);
  m_FilterModel = new PerformanceCounterFilterModel(m_Ctx, this);

  m_FilterModel->setSourceModel(m_ItemModel);
  ui->counterResults->setModel(m_FilterModel);

  ui->counterResults->horizontalHeader()->setSectionsMovable(true);
  ui->counterResults->horizontalHeader()->setStretchLastSection(false);

  ui->counterResults->setFont(Formatter::PreferredFont());

  ui->counterResults->setSortingEnabled(true);
  ui->counterResults->sortByColumn(0, Qt::AscendingOrder);

  m_Ctx.AddCaptureViewer(this);
}

PerformanceCounterViewer::~PerformanceCounterViewer()
{
  m_Ctx.BuiltinWindowClosed(this);

  m_Ctx.RemoveCaptureViewer(this);
  delete ui;
}

// Begin L2 sungxu : Render pass GPU duration

const char *GetGPUCounterString(GPUCounter counter)
{
  switch(counter)
  {
    case GPUCounter::CSInvocations: return "CSInvocations";
    case GPUCounter::EventGPUDuration: return "EventGPUDuration";
    case GPUCounter::InputVerticesRead: return "InputVerticesRead";
    case GPUCounter::IAPrimitives: return "IAPrimitives";
    case GPUCounter::GSPrimitives: return "GSPrimitives";
    case GPUCounter::RasterizerInvocations: return "RasterizerInvocations";
    case GPUCounter::RasterizedPrimitives: return "RasterizedPrimitives";
    case GPUCounter::SamplesPassed: return "SamplesPassed";
    case GPUCounter::VSInvocations: return "VSInvocations";
    //case GPUCounter::HSInvocations: return "HSInvocations";
    case GPUCounter::TCSInvocations: return "TCSInvocations";
    //case GPUCounter::DSInvocations: return "DSInvocations";
    case GPUCounter::TESInvocations: return "TESInvocations";
    //case GPUCounter::PSInvocations: return "PSInvocations";
    case GPUCounter::FSInvocations: return "FSInvocations";
    case GPUCounter::GSInvocations: return "GSInvocations";
    case GPUCounter::RenderPassGPUDuration: return "RenderPassGPUDuration";
    case GPUCounter::BeginRenderPassGPUDuration: return "BeginRenderPassGPUDuration";
    case GPUCounter::EventGPUDurationDrawCalls: return "EventGPUDurationDrawCalls";
    case GPUCounter::EventGPUDurationDispatches: return "EventGPUDurationDispatches";
    default: break;
  }
  return "UnKnown";
}

struct TotalStatistics
{
  float TotalDrawAndDispatchCallDuration = 0.0f;
  float TotalRenderPassDuration = 0.0f;
  float TotalBeginRenderPassDuration = 0.0f;
  float TotalDrawCallsDuration = 0.0f;
  float TotalDispatchCallsDuration = 0.0f;

  uint32_t TotalNoIndirectDrawCalls = 0;
  uint32_t TotalIndirectDrawCalls = 0;
  uint32_t TotalNoIndirectDispatches = 0;
  uint32_t TotalIndirectDispatches = 0;

  uint32_t TotalInputVerticesRead = 0;
  uint32_t TotalIAPrimitives = 0;
  uint32_t TotalVSInvocations = 0;
  uint32_t TotalPSInvocations = 0;
  uint32_t TotalCSInvocations = 0;
};

void StatisticsSubtraction(const TotalStatistics& left, const TotalStatistics& right, TotalStatistics& out)
{
  out.TotalDrawAndDispatchCallDuration = left.TotalDrawAndDispatchCallDuration - right.TotalDrawAndDispatchCallDuration;
  out.TotalDrawCallsDuration = left.TotalDrawCallsDuration - right.TotalDrawCallsDuration;
  out.TotalDispatchCallsDuration = left.TotalDispatchCallsDuration - right.TotalDispatchCallsDuration;
  out.TotalRenderPassDuration = left.TotalRenderPassDuration - right.TotalRenderPassDuration;
  out.TotalBeginRenderPassDuration = left.TotalBeginRenderPassDuration - right.TotalBeginRenderPassDuration;

  out.TotalNoIndirectDrawCalls = left.TotalNoIndirectDrawCalls - right.TotalNoIndirectDrawCalls;
  out.TotalIndirectDrawCalls = left.TotalIndirectDrawCalls - right.TotalIndirectDrawCalls;
  out.TotalNoIndirectDispatches = left.TotalNoIndirectDispatches - right.TotalNoIndirectDispatches;
  out.TotalIndirectDispatches = left.TotalIndirectDispatches - right.TotalIndirectDispatches;
  //
  out.TotalInputVerticesRead = left.TotalInputVerticesRead - right.TotalInputVerticesRead;
  out.TotalIAPrimitives = left.TotalIAPrimitives - right.TotalIAPrimitives;
  out.TotalVSInvocations = left.TotalVSInvocations - right.TotalVSInvocations;
  out.TotalPSInvocations = left.TotalPSInvocations - right.TotalPSInvocations;
  out.TotalCSInvocations = left.TotalCSInvocations - right.TotalCSInvocations;
}

void StstisticsProcessing(TotalStatistics &StatisticsPhase, rdcarray<CounterResult> &results, rdcarray<GPUCounter>& counters)
{
  for(size_t resultIdx = 0; resultIdx < results.size(); ++resultIdx)
  {
    CounterResult &result = results[resultIdx];
    if(result.counter == GPUCounter::EventGPUDuration)
    {
      StatisticsPhase.TotalDrawAndDispatchCallDuration +=
          result.value.d * 1000.0f;    // convert unit from us to ms.
    }
    else if(result.counter == GPUCounter::RenderPassGPUDuration)
    {
      StatisticsPhase.TotalRenderPassDuration +=
          result.value.d * 1000.0f;    // convert unit from us to ms.
    }
    else if(result.counter == GPUCounter::BeginRenderPassGPUDuration)
    {
      StatisticsPhase.TotalBeginRenderPassDuration +=
          result.value.d * 1000.0f;    // convert unit from us to ms.
    }
    else if(result.counter == GPUCounter::EventGPUDurationDrawCalls)
    {
      StatisticsPhase.TotalDrawCallsDuration +=
          result.value.d * 1000.0f;    // convert unit from us to ms.
    }
    else if(result.counter == GPUCounter::EventGPUDurationDispatches)
    {
      StatisticsPhase.TotalDispatchCallsDuration +=
          result.value.d * 1000.0f;    // convert unit from us to ms.
    }
    else if(result.counter == GPUCounter::InputVerticesRead)
    {
      StatisticsPhase.TotalInputVerticesRead += result.value.u32;
    }
    else if(result.counter == GPUCounter::IAPrimitives)
    {
      StatisticsPhase.TotalIAPrimitives += result.value.u32;
    }
    else if(result.counter == GPUCounter::VSInvocations)
    {
      StatisticsPhase.TotalVSInvocations += result.value.u32;
    }
    else if(result.counter == GPUCounter::PSInvocations)
    {
      StatisticsPhase.TotalPSInvocations += result.value.u32;
    }
    else if(result.counter == GPUCounter::CSInvocations)
    {
      StatisticsPhase.TotalCSInvocations += result.value.u32;
    }

    switch(result.eventType)
    {
      // Draws
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 83):
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 85):
        StatisticsPhase.TotalNoIndirectDrawCalls++;
        break;
      // Indirect draws
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 84):
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 86):
        StatisticsPhase.TotalIndirectDrawCalls++;
        break;
      // Dispatches
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 87):
        StatisticsPhase.TotalNoIndirectDispatches++;
        break;
      // Indirect Dispatches
      case(/*(uint32_t)SystemChunk::FirstDriverChunk*/ 1000 + 88):
        StatisticsPhase.TotalIndirectDispatches++;
        break;
      default: break;
    };
  }
}

// End L2 sungxu

void PerformanceCounterViewer::CaptureCounters()
{
  if(!m_Ctx.IsCaptureLoaded())
    return;

  PerformanceCounterSelection pcs(m_Ctx, m_SelectedCounters, this);
  if(RDDialog::show(&pcs) != QDialog::Accepted)
    return;
  m_SelectedCounters = pcs.GetSelectedCounters();

  ANALYTIC_SET(UIFeatures.PerformanceCounters, true);

  bool done = false;

  m_Ctx.Replay().AsyncInvoke([this, &done](IReplayController *controller) -> void {
    rdcarray<GPUCounter> counters;
    counters.resize(m_SelectedCounters.size());

    rdcarray<CounterDescription> counterDescriptions;

    for(int i = 0; i < m_SelectedCounters.size(); ++i)
    {
      counters[i] = (GPUCounter)m_SelectedCounters[i];
      counterDescriptions.push_back(controller->DescribeCounter(counters[i]));
    }

    auto *action = m_Ctx.GetLastAction();
    uint32_t maxEID = action->eventId;

    // Begin L2 sungxu : Render pass GPU duration
    // L2-qilincheng: Begin
    // Prepare event-mask array
#if defined(POP_DEBUG)
    rdcarray<EventStatusFiltered> eventStatusArray;
    for (uint32_t eid = 0; eid <= maxEID; eid++)
    {
      EventStatusFiltered eventStatus;
      eventStatus.m_ActionDesc = const_cast<ActionDescription*>(m_Ctx.GetEventBrowser()->GetActionForEID(eid));
      eventStatus.m_EventName = m_Ctx.GetEventBrowser()->GetEventName(eid);
      eventStatus.m_EventId = eid;
      eventStatus.m_EventVisibile = m_Ctx.GetEventBrowser()->IsAPIEventVisible(eid) ? 1 : 0;
      eventStatusArray.push_back(eventStatus);
    }
#else
    rdcarray<uint8_t> eventStatusArray;
    for(uint32_t eid = 0; eid <= maxEID; eid++)
    {
      eventStatusArray.push_back(m_Ctx.GetEventBrowser()->IsAPIEventVisible(eid) ? 1 : 0);
    }
#endif
    
    // L2-qilincheng: End
    
    // Do 1st step, FetchCounter without filters.
    rdcarray<CounterResult> results = controller->FetchCounters(counters, eventStatusArray, 0);
    TotalStatistics StatisticsFirstPhase;
    StstisticsProcessing(StatisticsFirstPhase, results, counters);
    // End L2 sungxu

    // Do 2nd step: FetchCounters with event-mask(filtered)
    rdcarray<CounterResult> maskedResults = controller->FetchCounters(counters, eventStatusArray, 1);
    TotalStatistics StatisticsSecondPhase;
    StstisticsProcessing(StatisticsSecondPhase, maskedResults, counters);

    GUIInvoke::call(this, [this, results, counterDescriptions, StatisticsFirstPhase, StatisticsSecondPhase]() {
      m_ItemModel->refresh(counterDescriptions, results);

      ui->counterResults->resizeColumnsToContents();

      TotalStatistics StatisticsDifference;
      StatisticsSubtraction(StatisticsFirstPhase, StatisticsSecondPhase, StatisticsDifference);

      // L2-qilincheng: Begin
      char buf[2048] = {0};
      sprintf(buf,
              "<table width='100%%' border='1' cellspacing='0' cellpadding='5'>"
                  "<tr><td> </td>                          <td> Total </td>           <td> Unselected </td>   <td> Selected </td></tr>"
                  "<tr><td> DrawAndDispatch </td>          <td> %.6fms </td>          <td> %.6fms </td>       <td> %.6fms </td></tr>"
                  "<tr><td> DrawCallDuration </td>         <td> %.6fms </td>          <td> %.6fms </td>       <td> %.6fms </td></tr>"
                  "<tr><td> DispatchCallDuration </td>     <td> %.6fms </td>          <td> %.6fms </td>       <td> %.6fms </td></tr>"
                  "<tr><td> RenderPassDuration </td>       <td> %.6fms </td>          <td> %.6fms </td>       <td> %.6fms </td></tr>"
                  "<tr><td> BeginRenderPassDuration </td>  <td> %.6fms </td>          <td> %.6fms </td>       <td> %.6fms </td></tr>"
                  "<tr><td> NoIndirectDrawCall </td>       <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> IndirectDrawCall </td>         <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> NoIndirectDispatch </td>       <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> IndirectDispatch </td>         <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> Input Vertices Read </td>      <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> Input Primitives </td>         <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> VS Invocations </td>           <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> PS Invocations </td>           <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
                  "<tr><td> CS Invocations </td>           <td> %u </td>              <td> %u </td>           <td> %u </td></tr>"
              "</table>",
              StatisticsFirstPhase.TotalDrawAndDispatchCallDuration, StatisticsSecondPhase.TotalDrawAndDispatchCallDuration, StatisticsDifference.TotalDrawAndDispatchCallDuration,
              StatisticsFirstPhase.TotalDrawCallsDuration,           StatisticsSecondPhase.TotalDrawCallsDuration,           StatisticsDifference.TotalDrawCallsDuration,
              StatisticsFirstPhase.TotalDispatchCallsDuration,       StatisticsSecondPhase.TotalDispatchCallsDuration,       StatisticsDifference.TotalDispatchCallsDuration,
              StatisticsFirstPhase.TotalRenderPassDuration,          StatisticsSecondPhase.TotalRenderPassDuration,          StatisticsDifference.TotalRenderPassDuration,
              StatisticsFirstPhase.TotalBeginRenderPassDuration,     StatisticsSecondPhase.TotalBeginRenderPassDuration,     StatisticsDifference.TotalBeginRenderPassDuration,
              StatisticsFirstPhase.TotalNoIndirectDrawCalls,         StatisticsSecondPhase.TotalNoIndirectDrawCalls,         StatisticsDifference.TotalNoIndirectDrawCalls,
              StatisticsFirstPhase.TotalIndirectDrawCalls,           StatisticsSecondPhase.TotalIndirectDrawCalls,           StatisticsDifference.TotalIndirectDrawCalls,
              StatisticsFirstPhase.TotalNoIndirectDispatches,        StatisticsSecondPhase.TotalNoIndirectDispatches,        StatisticsDifference.TotalNoIndirectDispatches,
              StatisticsFirstPhase.TotalIndirectDispatches,          StatisticsSecondPhase.TotalIndirectDispatches,          StatisticsDifference.TotalIndirectDispatches,
              //
              StatisticsFirstPhase.TotalInputVerticesRead,           StatisticsSecondPhase.TotalInputVerticesRead,           StatisticsDifference.TotalInputVerticesRead,
              StatisticsFirstPhase.TotalIAPrimitives,                StatisticsSecondPhase.TotalIAPrimitives,                StatisticsDifference.TotalIAPrimitives,
              StatisticsFirstPhase.TotalVSInvocations,               StatisticsSecondPhase.TotalVSInvocations,               StatisticsDifference.TotalVSInvocations,
              StatisticsFirstPhase.TotalPSInvocations,               StatisticsSecondPhase.TotalPSInvocations,               StatisticsDifference.TotalPSInvocations,
              StatisticsFirstPhase.TotalCSInvocations,               StatisticsSecondPhase.TotalCSInvocations,               StatisticsDifference.TotalCSInvocations
              );
      ui->statusText->setText(QString::fromUtf8(buf));
      // L2-qilincheng: End
    });

    done = true;
  });

  ShowProgressDialog(this, tr("Capturing counters"), [&done]() -> bool { return done; });
}

void PerformanceCounterViewer::OnCaptureClosed()
{
  ui->captureCounters->setEnabled(false);
  ui->saveCSV->setEnabled(false);

  m_ItemModel->refresh({}, {});
}

void PerformanceCounterViewer::UpdateDurationColumn()
{
  if(m_ItemModel->UpdateDurationColumn())
    ui->counterResults->viewport()->update();
}

void PerformanceCounterViewer::OnCaptureLoaded()
{
  ui->captureCounters->setEnabled(true);
  ui->saveCSV->setEnabled(true);
}

void PerformanceCounterViewer::OnEventChanged(uint32_t eventId)
{
  m_FilterModel->refresh(ui->syncViews->isChecked());
  if(ui->syncViews->isChecked())
  {
    const int numItems = (int)ui->counterResults->model()->rowCount();
    for(int i = 0; i < numItems; ++i)
    {
      QModelIndex index = ui->counterResults->model()->index(i, 0);
      if(index.data(EIDRole).toUInt() == eventId)
      {
        ui->counterResults->setCurrentIndex(index);
        ui->counterResults->scrollTo(index);
        break;
      }
    }
  }
}

void PerformanceCounterViewer::on_counterResults_doubleClicked(const QModelIndex &index)
{
  uint32_t eid = index.data(EIDRole).toUInt();

  m_Ctx.SetEventID({}, eid, eid);
}

void PerformanceCounterViewer::on_syncViews_toggled(bool checked)
{
  OnEventChanged(m_Ctx.CurEvent());
}

void PerformanceCounterViewer::on_saveCSV_clicked()
{
  QString filename = RDDialog::getSaveFileName(this, tr("Export counter results as CSV"), QString(),
                                               tr("CSV Files (*.csv)"));

  if(!filename.isEmpty())
  {
    QDir dirinfo = QFileInfo(filename).dir();
    if(dirinfo.exists())
    {
      QFile f(filename, this);
      if(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        QTextStream ts(&f);

        QAbstractItemModel *model = ui->counterResults->model();

        for(int col = 0; col < model->columnCount(); col++)
        {
          ts << model->headerData(col, Qt::Horizontal).toString();

          if(col == model->columnCount() - 1)
            ts << lit("\n");
          else
            ts << lit(",");
        }

        for(int row = 0; row < model->rowCount(); row++)
        {
          for(int col = 0; col < model->columnCount(); col++)
          {
            ts << model->index(row, col).data().toString();

            if(col == model->columnCount() - 1)
              ts << lit("\n");
            else
              ts << lit(",");
          }
        }

        return;
      }

      RDDialog::critical(
          this, tr("Error exporting counter results"),
          tr("Couldn't open path %1 for write.\n%2").arg(filename).arg(f.errorString()));
    }
    else
    {
      RDDialog::critical(this, tr("Invalid directory"),
                         tr("Cannot find target directory to save to"));
    }
  }
}
