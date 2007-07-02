dnl -------------------------------------------------------- -*- autoconf -*-
dnl Copyright 2005 The Apache Software Foundation
dnl
dnl Licensed under the Apache License, Version 2.0 (the "License");
dnl you may not use this file except in compliance with the License.
dnl You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl Unless required by applicable law or agreed to in writing, software
dnl distributed under the License is distributed on an "AS IS" BASIS,
dnl WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
dnl See the License for the specific language governing permissions and
dnl limitations under the License.

dnl Check for apr_memcache

dnl CHECK_APR_MEMCACHE(ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND])
AC_DEFUN([CHECK_APR_MEMCACHE],
[dnl

APR_MEMCACHE_URL="http://www.outoforder.cc/projects/libs/apr_memcache/"
AC_ARG_WITH(
    apr_memcache,
    [AC_HELP_STRING([--with-apr-memcache=PATH],[Path to apr_memcache library])],
    mc_path="$withval",
    :)

if test -z $mc_path; then
    test_paths="/usr/local/apr_memcache /usr/local /usr"
else
    test_paths="${mc_path}"
fi

for x in $test_paths ; do
    AC_MSG_CHECKING([for apr_memcache library in ${x}])
    if test -f ${x}/include/apr_memcache-0/apr_memcache.h; then
        AC_MSG_RESULT([yes])
        APR_MEMCACHE_LIBS="-R${x}/lib -L${x}/lib -lapr_memcache"
        APR_MEMCACHE_CFLAGS="-I${x}/include/apr_memcache-0"
        break
    else
        AC_MSG_RESULT([no])
    fi
done

AC_SUBST(APR_MEMCACHE_LIBS)
AC_SUBST(APR_MEMCACHE_CFLAGS)

if test -z "${APR_MEMCACHE_LIBS}"; then
  AC_MSG_NOTICE([*** apr_memcache library not found.])
  ifelse([$2], , AC_MSG_ERROR([The apr_memcache library is required.
                   See $APR_MEMCACHE_URL]), [$2])
else           
  AC_MSG_NOTICE([found apr_memcache.])    
  ifelse([$1], , , [$1]) 
fi 
])

