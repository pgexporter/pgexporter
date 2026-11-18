/*
 * Copyright (C) 2026 The pgexporter community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgexporter */
#include <pgexporter.h>
#include <console.h>
#include <console_provider.h>
#include <http.h>
#include <http_server.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <prometheus_client.h>
#include <management.h>
#include <message.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>

static int collect_simple_label_columns(struct console_category* category, char*** label_keys, int* label_key_count);
static const char* find_metric_label_value(struct console_metric* metric, const char* key);
static char* generate_metrics_table(struct console_category* category);
static char* generate_category_tabs(struct console_page* console);
static int home_page(SSL* client_ssl, int client_fd);
static int api_page(SSL* client_ssl, int client_fd);
static int console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result);
static int console_generate_html(struct console_page* console, char** html, size_t* html_size);
static int console_generate_json(struct console_page* console, char** json, size_t* json_size);
static int console_destroy(struct console_page* console);

static struct http_route console_routes[] = {
   {"/", home_page},
   {"/index.html", home_page},
   {"/api", api_page},
   {"/api/", api_page},
};

static int
console_init(int endpoint, const char* brand_name, const char* metric_prefix, struct console_page** result)
{
   struct console_page* console = NULL;
   const struct console_data_provider* provider = NULL;
   void* provider_ctx = NULL;
   struct console_query query = {
      .mode = "realtime",
      .timestamp = 0,
      .duration = 0,
   };

   (void)endpoint;

   if (result == NULL)
   {
      pgexporter_log_error("Invalid parameters for console init");
      goto error;
   }

   console = (struct console_page*)malloc(sizeof(struct console_page));
   if (console == NULL)
   {
      pgexporter_log_error("Failed to allocate console page");
      goto error;
   }

   memset(console, 0, sizeof(struct console_page));

   console->brand_name = brand_name ? strdup(brand_name) : strdup("Metrics Console");
   console->metric_prefix = metric_prefix ? strdup(metric_prefix) : NULL;

   console->status = (struct console_status*)malloc(sizeof(struct console_status));
   if (console->status == NULL)
   {
      pgexporter_log_error("Failed to allocate console status");
      goto error;
   }

   memset(console->status, 0, sizeof(struct console_status));

   provider = pgexporter_console_realtime_provider();
   if (provider == NULL || provider->init(&provider_ctx) != 0)
   {
      pgexporter_log_error("Failed to initialize console data provider");
      goto error;
   }

   if (provider->populate_page(provider_ctx, &query, console) != 0)
   {
      pgexporter_log_error("Failed to populate console page");
      provider->destroy(provider_ctx);
      goto error;
   }

   provider->destroy(provider_ctx);

   *result = console;

   return 0;

error:
   if (console != NULL)
   {
      console_destroy(console);
   }

   return 1;
}

static int
home_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* html = NULL;
   size_t html_size = 0;
   int status = MESSAGE_STATUS_OK;

   if (console_init(0, "pgexporter", "pgexporter_", &console))
   {
      pgexporter_log_error("Failed to initialize console");
      status = MESSAGE_STATUS_ERROR;
      goto done;
   }

   if (console_generate_html(console, &html, &html_size))
   {
      pgexporter_log_error("Failed to generate HTML");
      status = MESSAGE_STATUS_ERROR;
      goto done;
   }

   status = pgexporter_http_respond_ok(client_ssl, client_fd, "text/html; charset=utf-8", html, html_size);

done:
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_http_respond_400(client_ssl, client_fd);
   }
   free(html);
   if (console != NULL)
   {
      console_destroy(console);
   }

   return status;
}

static int
api_page(SSL* client_ssl, int client_fd)
{
   struct console_page* console = NULL;
   char* json = NULL;
   size_t json_size = 0;
   int status = MESSAGE_STATUS_OK;

   if (console_init(0, "pgexporter", "pgexporter_", &console))
   {
      pgexporter_log_error("Failed to initialize console for API");
      status = MESSAGE_STATUS_ERROR;
      goto done;
   }

   if (console_generate_json(console, &json, &json_size))
   {
      pgexporter_log_error("Failed to generate JSON");
      status = MESSAGE_STATUS_ERROR;
      goto done;
   }

   status = pgexporter_http_respond_ok(client_ssl, client_fd, "application/json; charset=utf-8", json, json_size);

done:
   if (status != MESSAGE_STATUS_OK)
   {
      pgexporter_http_respond_400(client_ssl, client_fd);
   }
   free(json);
   if (console != NULL)
   {
      console_destroy(console);
   }

   return status;
}

