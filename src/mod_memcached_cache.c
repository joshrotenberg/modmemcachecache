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

/*
 * mod_memcached: Memcached backed HTTP 1.1 Cache.
 * author: josh rotenberg 
 *
 * This module implements file caching for busy static files into one (or more)
 * memcached servers configured to store data. 
 *
 * pros:
 * - cached data can be shared across multiple apache instances, for example
 *   if many 
*/

#include "apr_strings.h"
#include "apr_memcache.h"
#include <mod_cache.h>
#include "mod_memcached_cache.h"
#include "ap_provider.h"

module AP_MODULE_DECLARE_DATA memcached_cache_module;

/* provider methods */
static int create_entity(cache_handle_t *h, request_rec *r, 
                         const char *key, apr_off_t len);
static int remove_entity(cache_handle_t *h);
static apr_status_t store_headers(cache_handle_t *h, request_rec *r, 
                                  cache_info *i);
static apr_status_t store_body(cache_handle_t *h, request_rec *r,
                               apr_bucket_brigade *b);
static apr_status_t recall_headers(cache_handle_t *h, request_rec *r);
static apr_status_t recall_body(cache_handle_t *h, apr_pool_t *p, apr_bucket_brigade *b);

static void *memcached_create_config(apr_pool_t *p, server_rec *s)
{
  memcached_cache_conf_t *conf = 
    apr_pcalloc(p, sizeof(memcached_cache_conf_t));
  
  
  conf->servers = apr_array_make(p, 10, sizeof(memcached_cache_server_t));
  return conf;
}

static int create_entity(cache_handle_t *h, request_rec *r, 
                         const char *key, apr_off_t len)
{

  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);
  cache_object_t *obj;
  memcached_cache_object_t *mobj;
  apr_size_t dlen;
  char *result;
  apr_status_t rv;

  h->cache_obj = obj = apr_pcalloc(r->pool, sizeof(cache_object_t));
  obj->vobj = mobj = apr_pcalloc(r->pool, sizeof(memcached_cache_object_t));

  obj->key = apr_pstrdup(r->pool, key);

  mobj->ikey = apr_pstrcat(r->pool, INFO_PREFIX, key, NULL);
  mobj->hikey = apr_pstrcat(r->pool, HEADER_IN_PREFIX, key, NULL);
  mobj->hokey = apr_pstrcat(r->pool, HEADER_OUT_PREFIX, key, NULL);
  mobj->bkey = apr_pstrcat(r->pool, BODY_PREFIX, key, NULL);
  mobj->tkey = apr_pstrcat(r->pool, TEMP_PREFIX, key, NULL);
  mobj->body_bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);

  /* the temp key tells us that some process is already working on
   * caching this URL, so we'll decline
   */
  rv = apr_memcache_getp(conf->memcache, r->pool, mobj->tkey, 
                         &result, &dlen, NULL);
  if(rv == APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                 "create_entity found: %s", result);
    
    return DECLINED;
  }

  /* if we didn't see it that time, add it with a timeout. if we successfully
   * cache this url, we'll delete it ourselves, otherwise it should time out
   */
  rv = apr_memcache_add(conf->memcache, mobj->tkey, NULL, 0, 10, 0);
  if(rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                 "create_entity: unable to create: %s", key);
    
    return DECLINED;
  }
  ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
               "create_entity: created: %s", key);
  
  return OK;
}

static int open_entity(cache_handle_t *h, request_rec *r, const char *key)
{

  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);
  cache_object_t *obj;
  memcached_cache_object_t *mobj;
  cache_info *info;
  char *result;
  apr_size_t len;
  apr_status_t rv, hrv, brv;

  h->cache_obj = NULL;
  h->cache_obj = obj = apr_pcalloc(r->pool, sizeof(cache_object_t));
  obj->vobj = mobj = apr_pcalloc(r->pool, sizeof(memcached_cache_object_t));

  info = &(obj->info);

  mobj->ikey = apr_pstrcat(r->pool, INFO_PREFIX, key, NULL);
  mobj->hikey = apr_pstrcat(r->pool, HEADER_IN_PREFIX, key, NULL);
  mobj->hokey = apr_pstrcat(r->pool, HEADER_OUT_PREFIX, key, NULL);
  mobj->bkey = apr_pstrcat(r->pool, BODY_PREFIX, key, NULL);
  mobj->tkey = apr_pstrcat(r->pool, TEMP_PREFIX, key, NULL);

  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "open_entity: %s", key);

  hrv = apr_memcache_getp(conf->memcache, r->pool, mobj->hkey, 
                          &(mobj->headers), &len, NULL);
  brv = apr_memcache_getp(conf->memcache, r->pool, mobj->bkey, 
                          &(mobj->body), &len, NULL);
  if(hrv == APR_SUCCESS &&
     brv == APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                 "open_entity found: %s", key);

    return OK;
  }
  else {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, hrv, r->server,
                 "open_entity hrv", result);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, brv, r->server,
                 "open_entity brv", result);

  }
  
  return DECLINED;
}

