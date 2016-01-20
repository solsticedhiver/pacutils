/*
 * Copyright 2012-2015 Andrew Gregory <andrew.gregory.8@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <string.h>
#include <glob.h>
#include <sys/utsname.h>

#include "../../ext/mini.c/mini.h"

#include "config.h"
#include "util.h"

#ifndef DBEXT
#define DBEXT ".db"
#endif

struct _pu_config_setting {
  char *name;
  pu_config_option_t type;
} _pu_config_settings[] = {
  {"RootDir",         PU_CONFIG_OPTION_ROOTDIR},
  {"DBPath",          PU_CONFIG_OPTION_DBPATH},
  {"GPGDir",          PU_CONFIG_OPTION_GPGDIR},
  {"LogFile",         PU_CONFIG_OPTION_LOGFILE},
  {"Architecture",    PU_CONFIG_OPTION_ARCHITECTURE},
  {"XferCommand",     PU_CONFIG_OPTION_XFERCOMMAND},

  {"CleanMethod",     PU_CONFIG_OPTION_CLEANMETHOD},
  {"Color",           PU_CONFIG_OPTION_COLOR},
  {"UseSyslog",       PU_CONFIG_OPTION_USESYSLOG},
  {"UseDelta",        PU_CONFIG_OPTION_USEDELTA},
  {"TotalDownload",   PU_CONFIG_OPTION_TOTALDOWNLOAD},
  {"CheckSpace",      PU_CONFIG_OPTION_CHECKSPACE},
  {"VerbosePkgLists", PU_CONFIG_OPTION_VERBOSEPKGLISTS},
  {"ILoveCandy",      PU_CONFIG_OPTION_ILOVECANDY},

  {"SigLevel",        PU_CONFIG_OPTION_SIGLEVEL},
  {"LocalFileSigLevel",  PU_CONFIG_OPTION_LOCAL_SIGLEVEL},
  {"RemoteFileSigLevel", PU_CONFIG_OPTION_REMOTE_SIGLEVEL},

  {"HoldPkg",         PU_CONFIG_OPTION_HOLDPKGS},
  {"IgnorePkg",       PU_CONFIG_OPTION_IGNOREPKGS},
  {"IgnoreGroup",     PU_CONFIG_OPTION_IGNOREGROUPS},
  {"NoUpgrade",       PU_CONFIG_OPTION_NOUPGRADE},
  {"NoExtract",       PU_CONFIG_OPTION_NOEXTRACT},
  {"CacheDir",        PU_CONFIG_OPTION_CACHEDIRS},

  {"Usage",           PU_CONFIG_OPTION_USAGE},

  {"Include",         PU_CONFIG_OPTION_INCLUDE},

  {"Server",          PU_CONFIG_OPTION_SERVER},

  {NULL, 0}
};

static char *_pu_strjoin(const char *sep, ...)
{
  char *c, *next, *dest;
  size_t tlen = 0, sep_len = (sep && *sep) ? strlen(sep) : 0;
  int argc = 0;
  va_list calc, args;

  va_start(args, sep);

  va_copy(calc, args);
  for(tlen = 0; (c = va_arg(calc, char*)); argc++, tlen += strlen(c) + sep_len);
  tlen -= sep_len;
  va_end(calc);

  if(argc == 0) {
    va_end(args);
    return strdup("");
  } else if((dest = malloc(tlen + 1)) == NULL) {
    va_end(args);
    return NULL;
  }

  c = dest;
  next = va_arg(args, char*);
  while(next) {
    c = stpcpy(c, next);
    next = va_arg(args, char*);
    if(next && sep_len) {
      c = stpcpy(c, sep);
    }
  }
  va_end(args);

  return dest;
}

static char *_pu_strreplace(const char *str,
    const char *target, const char *replacement)
{
  const char *ptr;
  char *newstr, *c;
  size_t found = 0;
  size_t tlen = strlen(target);
  size_t rlen = strlen(replacement);
  size_t newlen = strlen(str);

  /* count the number of occurrences */
  ptr = str;
  while((ptr = strstr(ptr, target))) {
    found++;
    ptr += tlen;
  }

  /* calculate the length of our new string */
  newlen += (found * (rlen - tlen));

  if((newstr = malloc(newlen + 1)) == NULL) { return NULL; }
  newstr[0] = '\0';

  /* copy the string with the replacements */
  ptr = str;
  c = newstr;
  while((ptr = strstr(ptr, target))) {
    c = stpncpy(c, str, ptr - str);
    c = stpcpy(c, replacement);
    ptr += tlen;
    str = ptr;
  }
  strcat(newstr, str);

  return newstr;
}