static int
console_generate_html(struct console_page* console, char** html, size_t* html_size)
{
   char* tabs_html = NULL;
   char* final_html = NULL;
   int status = 0;

   if (console == NULL || html == NULL || html_size == NULL)
   {
      pgexporter_log_error("Invalid parameters for HTML generation");
      status = 1;
      goto error;
   }

   final_html = pgexporter_append(final_html,
                                  "<!DOCTYPE html>\n"
                                  "<html>\n"
                                  "<head>\n"
                                  "<meta charset=\"UTF-8\">\n"
                                  "<title>Web Console</title>\n"
                                  "<style>\n"
                                  ":root { --bg: #fff; --text: #000; --border: #ccc; --header-bg: #fff; --header-border: #ddd; --th-bg: #eee; --hover-bg: #f5f5f5; --btn-bg: #f5f5f5; --btn-active-bg: #222; --btn-active-text: #fff; --shadow: rgba(0,0,0,0.03); --dropdown-shadow: rgba(0,0,0,0.08); }\n"
                                  "body.dark-mode { --bg: #1a1a1a; --text: #e0e0e0; --border: #444; --header-bg: #222; --header-border: #333; --th-bg: #2a2a2a; --hover-bg: #333; --btn-bg: #2a2a2a; --btn-active-bg: #0d7377; --btn-active-text: #fff; --shadow: rgba(0,0,0,0.3); --dropdown-shadow: rgba(0,0,0,0.5); }\n"
                                  "body { font-family: monospace; margin: 20px; background: var(--bg); color: var(--text); transition: background 0.3s, color 0.3s; }\n"
                                  "h1 { border-bottom: 1px solid var(--text); }\n"
                                  "h2 { margin-top: 12px; }\n"
                                  "table { border-collapse: collapse; width: 100%%; margin: 10px 0; }\n"
                                  "th, td { border: 1px solid var(--border); padding: 8px; text-align: left; }\n"
                                  "th { background-color: var(--th-bg); font-weight: bold; }\n"
                                  ".tab-bar { display: flex; gap: 18px; flex-wrap: wrap; align-items: center; justify-content: flex-start; margin: 12px 0 20px 0; }\n"
                                  ".tab-btn { border: 1px solid var(--border); background: var(--btn-bg); padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; color: var(--text); }\n"
                                  ".tab-btn.active { background: var(--btn-active-bg); color: var(--btn-active-text); }\n"
                                  ".tab-panel { display: none; }\n"
                                  ".tab-panel.active { display: block; }\n"
                                  ".view-toggle { display: flex; gap: 8px; align-items: center; }\n"
                                  ".view-btn { border: 1px solid var(--border); background: var(--btn-bg); padding: 6px 10px; cursor: pointer; border-radius: 4px; font-weight: 600; color: var(--text); }\n"
                                  ".view-btn.active { background: var(--btn-active-bg); color: var(--btn-active-text); }\n"
                                  ".col-simple-label { display: none; }\n"
                                  ".simple .col-simple-label { display: table-cell; }\n"
                                  ".simple .col-type, .simple .col-labels { display: none; }\n"
                                  ".tab-bar label { margin: 0; font-weight: 600; }\n"
                                  ".tab-bar select { padding: 6px 8px; border-radius: 4px; border: 1px solid var(--border); background: var(--bg); color: var(--text); }\n"
                                  ".search-row { display: flex; gap: 8px; align-items: center; margin-bottom: 10px; flex-wrap: wrap; }\n"
                                  ".search-row label { margin: 0; font-weight: 600; }\n"
                                  ".search-row input[type=\"search\"] { padding: 6px 8px; border-radius: 4px; border: 1px solid var(--border); background: var(--bg); color: var(--text); flex: 1 1 auto; width: auto; max-width: none; min-width: 52ch; }\n"
                                  ".dropdown { position: relative; display: inline-block; min-width: 180px; }\n"
                                  ".dropdown-btn { width: 100%%; text-align: left; padding: 6px 8px; border-radius: 4px; border: 1px solid var(--border); background: var(--bg); color: var(--text); cursor: pointer; font-family: inherit; }\n"
                                  ".dropdown-menu { display: none; position: absolute; top: 100%%; left: 0; right: 0; background: var(--bg); border: 1px solid var(--border); border-radius: 4px; margin-top: 4px; z-index: 2; max-height: 220px; overflow-y: auto; box-shadow: 0 2px 6px var(--dropdown-shadow); }\n"
                                  ".dropdown-menu.show { display: block; }\n"
                                  ".dropdown-option { display: block; padding: 6px 8px; cursor: pointer; color: var(--text); }\n"
                                  ".dropdown-option:hover { background: var(--hover-bg); }\n"
                                  ".dropdown-divider { border: 0; border-top: 1px solid var(--border); margin: 4px 0; }\n"
                                  ".header-box { position: relative; padding: 12px; background: var(--header-bg); border: 1px solid var(--header-border); border-radius: 8px; box-shadow: 0 1px 2px var(--shadow); margin-bottom: 14px; }\n"
                                  ".theme-toggle { position: absolute; top: 12px; right: 12px; background: var(--btn-bg); border: 1px solid var(--border); padding: 8px 14px; border-radius: 6px; cursor: pointer; font-weight: 600; font-size: 14px; transition: all 0.2s; }\n"
                                  ".theme-toggle:hover { background: var(--hover-bg); transform: scale(1.05); }\n"
                                  ".refresh-control { position: relative; display: inline-flex; margin-left: 8px; vertical-align: middle; }\n"
                                  ".refresh-btn { background: var(--btn-bg); border: 1px solid var(--border); border-right: 0; padding: 6px 12px; border-radius: 6px 0 0 6px; cursor: pointer; font-weight: 600; font-size: 12px; transition: all 0.2s; color: var(--text); }\n"
                                  ".refresh-btn.loading { opacity: 0.6; cursor: not-allowed; }\n"
                                  ".refresh-arrow-btn { background: var(--btn-bg); border: 1px solid var(--border); border-radius: 0 6px 6px 0; padding: 6px 10px; cursor: pointer; font-size: 12px; transition: all 0.2s; color: var(--text); }\n"
                                  ".refresh-dropdown-menu { display: none; position: absolute; top: calc(100%% + 4px); left: 0; min-width: 120px; background: var(--bg); border: 1px solid var(--border); border-radius: 6px; z-index: 4; box-shadow: 0 2px 6px var(--dropdown-shadow); overflow: hidden; }\n"
                                  ".refresh-dropdown-menu.show { display: block; }\n"
                                  ".refresh-interval-option { display: block; width: 100%%; text-align: left; border: 0; background: transparent; color: var(--text); padding: 8px 10px; cursor: pointer; font-family: inherit; }\n"
                                  ".refresh-interval-option:hover { background: var(--hover-bg); }\n"
                                  ".refresh-interval-option.selected { background: var(--hover-bg); font-weight: 700; }\n"
                                  ".tab-container { margin-top: 8px; }\n"
                                  "</style>\n"
                                  "</head>\n"
                                  "<body>\n"
                                  "<div class=\"header-box\">\n"
                                  "<button id=\"theme-toggle\" class=\"theme-toggle\" title=\"Toggle dark mode\">🌙 Dark</button>\n"
                                  "<h1>Web Console</h1>\n");
   final_html = pgexporter_format_and_append(final_html, "<p><strong>Service:</strong> %s | <strong>Version:</strong> %s | <strong>Updated:</strong> %s | ",
                                             console->status->status ? console->status->status : "Unknown",
                                             console->status->version ? console->status->version : "Unknown",
                                             console->status->last_updated ? console->status->last_updated : "Never");

   final_html = pgexporter_append(final_html, "<span id=\"refresh-control\" class=\"refresh-control\"><button type=\"button\" id=\"refresh-btn\" class=\"refresh-btn\" title=\"Refresh all metrics\">Refresh</button><button type=\"button\" id=\"refresh-dropdown-btn\" class=\"refresh-arrow-btn\" title=\"Auto refresh interval\" aria-haspopup=\"true\" aria-expanded=\"false\">&#9662;</button><span id=\"refresh-dropdown-menu\" class=\"refresh-dropdown-menu\"><button type=\"button\" class=\"refresh-interval-option\" data-minutes=\"0\">Clear</button><button type=\"button\" class=\"refresh-interval-option\" data-minutes=\"5\">5 min</button><button type=\"button\" class=\"refresh-interval-option\" data-minutes=\"10\">10 min</button><button type=\"button\" class=\"refresh-interval-option\" data-minutes=\"15\">15 min</button><button type=\"button\" class=\"refresh-interval-option\" data-minutes=\"20\">20 min</button></span></span></p> </div>\n");

   tabs_html = generate_category_tabs(console);
   if (tabs_html != NULL)
   {
      final_html = pgexporter_append(final_html, tabs_html);
      free(tabs_html);
      tabs_html = NULL;
   }

   final_html = pgexporter_append(final_html,
                                  "<script>\n"
                                  "(function(){\n"
                                  "  const themeToggle = document.getElementById('theme-toggle');\n"
                                  "  const savedTheme = localStorage.getItem('theme');\n"
                                  "  if(savedTheme === 'dark'){\n"
                                  "    document.body.classList.add('dark-mode');\n"
                                  "    themeToggle.textContent = '☀️ Light';\n"
                                  "  }\n"
                                  "  themeToggle.addEventListener('click', function(){\n"
                                  "    document.body.classList.toggle('dark-mode');\n"
                                  "    if(document.body.classList.contains('dark-mode')){\n"
                                  "      themeToggle.textContent = '☀️ Light';\n"
                                  "      localStorage.setItem('theme', 'dark');\n"
                                  "    } else {\n"
                                  "      themeToggle.textContent = '🌙 Dark';\n"
                                  "      localStorage.setItem('theme', 'light');\n"
                                  "    }\n"
                                  "  });\n"
                                  "  const refreshBtn = document.getElementById('refresh-btn');\n"
                                  "  const refreshDropdownBtn = document.getElementById('refresh-dropdown-btn');\n"
                                  "  const refreshDropdownMenu = document.getElementById('refresh-dropdown-menu');\n"
                                  "  const refreshIntervalOptions = document.querySelectorAll('.refresh-interval-option');\n"
                                  "  const refreshIntervalKey = 'pgexporter.autoRefreshMinutes';\n"
                                  "  let autoRefreshMinutes = parseInt(localStorage.getItem(refreshIntervalKey) || '0', 10);\n"
                                  "  let autoRefreshTimer = null;\n"
                                  "  if (![0, 5, 10, 15, 20].includes(autoRefreshMinutes)) {\n"
                                  "    autoRefreshMinutes = 0;\n"
                                  "  }\n"
                                  "  function updateRefreshButtonText(){\n"
                                  "    if (!refreshBtn) { return; }\n"
                                  "    refreshBtn.textContent = autoRefreshMinutes > 0 ? ('Refresh (' + autoRefreshMinutes + 'm)') : 'Refresh';\n"
                                  "  }\n"
                                  "  function updateRefreshMenuSelection(){\n"
                                  "    refreshIntervalOptions.forEach(function(option){\n"
                                  "      const optionMinutes = parseInt(option.getAttribute('data-minutes') || '0', 10);\n"
                                  "      if (optionMinutes === autoRefreshMinutes) {\n"
                                  "        option.classList.add('selected');\n"
                                  "      } else {\n"
                                  "        option.classList.remove('selected');\n"
                                  "      }\n"
                                  "    });\n"
                                  "  }\n"
                                  "  function scheduleAutoRefresh(){\n"
                                  "    if (autoRefreshTimer) {\n"
                                  "      clearTimeout(autoRefreshTimer);\n"
                                  "      autoRefreshTimer = null;\n"
                                  "    }\n"
                                  "    if (autoRefreshMinutes > 0) {\n"
                                  "      autoRefreshTimer = setTimeout(function(){\n"
                                  "        location.reload();\n"
                                  "      }, autoRefreshMinutes * 60 * 1000);\n"
                                  "    }\n"
                                  "  }\n"
                                  "  function closeRefreshDropdown(){\n"
                                  "    if (refreshDropdownMenu) {\n"
                                  "      refreshDropdownMenu.classList.remove('show');\n"
                                  "    }\n"
                                  "    if (refreshDropdownBtn) {\n"
                                  "      refreshDropdownBtn.setAttribute('aria-expanded', 'false');\n"
                                  "    }\n"
                                  "  }\n"
                                  "  function setAutoRefresh(minutes){\n"
                                  "    if (![0, 5, 10, 15, 20].includes(minutes)) {\n"
                                  "      return;\n"
                                  "    }\n"
                                  "    autoRefreshMinutes = minutes;\n"
                                  "    if (minutes === 0) {\n"
                                  "      localStorage.removeItem(refreshIntervalKey);\n"
                                  "    } else {\n"
                                  "      localStorage.setItem(refreshIntervalKey, String(minutes));\n"
                                  "    }\n"
                                  "    updateRefreshButtonText();\n"
                                  "    updateRefreshMenuSelection();\n"
                                  "    scheduleAutoRefresh();\n"
                                  "  }\n"
                                  "  if (refreshBtn) {\n"
                                  "    refreshBtn.addEventListener('click', function(){\n"
                                  "      location.reload();\n"
                                  "    });\n"
                                  "  }\n"
                                  "  if (refreshDropdownBtn && refreshDropdownMenu) {\n"
                                  "    refreshDropdownBtn.addEventListener('click', function(e){\n"
                                  "      e.stopPropagation();\n"
                                  "      const isOpen = refreshDropdownMenu.classList.toggle('show');\n"
                                  "      refreshDropdownBtn.setAttribute('aria-expanded', isOpen ? 'true' : 'false');\n"
                                  "    });\n"
                                  "    refreshIntervalOptions.forEach(function(option){\n"
                                  "      option.addEventListener('click', function(){\n"
                                  "        const minutes = parseInt(option.getAttribute('data-minutes') || '0', 10);\n"
                                  "        setAutoRefresh(minutes);\n"
                                  "        closeRefreshDropdown();\n"
                                  "      });\n"
                                  "    });\n"
                                  "    document.addEventListener('click', function(e){\n"
                                  "      if (!e.target.closest('#refresh-control')) {\n"
                                  "        closeRefreshDropdown();\n"
                                  "      }\n"
                                  "    });\n"
                                  "  }\n"
                                  "  updateRefreshButtonText();\n"
                                  "  updateRefreshMenuSelection();\n"
                                  "  scheduleAutoRefresh();\n"
                                  "})();\n"
                                  "</script>\n"
                                  "</body>\n</html>\n");

   *html = final_html;
   *html_size = strlen(final_html);

   return 0;

error:
   if (tabs_html != NULL)
   {
      free(tabs_html);
   }
   if (final_html != NULL)
   {
      free(final_html);
   }
   return status;
}

