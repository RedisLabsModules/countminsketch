/*
* countminsketch - an apporximate frequency counter on String DMA
* Based on the work of Graham Cormode and S. Muthukrishnan:
* http://dimacs.rutgers.edu/~graham/pubs/papers/cmsoft.pdf
* Copyright (C) 2016 Redis Labs
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include "../redismodule.h"
#include "../rmutil/strings.h"
#include "../rmutil/vector.h"
#include "../rmutil/test_util.h"
#include "xxhash.h"

#define RM_MODULE_NAME "COUNTMINSKETCH"
#define RLMODULE_VERSION "1.0.0"
#define RLMODULE_PROTO "1.0"
#define RLMODULE_DESC "An approximate frequency counter"

#define CMS_SIGNATURE "COUNTMINSKETCH:1.0:"
#define CMS_SEED 0

#define MAGIC 2147483647  // 2^31-1 according to the paper

#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct CMSketch {
  long long c;
  int d;
  int w;
  int *v;
  unsigned int *ha, *hb;
} CMSketch;

/* Creates a new sketch based with given dimensions. */
CMSketch *NewCMSketch(RedisModuleCtx *ctx, RedisModuleKey *key, int width,
                      int depth) {
  size_t slen = strlen(CMS_SIGNATURE) + sizeof(CMSketch) +
                sizeof(int) * width * depth +     // vector
                sizeof(unsigned int) * 2 * depth; // hashes

  if (RedisModule_StringTruncate(key, slen) != REDISMODULE_OK) {
    RedisModule_ReplyWithError(ctx,
                               "ERR could not truncate key to required size");
    return NULL;
  }
  size_t dlen;
  char *dma =
      RedisModule_StringDMA(key, &dlen, REDISMODULE_READ | REDISMODULE_WRITE);

  size_t off = strlen(CMS_SIGNATURE);
  memcpy(dma, CMS_SIGNATURE, off);

  CMSketch *s = (CMSketch *)&dma[off];
  s->c = 0;
  s->w = width;
  s->d = depth;
  off += sizeof(CMSketch);
  s->v = (int *)&dma[off];
  off += sizeof(int) * width * depth;
  s->ha = (unsigned int *)&dma[off];
  off += sizeof(unsigned int) * depth;
  s->hb = (unsigned int *)&dma[off];

  srand(CMS_SEED);
  for (int i = 0; i < s->d; i++) {
    s->ha[i] = rand() & MAGIC;
    s->hb[i] = rand() & MAGIC;
  }

  return s;
}

/* Loads a sketch from a string key. */
CMSketch *GetCMSketch(RedisModuleCtx *ctx, RedisModuleKey *key) {
  size_t dlen;
  char *dma = RedisModule_StringDMA(key, &dlen, REDISMODULE_READ);

  size_t off = strlen(CMS_SIGNATURE);
  if (strncmp(CMS_SIGNATURE, dma, off)) {
    RedisModule_ReplyWithError(ctx, "ERR invalid signature");
    return NULL;
  }

  CMSketch *s = (CMSketch *)&dma[off];
  off += sizeof(CMSketch);
  s->v = (int *)&dma[off];
  off += sizeof(int) * s->w * s->d;
  s->ha = (unsigned int *)&dma[off];
  off += sizeof(unsigned int) * s->d;
  s->hb = (unsigned int *)&dma[off];

  return s;
}