int _pu_parse_cleanmethod(pu_config_t *config, char *val)
{
  char *v, *ctx;
  int ret = 0;
  for(v = strtok_r(val, " ", &ctx); v; v = strtok_r(NULL, " ", &ctx)) {
    if(strcmp(v, "KeepInstalled") == 0) {
      config->cleanmethod |= PU_CONFIG_CLEANMETHOD_KEEP_INSTALLED;
    } else if(strcmp(v, "KeepCurrent") == 0) {
      config->cleanmethod |= PU_CONFIG_CLEANMETHOD_KEEP_CURRENT;
    } else {
      ret = -1;
    }
  }
  return ret;
}

int _pu_parse_siglevel(char *val,
    alpm_siglevel_t *level, alpm_siglevel_t *mask)
{
  char *v, *ctx;
  int ret = 0;

  for(v = strtok_r(val, " ", &ctx); v; v = strtok_r(NULL, " ", &ctx)) {
    int pkg = 1, db = 1;

    if(strncmp(v, "Package", 7) == 0) {
      v += 7;
      db = 0;
    } else if(strncmp(v, "Database", 8) == 0) {
      v += 8;
      pkg = 0;
    }

#define SET(siglevel) do { *level |= (siglevel); *mask |= (siglevel); } while(0)
#define UNSET(siglevel) do { *level &= ~(siglevel); *mask |= (siglevel); } while(0)
    if(strcmp(v, "Never") == 0) {
      if(pkg) { UNSET(ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL); }
      if(db) { UNSET(ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL); }
    } else if(strcmp(v, "Optional") == 0) {
      if(pkg) { SET(ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL); }
      if(db) { SET(ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL); }
    } else if(strcmp(v, "Required") == 0) {
      if(pkg) { SET(ALPM_SIG_PACKAGE); UNSET(ALPM_SIG_PACKAGE_OPTIONAL); }
      if(db) { SET(ALPM_SIG_DATABASE); UNSET(ALPM_SIG_DATABASE_OPTIONAL); }
    } else if(strcmp(v, "TrustedOnly") == 0) {
      if(pkg) { UNSET(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK); }
      if(db) { UNSET(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK); }
    } else if(strcmp(v, "TrustAll") == 0) {
      if(pkg) { SET(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK); }
      if(db) { SET(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK); }
    } else {
      ret = -1;
    }
  }
#undef SET
#undef UNSET

  *level &= ~ALPM_SIG_USE_DEFAULT;
  return ret;
}

static struct _pu_config_setting *_pu_config_lookup_setting(const char *optname)
{
  int i;
  for(i = 0; _pu_config_settings[i].name; ++i) {
    if(strcmp(optname, _pu_config_settings[i].name) == 0) {
      return &_pu_config_settings[i];
    }
  }
  return NULL;
}

pu_config_t *pu_config_new(void)
{
  pu_config_t *config = calloc(sizeof(pu_config_t), 1);
  if(config == NULL) { return NULL; }

  config->checkspace = PU_CONFIG_BOOL_UNSET;
  config->color = PU_CONFIG_BOOL_UNSET;
  config->ilovecandy = PU_CONFIG_BOOL_UNSET;
  config->totaldownload = PU_CONFIG_BOOL_UNSET;
  config->usesyslog = PU_CONFIG_BOOL_UNSET;
  config->verbosepkglists = PU_CONFIG_BOOL_UNSET;

  config->siglevel = ALPM_SIG_USE_DEFAULT;
  config->localfilesiglevel = ALPM_SIG_USE_DEFAULT;
  config->remotefilesiglevel = ALPM_SIG_USE_DEFAULT;

  config->usedelta = -1.0;

  return config;
}