static int
console_generate_json(struct console_page* console, char** json, size_t* json_size)
{
   char* json_buffer = NULL;
   int status = 0;

   if (console == NULL || json == NULL || json_size == NULL)
   {
      pgexporter_log_error("Invalid parameters for JSON generation");
      status = 1;
      goto error;
   }

   json_buffer = pgexporter_format_and_append(json_buffer,
                                              "{\"mode\":\"%s\",\"categories\":[",
                                              console->mode != NULL ? console->mode : "realtime");

   for (int i = 0; i < console->category_count; i++)
   {
      struct console_category* cat = &console->categories[i];

      if (i > 0)
      {
         json_buffer = pgexporter_append(json_buffer, ",");
      }

      json_buffer = pgexporter_format_and_append(json_buffer,
                                                 "{\"name\":\"%s\",\"metrics\":[",
                                                 cat->name);

      for (int j = 0; j < cat->metric_count; j++)
      {
         struct console_metric* metric = &cat->metrics[j];

         if (j > 0)
         {
            json_buffer = pgexporter_append(json_buffer, ",");
         }

         json_buffer = pgexporter_format_and_append(json_buffer,
                                                    "{\"name\":\"%s\",\"type\":\"%s\",\"value\":%.2f}",
                                                    metric->name,
                                                    metric->type,
                                                    metric->value);
      }

      json_buffer = pgexporter_append(json_buffer, "]}");
   }

   json_buffer = pgexporter_append(json_buffer, "]}");

   *json = json_buffer;
   *json_size = strlen(json_buffer);

   return 0;

error:
   if (json_buffer != NULL)
   {
      free(json_buffer);
   }
   return status;
}

