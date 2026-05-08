/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "px4_enum_mapping.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

namespace
{
// Strip ULog multi-instance suffix ".NN" (e.g. "sensor_gyro.00"). Does
// not strip "_N" suffixes since those are common in legitimate field/
// topic names (e.g. "home_position", "gps_2") and cannot be reliably
// disambiguated from instance suffixes.
QString stripInstanceSuffix(const QString& name)
{
  static const QRegularExpression re(R"(\.\d{2}$)");
  QString out = name;
  out.remove(re);
  return out;
}
}  // namespace

PX4EnumMapping::PX4EnumMapping(const QString& ulog_path)
{
  // Load all three layers up front. Lookup precedence (highest first)
  // is: sidecar > user override > bundled. The order of loads here
  // does not matter; precedence is enforced in lookup().
  _bundled = loadJsonResource(":/plotjuggler/DataLoadULog/px4_enums.json");

  QSettings settings;
  QString override_json = settings.value(kEnumOverrideJson).toString();
  if (!override_json.isEmpty() && QFileInfo::exists(override_json))
  {
    _user_override = loadJsonFile(override_json);
  }

  if (!ulog_path.isEmpty())
  {
    QFileInfo fi(ulog_path);
    QString sidecar = fi.absolutePath() + QDir::separator() + fi.completeBaseName() + ".enums.json";
    if (QFileInfo::exists(sidecar))
    {
      _sidecar = loadJsonFile(sidecar);
    }
  }
}

QVariantMap PX4EnumMapping::loadJsonFile(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
  {
    return {};
  }
  auto doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject())
  {
    return {};
  }
  return doc.object().toVariantMap();
}

QVariantMap PX4EnumMapping::loadJsonResource(const QString& resource_path)
{
  QFile f(resource_path);
  if (!f.open(QIODevice::ReadOnly))
  {
    return {};
  }
  auto doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject())
  {
    return {};
  }
  return doc.object().toVariantMap();
}

QVariantMap PX4EnumMapping::lookupIn(const QVariantMap& root, const QString& topic,
                                     const QString& field) const
{
  if (root.isEmpty())
  {
    return {};
  }
  auto topic_var = root.value(topic);
  if (!topic_var.isValid())
  {
    return {};
  }
  QVariantMap topic_map = topic_var.toMap();
  auto field_var = topic_map.value(field);
  if (!field_var.isValid())
  {
    return {};
  }
  return field_var.toMap();
}

QVariantMap PX4EnumMapping::lookup(const QString& topic, const QString& field) const
{
  const QString clean_topic = stripInstanceSuffix(topic);
  const QString clean_field = stripInstanceSuffix(field);

  auto try_layer = [&](const QVariantMap& layer) -> QVariantMap {
    auto m = lookupIn(layer, clean_topic, clean_field);
    if (!m.isEmpty())
    {
      return m;
    }
    // Fall back to original topic name (some JSONs may use instance suffix)
    if (clean_topic != topic)
    {
      m = lookupIn(layer, topic, clean_field);
      if (!m.isEmpty())
      {
        return m;
      }
    }
    return {};
  };

  // Lookup precedence (highest first): sidecar, user override, bundled.
  // First non-empty match wins.
  for (const QVariantMap* layer : { &_sidecar, &_user_override, &_bundled })
  {
    auto m = try_layer(*layer);
    if (!m.isEmpty())
    {
      return m;
    }
  }
  return {};
}
