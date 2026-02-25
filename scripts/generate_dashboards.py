#!/usr/bin/env python3
"""
Generate versioned Grafana dashboard JSON files from a single annotated template.

The template (postgresql_dashboard.json) is the superset dashboard containing
all panels across every supported PostgreSQL version.  Panels that require a
minimum PostgreSQL version carry a "version" field (e.g. "version": 14).
Panels without this field are available from PostgreSQL 13 onward.

For each target PostgreSQL version the script:
  1. Filters out panels whose "version" exceeds the target.
  2. Strips the "version" annotation from the output.
  3. Reassigns sequential panel IDs (1, 2, 3, ...).
  4. Recalculates gridPos.y so the layout remains contiguous.
  5. Replaces version-specific metadata (title, uid, description).
  6. Writes contrib/grafana/postgresql_dashboard_pg<ver>.json

Usage:
    generate_dashboards.py --template <path> [--check] [--verbose]
"""

import argparse
import copy
import json
import logging
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, List

SUPPORTED_VERSIONS: List[int] = [13, 14, 15, 16, 17, 18]

# The template is built from the highest supported version
TEMPLATE_VERSION: int = SUPPORTED_VERSIONS[-1]

# Panels without an explicit "version" field are assumed available from this version
DEFAULT_MIN_VERSION: int = SUPPORTED_VERSIONS[0]


def setup_logging(verbose: bool = False) -> logging.Logger:
    """Configure and return the module logger."""
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format='%(levelname)s: %(message)s'
    )
    return logging.getLogger(__name__)


def load_template(path: Path) -> Dict[str, Any]:
    """Read and parse the annotated dashboard template."""
    with open(path, 'r') as f:
        return json.load(f)


def panel_min_version(panel: Dict[str, Any]) -> int:
    """Return the minimum PostgreSQL version required by a panel."""
    return panel.get('version', DEFAULT_MIN_VERSION)


def filter_panels(
    panels: List[Dict[str, Any]],
    target_version: int
) -> List[Dict[str, Any]]:
    """Keep only panels whose minimum version does not exceed target_version."""
    return [p for p in panels if panel_min_version(p) <= target_version]


def strip_version_field(panel: Dict[str, Any]) -> Dict[str, Any]:
    """Return a deep copy of the panel with the "version" annotation removed."""
    cleaned = copy.deepcopy(panel)
    cleaned.pop('version', None)
    return cleaned