void pu_repo_free(pu_repo_t *repo)
{
  if(!repo) {
    return;
  }

  free(repo->name);
  FREELIST(repo->servers);

  free(repo);
}

pu_repo_t *pu_repo_new(void)
{
  return calloc(sizeof(pu_repo_t), 1);
}

void pu_config_free(pu_config_t *config)
{
  if(!config) {
    return;
  }

  free(config->rootdir);
  free(config->dbpath);
  free(config->logfile);
  free(config->gpgdir);
  free(config->architecture);
  free(config->xfercommand);

  FREELIST(config->holdpkgs);
  FREELIST(config->ignorepkgs);
  FREELIST(config->ignoregroups);
  FREELIST(config->noupgrade);
  FREELIST(config->noextract);
  FREELIST(config->cachedirs);

  alpm_list_free_inner(config->repos, (alpm_list_fn_free) pu_repo_free);
  alpm_list_free(config->repos);

  free(config);
}

static int _pu_subst_server_vars(pu_config_t *config)
{
  alpm_list_t *r;
  for(r = config->repos; r; r = r->next) {
    pu_repo_t *repo = r->data;
    alpm_list_t *s;
    for(s = repo->servers; s; s = s->next) {
      char *rrepo, *rarch;
      rrepo = _pu_strreplace(s->data, "$repo", repo->name);
      if(rrepo == NULL) { return -1; }
      rarch = _pu_strreplace(rrepo, "$arch", config->architecture);
      free(rrepo);
      if(rarch == NULL) { return -1; }
      free(s->data);
      s->data = rarch;
    }
  }
  return 0;
}

alpm_handle_t *pu_initialize_handle_from_config(pu_config_t *config)
{
  alpm_handle_t *handle = alpm_initialize(config->rootdir, config->dbpath, NULL);

  if(!handle) {
    return NULL;
  }

  alpm_option_set_cachedirs(handle, config->cachedirs);
  alpm_option_set_noupgrades(handle, alpm_list_strdup(config->noupgrade));
  alpm_option_set_noextracts(handle, alpm_list_strdup(config->noextract));
  alpm_option_set_ignorepkgs(handle, alpm_list_strdup(config->ignorepkgs));
  alpm_option_set_ignoregroups(handle, alpm_list_strdup(config->ignoregroups));

  alpm_option_set_logfile(handle, config->logfile);
  alpm_option_set_gpgdir(handle, config->gpgdir);
  alpm_option_set_usesyslog(handle, config->usesyslog);
  alpm_option_set_arch(handle, config->architecture);

  alpm_option_set_default_siglevel(handle, config->siglevel);
  alpm_option_set_local_file_siglevel(handle, config->localfilesiglevel);
  alpm_option_set_remote_file_siglevel(handle, config->remotefilesiglevel);

  alpm_option_set_dbext(handle, DBEXT);

  return handle;
}

alpm_db_t *pu_register_syncdb(alpm_handle_t *handle, pu_repo_t *repo)
{
  alpm_db_t *db = alpm_register_syncdb(handle, repo->name, repo->siglevel);
  if(db) {
    alpm_db_set_servers(db, alpm_list_strdup(repo->servers));
    alpm_db_set_usage(db, repo->usage);
  }
  return db;
}

alpm_list_t *pu_register_syncdbs(alpm_handle_t *handle, alpm_list_t *repos)
{
  alpm_list_t *r;
  for(r = repos; r; r = r->next) {
    pu_register_syncdb(handle, r->data);
  }
  return alpm_get_syncdbs(handle);
}

