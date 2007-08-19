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
static int remove_url(cache_handle_t *h, apr_pool_t *p);

static apr_status_t store_headers(cache_handle_t *h, request_rec *r, 
                                  cache_info *i);
static apr_status_t store_body(cache_handle_t *h, request_rec *r,
                               apr_bucket_brigade *b);

static apr_status_t recall_headers(cache_handle_t *h, request_rec *r);
static apr_status_t recall_body(cache_handle_t *h, apr_pool_t *p, 
                                apr_bucket_brigade *b);

static void *memcached_create_config(apr_pool_t *p, server_rec *s)
{
  memcached_cache_conf_t *conf = 
    apr_pcalloc(p, sizeof(memcached_cache_conf_t));
  
  conf->servers = apr_array_make(p, 10, sizeof(memcached_cache_server_t));
  conf->min_size = DEFAULT_MIN_SIZE;
  conf->max_size = DEFAULT_MAX_SIZE;

  return conf;
}

static int create_entity(cache_handle_t *h, request_rec *r, 
                         const char *key, apr_off_t len)
{
  memcached_cache_conf_t *conf = ap_get_module_config(r->server->module_config,
                                                      &memcached_cache_module);
  cache_object_t *obj;
  memcached_cache_object_t *mobj;
  apr_status_t rv;

  h->cache_obj = obj = apr_pcalloc(r->pool, sizeof(cache_object_t));
  obj->vobj = mobj = apr_pcalloc(r->pool, sizeof(memcached_cache_object_t));

  mobj->mc = conf->memcache;
  obj->key = apr_pstrdup(r->pool, key);
  
  mobj->headers_bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
  mobj->body_bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);

  rv = apr_memcache_set(mobj->mc,
                        apr_pstrcat(r->pool,
                                    "h:", obj->key, NULL),
                        NULL, 0, 5, 0);

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

  memcached_cache_conf_t *conf;
  cache_object_t *obj;
  memcached_cache_object_t *mobj;
  cache_info *info;
  char *headers;
  apr_status_t rv;
  apr_table_t *info_table;
  char *token, *ctx;
  int state = 0;

  conf = 
    (memcached_cache_conf_t *)ap_get_module_config(r->server->module_config,
                                                   &memcached_cache_module);
  h->cache_obj = NULL;
  h->cache_obj = obj = apr_pcalloc(r->pool, sizeof(cache_object_t));
  obj->vobj = mobj = apr_pcalloc(r->pool, sizeof(memcached_cache_object_t));

  obj->key = apr_pstrdup(r->pool, key);

  mobj->mc = conf->memcache;
  mobj->headers_bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
  mobj->body_bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);

  info = &(obj->info);

  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "open_entity: %s", key);

  rv = apr_memcache_getp(conf->memcache, r->pool, 
                         apr_pstrcat(r->pool, "h:", key, NULL),
                         &(mobj->headers), &(mobj->hlen), NULL);

  if(rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server, 
                 "mod_memcached_cache: no header info found for %s", 
                 key);

    return DECLINED;
  }

  rv = apr_memcache_getp(mobj->mc, r->pool, 
                         apr_pstrcat(r->pool, "b:", key, NULL),
                         &(mobj->body), &(mobj->blen), NULL);

  if(rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server, 
                 "mod_memcached_cache: no body found for %s", 
                 key);

    return DECLINED;
  }
  
  info_table = apr_table_make(r->pool, 5);
  headers = apr_pstrdup(r->pool, mobj->headers);

  while((token = apr_strtok(headers, CRLF, &ctx)) != NULL) {
    char *n, *v;
    
    headers = NULL;
    n = apr_pstrdup(r->pool, token);
    
    if(strcmp("----", n) == 0) {
      state++;
    }
    if(state == 1) {
      
      v = strchr(n, ':');
      if(v) {
        *(v++) ='\0';
        
      }
      apr_table_set(info_table, n, v);
    }
    
  }
  
  info->status = apr_atoi64(apr_table_get(info_table, "status"));
  info->date = apr_atoi64(apr_table_get(info_table, "date"));
  info->expire = apr_atoi64(apr_table_get(info_table, "expire"));
  info->request_time = apr_atoi64(apr_table_get(info_table, "request_time"));
  info->response_time = apr_atoi64(apr_table_get(info_table, "response_time"));

  return OK;
}

