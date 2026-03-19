/* ==============================================================================
 * sindarin-pkg-mongo/src/mongo.sn.c — MongoDB client implementation
 * ==============================================================================
 * Implements MongoClient, MongoCollection, and MongoDoc via the libmongoc C API.
 *
 * mongoc_init() / mongoc_cleanup() are called automatically via constructor /
 * destructor attributes so callers need no explicit setup.
 *
 * Query results are collected by iterating the mongoc cursor and copying each
 * bson_t into a heap-allocated element stored in an SnArray. The elem_release
 * callback frees each copied bson_t when the array is released.
 *
 * All JSON strings passed from Sindarin are parsed via bson_new_from_json().
 * Field access on MongoDoc uses bson_iter to traverse the copied bson_t.
 * ============================================================================== */

#define MONGOC_STATIC
#define BSON_STATIC

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <mongoc/mongoc.h>

/* ============================================================================
 * Type Definitions
 * ============================================================================ */

typedef __sn__MongoDoc        RtMongoDoc;
typedef __sn__MongoCollection RtMongoCollection;
typedef __sn__MongoClient     RtMongoClient;

/* ============================================================================
 * Driver Lifecycle
 * ============================================================================ */

static void __attribute__((constructor)) mongo_driver_init(void)
{
    mongoc_init();
}

static void __attribute__((destructor)) mongo_driver_cleanup(void)
{
    mongoc_cleanup();
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

#define CLIENT_PTR(c)  ((mongoc_client_t     *)(uintptr_t)(c)->client_ptr)
#define COLL_PTR(c)    ((mongoc_collection_t *)(uintptr_t)(c)->coll_ptr)
#define DOC_BSON(d)    ((bson_t              *)(uintptr_t)(d)->bson_ptr)

static bson_t *parse_json_or_die(const char *json, const char *ctx)
{
    bson_error_t err;
    bson_t *b = bson_new_from_json((const uint8_t *)json, -1, &err);
    if (!b) {
        fprintf(stderr, "mongo: %s: invalid JSON: %s\n", ctx, err.message);
        exit(1);
    }
    return b;
}

/* ============================================================================
 * MongoDoc — cleanup and cursor collection
 * ============================================================================ */

static void cleanup_mongo_doc_elem(void *p)
{
    RtMongoDoc *doc = (RtMongoDoc *)p;
    bson_t *b = DOC_BSON(doc);
    if (b) bson_destroy(b);
    doc->bson_ptr = 0;
}

static SnArray *collect_docs(mongoc_cursor_t *cursor)
{
    SnArray *arr = sn_array_new(sizeof(RtMongoDoc), 8);
    arr->elem_tag     = SN_TAG_STRUCT;
    arr->elem_release = cleanup_mongo_doc_elem;

    const bson_t *doc;
    while (mongoc_cursor_next(cursor, &doc)) {
        bson_t *copy = bson_copy(doc);
        RtMongoDoc elem = {0};
        elem.bson_ptr = (long long)(uintptr_t)copy;
        sn_array_push(arr, &elem);
    }

    bson_error_t err;
    if (mongoc_cursor_error(cursor, &err)) {
        fprintf(stderr, "mongo: cursor error: %s\n", err.message);
        mongoc_cursor_destroy(cursor);
        exit(1);
    }

    mongoc_cursor_destroy(cursor);
    return arr;
}

/* ============================================================================
 * MongoDoc Accessors
 * ============================================================================ */

char *sn_mongo_doc_get_string(__sn__MongoDoc *doc, char *key)
{
    if (!doc || !key) return strdup("");
    bson_t *b = DOC_BSON(doc);
    if (!b) return strdup("");

    bson_iter_t iter;
    if (bson_iter_init_find(&iter, b, key)) {
        if (BSON_ITER_HOLDS_UTF8(&iter)) {
            const char *s = bson_iter_utf8(&iter, NULL);
            return strdup(s ? s : "");
        }
        if (BSON_ITER_HOLDS_INT32(&iter)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", bson_iter_int32(&iter));
            return strdup(buf);
        }
        if (BSON_ITER_HOLDS_INT64(&iter)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)bson_iter_int64(&iter));
            return strdup(buf);
        }
        if (BSON_ITER_HOLDS_DOUBLE(&iter)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", bson_iter_double(&iter));
            return strdup(buf);
        }
        if (BSON_ITER_HOLDS_BOOL(&iter))
            return strdup(bson_iter_bool(&iter) ? "true" : "false");
    }
    return strdup("");
}