int pu_config_resolve(pu_config_t *config)
{
  alpm_list_t *i;

#define SETDEFAULT(opt, val) if(!opt) { opt = val; if(!opt) { return -1; } }
  if(config->rootdir) {
    SETDEFAULT(config->dbpath,
        _pu_strjoin("/", config->rootdir, "var/lib/pacman/", NULL));
    SETDEFAULT(config->logfile,
        _pu_strjoin("/", config->rootdir, "var/log/pacman.log", NULL));
  } else {
    SETDEFAULT(config->rootdir, strdup("/"));
    SETDEFAULT(config->dbpath, strdup("/var/lib/pacman/"));
    SETDEFAULT(config->logfile, strdup("/var/log/pacman.log"));
  }
  SETDEFAULT(config->gpgdir, strdup("/etc/pacman.d/gnupg/"));
  SETDEFAULT(config->cachedirs,
      alpm_list_add(NULL, strdup("/var/cache/pacman/pkg")));
  SETDEFAULT(config->cleanmethod, PU_CONFIG_CLEANMETHOD_KEEP_INSTALLED);

  if(!config->architecture || strcmp(config->architecture, "auto") == 0) {
    struct utsname un;
    char *arch;
    if(uname(&un) != 0 || (arch = strdup(un.machine)) == NULL) { return -1; }
    free(config->architecture);
    config->architecture = arch;
  }

#define SETBOOL(opt) if(opt == -1) { opt = 0; }
  SETBOOL(config->checkspace);
  SETBOOL(config->color);
  SETBOOL(config->ilovecandy);
  SETBOOL(config->totaldownload);
  SETBOOL(config->usesyslog);
  SETBOOL(config->verbosepkglists);

#define SETSIGLEVEL(l, m) \
  if(m) { l = (l & (m)) | (config->siglevel & ~(m)); } \
  else { l = ALPM_SIG_USE_DEFAULT; }

  if(!config->siglevel_mask) {
    config->siglevel = (
        ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL |
        ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL);
  }
  SETSIGLEVEL(config->localfilesiglevel, config->localfilesiglevel_mask);
  SETSIGLEVEL(config->remotefilesiglevel, config->remotefilesiglevel_mask);

  for(i = config->repos; i; i = i->next) {
    pu_repo_t *r = i->data;
    SETDEFAULT(r->usage, ALPM_DB_USAGE_ALL);
    SETSIGLEVEL(r->siglevel, r->siglevel_mask);
  }
#undef SETSIGLEVEL
#undef SETBOOL
#undef SETDEFAULT

  if(_pu_subst_server_vars(config) != 0) { return -1; }

  return 0;
}

void pu_config_merge(pu_config_t *dest, pu_config_t *src)
{
#define MERGESTR(ds, ss) if(!ds) { ds = ss; ss = NULL; }
#define MERGELIST(dl, sl) do { dl = alpm_list_join(dl, sl); sl = NULL; } while(0)
#define MERGEVAL(dv, sv) if(!dv) { dv = sv; }
#define MERGESL(ds, dm, ss, sm) if(!dm) { ds = ss; dm = sm; }
#define MERGEBOOL(dv, sv) if(dv == -1) { dv = sv; }

  MERGEBOOL(dest->usesyslog, src->usesyslog);
  MERGEBOOL(dest->totaldownload, src->totaldownload);
  MERGEBOOL(dest->checkspace, src->checkspace);
  MERGEBOOL(dest->verbosepkglists, src->verbosepkglists);
  MERGEBOOL(dest->color, src->color);
  MERGEBOOL(dest->ilovecandy, src->ilovecandy);

  MERGEVAL(dest->cleanmethod, src->cleanmethod);
  MERGEVAL(dest->usedelta, src->usedelta);

  MERGESTR(dest->rootdir, src->rootdir);
  MERGESTR(dest->dbpath, src->dbpath);
  MERGESTR(dest->logfile, src->logfile);
  MERGESTR(dest->gpgdir, src->gpgdir);
  MERGESTR(dest->xfercommand, src->xfercommand);
  MERGESTR(dest->architecture, src->architecture);

  MERGELIST(dest->cachedirs, src->cachedirs);
  MERGELIST(dest->holdpkgs, src->holdpkgs);
  MERGELIST(dest->noextract, src->noextract);
  MERGELIST(dest->noupgrade, src->noupgrade);
  MERGELIST(dest->ignorepkgs, src->ignorepkgs);
  MERGELIST(dest->ignoregroups, src->ignoregroups);
  MERGELIST(dest->repos, src->repos);

  MERGESL(dest->siglevel, dest->siglevel_mask,
      src->siglevel, src->siglevel_mask);
  MERGESL(dest->localfilesiglevel, dest->localfilesiglevel_mask,
      src->localfilesiglevel, src->localfilesiglevel_mask);
  MERGESL(dest->remotefilesiglevel, dest->remotefilesiglevel_mask,
      src->remotefilesiglevel, src->remotefilesiglevel_mask);

#undef MERGESTR
#undef MERGELIST
#undef MERGEVAL
#undef MERGESL
#undef MERGEBOOL

  pu_config_free(src);
}