/* CMS.INITBYDIM key width depth
*  CMS.INITBYERR key error probility
*/
int CMSInitCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc != 4) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  RedisModuleKey *key =
      RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);

  /* Verify that the key is empty. */
  if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_EMPTY) {
    RedisModule_ReplyWithError(ctx, "ERR key already exists");
    return REDISMODULE_ERR;
  }

  /* Get width and depth. */
  long long width, depth;
  size_t cmdlen;
  const char *cmd = RedisModule_StringPtrLen(argv[0], &cmdlen);
  if (!strcasecmp("cms.initbydim", cmd)) {
    if ((RedisModule_StringToLongLong(argv[2], &width) != REDISMODULE_OK) ||
        (width < 1) || (width > UINT16_MAX)) {
      RedisModule_ReplyWithError(ctx, "ERR invalid width");
      return REDISMODULE_ERR;
    }
    if ((RedisModule_StringToLongLong(argv[3], &depth) != REDISMODULE_OK) ||
        (depth < 1) || (depth > UINT16_MAX)) {
      RedisModule_ReplyWithError(ctx, "ERR invalid depth");
      return REDISMODULE_ERR;
    }
  } else {
    double error, prob;
    if ((RedisModule_StringToDouble(argv[2], &error) != REDISMODULE_OK) ||
        (error < 0) || (error >= 1)) {
      RedisModule_ReplyWithError(ctx, "ERR invalid error");
      return REDISMODULE_ERR;
    }
    if ((RedisModule_StringToDouble(argv[3], &prob) != REDISMODULE_OK) ||
        (prob < 0) || (prob >= 1)) {
      RedisModule_ReplyWithError(ctx, "ERR invalid probabilty");
      return REDISMODULE_ERR;
    }
    width = ceil(2 / error);
    depth = ceil(log10f(prob) / log10f(0.5));
  }

  CMSketch *s = NewCMSketch(ctx, key, width, depth);
  if (!s) {
    return REDISMODULE_ERR;
  }

  RedisModule_ReplyWithSimpleString(ctx, "OK");
  return REDISMODULE_OK;
}

/* CMS.INCRBY key item value [...]
*/
int CMSIncrByCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if ((argc < 4) || (argc % 2 != 0)) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  /* Validate that all values are integers. */
  for (int i = 3; i < argc; i += 2) {
    long long l;
    // TODO: check for integer over/underflow
    if ((RedisModule_StringToLongLong(argv[i], &l) != REDISMODULE_OK)) {
      RedisModule_ReplyWithError(ctx, "ERR value is not a valid integer");
      return REDISMODULE_ERR;
    }
  }

  RedisModuleKey *key =
      RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);

  /* If the key is empty, initialize with the defaults. */
  int init = 0;
  CMSketch *s;
  if (RedisModule_KeyType(key) == REDISMODULE_KEYTYPE_EMPTY) {
    init = 1;
  } else if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STRING) {
    RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    return REDISMODULE_ERR;
  }

  if (init) {
    s = NewCMSketch(ctx, key, 2000, 10);  // %0.01 error at %0.01 probabilty
  } else {
    s = GetCMSketch(ctx, key);
  }
  if (!s) {
    return REDISMODULE_ERR;
  }

  /* Loop over the input items and update their counts. */
  for (int i = 3; i < argc; i += 2) {
    size_t len;
    const char *item = RedisModule_StringPtrLen(argv[i - 1], &len);
    long long value;
    RedisModule_StringToLongLong(argv[i], &value);
    s->c += value;
    for (int j = 0; j < s->d; j++) {
      long long h = (s->ha[j] * XXH32(item, len, MAGIC) + s->hb[j]) & MAGIC;
      // TODO: check for over/underflow
      s->v[j * s->w + h % s->w] += value;
    }
  }

  RedisModule_ReplyWithSimpleString(ctx, "OK");
  return REDISMODULE_OK;
}

/* CMS.QUERY key item [...]
*/
int CMSQueryCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc < 3) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);

  /* If the key is empty, return NULL, otherwise verify that it is a string. */
  if (RedisModule_KeyType(key) == REDISMODULE_KEYTYPE_EMPTY) {
    RedisModule_ReplyWithNull(ctx);
    return REDISMODULE_OK;
  } else if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STRING) {
    RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    return REDISMODULE_ERR;
  }

  CMSketch *s = GetCMSketch(ctx, key);
  if (!s) {
    return REDISMODULE_ERR;
  }

  /* Loop over the items and estimate their counts. */
  RedisModule_ReplyWithArray(ctx, argc - 2);
  for (int i = 2; i < argc; i++) {
    size_t len;
    const char *value = RedisModule_StringPtrLen(argv[i], &len);
    long long h = (s->ha[0] * XXH32(value, len, MAGIC) + s->hb[0]) & MAGIC;
    int freq = s->v[h % s->w];
    for (int j = 1; j < s->d; j++) {
      h = (s->ha[j] * XXH32(value, len, MAGIC) + s->hb[j]) & MAGIC;
      freq = min(freq, s->v[j * s->w + h % s->w]);
    }
    RedisModule_ReplyWithLongLong(ctx, freq);
  }

  return REDISMODULE_OK;
}