long long sn_mongo_doc_get_int(__sn__MongoDoc *doc, char *key)
{
    if (!doc || !key) return 0;
    bson_t *b = DOC_BSON(doc);
    if (!b) return 0;

    bson_iter_t iter;
    if (bson_iter_init_find(&iter, b, key)) {
        if (BSON_ITER_HOLDS_INT32(&iter))  return (long long)bson_iter_int32(&iter);
        if (BSON_ITER_HOLDS_INT64(&iter))  return (long long)bson_iter_int64(&iter);
        if (BSON_ITER_HOLDS_DOUBLE(&iter)) return (long long)bson_iter_double(&iter);
    }
    return 0;
}

double sn_mongo_doc_get_float(__sn__MongoDoc *doc, char *key)
{
    if (!doc || !key) return 0.0;
    bson_t *b = DOC_BSON(doc);
    if (!b) return 0.0;

    bson_iter_t iter;
    if (bson_iter_init_find(&iter, b, key)) {
        if (BSON_ITER_HOLDS_DOUBLE(&iter)) return bson_iter_double(&iter);
        if (BSON_ITER_HOLDS_INT32(&iter))  return (double)bson_iter_int32(&iter);
        if (BSON_ITER_HOLDS_INT64(&iter))  return (double)bson_iter_int64(&iter);
    }
    return 0.0;
}

bool sn_mongo_doc_get_bool(__sn__MongoDoc *doc, char *key)
{
    if (!doc || !key) return false;
    bson_t *b = DOC_BSON(doc);
    if (!b) return false;

    bson_iter_t iter;
    if (bson_iter_init_find(&iter, b, key)) {
        if (BSON_ITER_HOLDS_BOOL(&iter)) return bson_iter_bool(&iter);
    }
    return false;
}

bool sn_mongo_doc_is_null(__sn__MongoDoc *doc, char *key)
{
    if (!doc || !key) return true;
    bson_t *b = DOC_BSON(doc);
    if (!b) return true;

    bson_iter_t iter;
    if (!bson_iter_init_find(&iter, b, key)) return true;
    return BSON_ITER_HOLDS_NULL(&iter);
}

char *sn_mongo_doc_to_json(__sn__MongoDoc *doc)
{
    if (!doc) return strdup("{}");
    bson_t *b = DOC_BSON(doc);
    if (!b) return strdup("{}");

    size_t len;
    char *json = bson_as_relaxed_extended_json(b, &len);
    char *result = strdup(json);
    bson_free(json);
    return result;
}

/* ============================================================================
 * MongoClient
 * ============================================================================ */

RtMongoClient *sn_mongo_client_connect(char *uri)
{
    if (!uri) {
        fprintf(stderr, "MongoClient.connect: uri is NULL\n");
        exit(1);
    }

    bson_error_t err;
    mongoc_uri_t *u = mongoc_uri_new_with_error(uri, &err);
    if (!u) {
        fprintf(stderr, "MongoClient.connect: invalid URI '%s': %s\n", uri, err.message);
        exit(1);
    }

    mongoc_client_t *client = mongoc_client_new_from_uri(u);
    mongoc_uri_destroy(u);

    if (!client) {
        fprintf(stderr, "MongoClient.connect: failed to create client\n");
        exit(1);
    }

    RtMongoClient *c = (RtMongoClient *)calloc(1, sizeof(RtMongoClient));
    if (!c) {
        fprintf(stderr, "MongoClient.connect: allocation failed\n");
        mongoc_client_destroy(client);
        exit(1);
    }
    c->client_ptr = (long long)(uintptr_t)client;
    return c;
}

RtMongoCollection *sn_mongo_client_collection(RtMongoClient *c, char *db, char *name)
{
    if (!c || !db || !name) {
        fprintf(stderr, "MongoClient.collection: NULL argument\n");
        exit(1);
    }

    mongoc_collection_t *coll = mongoc_client_get_collection(CLIENT_PTR(c), db, name);
    if (!coll) {
        fprintf(stderr, "MongoClient.collection: failed to get collection '%s.%s'\n", db, name);
        exit(1);
    }

    RtMongoCollection *col = (RtMongoCollection *)calloc(1, sizeof(RtMongoCollection));
    if (!col) {
        fprintf(stderr, "MongoClient.collection: allocation failed\n");
        mongoc_collection_destroy(coll);
        exit(1);
    }
    col->coll_ptr = (long long)(uintptr_t)coll;
    return col;
}