static int _pu_glob(alpm_list_t **dest, const char *pattern)
{
  glob_t gbuf;
  size_t gindex;
  alpm_list_t *items = NULL;
  int gret = glob(pattern, GLOB_NOCHECK, NULL, &gbuf);

  if(gret != 0 && gret != GLOB_NOMATCH) { return -1; }

  for(gindex = 0; gindex < gbuf.gl_pathc; gindex++) {
    char *dup = strdup(gbuf.gl_pathv[gindex]);
    if(dup == NULL || _pu_list_append(&items, dup) == NULL) {
      free(dup);
      FREELIST(items);
      globfree(&gbuf);
      return -1;
    }
  }

  globfree(&gbuf);
  *dest = alpm_list_join(*dest, items);

  return 0;
}

#define SETSTROPT(dest, val) if(!dest) { \
  char *dup = strdup(val); \
  if(dup) { \
    free(dest); \
    dest = dup; \
  } else { \
    reader->status = PU_CONFIG_READER_STATUS_ERROR; \
    reader->error = 1; \
    return -1; \
  } \
}
#define APPENDLIST(dest, str) do { \
  char *v, *ctx; \
  for(v = strtok_r(str, " ", &ctx); v; v = strtok_r(NULL, " ", &ctx)) { \
    char *dup = strdup(v); \
    if(dup == NULL || _pu_list_append(dest, dup) == NULL) { \
      reader->status = PU_CONFIG_READER_STATUS_ERROR; \
      reader->error = 1; \
      return -1; \
    } \
  } \
} while(0)