/* CMS.DEBUG key
*/
int CMSDebugCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  if (argc != 2) {
    return RedisModule_WrongArity(ctx);
  }
  RedisModule_AutoMemory(ctx);

  RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);

  /* Verify that the key is empty or a string. */
  if (RedisModule_KeyType(key) == REDISMODULE_KEYTYPE_EMPTY) {
    RedisModule_ReplyWithNull(ctx);
    return REDISMODULE_OK;
  }
  if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STRING) {
    RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    return REDISMODULE_ERR;
  }

  CMSketch *s = GetCMSketch(ctx, key);
  if (!s) {
    return REDISMODULE_ERR;
  }

  RedisModule_ReplyWithArray(ctx, 4);
  RedisModule_ReplyWithString(
      ctx, RMUtil_CreateFormattedString(ctx, "Count: %lld", s->c));
  RedisModule_ReplyWithString(
      ctx, RMUtil_CreateFormattedString(ctx, "Width: %d", s->w));
  RedisModule_ReplyWithString(
      ctx, RMUtil_CreateFormattedString(ctx, "Depth: %d", s->d));
  RedisModule_ReplyWithString(
      ctx, RMUtil_CreateFormattedString(ctx, "Size: %d",
                                        RedisModule_ValueLength(key)));
  return REDISMODULE_OK;
}

int testSanity(RedisModuleCtx *ctx) {
  RedisModuleCallReply *r;

  r = RedisModule_Call(ctx, "cms.incrby", "ccccccccccc", "cms", "a", "1", "b",
                       "2", "c", "3", "d", "4", "e", "5");
  r = RedisModule_Call(ctx, "cms.query", "ccccccc", "cms", "a", "b", "c", "d",
                       "e", "foo");
  RMUtil_Assert(RedisModule_CallReplyLength(r) == 6);
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 0), "1");
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 1), "2");
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 2), "3");
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 3), "4");
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 4), "5");
  RMUtil_AssertReplyEquals(RedisModule_CallReplyArrayElement(r, 5), "0");

  r = RedisModule_Call(ctx, "FLUSHALL", "");

  return 0;
}

int TestModule(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  RedisModule_AutoMemory(ctx);

  /* TODO: calling flushall but checking only for db 0. */
  RedisModuleCallReply *r = RedisModule_Call(ctx, "DBSIZE", "");
  if (RedisModule_CallReplyInteger(r) != 0) {
    RedisModule_ReplyWithError(ctx,
                               "ERR test must be run on an empty instance");
    return REDISMODULE_ERR;
  }

  RMUtil_Test(testSanity);

  RedisModule_ReplyWithSimpleString(ctx, "PASS");
  return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) {
  if (RedisModule_Init(ctx, RM_MODULE_NAME, 1, REDISMODULE_APIVER_1) ==
      REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "cms.initbydim", CMSInitCommand,
                                "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;
  if (RedisModule_CreateCommand(ctx, "cms.initbyerr", CMSInitCommand,
                                "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;
  if (RedisModule_CreateCommand(ctx, "cms.incrby", CMSIncrByCommand,
                                "write deny-oom", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;
  if (RedisModule_CreateCommand(ctx, "cms.query", CMSQueryCommand, "readonly",
                                1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;
  if (RedisModule_CreateCommand(ctx, "cms.debug", CMSDebugCommand, "readonly",
                                2, 2, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;
  if (RedisModule_CreateCommand(ctx, "cms.test", TestModule, "write deny-oom",
                                0, 0, 0) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
