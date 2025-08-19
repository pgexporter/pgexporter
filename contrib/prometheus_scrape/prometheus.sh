#!/bin/bash

if [ $# -lt 2 ]; then
    echo "Usage: $0 <port> <extra_info_file>"
    exit 1
fi

PORT=$1
EXTRA_INFO_FILE=$2
TEMP_DIR="/tmp"
METRICS_FILE="${TEMP_DIR}/prometheus.raw"
EXTRA_INFO_PROCESSED="${TEMP_DIR}/extra.processed"
MD_OUTPUT="prometheus.md"
HTML_OUTPUT="prometheus.html"

rm -f "$METRICS_FILE" "$EXTRA_INFO_PROCESSED" "$MD_OUTPUT" "$HTML_OUTPUT"

echo "Fetching metrics from port $PORT..."
curl -s -o "$METRICS_FILE" "http://localhost:$PORT/metrics"
if [ $? -ne 0 ]; then
    echo "Error: Failed to fetch metrics from http://localhost:$PORT/metrics"
    rm -f "$METRICS_FILE" "$EXTRA_INFO_PROCESSED"
    exit 1
fi
if [ ! -s "$METRICS_FILE" ]; then
    echo "Error: Fetched metrics file ($METRICS_FILE) is empty or does not exist."
    rm -f "$EXTRA_INFO_PROCESSED"
    exit 1
fi

echo "Processing extra info file..."
if [ ! -f "$EXTRA_INFO_FILE" ]; then
    echo "Error: Extra info file '$EXTRA_INFO_FILE' not found."
    rm -f "$METRICS_FILE" "$EXTRA_INFO_PROCESSED"
    exit 1
fi

awk '
BEGIN { metric = ""; description = ""; details = ""; version = ""; }
/^pgexporter_[a-zA-Z0-9_]+/ {
    if (metric != "") {
        print metric;
        print "DESCRIPTION_START"; print description; print "DESCRIPTION_END";
        print "DETAILS_START"; print details; print "DETAILS_END";
        print "VERSION_START"; print version; print "VERSION_END";
        print "METRIC_END";
    }
    metric = $0;
    description = "";
    details = "";
    version = "";
}
/^\+/ {
    line = $0;
    sub(/^\+ /, "", line);
    description = description line "\n";
}
/^\*/ {
    line = $0;
    details = details line "\n";
}
END {
    if (metric != "") {
        print metric;
        print "DESCRIPTION_START"; print description; print "DESCRIPTION_END";
        print "DETAILS_START"; print details; print "DETAILS_END";
        print "VERSION_START"; print version; print "VERSION_END";
        print "METRIC_END";
    }
}' "$EXTRA_INFO_FILE" > "$EXTRA_INFO_PROCESSED"

echo "Generating documentation..."

cat > "$HTML_OUTPUT" << EOF
<html>
  <head>
    <title>Prometheus Metrics Documentation</title>
    <style>
      body { font-family: sans-serif; line-height: 1.4; padding: 15px; }
      h1 { border-bottom: 2px solid #ccc; padding-bottom: 5px;}
      h2 { border-bottom: 1px solid #eee; padding-bottom: 5px; margin-top: 25px;}
      h3 { border-bottom: 1px solid #f0f0f0; padding-bottom: 3px; margin-top: 20px;}
      table { border-collapse: collapse; width: 100%; margin: 15px 0; border: 1px solid #ccc; }
      th, td { border: 1px solid #ddd; padding: 6px; text-align: left; vertical-align: top; word-wrap: break-word; }
      th { background-color: #f9f9f9; font-weight: bold; }
      pre { background-color: #f8f8f8; padding: 8px; border: 1px solid #ddd; border-radius: 3px; overflow-x: auto; white-space: pre-wrap; word-wrap: break-word; }
      ul { margin-top: 5px; padding-left: 25px; }
      li { margin-bottom: 4px; }
      .toc { margin-bottom: 20px; padding: 10px; background-color: #fdfdfd; border: 1px solid #eee;}
      .toc h2 { border: none; margin-top: 0;}
      .toc ul { list-style-type: none; padding-left: 0; }
      .toc li a { text-decoration: none; }
      .toc li a:hover { text-decoration: underline; }
      .extension-section { margin-top: 30px; padding-top: 20px; border-top: 2px solid #ddd; }
    </style>
  </head>
  <body>
  <h1>Prometheus Metrics Documentation</h1>
  <p>This document contains all available metrics from the pgexporter system.</p>
EOF

cat > "$MD_OUTPUT" << EOF
# Prometheus Metrics Documentation

This document contains all available metrics from the pgexporter system.

## Table of Contents

### Core PostgreSQL Metrics

EOF

# Define extension patterns
extension_patterns=(
    "pgexporter_pg_buffercache_"
    "pgexporter_pgcrypto_"
    "pgexporter_pg_stat_statements_"
    "pgexporter_postgis_"
    "pgexporter_postgis_raster_"
    "pgexporter_postgis_topology_"
    "pgexporter_timescaledb_"
    "pgexporter_vector_"
)

# Function to check if a metric is an extension metric
is_extension_metric() {
    local metric=$1
    for pattern in "${extension_patterns[@]}"; do
        if [[ "$metric" == $pattern* ]]; then
            return 0
        fi
    done
    return 1
}

metric_count=0
core_metric_count=0
extension_metric_count=0
declare -A toc_metrics
declare -A core_metrics
declare -A extension_metrics

while IFS= read -r line; do
    if [[ "$line" =~ ^#HELP ]]; then
        metric=$(echo "$line" | cut -d ' ' -f 2)
        if [[ -z "${toc_metrics[$metric]}" ]]; then
           toc_metrics["$metric"]=1
           ((metric_count++))
           
           if is_extension_metric "$metric"; then
               extension_metrics["$metric"]=1
               ((extension_metric_count++))
           else
               core_metrics["$metric"]=1
               ((core_metric_count++))
           fi
        fi
    fi
done < "$METRICS_FILE"

# Sort core metrics
core_sorted_keys=($(printf "%s\n" "${!core_metrics[@]}" | sort))
extension_sorted_keys=($(printf "%s\n" "${!extension_metrics[@]}" | sort))

# Generate core metrics TOC
for metric in "${core_sorted_keys[@]}"; do
    echo "- [$metric](#$metric)" >> "$MD_OUTPUT"
done

echo "" >> "$MD_OUTPUT"
echo "### Extension Metrics" >> "$MD_OUTPUT"
echo "" >> "$MD_OUTPUT"

# Generate extension metrics TOC
for metric in "${extension_sorted_keys[@]}"; do
    echo "- [$metric](#$metric)" >> "$MD_OUTPUT"
done

# HTML TOC
echo "<div class=\"toc\">" >> "$HTML_OUTPUT"
echo "<h2>Table of Contents</h2>" >> "$HTML_OUTPUT"
echo "<h3>Core PostgreSQL Metrics</h3>" >> "$HTML_OUTPUT"
echo "<ul>" >> "$HTML_OUTPUT"
for metric in "${core_sorted_keys[@]}"; do
    echo "  <li><a href=\"#$metric\">$metric</a></li>" >> "$HTML_OUTPUT"
done
echo "</ul>" >> "$HTML_OUTPUT"

echo "<h3>Extension Metrics</h3>" >> "$HTML_OUTPUT"
echo "<ul>" >> "$HTML_OUTPUT"
for metric in "${extension_sorted_keys[@]}"; do
    echo "  <li><a href=\"#$metric\">$metric</a></li>" >> "$HTML_OUTPUT"
done
echo "</ul>" >> "$HTML_OUTPUT"
echo "<p>Total metrics: $metric_count (Core: $core_metric_count, Extensions: $extension_metric_count)</p>" >> "$HTML_OUTPUT"
echo "</div>" >> "$HTML_OUTPUT"

echo "" >> "$MD_OUTPUT"
echo "**Total metrics: $metric_count (Core: $core_metric_count, Extensions: $extension_metric_count)**" >> "$MD_OUTPUT"
echo "" >> "$MD_OUTPUT"
echo "---" >> "$MD_OUTPUT"
echo "## Core PostgreSQL Metrics" >> "$MD_OUTPUT"
echo "" >> "$MD_OUTPUT"

echo "<h2>Core PostgreSQL Metrics</h2>" >> "$HTML_OUTPUT"

metric_name=""
metric_help=""
metric_type=""
extra_desc=""
extra_details=""

current_best_example_line=""
current_best_example_priority=0

processed_metrics=()

is_processed() {
    local metric=$1
    for processed in "${processed_metrics[@]}"; do
        if [[ "$processed" == "$metric" ]]; then
            return 0
        fi
    done
    return 1
}

process_attributes_and_example() {
    local metric_to_process="$1"
    local example_line="$2"
    local details_for_metric="$3"

    local has_attrs=false
    local md_table_content=""
    local html_table_content=""

    if [ ! -z "$details_for_metric" ]; then
        while IFS= read -r detail_line; do
             if [[ "$detail_line" =~ ^\*[[:space:]]*([^:]+):[[:space:]]*(.*) ]]; then
                local key="${BASH_REMATCH[1]}"
                local value="${BASH_REMATCH[2]}"
                key=$(echo "$key" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
                value=$(echo "$value" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')

                if [ "$has_attrs" = false ]; then
                    md_table_content="| Attribute | Value |\n| :-------- | :---- |\n"
                    html_table_content="<thead><tr><th>Attribute</th><th>Value</th></tr></thead>\n<tbody>\n"
                    has_attrs=true
                fi

                md_table_content+="| $key | $value |\n"
                html_table_content+="  <tr><td>$key</td><td>$value</td></tr>\n"
             fi
        done < <(echo -e "$details_for_metric")
    fi

    if [ "$has_attrs" = true ]; then
        echo "**Attributes:**" >> "$MD_OUTPUT"
        echo "" >> "$MD_OUTPUT"
        echo -e "${md_table_content%\\n}" >> "$MD_OUTPUT"
        echo "" >> "$MD_OUTPUT"
        html_table_content+="</tbody>"
    fi

    if [ "$has_attrs" = true ]; then
        echo "<p><strong>Attributes:</strong></p>" >> "$HTML_OUTPUT"
        echo "<table>" >> "$HTML_OUTPUT"
        echo -e "$html_table_content" >> "$HTML_OUTPUT"
        echo "</table>" >> "$HTML_OUTPUT"
        echo "" >> "$HTML_OUTPUT"
    fi

    if [ ! -z "$example_line" ]; then
        echo "**Example:**" >> "$MD_OUTPUT"
        echo "" >> "$MD_OUTPUT"
        echo '```' >> "$MD_OUTPUT"
        echo "$example_line" >> "$MD_OUTPUT"
        echo '```' >> "$MD_OUTPUT"

        echo "<p><strong>Example:</strong></p>" >> "$HTML_OUTPUT"
        echo "<pre>$example_line</pre>" >> "$HTML_OUTPUT"
    else
        echo "**Example:**" >> "$MD_OUTPUT"
        echo "" >> "$MD_OUTPUT"
        echo "(No example available)" >> "$MD_OUTPUT"

        echo "<p><strong>Example:</strong></p>" >> "$HTML_OUTPUT"
        echo "<p>(No example available)</p>" >> "$HTML_OUTPUT"
    fi

    echo "" >> "$MD_OUTPUT"
    echo "---" >> "$MD_OUTPUT"
    echo "" >> "$MD_OUTPUT"
    echo "" >> "$HTML_OUTPUT"
}

extension_section_started=false

# Process all metrics (core first, then extensions)
all_sorted_keys=("${core_sorted_keys[@]}" "${extension_sorted_keys[@]}")

for target_metric in "${all_sorted_keys[@]}"; do
    # Check if we need to start extension section
    if ! $extension_section_started && is_extension_metric "$target_metric"; then
        echo "## Extension Metrics" >> "$MD_OUTPUT"
        echo "" >> "$MD_OUTPUT"
        echo "<div class=\"extension-section\">" >> "$HTML_OUTPUT"
        echo "<h2>Extension Metrics</h2>" >> "$HTML_OUTPUT"
        extension_section_started=true
    fi
    
    # Process this specific metric from the metrics file
    while IFS= read -r line; do
        if [[ "$line" =~ ^#HELP ]]; then
            if [ ! -z "$metric_name" ] && [ "$metric_name" == "$target_metric" ]; then
                echo "$metric_help Type is $metric_type." >> "$MD_OUTPUT"
                echo "" >> "$MD_OUTPUT"

                formatted_desc=""
                if [ ! -z "$extra_desc" ]; then
                     formatted_desc=$(cat <<< "$extra_desc" | sed 's/^[[:space:]]*+//g' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | paste -sd ' ')
                fi
                final_desc="${formatted_desc:-$metric_help}"
                echo "$final_desc" >> "$MD_OUTPUT"
                echo "" >> "$MD_OUTPUT"

                echo "<p>$metric_help Type is $metric_type.</p>" >> "$HTML_OUTPUT"
                echo "<p>$final_desc</p>" >> "$HTML_OUTPUT"

                process_attributes_and_example "$metric_name" "$current_best_example_line" "$extra_details"
                break
            fi

            new_metric_name=$(echo "$line" | cut -d ' ' -f 2)
            
            if [ "$new_metric_name" != "$target_metric" ]; then
                metric_name=""
                continue
            fi
            
            metric_help=$(echo "$line" | cut -d ' ' -f 3-)

            if is_processed "$new_metric_name"; then
                 metric_name=""
                 continue
            fi
            processed_metrics+=("$new_metric_name")

            metric_name=$new_metric_name
            metric_type=""
            extra_desc=""
            extra_details=""
            current_best_example_line=""
            current_best_example_priority=0

            extra_block=$(awk -v metric="^$metric_name$" '/^METRIC_END$/ { found=0 } $0 ~ metric { found=1 } found' "$EXTRA_INFO_PROCESSED")
            extra_desc=$(echo "$extra_block" | awk '/^DESCRIPTION_START$/,/^DESCRIPTION_END$/' | sed '1d;$d' | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
            extra_details=$(echo "$extra_block" | awk '/^DETAILS_START$/,/^DETAILS_END$/' | sed '1d;$d' )

            echo "<a name=\"$metric_name\"></a>" >> "$MD_OUTPUT"
            echo "### $metric_name" >> "$MD_OUTPUT"; echo "" >> "$MD_OUTPUT"

            echo "<h3 id=\"$metric_name\">$metric_name</h3>" >> "$HTML_OUTPUT"

        elif [[ "$line" =~ ^#TYPE && ! -z "$metric_name" && "$metric_name" == $(echo "$line" | cut -d ' ' -f 2) ]]; then
            metric_type=$(echo "$line" | cut -d ' ' -f 3)

        elif [[ ! "$line" =~ ^# ]] && [[ ! -z "$metric_name" ]] && [[ "$line" == $metric_name* ]]; then
            line_priority=0
            db_value=""
            if [[ "$line" =~ database=\"([^\"]+)\" ]]; then
                db_value="${BASH_REMATCH[1]}"
                if [[ "$db_value" == "postgres" ]]; then
                    line_priority=2
                else
                    line_priority=1
                fi
            elif [[ "$line" == *"{"* ]]; then
                 line_priority=1
            else
                 line_priority=1
            fi

            if [[ "$line_priority" -gt "$current_best_example_priority" ]]; then
                current_best_example_line="$line"
                current_best_example_priority="$line_priority"
            elif [[ "$current_best_example_priority" -eq 0 && "$line_priority" -ge 1 ]]; then
                current_best_example_line="$line"
                current_best_example_priority="$line_priority"
            fi
        fi
    done < "$METRICS_FILE"
done

# Handle the last metric if it exists
if [ ! -z "$metric_name" ]; then
    echo "$metric_help Type is $metric_type." >> "$MD_OUTPUT"
    echo "" >> "$MD_OUTPUT"

    formatted_desc=""
    if [ ! -z "$extra_desc" ]; then
         formatted_desc=$(cat <<< "$extra_desc" | sed 's/^[[:space:]]*+//g' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' | paste -sd ' ')
    fi
    final_desc="${formatted_desc:-$metric_help}"
    echo "$final_desc" >> "$MD_OUTPUT"
    echo "" >> "$MD_OUTPUT"

    echo "<p>$metric_help Type is $metric_type.</p>" >> "$HTML_OUTPUT"
    echo "<p>$final_desc</p>" >> "$HTML_OUTPUT"

    process_attributes_and_example "$metric_name" "$current_best_example_line" "$extra_details"
fi

if $extension_section_started; then
    echo "</div>" >> "$HTML_OUTPUT"
fi

echo "  </body>" >> "$HTML_OUTPUT"
echo "</html>" >> "$HTML_OUTPUT"

rm -f "$METRICS_FILE" "$EXTRA_INFO_PROCESSED"

echo "Documentation generated:"
echo "- Markdown: $MD_OUTPUT"
echo "- HTML: $HTML_OUTPUT"