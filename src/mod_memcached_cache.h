/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MOD_MEMCACHED_H
#define MOD_MEMCACHED_H

typedef struct {
  char *info;
  char *h_in;
  char *h_out;
  char *body;
  apr_bucket_brigade *headers_bb;
  apr_bucket_brigade *body_bb;
} memcached_cache_object_t;

typedef enum {
  PARSE_UNKNOWN = 0,
  PARSE_INFO,
  PARSE_HEADERS_OUT,
  PARSE_HEADERS_IN,
  PARSE_BODY
} parse_state;

/* memcached server stuff */

#define DEFAULT_MAX_SERVERS 10
#define DEFAULT_MIN 2
#define DEFAULT_SMAX 6
#define DEFAULT_MAX 10
#define DEFAULT_TTL 10

typedef struct {
  const char *host;
  apr_port_t port;
  apr_memcache_server_t *server;
} memcached_cache_server_t;


/* configuration */

typedef struct {

  /* contains our list of memcached_cache_server_t */
  apr_array_header_t *servers;
  apr_memcache_t *memcache;
  apr_uint32_t min;
  apr_uint32_t smax;
  apr_uint32_t max;
  apr_uint32_t ttl;
} memcached_cache_conf_t;

#endif /* MOD_MEMCACHED_H */