void sn_mongo_client_dispose(RtMongoClient *c)
{
    if (!c) return;
    mongoc_client_destroy(CLIENT_PTR(c));
    c->client_ptr = 0;
}

/* ============================================================================
 * MongoCollection
 * ============================================================================ */

void sn_mongo_coll_insert_one(RtMongoCollection *col, char *json)
{
    if (!col || !json) return;
    bson_t *doc = parse_json_or_die(json, "insertOne");
    bson_error_t err;
    if (!mongoc_collection_insert_one(COLL_PTR(col), doc, NULL, NULL, &err)) {
        fprintf(stderr, "mongo: insertOne: %s\n", err.message);
        bson_destroy(doc);
        exit(1);
    }
    bson_destroy(doc);
}

SnArray *sn_mongo_coll_find(RtMongoCollection *col, char *filter)
{
    if (!col || !filter) return sn_array_new(sizeof(RtMongoDoc), 0);
    bson_t *f = parse_json_or_die(filter, "find");
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(COLL_PTR(col), f, NULL, NULL);
    bson_destroy(f);
    return collect_docs(cursor);
}

void sn_mongo_coll_update_one(RtMongoCollection *col, char *filter, char *update)
{
    if (!col || !filter || !update) return;
    bson_t *f = parse_json_or_die(filter, "updateOne filter");
    bson_t *u = parse_json_or_die(update, "updateOne update");
    bson_error_t err;
    if (!mongoc_collection_update_one(COLL_PTR(col), f, u, NULL, NULL, &err)) {
        fprintf(stderr, "mongo: updateOne: %s\n", err.message);
        bson_destroy(f);
        bson_destroy(u);
        exit(1);
    }
    bson_destroy(f);
    bson_destroy(u);
}

void sn_mongo_coll_update_many(RtMongoCollection *col, char *filter, char *update)
{
    if (!col || !filter || !update) return;
    bson_t *f = parse_json_or_die(filter, "updateMany filter");
    bson_t *u = parse_json_or_die(update, "updateMany update");
    bson_error_t err;
    if (!mongoc_collection_update_many(COLL_PTR(col), f, u, NULL, NULL, &err)) {
        fprintf(stderr, "mongo: updateMany: %s\n", err.message);
        bson_destroy(f);
        bson_destroy(u);
        exit(1);
    }
    bson_destroy(f);
    bson_destroy(u);
}

void sn_mongo_coll_delete_one(RtMongoCollection *col, char *filter)
{
    if (!col || !filter) return;
    bson_t *f = parse_json_or_die(filter, "deleteOne");
    bson_error_t err;
    if (!mongoc_collection_delete_one(COLL_PTR(col), f, NULL, NULL, &err)) {
        fprintf(stderr, "mongo: deleteOne: %s\n", err.message);
        bson_destroy(f);
        exit(1);
    }
    bson_destroy(f);
}

void sn_mongo_coll_delete_many(RtMongoCollection *col, char *filter)
{
    if (!col || !filter) return;
    bson_t *f = parse_json_or_die(filter, "deleteMany");
    bson_error_t err;
    if (!mongoc_collection_delete_many(COLL_PTR(col), f, NULL, NULL, &err)) {
        fprintf(stderr, "mongo: deleteMany: %s\n", err.message);
        bson_destroy(f);
        exit(1);
    }
    bson_destroy(f);
}

long long sn_mongo_coll_count(RtMongoCollection *col, char *filter)
{
    if (!col || !filter) return 0;
    bson_t *f = parse_json_or_die(filter, "count");
    bson_error_t err;
    int64_t n = mongoc_collection_count_documents(COLL_PTR(col), f, NULL, NULL, NULL, &err);
    bson_destroy(f);
    if (n < 0) {
        fprintf(stderr, "mongo: count: %s\n", err.message);
        exit(1);
    }
    return (long long)n;
}

void sn_mongo_coll_dispose(RtMongoCollection *col)
{
    if (!col) return;
    mongoc_collection_destroy(COLL_PTR(col));
    col->coll_ptr = 0;
}
