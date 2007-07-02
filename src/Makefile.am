mod_memcached_cache_la_SOURCES = mod_memcached_cache.c mod_memcached_cache.h 
mod_memcached_cache_la_CFLAGS = -Wall ${MODULE_CFLAGS}
mod_memcached_cache_la_LDFLAGS = -rpath ${AP_LIBEXECDIR} -module -avoid-version ${MODULE_LDFLAGS}

mod_LTLIBRARIES = mod_memcached_cache.la
moddir=${AP_LIBEXECDIR}

install: install-am
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/mod_memcached_cache.a
	rm -f $(DESTDIR)${AP_LIBEXECDIR}/mod_memcached_cache.la