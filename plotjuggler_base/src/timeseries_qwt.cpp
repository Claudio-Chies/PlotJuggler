/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "timeseries_qwt.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <QMessageBox>
#include <QPushButton>
#include <QString>

QVariantMap valueLabelMap(const QwtSeriesData<QPointF>* series)
{
  auto wrapper = dynamic_cast<const QwtSeriesWrapper*>(series);
  if (!wrapper)
  {
    return {};
  }
  const PJ::PlotDataXY* data = wrapper->plotData();
  if (!data)
  {
    return {};
  }
  QVariant attr = data->attribute(PJ::VALUE_LABELS);
  if (!attr.isValid() || attr.type() != QVariant::Map)
  {
    return {};
  }
  return attr.toMap();
}

std::optional<QString> lookupValueLabel(const QVariantMap& map, double v)
{
  if (map.isEmpty() || std::isnan(v) || std::isinf(v))
  {
    return std::nullopt;
  }
  const qint64 iv = qRound64(v);
  if (static_cast<double>(iv) != v)
  {
    return std::nullopt;
  }
  auto it = map.find(QString::number(iv));
  if (it == map.end())
  {
    return std::nullopt;
  }
  return it.value().toString();
}

RangeOpt QwtSeriesWrapper::getVisualizationRangeY(Range range_x)
{
  if (range_x.min <= std::numeric_limits<double>::lowest() &&
      range_x.min <= std::numeric_limits<double>::max())
  {
    return _data->rangeY();
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (size_t i = 0; i < size(); i++)
  {
    const double Y = sample(i).y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  return Range{ min_y, max_y };
}

RangeOpt QwtTimeseries::getVisualizationRangeY(Range range_X)
{
  int first_index = _ts_data->getIndexFromX(range_X.min + _time_offset);
  int last_index = _ts_data->getIndexFromX(range_X.max + _time_offset);

  if (first_index > last_index || first_index < 0 || last_index < 0)
  {
    return {};
  }

  if (first_index == 0 && last_index == plotData()->size() - 1)
  {
    return _ts_data->rangeY();
  }

  double min_y = (std::numeric_limits<double>::max());
  double max_y = (std::numeric_limits<double>::lowest());

  for (size_t i = first_index; i < last_index; i++)
  {
    const double Y = sample(i).y();
    min_y = std::min(min_y, Y);
    max_y = std::max(max_y, Y);
  }
  return Range{ min_y, max_y };
}

std::optional<QPointF> QwtTimeseries::sampleFromTime(double t)
{
  int index = _ts_data->getIndexFromX(t);
  if (index < 0)
  {
    return {};
  }
  const auto& p = plotData()->at(size_t(index));
  return QPointF(p.x, p.y);
}

TransformedTimeseries::TransformedTimeseries(const PlotData* source_data)
  : QwtTimeseries(&_dst_data), _dst_data(source_data->plotName(), {}), _src_data(source_data)
{
  // Only VALUE_LABELS is mirrored: axis renderers and the cursor tracker
  // see the wrapped destination series, and need the mapping to render
  // symbolic labels for raw (un-transformed) curves. Cleared again when
  // a transform is applied (transforms invalidate the value->label map).
  auto labels = source_data->attribute(VALUE_LABELS);
  if (labels.isValid())
  {
    _dst_data.setAttribute(VALUE_LABELS, labels);
  }
}

TransformFunction::Ptr TransformedTimeseries::transform()
{
  return _transform;
}

bool TransformedTimeseries::setTransform(QString transform_ID)
{
  if (transformName() == transform_ID)
  {
    return true;
  }
  if (transform_ID.isEmpty())
  {
    _transform.reset();
    // Restore VALUE_LABELS from source when transform is cleared (data is
    // now a 1:1 copy, so the mapping applies again).
    auto attr = _src_data->attribute(VALUE_LABELS);
    if (attr.isValid())
    {
      _dst_data.setAttribute(VALUE_LABELS, attr);
    }
    else
    {
      _dst_data.clearAttribute(VALUE_LABELS);
    }
    return false;
  }

  _transform = TransformFactory::create(transform_ID.toStdString());
  if (!_transform)
  {
    return false;
  }
  // Clear VALUE_LABELS: transformed values no longer correspond to the
  // original mapping (e.g., scale, derivative, filter).
  _dst_data.clearAttribute(VALUE_LABELS);
  std::vector<PlotData*> dest = { &_dst_data };
  _dst_data.clear();
  _transform->setData(nullptr, { _src_data }, dest);
  return true;
}

void TransformedTimeseries::updateCache(bool reset_old_data)
{
  if (_transform)
  {
    if (reset_old_data)
    {
      _dst_data.clear();
      _transform->reset();
    }
    std::vector<PlotData*> dest = { &_dst_data };
    _transform->calculate();
  }
  else
  {
    // TODO: optimize ??
    _dst_data.clear();
    for (size_t i = 0; i < _src_data->size(); i++)
    {
      _dst_data.pushBack(_src_data->at(i));
    }
  }
}

QString TransformedTimeseries::transformName()
{
  return (!_transform) ? QString() : _transform->name();
}

QString TransformedTimeseries::alias() const
{
  return _alias;
}

void TransformedTimeseries::setAlias(QString alias)
{
  _alias = alias;
}

QRectF QwtSeriesWrapper::boundingRect() const
{
  if (size() == 0)
  {
    return {};
  }
  auto range_x = plotData()->rangeX().value();
  auto range_y = plotData()->rangeY().value();

  QRectF box;
  box.setLeft(range_x.min);
  box.setRight(range_x.max);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QRectF QwtTimeseries::boundingRect() const
{
  if (size() == 0)
  {
    return {};
  }
  auto range_x = plotData()->rangeX().value();
  auto range_y = plotData()->rangeY().value();

  QRectF box;
  box.setLeft(range_x.min - _time_offset);
  box.setRight(range_x.max - _time_offset);
  box.setTop(range_y.max);
  box.setBottom(range_y.min);
  return box;
}

QPointF QwtSeriesWrapper::sample(size_t i) const
{
  const auto& p = _data->at(i);
  return QPointF(p.x, p.y);
}

QPointF QwtTimeseries::sample(size_t i) const
{
  const auto& p = _ts_data->at(i);
  return QPointF(p.x - _time_offset, p.y);
}

size_t QwtSeriesWrapper::size() const
{
  return _data->size();
}

void QwtTimeseries::setTimeOffset(double offset)
{
  _time_offset = offset;
}

RangeOpt QwtSeriesWrapper::getVisualizationRangeX()
{
  if (this->size() < 2)
  {
    return {};
  }
  else
  {
    return _data->rangeX();
  }
}

RangeOpt QwtTimeseries::getVisualizationRangeX()
{
  if (this->size() < 2)
  {
    return {};
  }
  else
  {
    auto range = _ts_data->rangeX().value();
    return RangeOpt({ range.min - _time_offset, range.max - _time_offset });
  }
}

const PlotDataBase<double, double>* QwtSeriesWrapper::plotData() const
{
  return _data;
}
