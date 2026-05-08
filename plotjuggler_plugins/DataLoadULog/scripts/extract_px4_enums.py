#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Extract PX4 uORB enum constants from a PX4-Autopilot/msg tree.

For every .msg file we record:
  - integer field declarations (with any inline ``# one of FOO_*`` hint)
  - constant declarations of the form ``uintN NAME = N``

Then for each integer field we derive the expected constant-name prefix
(comment hint > snake_case match > alias table) and gather every
constant in the same file whose name starts with ``PREFIX_``. The
prefix is stripped to produce the symbolic label.

Output JSON shape:

    {
      "<topic_snake_case>": {
        "<field_name>": {"<int>": "<symbolic_name>", ...},
        ...
      },
      ...
    }

Usage:
    extract_px4_enums.py <path/to/PX4-Autopilot/msg> <output.json>
"""

import argparse
import json
import os
import re
import subprocess
import sys
from collections import OrderedDict

INT_TYPES = {f"uint{n}" for n in (8, 16, 32, 64)} | {f"int{n}" for n in (8, 16, 32, 64)}

FIELD_RE = re.compile(
    r"^\s*([a-zA-Z_][\w\[\]]*)\s+([a-z][a-zA-Z0-9_]*)\s*(#.*)?$"
)
CONST_RE = re.compile(
    r"^\s*([a-zA-Z_][\w]*)\s+([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+)\s*(?:#.*)?$"
)
HINT_RE = re.compile(r"\b(?:one of|see)\s+([A-Z][A-Z0-9_]*)_\*", re.IGNORECASE)

# Prefixes that don't match their field via snake_case.
PREFIX_ALIASES = {
    "nav_state": "NAVIGATION_STATE",
    "latest_arming_reason": "ARM_DISARM_REASON",
    "latest_disarming_reason": "ARM_DISARM_REASON",
}


def pascal_to_snake(name: str) -> str:
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1).lower()


def base_int_type(t: str) -> str:
    return t.split("[")[0]


def parse_msg_file(path):
    fields = []  # [(name, hint_prefix_or_None)]
    consts = []  # [(name, value)]

    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            cm = CONST_RE.match(line)
            if cm and base_int_type(cm.group(1)) in INT_TYPES:
                consts.append((cm.group(2), int(cm.group(3))))
                continue
            fm = FIELD_RE.match(line)
            if fm and base_int_type(fm.group(1)) in INT_TYPES:
                hint = None
                if fm.group(3):
                    hm = HINT_RE.search(fm.group(3))
                    if hm:
                        hint = hm.group(1).upper()
                fields.append((fm.group(2), hint))

    out = OrderedDict()
    for fname, hint in fields:
        if hint:
            prefix = hint
        elif fname in PREFIX_ALIASES:
            prefix = PREFIX_ALIASES[fname]
        else:
            prefix = fname.upper()
        members = [(n, v) for n, v in consts if n.startswith(prefix + "_")]
        if not members:
            continue
        plen = len(prefix.split("_"))
        bucket = OrderedDict()
        for name, val in members:
            tail = "_".join(name.split("_")[plen:]) or name
            bucket.setdefault(str(val), tail)
        if bucket:
            out[fname] = bucket
    return out


def topic_name_from_msg_path(msg_path: str) -> str:
    base = os.path.basename(msg_path)
    name = os.path.splitext(base)[0]
    return pascal_to_snake(name)


def detect_source_ref(msg_dir: str) -> str:
    """Best-effort: return git describe / SHA of the msg tree, or 'unknown'."""
    try:
        out = subprocess.run(
            ["git", "-C", msg_dir, "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True,
        )
        return out.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def build_mapping(msg_dir: str) -> "OrderedDict":
    out = OrderedDict()
    for root, _dirs, files in os.walk(msg_dir):
        for fn in files:
            if not fn.endswith(".msg"):
                continue
            path = os.path.join(root, fn)
            mapping = parse_msg_file(path)
            if not mapping:
                continue
            topic = topic_name_from_msg_path(path)
            existing = out.get(topic, OrderedDict())
            for field, m in mapping.items():
                merged = existing.get(field, OrderedDict())
                for k, v in m.items():
                    merged.setdefault(k, v)
                existing[field] = merged
            out[topic] = existing

    out = OrderedDict(sorted(out.items()))
    for topic, fields in out.items():
        out[topic] = OrderedDict(sorted(fields.items()))
    return out


def render_json(mapping: "OrderedDict", source_ref: str) -> str:
    payload = OrderedDict()
    payload["_meta"] = OrderedDict(
        [
            ("description", "PX4 uORB enum mappings for PlotJuggler ULog loader"),
            ("generated_from", "PX4-Autopilot/msg using scripts/extract_px4_enums.py"),
            ("source_ref", source_ref),
        ]
    )
    payload.update(mapping)
    return json.dumps(payload, indent=2, sort_keys=False) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("msg_dir", help="path to PX4-Autopilot/msg")
    ap.add_argument("output", help="output JSON path")
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if `output` differs from regenerated content")
    args = ap.parse_args()

    mapping = build_mapping(args.msg_dir)
    source_ref = detect_source_ref(args.msg_dir)
    rendered = render_json(mapping, source_ref)

    if args.check:
        try:
            with open(args.output, "r", encoding="utf-8") as f:
                existing = f.read()
        except OSError:
            existing = ""
        if existing != rendered:
            print(f"{args.output} is out of date; rerun without --check.", file=sys.stderr)
            sys.exit(1)
        print(f"{args.output} is up to date.")
        return

    with open(args.output, "w", encoding="utf-8") as f:
        f.write(rendered)
    n_topics = len(mapping)
    n_fields = sum(len(v) for v in mapping.values())
    print(f"Wrote {args.output}: {n_topics} topics, {n_fields} enum fields "
          f"(source_ref={source_ref})")


if __name__ == "__main__":
    main()
