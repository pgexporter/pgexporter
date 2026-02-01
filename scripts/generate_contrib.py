#!/usr/bin/env python3
"""
Generate versioned PostgreSQL metric YAML and JSON files from src/include/internal.h.

This script reads the INTERNAL_YAML macro from the C header file, applies a
selection algorithm to pick the correct query variant per PostgreSQL version,
and outputs versioned files to contrib/yaml and contrib/json directories.

Usage:
    generate_contrib.py --internal-h <path> [--yaml-only | --json-only] [--check] [--verbose]
"""

import argparse
import json
import logging
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

try:
    import yaml 
except ImportError as exc: 
    raise SystemExit(
        "PyYAML is required. Install with: python3 -m pip install pyyaml"
    ) from exc


def setup_logging(verbose: bool = False) -> logging.Logger:
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format='%(levelname)s: %(message)s'
    )
    return logging.getLogger(__name__)


SUPPORTED_VERSIONS: List[int] = [13, 14, 15, 16, 17, 18]


def extract_internal_yaml(internal_h_path: Path) -> str:
    with open(internal_h_path, 'r') as f:
        lines = f.readlines()
    
    # Find the start of #define INTERNAL_YAML
    macro_start = None
    for i, line in enumerate(lines):
        if '#define INTERNAL_YAML' in line:
            macro_start = i
            break
    
    if macro_start is None:
        raise ValueError("Could not find INTERNAL_YAML macro in internal.h")
    
    # Collect all lines of the macro (including line continuations)
    macro_lines = []
    i = macro_start
    while i < len(lines):
        line = lines[i]
        if line.rstrip().endswith('\\'):
            macro_lines.append(line.rstrip()[:-1])  # Remove \ and trailing whitespace
        else:
            macro_lines.append(line.rstrip())
            break
        i += 1
    
    # Join all lines
    full_macro = ''.join(macro_lines)
    
    # Remove the #define INTERNAL_YAML "" prefix
    import re
    define_match = re.match(r'#define\s+INTERNAL_YAML\s+""', full_macro)
    if not define_match:
        raise ValueError("Unexpected INTERNAL_YAML macro format")
    
    content = full_macro[define_match.end():]
    
    # Manually extract quoted strings and properly handle escapes
    yaml_parts = []
    in_string = False
    current_string = []
    i = 0
    while i < len(content):
        c = content[i]
        
        if not in_string:
            if c == '"':
                in_string = True
                i += 1
                continue
            else:
                i += 1
                continue
        
        if c == '\\' and i + 1 < len(content):
            # Escape sequence
            next_char = content[i + 1]
            if next_char == 'n':
                current_string.append('\n')
                i += 2
            elif next_char == 't':
                current_string.append('\t')
                i += 2
            elif next_char == 'r':
                current_string.append('\r')
                i += 2
            elif next_char == '\\':
                current_string.append('\\')
                i += 2
            elif next_char == '"':
                current_string.append('"')
                i += 2
            else:
                # Unknown escape, keep as-is
                current_string.append(c)
                i += 1
        elif c == '"':
            # End of string
            in_string = False
            yaml_parts.append(''.join(current_string))
            current_string = []
            i += 1
        else:
            current_string.append(c)
            i += 1
    
    if not yaml_parts:
        raise ValueError("Could not extract YAML strings from INTERNAL_YAML macro")
    
    yaml_str = ''.join(yaml_parts)
    return yaml_str


def parse_internal_yaml(yaml_str: str) -> Dict[str, Any]:
    data = yaml.safe_load(yaml_str)
    validate_schema(data)
    return data