static int
console_destroy(struct console_page* console)
{
   if (console != NULL)
   {
      if (console->categories != NULL)
      {
         for (int i = 0; i < console->category_count; i++)
         {
            struct console_category* cat = &console->categories[i];
            free(cat->name);

            if (cat->metrics != NULL)
            {
               for (int j = 0; j < cat->metric_count; j++)
               {
                  pgexporter_console_metric_free(&cat->metrics[j]);
               }
               free(cat->metrics);
            }
         }
         free(console->categories);
      }

      if (console->status != NULL)
      {
         free(console->status->status);
         free(console->status->version);
         free(console->status->last_updated);

         if (console->status->servers != NULL)
         {
            for (int i = 0; i < console->status->num_servers; i++)
            {
               free(console->status->servers[i].name);
            }
            free(console->status->servers);
         }

         free(console->status);
      }

      free(console->brand_name);
      free(console->metric_prefix);
      free(console->mode);
      free(console);
   }

   return 0;
}

/**
 * Helper: Generate HTML table for metrics in a category
 */
static char*
generate_metrics_table(struct console_category* category)
{
   char* table_html = NULL;
   char** label_keys = NULL;
   int label_key_count = 0;

   if (category == NULL || category->metric_count == 0)
   {
      return pgexporter_append(NULL, "<p>No metrics</p>\n");
   }

   if (collect_simple_label_columns(category, &label_keys, &label_key_count))
   {
      pgexporter_log_warn("Failed to collect simple view label columns for category %s", category->name != NULL ? category->name : "unknown");
   }

   table_html = pgexporter_append(table_html,
                                  "<table class=\"metrics-table\">\n"
                                  "<tr><th class=\"col-name\">Name</th><th class=\"col-type\">Type</th><th class=\"col-value\">Value</th><th class=\"col-labels\">Labels</th>");

   for (int i = 0; i < label_key_count; i++)
   {
      table_html = pgexporter_format_and_append(table_html,
                                                "<th class=\"col-simple-label\">%s</th>",
                                                label_keys[i]);
   }

   table_html = pgexporter_append(table_html, "</tr>\n");

   for (int m_idx = 0; m_idx < category->metric_count; m_idx++)
   {
      struct console_metric* metric = &category->metrics[m_idx];
      char* labels_str = NULL;
      bool first_label = true;

      if (metric->label_count > 0)
      {
         for (int j = 0; j < metric->label_count; j++)
         {
            if (metric->labels[j].key == NULL || metric->labels[j].value == NULL)
            {
               continue;
            }

            if (!first_label)
            {
               labels_str = pgexporter_append(labels_str, ", ");
            }

            labels_str = pgexporter_append(labels_str, metric->labels[j].key);
            labels_str = pgexporter_append_char(labels_str, '=');
            labels_str = pgexporter_append(labels_str, metric->labels[j].value);
            first_label = false;
         }
      }

      char value_str[64];
      long long int_value = (long long)metric->value;
      if ((double)int_value == metric->value && int_value >= LLONG_MIN && int_value <= LLONG_MAX)
      {
         pgexporter_snprintf(value_str, sizeof(value_str), "%lld", int_value);
      }
      else
      {
         pgexporter_snprintf(value_str, sizeof(value_str), "%.2f", metric->value);
      }

      table_html = pgexporter_format_and_append(table_html,
                                                "<tr data-server=\"%s\"><td class=\"col-name\">%s</td><td class=\"col-type\">%s</td><td class=\"col-value\">%s</td><td class=\"col-labels\">%s</td>",
                                                metric->server != NULL ? metric->server : "all",
                                                metric->name,
                                                metric->type,
                                                value_str,
                                                labels_str != NULL ? labels_str : "");

      for (int k = 0; k < label_key_count; k++)
      {
         const char* label_value = find_metric_label_value(metric, label_keys[k]);
         table_html = pgexporter_format_and_append(table_html,
                                                   "<td class=\"col-simple-label\">%s</td>",
                                                   label_value != NULL ? label_value : "");
      }

      table_html = pgexporter_append(table_html, "</tr>\n");

      free(labels_str);
   }

   table_html = pgexporter_append(table_html, "</table>\n");

   if (label_keys != NULL)
   {
      for (int i = 0; i < label_key_count; i++)
      {
         free(label_keys[i]);
      }
      free(label_keys);
   }

   return table_html;
}

