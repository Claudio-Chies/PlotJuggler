/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <QString>
#include <QVariantMap>

// QSettings keys for enum display configuration.
inline constexpr char kEnumNamesEnabled[] = "DataLoadULog/enum_names_enabled";
inline constexpr char kEnumOverrideJson[] = "DataLoadULog/enum_override_json";

/**
 * PX4/ULog enum name resolver.
 *
 * Looks up symbolic names for integer enum values using a layered search:
 *   1. Sidecar JSON next to the log file (<logname>.enums.json)
 *   2. User-configured override JSON (via QSettings)
 *   3. Bundled resource (:/plotjuggler/DataLoadULog/px4_enums.json)
 *
 * JSON schema:
 *   {
 *     "<topic_name>": {
 *       "<field_name>": {
 *         "<int_as_string>": "<symbolic_name>", ...
 *       }, ...
 *     }, ...
 *   }
 *
 * Example:
 *   { "vehicle_status": { "nav_state": { "0": "MANUAL", "2": "POSCTL" } } }
 */
class PX4EnumMapping
{
public:
  explicit PX4EnumMapping(const QString& ulog_path);

  /// Value->name map for (topic, field), or empty if unknown.
  /// Handles PX4 multi-instance suffixes (.NN) automatically.
  QVariantMap lookup(const QString& topic, const QString& field) const;

private:
  static QVariantMap loadJsonFile(const QString& path);
  static QVariantMap loadJsonResource(const QString& resource_path);
  QVariantMap lookupIn(const QVariantMap& root, const QString& topic, const QString& field) const;

  QVariantMap _sidecar;
  QVariantMap _user_override;
  QVariantMap _bundled;
};
