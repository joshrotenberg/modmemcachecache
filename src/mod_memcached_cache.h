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
  char *body;
  apr_size_t blen;
  apr_bucket_brigade *headers_bb;
  apr_bucket_brigade *body_bb;
  apr_memcache_t *mc;
  apr_table_t *req_hdrs;
  apr_table_t *resp_hdrs;
} memcached_cache_object_t;

/* memcached server stuff */

#define DEFAULT_MAX_SERVERS 10
#define DEFAULT_MIN 5
#define DEFAULT_SMAX 10
#define DEFAULT_MAX 15
#define DEFAULT_TTL 10

typedef struct {
  const char *host;
  apr_port_t port;
  apr_memcache_server_t *server;
} memcached_cache_server_t;


/* configuration */

#define DEFAULT_MIN_SIZE 1
#define DEFAULT_MAX_SIZE 1048576

typedef struct {

  /* contains our list of memcached_cache_server_t */
  apr_array_header_t *servers;
  
  apr_memcache_t *memcache;
  apr_uint32_t conn_min;
  apr_uint32_t conn_smax;
  apr_uint32_t conn_max;
  apr_uint32_t conn_ttl;
  apr_uint32_t max_servers;
  apr_off_t min_size;
  apr_off_t max_size;
} memcached_cache_conf_t;

#endif /* MOD_MEMCACHED_H */