static int remove_entity(cache_handle_t *h)
{

  h->cache_obj = NULL;
  
  /*
  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "remove_entity");
  */
  return OK;

}

static apr_status_t store_headers(cache_handle_t *h, request_rec *r, 
                                  cache_info *info)
{
  apr_status_t rv;
  cache_object_t *obj = h->cache_obj;
  char *token;
  memcached_cache_object_t *mobj = 
    (memcached_cache_object_t *) h->cache_obj->vobj;
  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);

  
  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "store_headers");

  obj->info.status = info->status;
  if (info->date) {
    obj->info.date = info->date;
  }
  if (info->response_time) {
    obj->info.response_time = info->response_time;
  }
  if (info->request_time) {
    obj->info.request_time = info->request_time;
  }
  if (info->expire) {
    obj->info.expire = info->expire;
  }

  if(r->headers_out) {
    apr_table_t *headers_out;
    apr_table_entry_t *elts;
    int i;
    char *h_out = NULL;
    char *hkey = NULL;

    headers_out = ap_cache_cacheable_hdrs_out(r->pool, r->headers_out,
                                              r->server);
    if (!apr_table_get(headers_out, "Content-Type")
        && r->content_type) {
      apr_table_setn(headers_out, "Content-Type",
                     ap_make_content_type(r, r->content_type));
    }
    
    headers_out = apr_table_overlay(r->pool, headers_out,
                                    r->err_headers_out);

    elts = (apr_table_entry_t *) apr_table_elts(headers_out)->elts;
    for( i = 0; i < apr_table_elts(headers_out)->nelts; i++) {
      h_out = apr_pstrcat(r->pool, h_out, 
                          elts[i].key, ": ", elts[i].val, CRLF, NULL); 
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                 "store_headers: out: %s ",
                 h_out);

    rv = 
      apr_memcache_set(conf->memcache, mobj->hkey, h_out, strlen(h_out) - 1, 0, 0);
    if (rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_headers: error");

      return rv;
    }
  }

  return APR_SUCCESS; 
  /*
  if(r->headers_in) {
    apr_table_t *headers_in;
    apr_table_entry_t *elts;
    int i;
    char *h_in = NULL;
    char *hkey = NULL;

    headers_in = ap_cache_cacheable_hdrs_out(r->pool, r->headers_in,
                                              r->server);

    elts = (apr_table_entry_t *) apr_table_elts(headers_in)->elts;
    for( i = 0; i < apr_table_elts(headers_in)->nelts; i++) {
      h_in = apr_pstrcat(r->pool, h_in, 
                          elts[i].key, ": ", elts[i].val, CRLF, NULL); 
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                 "store_headers: in: %s ",
                 h_in);

    hkey = apr_pstrcat(r->pool, "hi:", obj->key, NULL);
    rv = 
      apr_memcache_set(conf->memcache, hkey, h_in, strlen(h_in) - 1, 0, 0);
    if (rv != APR_SUCCESS) {
      return rv;
    }
  }
  */

}

static apr_status_t store_body(cache_handle_t *h, request_rec *r,
                               apr_bucket_brigade *bb)
{
  apr_status_t rv;
  apr_bucket *e;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *)obj->vobj;
  char *tmpkey, *realkey;
  char *stored, *flat, *new;
  apr_size_t slen, flen, nlen;
  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);

  for(e = APR_BRIGADE_FIRST(bb);
      e != APR_BRIGADE_SENTINEL(bb);
      e = APR_BUCKET_NEXT(e)) {
    apr_bucket *cpy;

    apr_bucket_copy(e, &cpy);
    APR_BRIGADE_INSERT_TAIL(mobj->body_bb, cpy);
  }

  if(APR_BUCKET_IS_EOS(APR_BRIGADE_LAST(bb))) {
    char *body;
    apr_size_t blen;
    
    rv = apr_brigade_pflatten(mobj->body_bb, &body, &blen, r->pool);
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_body: pflatten failed");

      return rv;
    }
    rv = apr_memcache_set(conf->memcache, mobj->bkey, body, blen, 0, 0);
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_body: storage failed");
      
      return rv;
    }
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                 "store_body: stored %d bytes at %s", blen, mobj->bkey);

    rv = apr_memcache_delete(conf->memcache, mobj->tkey, 0);
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_body: temp key delete failted");
      
      return rv;
    }
  }

  return APR_SUCCESS;
}