def reassign_ids(panels: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    """Assign sequential IDs (1-based) to panels."""
    for i, panel in enumerate(panels, start=1):
        panel['id'] = i
    return panels


def recalculate_grid_positions(
    all_panels: List[Dict[str, Any]],
    kept_panels: List[Dict[str, Any]],
) -> List[Dict[str, Any]]:
    """Shift gridPos.y values upward to fill gaps left by removed panels.

    For each unique y position in the full template, if every panel at that y
    was removed the row's height is accumulated as a downward offset.  Every
    kept panel below a removed row has its y reduced by the total accumulated
    offset at its original position.
    """
    if not kept_panels:
        return kept_panels

    all_y_values = sorted(set(
        p['gridPos']['y'] for p in all_panels if 'gridPos' in p
    ))

    kept_y_values = set(
        p['gridPos']['y'] for p in kept_panels if 'gridPos' in p
    )

    # Build cumulative offset by walking y positions top-to-bottom
    y_offset: Dict[int, int] = {}
    cumulative = 0

    for y in all_y_values:
        if y not in kept_y_values:
            row_height = max(
                p['gridPos']['h']
                for p in all_panels
                if 'gridPos' in p and p['gridPos']['y'] == y
            )
            cumulative += row_height
        y_offset[y] = cumulative

    for panel in kept_panels:
        if 'gridPos' in panel:
            orig_y = panel['gridPos']['y']
            panel['gridPos']['y'] = orig_y - y_offset.get(orig_y, 0)

    return kept_panels


def update_dashboard_metadata(
    dashboard: Dict[str, Any],
    target_version: int,
) -> None:
    """Replace the template version number in top-level metadata fields."""
    for field in ('title', 'uid', 'description'):
        if field in dashboard and isinstance(dashboard[field], str):
            dashboard[field] = dashboard[field].replace(
                str(TEMPLATE_VERSION), str(target_version)
            )
    if 'tags' in dashboard and isinstance(dashboard['tags'], list):
        template_tag = f'pg{TEMPLATE_VERSION}'
        target_tag = f'pg{target_version}'
        dashboard['tags'] = [
            target_tag if tag == template_tag else tag
            for tag in dashboard['tags']
        ]


def generate_for_version(
    template: Dict[str, Any],
    target_version: int,
    logger: logging.Logger,
) -> Dict[str, Any]:
    """Produce a complete dashboard dict for a single PostgreSQL version."""
    dashboard = copy.deepcopy(template)
    all_panels = dashboard.get('panels', [])

    logger.info(
        f"Processing {len(all_panels)} panels for PostgreSQL {target_version}"
    )

    kept_panels = filter_panels(all_panels, target_version)
    logger.info(f"After filtering: {len(kept_panels)} panels")

    kept_panels = [strip_version_field(p) for p in kept_panels]
    kept_panels = reassign_ids(kept_panels)

    # Recalculate layout using the full panel list as reference
    all_stripped = [strip_version_field(p) for p in all_panels]
    kept_panels = recalculate_grid_positions(all_stripped, kept_panels)

    dashboard['panels'] = kept_panels
    update_dashboard_metadata(dashboard, target_version)

    return dashboard


def write_dashboard(
    dashboard: Dict[str, Any],
    path: Path,
    logger: logging.Logger,
) -> None:
    """Serialize dashboard to a JSON file with a trailing newline."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w') as f:
        json.dump(dashboard, f, indent=2)
        f.write('\n')
    logger.info(f"Wrote {path}")


def check_file_diff(
    generated_path: Path,
    existing_path: Path,
    logger: logging.Logger,
) -> bool:
    """Return True if generated_path differs from existing_path."""
    if not existing_path.exists():
        logger.warning(f"Missing file: {existing_path}")
        return True

    with open(generated_path, 'r') as f:
        generated = f.read()
    with open(existing_path, 'r') as f:
        existing = f.read()

    if generated != existing:
        logger.warning(f"Diff detected: {existing_path}")
        return True

    return False


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        '--template',
        required=True,
        type=Path,
        help='Path to the annotated postgresql_dashboard.json template',
    )
    parser.add_argument(
        '--check',
        action='store_true',
        help='Compare generated files with existing ones; exit non-zero on diff',
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Enable debug-level logging',
    )

    args = parser.parse_args()
    logger = setup_logging(args.verbose)

    output_dir = args.template.parent

    try:
        logger.info(f"Loading template: {args.template}")
        template = load_template(args.template)

        has_diffs = False
        diff_paths: List[Path] = []

        for version in SUPPORTED_VERSIONS:
            dashboard = generate_for_version(template, version, logger)
            output_path = output_dir / f'postgresql_dashboard_pg{version}.json'

            if args.check:
                temp_path = Path(tempfile.mktemp(suffix='.json'))
                write_dashboard(dashboard, temp_path, logger)

                if check_file_diff(temp_path, output_path, logger):
                    has_diffs = True
                    diff_paths.append(output_path)

                temp_path.unlink(missing_ok=True)
            else:
                write_dashboard(dashboard, output_path, logger)

        if args.check and has_diffs:
            logger.error("Drift detected: generated files differ from existing")
            for p in diff_paths:
                logger.error(f"  {p}")
            sys.exit(1)

        logger.info("Done")
        sys.exit(0)

    except Exception as e:
        logger.error(f"{type(e).__name__}: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