def validate_schema(data: Dict[str, Any]) -> None:
    if not isinstance(data, dict):
        raise ValueError("YAML root must be a dict")
    
    if 'version' not in data:
        raise ValueError("YAML missing required 'version' field")
    
    if 'metrics' not in data:
        raise ValueError("YAML missing required 'metrics' field")
    
    if not isinstance(data['metrics'], list):
        raise ValueError("'metrics' must be a list")
    
    seen_tags = set()
    valid_column_types = {'gauge', 'counter', 'label', 'histogram'}
    
    for i, metric in enumerate(data['metrics']):
        if not isinstance(metric, dict):
            raise ValueError(f"Metric {i} must be a dict")
        
        # Check required fields
        if 'tag' not in metric:
            raise ValueError(f"Metric {i} missing required 'tag' field")
        
        tag = metric['tag']
        if not isinstance(tag, str):
            raise ValueError(f"Metric {i} 'tag' must be a string, got {type(tag)}")
        
        if tag in seen_tags:
            raise ValueError(f"Metric {i}: duplicate tag '{tag}'")
        seen_tags.add(tag)
        
        if 'queries' not in metric:
            raise ValueError(f"Metric {i} ({tag}) missing required 'queries' field")
        
        if not isinstance(metric['queries'], list):
            raise ValueError(f"Metric {i} ({tag}) 'queries' must be a list")
        
        if not metric['queries']:
            raise ValueError(f"Metric {i} ({tag}) has empty 'queries' list")
        
        # Validate each query variant
        versions_seen = set()
        for j, query in enumerate(metric['queries']):
            if not isinstance(query, dict):
                raise ValueError(f"Metric {i} ({tag}) query {j} must be a dict")
            
            if 'version' not in query:
                raise ValueError(f"Metric {i} ({tag}) query {j} missing 'version'")
            
            if 'query' not in query:
                raise ValueError(f"Metric {i} ({tag}) query {j} missing 'query'")
            
            if 'columns' not in query:
                raise ValueError(f"Metric {i} ({tag}) query {j} missing 'columns'")
            
            version = query['version']
            if version in versions_seen:
                raise ValueError(f"Metric {i} ({tag}): duplicate version {version}")
            versions_seen.add(version)
            
            # Validate columns
            columns = query['columns']
            if not isinstance(columns, list):
                raise ValueError(f"Metric {i} ({tag}) query {j} 'columns' must be a list")
            
            for k, col in enumerate(columns):
                if not isinstance(col, dict):
                    raise ValueError(f"Metric {i} ({tag}) query {j} column {k} must be a dict")
                
                if 'type' not in col:
                    raise ValueError(f"Metric {i} ({tag}) query {j} column {k} missing 'type'")
                
                col_type = col['type']
                if col_type not in valid_column_types:
                    raise ValueError(
                        f"Metric {i} ({tag}) query {j} column {k}: "
                        f"invalid type '{col_type}' (valid: {valid_column_types})"
                    )


def select_query_variant(metric: Dict[str, Any], target_version: int) -> Optional[Dict[str, Any]]:
    queries = metric.get('queries', [])
    if not queries:
        return None
    
    # Sort by version descending to find highest matching version first
    sorted_queries = sorted(queries, key=lambda q: q.get('version', 0), reverse=True)
    
    for query_variant in sorted_queries:
        variant_version = query_variant.get('version', 0)
        if variant_version <= target_version:
            return query_variant
    
    return None


def build_metric_for_version(
    metric: Dict[str, Any],
    target_version: int,
    logger: logging.Logger
) -> Optional[Dict[str, Any]]:
    tag = metric.get('tag')
    if not tag:
        logger.warning("Metric missing 'tag' field, skipping")
        return None
    
    selected_query = select_query_variant(metric, target_version)
    if not selected_query:
        logger.debug(f"No query variant found for {tag} on PG {target_version}")
        return None
    
    # Build output metric: top-level fields + selected query
    output_metric = {
        'tag': tag,
        'collector': metric.get('collector', tag),
    }
    
    # Add optional top-level fields if present
    for field in ['sort', 'server', 'database', 'optional']:
        if field in metric:
            output_metric[field] = metric[field]
    
    # Add the selected query and its columns
    output_metric['queries'] = [
        {
            'query': selected_query['query'],
            'version': selected_query['version'],
            'columns': selected_query.get('columns', []),
        }
    ]
    
    return output_metric


def generate_for_version(
    data: Dict[str, Any],
    target_version: int,
    logger: logging.Logger
) -> Dict[str, Any]:
    output = {
        'version': target_version,
        'metrics': []
    }
    
    metrics = data.get('metrics', [])
    logger.info(f"Processing {len(metrics)} metrics for PostgreSQL {target_version}")
    
    for metric in metrics:
        built_metric = build_metric_for_version(metric, target_version, logger)
        if built_metric:
            output['metrics'].append(built_metric)
    
    logger.info(f"Generated {len(output['metrics'])} metrics for PostgreSQL {target_version}")
    return output