static int
collect_simple_label_columns(struct console_category* category, char*** label_keys, int* label_key_count)
{
   char** keys = NULL;
   int count = 0;

   if (category == NULL || label_keys == NULL || label_key_count == NULL)
   {
      return 1;
   }

   *label_keys = NULL;
   *label_key_count = 0;

   for (int m_idx = 0; m_idx < category->metric_count; m_idx++)
   {
      struct console_metric* metric = &category->metrics[m_idx];

      for (int l_idx = 0; l_idx < metric->label_count; l_idx++)
      {
         char* key = metric->labels[l_idx].key;
         bool exists = false;

         if (key == NULL || metric->labels[l_idx].value == NULL)
         {
            continue;
         }

         if (strcmp(key, "endpoint") == 0)
         {
            continue;
         }

         for (int i = 0; i < count; i++)
         {
            if (strcmp(keys[i], key) == 0)
            {
               exists = true;
               break;
            }
         }

         if (!exists)
         {
            char** resized = realloc(keys, (count + 1) * sizeof(char*));

            if (resized == NULL)
            {
               goto error;
            }

            keys = resized;
            keys[count] = strdup(key);
            if (keys[count] == NULL)
            {
               goto error;
            }
            count++;
         }
      }
   }

   *label_keys = keys;
   *label_key_count = count;

   return 0;

error:
   if (keys != NULL)
   {
      for (int i = 0; i < count; i++)
      {
         free(keys[i]);
      }
      free(keys);
   }

   return 1;
}