static int remove_entity(cache_handle_t *h)
{

  h->cache_obj = NULL;
  
  return OK;

}

static apr_status_t remove_url(cache_handle_t *h, apr_pool_t *p)
{

  apr_status_t rv;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *) obj->vobj;

  ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, p,
                "remove_url");

  rv = apr_memcache_delete(mobj->mc, 
                           apr_pstrcat(p, "h:", obj->key, NULL), 
                           0);

  /* if it wasn't successful, and it wasn't just because the object was no 
   *  longer found (maybe it LRU'ed out or something), then return DECLINED
  */
  if(rv != APR_SUCCESS && 
     rv != APR_NOTFOUND) {
    ap_log_perror(APLOG_MARK, APLOG_ERR, rv, p, 
                  "unable to remove headers for %s", obj->key);
    return DECLINED;
  }

  rv = apr_memcache_delete(mobj->mc, 
                           apr_pstrcat(p, "b:", obj->key, NULL), 
                           0);

  if(rv != APR_SUCCESS && 
     rv != APR_NOTFOUND) {
    ap_log_perror(APLOG_MARK, APLOG_ERR, rv, p, 
                  "unable to remove body for %s", obj->key);
    return DECLINED;
  }

  return OK;
}

/* shove formatted data into a bucket brigade */
static apr_status_t _serialize(apr_bucket_brigade *bb,
                               const char *fmt,
                               ...)
{
  va_list args;
  apr_status_t rv;

  va_start(args, fmt);
  rv = apr_brigade_vprintf(bb, NULL, NULL, fmt, args);
  va_end(args);
  return rv;
}