def write_yaml_file(output_data: Dict[str, Any], output_path: Path, logger: logging.Logger) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, 'w') as f:
        yaml.dump(
            output_data,
            f,
            default_flow_style=False,
            allow_unicode=True,
            sort_keys=False,
            indent=2,
            width=4096,
        )
    logger.info(f"Wrote {output_path}")


def write_json_file(output_data: Dict[str, Any], output_path: Path, logger: logging.Logger) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w') as f:
        json.dump(output_data, f, indent=2)
        f.write('\n')
    
    logger.info(f"Wrote {output_path}")


def check_file_diff(generated_path: Path, existing_path: Path, logger: logging.Logger) -> bool:
    if not existing_path.exists():
        logger.warning(f"Existing file not found: {existing_path}")
        return True
    
    with open(generated_path, 'r') as f:
        generated = f.read()
    
    with open(existing_path, 'r') as f:
        existing = f.read()
    
    if generated != existing:
        logger.warning(f"Diff detected: {existing_path}")
        return True
    
    return False


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        '--internal-h',
        required=True,
        type=Path,
        help='Path to src/include/internal.h'
    )
    parser.add_argument(
        '--yaml-only',
        action='store_true',
        help='Generate only YAML files'
    )
    parser.add_argument(
        '--json-only',
        action='store_true',
        help='Generate only JSON files'
    )
    parser.add_argument(
        '--check',
        action='store_true',
        help='Check mode: compare generated files with existing; exit non-zero if diffs found'
    )
    parser.add_argument(
        '--verbose',
        action='store_true',
        help='Verbose logging'
    )
    
    args = parser.parse_args()
    logger = setup_logging(args.verbose)
    
    # Validate arguments
    if args.yaml_only and args.json_only:
        logger.error("Cannot use both --yaml-only and --json-only")
        sys.exit(1)
    
    # Determine output directory
    repo_root = args.internal_h.parent.parent.parent
    contrib_yaml_dir = repo_root / 'contrib' / 'yaml'
    contrib_json_dir = repo_root / 'contrib' / 'json'
    
    logger.info(f"Repository root: {repo_root}")
    logger.info(f"YAML output: {contrib_yaml_dir}")
    logger.info(f"JSON output: {contrib_json_dir}")
    
    try:
        # Extract and parse INTERNAL_YAML
        logger.info(f"Reading {args.internal_h}")
        yaml_str = extract_internal_yaml(args.internal_h)
        data = parse_internal_yaml(yaml_str)
        
        # If check mode, write to temp location first
        if args.check:
            import tempfile
            temp_dir = Path(tempfile.mkdtemp(prefix='pgexporter_gen_'))
            output_yaml_dir = temp_dir / 'yaml'
            output_json_dir = temp_dir / 'json'
            output_yaml_dir.mkdir(parents=True, exist_ok=True)
            output_json_dir.mkdir(parents=True, exist_ok=True)
        else:
            output_yaml_dir = contrib_yaml_dir
            output_json_dir = contrib_json_dir
        
        # Generate for each version
        has_diffs = False
        diff_paths: List[Path] = []
        
        for version in SUPPORTED_VERSIONS:
            output_data = generate_for_version(data, version, logger)
            
            # Write YAML
            if not args.json_only:
                yaml_path = output_yaml_dir / f'postgresql-{version}.yaml'
                write_yaml_file(output_data, yaml_path, logger)
                
                if args.check:
                    existing_yaml = contrib_yaml_dir / f'postgresql-{version}.yaml'
                    if check_file_diff(yaml_path, existing_yaml, logger):
                        has_diffs = True
                        diff_paths.append(existing_yaml)
            
            # Write JSON
            if not args.yaml_only:
                json_path = output_json_dir / f'postgresql-{version}.json'
                write_json_file(output_data, json_path, logger)
                
                if args.check:
                    existing_json = contrib_json_dir / f'postgresql-{version}.json'
                    if check_file_diff(json_path, existing_json, logger):
                        has_diffs = True
                        diff_paths.append(existing_json)
        
        if args.check and has_diffs:
            logger.error("Drift detected: generated files differ from existing")
            for p in diff_paths:
                logger.error(f"Diff file: {p}")
            sys.exit(1)
        
        logger.info("Done")
        sys.exit(0)
    
    except Exception as e:
        logger.error(f"{type(e).__name__}: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()