int pu_config_reader_next(pu_config_reader_t *reader)
{
  mini_t *mini = reader->_mini;
  pu_config_t *config = reader->config;

  reader->status = PU_CONFIG_READER_STATUS_OK;

#define _PU_ERR(r, s) { r->status = s; r->error = 1; return -1; }

  if(mini_next(mini) == NULL) {
    if(mini->eof) {
      if(reader->_parent == NULL) {
        reader->eof = 1;
        return -1;
      }

      mini_free(mini);
      free(reader->file);

      if(reader->_parent->_includes) {
        /* switch to the next included file */
        reader->file = _pu_list_shift(&reader->_parent->_includes);
        reader->_includes = NULL;
        if(reader->_mini == mini_init(reader->file)) {
          _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
        }
      } else {
        /* switch back to the parent */
        pu_config_reader_t *p = reader->_parent;
        reader->file = p->file;
        reader->_mini = p->_mini;
        reader->_parent = p->_parent;
        reader->_includes = p->_includes;
        free(p);
      }
      return pu_config_reader_next(reader);
    } else {
      _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
    }
  }

  reader->line = mini->lineno;
  reader->key = mini->key;
  reader->value = mini->value;

  if(!mini->key) {
    free(reader->section);
    if((reader->section = strdup(mini->section)) == NULL) {
      _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
    }

    if(strcmp(reader->section, "options") == 0) {
      reader->repo = NULL;
    } else {
      pu_repo_t *r = pu_repo_new();
      if(r == NULL || (r->name = strdup(reader->section)) == NULL) {
        _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
      }
      r->siglevel = ALPM_SIG_USE_DEFAULT;
      if(_pu_list_append(&config->repos, r) == NULL) {
        _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
        return -1;
      }
      reader->repo = r;
    }
  } else {
    struct _pu_config_setting *s;

    if(!(s = _pu_config_lookup_setting(mini->key))) {
      reader->status = PU_CONFIG_READER_STATUS_UNKNOWN_OPTION;
      return 0;
    }

    if(s->type == PU_CONFIG_OPTION_INCLUDE) {
      if(_pu_glob(&reader->_includes, mini->value) != 0) {
        _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
      } else if(reader->_includes == NULL) {
        return pu_config_reader_next(reader);
      } else {
        char *file = _pu_list_shift(&reader->_includes);
        pu_config_reader_t *p = malloc(sizeof(pu_config_reader_t));
        mini_t *newmini = mini_init(file);

        if(p == NULL || newmini == NULL) {
          free(file);
          free(p);
          mini_free(newmini);
          _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
        }

        memcpy(p, reader, sizeof(pu_config_reader_t));
        reader->file = file;
        reader->line = 0;
        reader->_parent = p;
        reader->_mini = newmini;
        reader->_includes = NULL;
      }
      return 0;
    }

    if(reader->repo) {
      pu_repo_t *r = reader->repo;
      char *v, *ctx;
      switch(s->type) {
        case PU_CONFIG_OPTION_SIGLEVEL:
          if(_pu_parse_siglevel(mini->value, &(r->siglevel),
                &(r->siglevel_mask)) != 0) {
            reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
            reader->error = 1;
          }
          break;
        case PU_CONFIG_OPTION_SERVER:
          r->servers = alpm_list_add(r->servers, strdup(mini->value));
          break;
        case PU_CONFIG_OPTION_USAGE:
          for(v = strtok_r(mini->value, " ", &ctx); v; v = strtok_r(NULL, " ", &ctx)) {
            if(strcmp(v, "Sync") == 0) {
              r->usage |= ALPM_DB_USAGE_SYNC;
            } else if(strcmp(v, "Search") == 0) {
              r->usage |= ALPM_DB_USAGE_SEARCH;
            } else if(strcmp(v, "Install") == 0) {
              r->usage |= ALPM_DB_USAGE_INSTALL;
            } else if(strcmp(v, "Upgrade") == 0) {
              r->usage |= ALPM_DB_USAGE_UPGRADE;
            } else if(strcmp(v, "All") == 0) {
              r->usage |= ALPM_DB_USAGE_ALL;
            } else {
              reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
              reader->error = 1;
            }
          }
          break;
        default:
          reader->status = PU_CONFIG_READER_STATUS_UNKNOWN_OPTION;
          break;
      }
    } else if(reader->section == NULL) {
      reader->status = PU_CONFIG_READER_STATUS_UNKNOWN_OPTION;
    } else if(mini->value) {
      switch(s->type) {
        case PU_CONFIG_OPTION_ROOTDIR:
          SETSTROPT(config->rootdir, mini->value);
          break;
        case PU_CONFIG_OPTION_DBPATH:
          SETSTROPT(config->dbpath, mini->value);
          break;
        case PU_CONFIG_OPTION_GPGDIR:
          SETSTROPT(config->gpgdir, mini->value);
          break;
        case PU_CONFIG_OPTION_LOGFILE:
          SETSTROPT(config->logfile, mini->value);
          break;
        case PU_CONFIG_OPTION_ARCHITECTURE:
          SETSTROPT(config->architecture, mini->value);
          break;
        case PU_CONFIG_OPTION_XFERCOMMAND:
          free(config->xfercommand);
          if((config->xfercommand = strdup(mini->value)) == NULL) {
            _PU_ERR(reader, PU_CONFIG_READER_STATUS_ERROR);
          }
          break;
        case PU_CONFIG_OPTION_CLEANMETHOD:
          if(_pu_parse_cleanmethod(config, mini->value) != 0) {
              reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
              reader->error = 1;
          }
          break;
        case PU_CONFIG_OPTION_USEDELTA:
          {
            char *end;
            float d = strtof(mini->value, &end);
            if(*end != '\0' || d < 0.0 || d > 2.0) {
              reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
              reader->error = 1;
            } else {
              config->usedelta = d;
            }
          }
          break;
        case PU_CONFIG_OPTION_SIGLEVEL:
          if(_pu_parse_siglevel(mini->value, &(config->siglevel),
                &(config->siglevel_mask)) != 0) {
            reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
            reader->error = 1;
          }
          break;
        case PU_CONFIG_OPTION_LOCAL_SIGLEVEL:
          if(_pu_parse_siglevel(mini->value, &(config->localfilesiglevel),
                &(config->localfilesiglevel_mask)) != 0) {
            reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
            reader->error = 1;
          }
          break;
        case PU_CONFIG_OPTION_REMOTE_SIGLEVEL:
          if(_pu_parse_siglevel(mini->value, &(config->remotefilesiglevel),
                &(config->remotefilesiglevel_mask)) != 0) {
            reader->status = PU_CONFIG_READER_STATUS_INVALID_VALUE;
            reader->error = 1;
          }
          break;
        case PU_CONFIG_OPTION_HOLDPKGS:
          APPENDLIST(&config->holdpkgs, mini->value);
          break;
        case PU_CONFIG_OPTION_IGNOREPKGS:
          APPENDLIST(&config->ignorepkgs, mini->value);
          break;
        case PU_CONFIG_OPTION_IGNOREGROUPS:
          APPENDLIST(&config->ignoregroups, mini->value);
          break;
        case PU_CONFIG_OPTION_NOUPGRADE:
          APPENDLIST(&config->noupgrade, mini->value);
          break;
        case PU_CONFIG_OPTION_NOEXTRACT:
          APPENDLIST(&config->noextract, mini->value);
          break;
        case PU_CONFIG_OPTION_CACHEDIRS:
          APPENDLIST(&config->cachedirs, mini->value);
          break;
        default:
          reader->status = PU_CONFIG_READER_STATUS_UNKNOWN_OPTION;
          break;
      }
    } else {
      switch(s->type) {
        case PU_CONFIG_OPTION_COLOR:
          config->color = 1;
          break;
        case PU_CONFIG_OPTION_USESYSLOG:
          config->usesyslog = 1;
          break;
        case PU_CONFIG_OPTION_USEDELTA:
          config->usedelta = 0.7;
          break;
        case PU_CONFIG_OPTION_TOTALDOWNLOAD:
          config->totaldownload = 1;
          break;
        case PU_CONFIG_OPTION_CHECKSPACE:
          config->checkspace = 1;
          break;
        case PU_CONFIG_OPTION_VERBOSEPKGLISTS:
          config->verbosepkglists = 1;
          break;
        case PU_CONFIG_OPTION_ILOVECANDY:
          config->ilovecandy = 1;
          break;
        default:
          reader->status = PU_CONFIG_READER_STATUS_UNKNOWN_OPTION;
          break;
      }
    }
  }

#undef _PU_ERR

  return  0;
}

#undef SETSTROPT
#undef APPENDLIST

pu_config_reader_t *pu_config_reader_new(pu_config_t *config, const char *file)
{
  pu_config_reader_t *reader = calloc(sizeof(pu_config_reader_t), 1);
  if(reader == NULL) { return NULL; }
  if((reader->_mini = mini_init(file)) == NULL) {
    pu_config_reader_free(reader); return NULL;
  }
  if((reader->file = strdup(file)) == NULL) {
    pu_config_reader_free(reader); return NULL;
  }
  reader->config = config;
  return reader;
}

void pu_config_reader_free(pu_config_reader_t *reader)
{
  if(!reader) { return; }
  free(reader->file);
  free(reader->section);
  mini_free(reader->_mini);
  FREELIST(reader->_includes);
  pu_config_reader_free(reader->_parent);
  free(reader);
}
/* vim: set ts=2 sw=2 et: */
