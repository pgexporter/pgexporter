/*
 * Copyright (C) 2024 The pgexporter community
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

#ifndef PGEXPORTER_PROMETHUS_CLIENT_H
#define PGEXPORTER_PROMETHUS_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgexporter.h>
#include <art.h>
#include <http.h>

#include <stdbool.h>
#include <stdio.h>

/**
 * @struct prometheus_bridge
 * Prometheus metrics from multiple endpoints
 */
struct prometheus_bridge
{
   struct art* metrics; /**< prometheus_metric::name -> ValueRef<prometheus_metric> */
};

/**
 * @struct prometheus_metric
 * Description
 */
struct prometheus_metric
{
   char* name;                /**< The name of the metric */
   char* help;                /**< The #HELP of the metric */
   char* type;                /**< The #TYPE of the metric */
   struct deque* definitions; /**< The attributes of the metric - ValueRef<prometheus_attributes> */
};

/**
 * @struct prometheus_attributes
 * The definition of the attributes for a metric
 */
struct prometheus_attributes
{
   struct deque* attributes; /**< Each attribute - ValueRef<prometheus_attribute> */
   struct deque* values;     /**< The values - ValueRef<prometheus_value> */
};

/**
 * @struct prometheus_attribute
 * An attribute
 */
struct prometheus_attribute
{
   char* key;   /**< The key */
   char* value; /**< The value */
};

/**
 * @struct prometheus_value
 * A value
 */
struct prometheus_value
{
   time_t timestamp; /**< The timestamp */
   char* value;      /**< The value */
};

/**
 * Create the bridge
 * @param bridge The resulting bridge
 * @return 0 if success, otherwise 1
 */
int
pgexporter_prometheus_client_create_bridge(struct prometheus_bridge** bridge);

/**
 * Destroy the bridge
 * @param bridge The resulting bridge
 * @return 0 if success, otherwise 1
 */
int
pgexporter_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge);

/**
 * Get a response from a Prometheus endpoint
 * @param url The URL
 * @param metrics The resulting metrics
 * @return 0 if success, otherwise 1
 */
int
pgexporter_prometheus_client_get(char* url, struct deque** metrics);

/**
 * Merge metrics into the bridge
 * @param bridge The bridge
 * @param metrics The resulting metrics
 * @return 0 if success, otherwise 1
 */
int
pgexporter_prometheus_client_merge(struct prometheus_bridge* bridge, struct deque* metrics);

#ifdef __cplusplus
}
#endif

#endif