static apr_status_t store_headers(cache_handle_t *h, request_rec *r, 
                                  cache_info *info)
{
  apr_status_t rv;
  cache_object_t *obj = h->cache_obj;
  apr_bucket *e;
  char *headers;
  apr_size_t hlen;
  memcached_cache_object_t *mobj = 
    (memcached_cache_object_t *) h->cache_obj->vobj;
  memcached_cache_conf_t *conf;
  apr_bucket_alloc_t *bucket_alloc = r->connection->bucket_alloc;
  
  conf = ap_get_module_config(r->server->module_config,
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

  e = apr_bucket_immortal_create("----\r\n", 6, bucket_alloc);
  APR_BRIGADE_INSERT_HEAD(mobj->headers_bb, e);

  rv = _serialize(mobj->headers_bb,
                  "status:%d\r\ndate: %"APR_TIME_T_FMT"\r\nresponse_time:%"APR_TIME_T_FMT"\r\nrequest_time:%"APR_TIME_T_FMT"\r\nexpire:%"APR_TIME_T_FMT"\r\n",
                  info->status,
                  info->date,
                  info->response_time,
                  info->request_time,
                  info->expire);

  e = apr_bucket_immortal_create("----\r\n", 6, bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(mobj->headers_bb, e);

  if(r->headers_out) {
    apr_table_t *headers_out;
    apr_table_entry_t *elts;
    int i;
    
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
      rv = _serialize(mobj->headers_bb, 
                      "%s: %s\r\n",
                      elts[i].key, elts[i].val);
      
    }
  }    

  e = apr_bucket_immortal_create("----\r\n", 6, bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(mobj->headers_bb, e);
  
  if(r->headers_in) {
    apr_table_t *headers_in;
    apr_table_entry_t *elts;
    int i;
    
    headers_in = ap_cache_cacheable_hdrs_out(r->pool, r->headers_in,
                                             r->server);
    
    elts = (apr_table_entry_t *) apr_table_elts(headers_in)->elts;
    for( i = 0; i < apr_table_elts(headers_in)->nelts; i++) {
      rv = _serialize(mobj->headers_bb, 
                      "%s: %s\r\n",
                      elts[i].key, elts[i].val);
    }
  }

  e = apr_bucket_eos_create(bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(mobj->headers_bb, e);

  rv = apr_brigade_pflatten(mobj->headers_bb, &headers, &hlen, r->pool);

  rv = apr_memcache_set(mobj->mc,
                        apr_pstrcat(r->pool, "h:", obj->key, NULL),
                        headers, hlen, 0, 0);

  if(rv != APR_SUCCESS) {
    ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                 "store_headers: storage failed");
    
    return rv;
  }
  
  return APR_SUCCESS;
}

static apr_status_t store_body(cache_handle_t *h, request_rec *r,
                               apr_bucket_brigade *bb)
{
  apr_status_t rv;
  apr_bucket *e;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *)obj->vobj;
  memcached_cache_conf_t *conf;
  apr_bucket_alloc_t *bucket_alloc = r->connection->bucket_alloc;
  
  conf = ap_get_module_config(r->server->module_config,
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
    apr_bucket *e;
    apr_size_t blen;

    e = apr_bucket_eos_create(bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(mobj->body_bb, e);
    
    rv = apr_brigade_pflatten(mobj->body_bb, &body, &blen, r->pool);
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_body: pflatten failed");

      return rv;
    }
    
    if(blen > conf->max_size) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                   "content for %s is greater than the max size"
                   "(%" APR_SIZE_T_FMT " > %" APR_OFF_T_FMT ")",
                   obj->key, blen, conf->max_size);
      rv = apr_memcache_delete(mobj->mc, 
                               apr_pstrcat(r->pool, "h:", obj->key, NULL),
                               0);
      return APR_EGENERAL;
    }

    if(blen < conf->min_size) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                   "content for %s is smaller than the min size"
                   "(%" APR_SIZE_T_FMT " < %" APR_OFF_T_FMT ")",
                   obj->key, blen, conf->min_size);
      rv = apr_memcache_delete(mobj->mc, 
                               apr_pstrcat(r->pool, "h:", obj->key, NULL),
                               0);
      return APR_EGENERAL;
    }

    rv = apr_memcache_set(mobj->mc,
                          apr_pstrcat(r->pool,
                                      "b:", obj->key, NULL),
                          body, blen, 0, 0);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
                 "store_body");

    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, r->server,
                   "store_body: storage failed");
      
      return rv;
    }
  }

  return APR_SUCCESS;
}


static apr_status_t recall_headers(cache_handle_t *h, request_rec *r)
{
  memcached_cache_conf_t *conf;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *)obj->vobj;
  char *result;
  apr_status_t rv;
  apr_size_t len;
  char *token;
  char *ctx;
  int state;

  conf = ap_get_module_config(r->server->module_config,
                              &memcached_cache_module);

  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, r->server,
               "recall_headers: %s", obj->key);

  rv = apr_memcache_getp(mobj->mc, r->pool, 
                         apr_pstrcat(r->pool, "h:", obj->key, NULL),
                          &result, &len, NULL);

  h->req_hdrs = apr_table_make(r->pool, 20);
  h->resp_hdrs = apr_table_make(r->pool, 20);

  while((token = apr_strtok(mobj->headers, CRLF, &ctx)) != NULL) {
    char *n, *v;
    
    mobj->headers = NULL;
    
    n = apr_pstrdup(r->pool, token);
    
    if(strcmp("----", n) == 0) {
      state++;
    }
    else {
      v = strchr(n, ':');
      if(v) {
        *(v++) = '\0';
      }
      v++; /* to get rid of the leading space */

      if(state == 2) {
        apr_table_set(h->req_hdrs, n, v);
      }
      else if(state == 3) {
        apr_table_set(h->resp_hdrs, n, v);
      }
    }
  } 

  return APR_SUCCESS;
}