static const char*
find_metric_label_value(struct console_metric* metric, const char* key)
{
   if (metric == NULL || key == NULL)
   {
      return NULL;
   }

   for (int i = 0; i < metric->label_count; i++)
   {
      if (metric->labels[i].key == NULL || metric->labels[i].value == NULL)
      {
         continue;
      }

      if (strcmp(metric->labels[i].key, key) == 0)
      {
         return metric->labels[i].value;
      }
   }

   return NULL;
}

/**
 * Helper: Generate tab buttons and content sections for all categories
 */
static char*
generate_category_tabs(struct console_page* console)
{
   char* tabs_html = NULL;

   if (console == NULL || console->category_count == 0)
   {
      return strdup("<p>No metrics available</p>\n");
   }

   tabs_html = pgexporter_append(tabs_html,
                                 "<div class=\"tab-container\">\n"
                                 "<div class=\"search-row\">\n"
                                 "<label for=\"metric-search\">Search:</label>\n"
                                 "<input type=\"search\" id=\"metric-search\" placeholder=\"Search across all categories (min 3 characters)\" autocomplete=\"off\">\n"
                                 "</div>\n"
                                 "<div class=\"tab-bar\">\n");

   {
      char view_buf[256];
      pgexporter_snprintf(view_buf, sizeof(view_buf),
                          "<div class=\"view-toggle\">\n"
                          "<label for=\"view-select\">View:</label>\n"
                          "<select id=\"view-select\">\n"
                          "<option value=\"simple\" selected>Simple</option>\n"
                          "<option value=\"detailed\">Advanced</option>\n"
                          "</select>\n"
                          "</div>\n");

      tabs_html = pgexporter_append(tabs_html, view_buf);
   }

   tabs_html = pgexporter_append(tabs_html, "<label for=\"category-select\">Category:</label>\n");
   tabs_html = pgexporter_append(tabs_html, "<select id=\"category-select\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      tabs_html = pgexporter_format_and_append(tabs_html,
                                               "<option value=\"cat-%d\"%s>%s</option>\n",
                                               i,
                                               i == 0 ? " selected" : "",
                                               console->categories[i].name);
   }

   tabs_html = pgexporter_append(tabs_html, "</select>\n");

   tabs_html = pgexporter_append(tabs_html,
                                 "<label for=\"server-dropdown-btn\">Servers:</label>\n"
                                 "<div class=\"dropdown\" id=\"server-dropdown\">\n"
                                 "<button type=\"button\" id=\"server-dropdown-btn\" class=\"dropdown-btn\">All Selected</button>\n"
                                 "<div id=\"server-dropdown-menu\" class=\"dropdown-menu\">\n"
                                 "<label class=\"dropdown-option\"><input type=\"checkbox\" id=\"server-all\" checked> <strong>All</strong></label>\n"
                                 "<hr class=\"dropdown-divider\">\n");

   if (console->status && console->status->servers && console->status->num_servers > 0)
   {
      for (int s = 0; s < console->status->num_servers; s++)
      {
         const char* name = console->status->servers[s].name ? console->status->servers[s].name : "server";
         tabs_html = pgexporter_format_and_append(tabs_html,
                                                  "<label class=\"dropdown-option\"><input type=\"checkbox\" class=\"server-item\" value=\"%s\" checked> %s</label>\n",
                                                  name, name);
      }
   }
   else
   {
      tabs_html = pgexporter_append(tabs_html,
                                    "<label class=\"dropdown-option\"><input type=\"checkbox\" disabled> No servers</label>\n");
   }

   tabs_html = pgexporter_append(tabs_html, "</div>\n</div>\n");

   tabs_html = pgexporter_append(tabs_html, "</div>\n<div class=\"tab-panels\">\n");

   for (int i = 0; i < console->category_count; i++)
   {
      char* metrics_table = NULL;
      tabs_html = pgexporter_format_and_append(tabs_html,
                                               "<div class=\"tab-panel\" id=\"cat-%d\" style=\"display:%s\">\n<h2>%s</h2>\n",
                                               i,
                                               i == 0 ? "block" : "none",
                                               console->categories[i].name);

      metrics_table = generate_metrics_table(&console->categories[i]);
      if (metrics_table != NULL)
      {
         tabs_html = pgexporter_append(tabs_html, metrics_table);
         free(metrics_table);
      }

      tabs_html = pgexporter_append(tabs_html, "</div>\n");
   }

   tabs_html = pgexporter_append(tabs_html, "</div>\n</div>\n");

   tabs_html = pgexporter_append(tabs_html,
                                 "<script>\n"
                                 "(function(){\n"
                                 "  const select = document.getElementById('category-select');\n"
                                 "  const searchInput = document.getElementById('metric-search');\n"
                                 "  const panels = document.querySelectorAll('.tab-panel');\n"
                                 "  const container = document.querySelector('.tab-container');\n"
                                 "  const serverBtn = document.getElementById('server-dropdown-btn');\n"
                                 "  const serverMenu = document.getElementById('server-dropdown-menu');\n"
                                 "  const serverAll = document.getElementById('server-all');\n"
                                 "  const serverItems = document.querySelectorAll('.server-item');\n"
                                 "  function getSelectedServers(){\n"
                                 "    const checked = document.querySelectorAll('.server-item:checked');\n"
                                 "    return Array.from(checked).map(function(item){ return item.value; });\n"
                                 "  }\n"
                                 "  function rowMatchesServer(row, selectedServers){\n"
                                 "    const rowServer = row.getAttribute('data-server');\n"
                                 "    return !!(serverAll && serverAll.checked) || selectedServers.length === 0 || selectedServers.includes(rowServer) || rowServer === 'all';\n"
                                 "  }\n"
                                 "  function rowMatchesSearch(row, query){\n"
                                 "    if (!query) { return true; }\n"
                                 "    return row.textContent.toLowerCase().includes(query);\n"
                                 "  }\n"
                                 "  function applyFilters(){\n"
                                 "    const selectedId = select ? select.value : null;\n"
                                 "    const selectedServers = getSelectedServers();\n"
                                 "    const rawQuery = searchInput ? searchInput.value.trim().toLowerCase() : '';\n"
                                 "    const query = rawQuery.length >= 3 ? rawQuery : '';\n"
                                 "    const searchActive = query.length > 0;\n"
                                 "    panels.forEach(function(panel){\n"
                                 "      const categoryName = panel.querySelector('h2') ? panel.querySelector('h2').textContent.toLowerCase() : '';\n"
                                 "      const categoryMatch = query && categoryName.includes(query);\n"
                                 "      const rows = panel.querySelectorAll('.metrics-table tr[data-server]');\n"
                                 "      let visibleRows = 0;\n"
                                 "      rows.forEach(function(row){\n"
                                 "        const visible = categoryMatch ? rowMatchesServer(row, selectedServers) : (rowMatchesServer(row, selectedServers) && rowMatchesSearch(row, query));\n"
                                 "        row.style.display = visible ? '' : 'none';\n"
                                 "        if (visible) { visibleRows++; }\n"
                                 "      });\n"
                                 "      if (searchActive) {\n"
                                 "        panel.style.display = visibleRows > 0 ? 'block' : 'none';\n"
                                 "      } else {\n"
                                 "        panel.style.display = panel.id === selectedId ? 'block' : 'none';\n"
                                 "      }\n"
                                 "    });\n"
                                 "  }\n"
                                 "  // View mode dropdown\n"
                                 "  const viewSelect = document.getElementById('view-select');\n"
                                 "  if(viewSelect){\n"
                                 "    if(viewSelect.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
                                 "    viewSelect.addEventListener('change', function(){\n"
                                 "      if(this.value === 'simple'){ container.classList.add('simple'); } else { container.classList.remove('simple'); }\n"
                                 "    });\n"
                                 "  }\n"
                                 "  // Server dropdown\n"
                                 "  if(serverBtn && serverMenu && serverAll){\n"
                                 "    serverBtn.addEventListener('click', function(e){\n"
                                 "      e.stopPropagation();\n"
                                 "      serverMenu.classList.toggle('show');\n"
                                 "    });\n"
                                 "    document.addEventListener('click', function(e){\n"
                                 "      if(!e.target.closest('#server-dropdown')){\n"
                                 "        serverMenu.classList.remove('show');\n"
                                 "      }\n"
                                 "    });\n"
                                 "    function updateServerText(){\n"
                                 "      const checked = document.querySelectorAll('.server-item:checked');\n"
                                 "      if(serverAll.checked){\n"
                                 "        serverBtn.textContent = 'All Selected';\n"
                                 "      } else if(checked.length === 0){\n"
                                 "        serverBtn.textContent = 'None Selected';\n"
                                 "      } else {\n"
                                 "        const vals = Array.from(checked).map(i => i.value);\n"
                                 "        serverBtn.textContent = vals.join(', ');\n"
                                 "      }\n"
                                 "    }\n"
                                 "    serverAll.addEventListener('change', function(){\n"
                                 "      serverItems.forEach(i => { i.checked = serverAll.checked; });\n"
                                 "      serverAll.indeterminate = false;\n"
                                 "      updateServerText();\n"
                                 "      applyFilters();\n"
                                 "    });\n"
                                 "    serverItems.forEach(i => {\n"
                                 "      i.addEventListener('change', function(){\n"
                                 "        const checkedCount = document.querySelectorAll('.server-item:checked').length;\n"
                                 "        serverAll.checked = (checkedCount === serverItems.length);\n"
                                 "        serverAll.indeterminate = (checkedCount > 0 && checkedCount < serverItems.length);\n"
                                 "        updateServerText();\n"
                                 "        applyFilters();\n"
                                 "      });\n"
                                 "    });\n"
                                 "    updateServerText();\n"
                                 "  }\n"
                                 "  if(select){\n"
                                 "    select.addEventListener('change', applyFilters);\n"
                                 "  }\n"
                                 "  if(searchInput){\n"
                                 "    searchInput.addEventListener('input', applyFilters);\n"
                                 "  }\n"
                                 "  applyFilters();\n"
                                 "})();\n"
                                 "</script>\n");

   return tabs_html;
}