static int headers_debug(void *data, const char *key, const char *value)
{
  request_rec *r = data;
  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "%s: %s", key, value);

  return 1;
}
static apr_status_t recall_headers(cache_handle_t *h, request_rec *r)
{
  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *)obj->vobj;
  char *result;
  apr_status_t rv;
  apr_size_t len;
  char *hokey, *hikey, *token;
  char *ctx;

  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "recall_headers: %s", mobj->hkey);

  rv = apr_memcache_getp(conf->memcache, r->pool, mobj->hkey, 
                         &result, &len, NULL);

  h->req_hdrs = apr_table_make(r->pool, 20);
  h->resp_hdrs = apr_table_make(r->pool, 20);

  while((token = apr_strtok(result, CRLF, &ctx)) != NULL) {
    char *n, *v, *c;

    result = NULL;
  
    n = apr_strtok(token, ":", &c);
    v = apr_strtok(NULL, "\n", &c);
    v++;
    apr_table_set(h->resp_hdrs, n, v);
  }
  
  apr_table_do(headers_debug, r, h->resp_hdrs, NULL);

  return APR_SUCCESS;
}

static apr_status_t recall_body(cache_handle_t *h, apr_pool_t *p, 
                                apr_bucket_brigade *bb)
{

  apr_bucket *e;
  ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, p,
               "recall_body");


  e = apr_bucket_heap_create("foo", sizeof("foo") - 1, NULL, 
                             bb->bucket_alloc);

  APR_BRIGADE_INSERT_HEAD(bb, e);
  e = apr_bucket_eos_create(bb->bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(bb, e);

  return APR_SUCCESS;

}

static apr_status_t remove_url(cache_handle_t *h, apr_pool_t *p)
{

  apr_status_t rv;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *) obj->vobj;

  ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, p,
                "remove_url: ");

  return OK;
}

static const char *add_cache_server(cmd_parms *parms, void *ptr, 
                                    const char *host, const char *port)
{
  
  memcached_cache_conf_t *conf = 
    ap_get_module_config(parms->server->module_config,
                         &memcached_cache_module);
  memcached_cache_server_t *s = NULL;

  s = apr_array_push(conf->servers);
  s->host = apr_pstrdup(parms->pool, host);
  s->port = apr_atoi64(port);
  
  return NULL;
}

/* apr_memcache and apr_memcache_servers are init'ed per child */

static void child_init(apr_pool_t *p, server_rec *s)
{
  apr_status_t rv;
  int i;
  memcached_cache_conf_t *conf = 
      ap_get_module_config(s->module_config,
                           &memcached_cache_module);
  memcached_cache_server_t *svr;

  rv = apr_memcache_create(p, DEFAULT_MAX_SERVERS, 0, &(conf->memcache));
  if(rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_ERR, rv, s, 
                   "Unable to create memcache struct");
  }    
  
  svr = (memcached_cache_server_t *)conf->servers->elts;
  
  for(i = 0; i < conf->servers->nelts; i++) {
    
    rv = apr_memcache_server_create(p, svr[i].host, svr[i].port,
                                    DEFAULT_MIN, DEFAULT_SMAX, DEFAULT_MAX,
                                    DEFAULT_TTL,
                                      &(svr[i].server));
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, s, 
                   "Unable to create memcache server for %s:%d",
                   svr[i].host, svr[i].port);
      continue;
    }    
    
    rv = apr_memcache_add_server(conf->memcache, svr[i].server);
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, s, 
                   "Unable to add memcache server for %s:%d",
                   svr[i].host, svr[i].port);
    }    
  }
}


static const command_rec memcached_cmds[] = 
{
  AP_INIT_TAKE2("MemcacheCacheServer", add_cache_server, NULL, RSRC_CONF,
                "bleh"),

  {NULL}
};

static const cache_provider cache_memcached_provider =
{
    &remove_entity,
    &store_headers,
    &store_body,
    &recall_headers,
    &recall_body,
    &create_entity,
    &open_entity, 
    &remove_url
};

static void memcached_register_hooks(apr_pool_t *p)
{

  ap_register_provider(p, CACHE_PROVIDER_GROUP, "memcached", "0",
                       &cache_memcached_provider);
  ap_hook_child_init(child_init, NULL, NULL, APR_HOOK_MIDDLE);

}

module AP_MODULE_DECLARE_DATA memcached_cache_module = {
  STANDARD20_MODULE_STUFF,
  NULL,                       /* create per-directory config structure */
  NULL,                       /* merge per-directory config structures */
  memcached_create_config,           /* create per-server config structure */
  NULL,                       /* merge per-server config structures */
  memcached_cmds,                    /* command apr_table_t */
  memcached_register_hooks           /* register hooks */
};