static apr_status_t recall_body(cache_handle_t *h, apr_pool_t *p, 
                                apr_bucket_brigade *bb)
{

  apr_bucket *e;
  char *body;
  apr_size_t len;
  apr_status_t rv;
  cache_object_t *obj = h->cache_obj;
  memcached_cache_object_t *mobj = (memcached_cache_object_t *) obj->vobj;

  ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, p,
               "recall_body");

  rv = apr_memcache_getp(mobj->mc, p, 
                         apr_pstrcat(p, "b:", obj->key, NULL),
                         &body, &len, NULL);

  e = apr_bucket_heap_create(body, len, NULL, bb->bucket_alloc);

  APR_BRIGADE_INSERT_HEAD(bb, e);
  e = apr_bucket_eos_create(bb->bucket_alloc);
  APR_BRIGADE_INSERT_TAIL(bb, e);

  return APR_SUCCESS;
}


static const char *add_cache_server(cmd_parms *parms, void *dummy,
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

static const char *set_mccache_min_size(cmd_parms *parms, void *dummy,
                                        const char *min)
{
  memcached_cache_conf_t *conf = 
    ap_get_module_config(parms->server->module_config,
                         &memcached_cache_module);
  if(apr_strtoff(&conf->min_size, min, NULL, 0) != APR_SUCCESS ||
     conf->min_size < 0) {
    
    return "MemcachedCacheMinFileSize should be a numeric value in bytes that specifies the minimum size of a document to store";

  }
  return NULL;
}

static const char *set_mccache_max_size(cmd_parms *parms, void *dummy, 
                                        const char *max)
{
  memcached_cache_conf_t *conf = 
    ap_get_module_config(parms->server->module_config,
                         &memcached_cache_module);
    if(apr_strtoff(&conf->max_size, max, NULL, 0) != APR_SUCCESS ||
     conf->max_size < 0) {
    
    return "MemcachedCacheMaxFileSize should be a numeric value in bytes that specifies the maximum size of a document to store";

  }
  return NULL;
}

static int post_config(apr_pool_t *p, apr_pool_t *plog,
                       apr_pool_t *ptemp, server_rec *s)
{

  memcached_cache_conf_t *conf;
  memcached_cache_server_t *svr;
  server_rec *sp;

  for(sp = s; sp; sp = sp->next) {
    int i;
    apr_status_t rv;

    conf = 
      (memcached_cache_conf_t *)ap_get_module_config(sp->module_config,
                                                     &memcached_cache_module);

    rv = apr_memcache_create(p, DEFAULT_MAX_SERVERS, 0, &(conf->memcache));
    if(rv != APR_SUCCESS) {
      ap_log_error(APLOG_MARK, APLOG_ERR, rv, sp, 
                   "Unable to create memcache struct");
    }    
    
    svr = (memcached_cache_server_t *)conf->servers->elts;
  
    for(i = 0; i < conf->servers->nelts; i++) {
    
      rv = apr_memcache_server_create(p, svr[i].host, svr[i].port,
                                      DEFAULT_MIN, DEFAULT_SMAX, DEFAULT_MAX,
                                      DEFAULT_TTL,
                                      &(svr[i].server));
      if(rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, sp, 
                   "Unable to create memcache server for %s:%d",
                     svr[i].host, svr[i].port);
        continue;
      }    
     
      rv = apr_memcache_add_server(conf->memcache, svr[i].server);
      if(rv != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, sp, 
                     "Unable to add memcache server for %s:%d",
                     svr[i].host, svr[i].port);
      }    
    }
  }
  return OK;
}

static const command_rec memcached_cmds[] = 
{
  AP_INIT_TAKE2("MemcachedCacheServer", add_cache_server, NULL, RSRC_CONF,
                "bleh"),
  AP_INIT_TAKE1("MemcachedCacheMinFileSize", set_mccache_min_size, NULL, 
                RSRC_CONF, 
                "The minimum file size required to cache a document"),
  AP_INIT_TAKE1("MemcachedCacheMaxFileSize", set_mccache_max_size, NULL,
                RSRC_CONF, 
                "The maximum file size required to cache a document"),

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
  ap_hook_post_config(post_config, NULL, NULL, APR_HOOK_MIDDLE);

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
