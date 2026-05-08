# PX4 Enum Extraction Scripts

## extract_px4_enums.py

Extracts enum constant mappings from PX4 `.msg` files for use in PlotJuggler's
ULog loader. The output JSON enables symbolic display of enum values (e.g.,
`POSCTL` instead of `2`) on Y-axis ticks and in the cursor tracker.

### Usage

```bash
./extract_px4_enums.py <path/to/PX4-Autopilot/msg> <output.json>
```

### Example

```bash
# Clone PX4-Autopilot (or use an existing checkout)
git clone --depth=1 https://github.com/PX4/PX4-Autopilot.git /tmp/px4

# Extract enums
./extract_px4_enums.py /tmp/px4/msg px4_enums.json

# Update the bundled resource
cp px4_enums.json ../resources/px4_enums.json
```

### How It Works

1. Walks all `.msg` files in the given directory tree
2. Parses integer field declarations and constant definitions (`uintN NAME = N`)
3. Matches fields to constants using:
   - Inline `# one of FOO_*` or `# see FOO_*` hints in field comments
   - Snake-case field name to UPPER_CASE prefix matching
   - Alias table for common abbreviations (e.g., `nav_state` → `NAVIGATION_STATE`)
4. Outputs a JSON mapping: `topic → field → {int_value: "symbolic_name"}`

### Output Format

```json
{
  "vehicle_status": {
    "nav_state": {
      "0": "MANUAL",
      "1": "ALTCTL",
      "2": "POSCTL",
      ...
    },
    "arming_state": {
      "1": "DISARMED",
      "2": "ARMED"
    }
  }
}
```

### When to Regenerate

Run this script when:
- Updating to a new PX4 release with changed enum values
- Adding support for new message types
- Fixing incorrect or missing mappings

The bundled `resources/px4_enums.json` should cover common use cases. For custom
PX4 forks, place a `<logname>.enums.json` sidecar file next to your `.ulg` file,
or set `DataLoadULog/enum_override_json` in QSettings to point to a custom JSON.