void
pgexporter_console(SSL* client_ssl, int client_fd)
{
   struct http_server_request* req = NULL;
   int status = MESSAGE_STATUS_OK;
   struct configuration* config;

   pgexporter_start_logging();
   pgexporter_memory_init();

   config = (struct configuration*)shmem;

   if (client_ssl)
   {
      int ssl_status = pgexporter_http_server_ssl_accept(client_ssl, client_fd);

      if (ssl_status == MESSAGE_STATUS_ERROR)
      {
         pgexporter_log_error("Console: SSL accept failed");
         goto error;
      }

      if (ssl_status == MESSAGE_STATUS_ZERO)
      {
         /*
          * Plain HTTP on a TLS port — redirect to HTTPS. Read the raw message
          * here (not via pgexporter_http_server_parse) because we need the path
          * for the redirect URL and the process exits immediately after.
          */
         struct message* redirect_msg = NULL;
         char* path = "/";
         char* base_url = NULL;

         if (pgexporter_read_timeout_message(NULL, client_fd, (int)pgexporter_time_convert(config->authentication_timeout, FORMAT_TIME_S), &redirect_msg) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("Failed to read redirect message");
            goto error;
         }

         char* path_start = strstr(redirect_msg->data, " ");
         if (path_start)
         {
            path_start++;
            char* path_end = strstr(path_start, " ");
            if (path_end)
            {
               *path_end = '\0';
               path = path_start;
            }
         }

         base_url = pgexporter_format_and_append(base_url, "https://localhost:%d%s", config->console, path);

         if (pgexporter_http_respond_redirect(NULL, client_fd, base_url) != MESSAGE_STATUS_OK)
         {
            pgexporter_log_error("Failed to redirect to: %s", base_url);
            free(base_url);
            goto error;
         }

         free(base_url);
         pgexporter_close_ssl(client_ssl);
         pgexporter_disconnect(client_fd);
         pgexporter_memory_destroy();
         pgexporter_stop_logging();
         exit(0);
      }
      /* MESSAGE_STATUS_OK: TLS handshake done, proceed to parse */
   }

   if (pgexporter_http_server_parse(client_ssl, client_fd, &req) != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgexporter_http_server_dispatch(client_ssl, client_fd, req,
                                            console_routes,
                                            sizeof(console_routes) / sizeof(console_routes[0]));

error:
   pgexporter_http_server_request_destroy(req);
   pgexporter_close_ssl(client_ssl);
   pgexporter_disconnect(client_fd);

   pgexporter_memory_destroy();
   pgexporter_stop_logging();

   if (status == MESSAGE_STATUS_OK)
   {
      exit(0);
   }

   exit(1);
}
