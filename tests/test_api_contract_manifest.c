#include "api/api_contract.h"
#include "protocol/crypto.h"
#include "support/test_check.h"
#include "yyjson.h"
#include <stdio.h>
#include <string.h>

static char manifest_bytes[65536], fixture_bytes[API_CONTRACT_RESPONSE_CAP + 1];
static unsigned char manifest_pool[262144], fixture_pool[4 * 1024 * 1024];
static Crypto crypto;

static bool basename_only(const char *name)
{
    return name != NULL && name[0] != '\0' && strlen(name) < 96 &&
        strpbrk(name,"/\\:") == NULL && strstr(name,"..") == NULL;
}
static size_t load(const char *name, char *buffer, size_t cap)
{
    char path[1024]; FILE *file = NULL;
    CHECK(basename_only(name));
    CHECK(sprintf_s(path,sizeof(path),"%s/%s",API_FIXTURE_DIR,name) > 0);
    CHECK(fopen_s(&file,path,"rb") == 0 && file != NULL);
    size_t bytes = fread(buffer,1,cap,file);
    CHECK(!ferror(file) && bytes < cap && fgetc(file) == EOF);
    CHECK(fclose(file) == 0); return bytes;
}
static yyjson_doc *parse(char *bytes, size_t size, void *pool, size_t capacity)
{
    yyjson_alc allocator;
    CHECK(yyjson_alc_pool_init(&allocator,pool,capacity));
    yyjson_doc *doc = yyjson_read_opts(bytes,size,0,&allocator,NULL);
    CHECK(doc != NULL); return doc;
}
static const char *string(yyjson_val *obj, const char *key)
{
    yyjson_val *v = yyjson_obj_get(obj,key);
    CHECK(yyjson_is_str(v) && yyjson_get_len(v) == strlen(yyjson_get_str(v)));
    return yyjson_get_str(v);
}
static uint64_t number(yyjson_val *obj, const char *key)
{
    yyjson_val *v = yyjson_obj_get(obj,key); CHECK(yyjson_is_uint(v)); return yyjson_get_uint(v);
}
static bool digest_matches(const char *bytes, size_t size, const char *expected)
{
    uint8_t digest[32]; char hex[65]; static const char digits[] = "0123456789abcdef";
    CHECK(crypto_sha256(&crypto,bytes,size,digest) == CRYPTO_OK);
    for (size_t i = 0; i < 32; ++i) { hex[i*2] = digits[digest[i] >> 4]; hex[i*2+1] = digits[digest[i] & 15]; }
    hex[64] = '\0'; return strcmp(hex,expected) == 0;
}
static bool count_matches(yyjson_val *root, const char *pointer, uint64_t expected)
{
    if (pointer[0] == '\0') { return yyjson_is_obj(root) && expected == 0; }
    yyjson_val *array = yyjson_ptr_get(root,pointer);
    return yyjson_is_arr(array) && (uint64_t)yyjson_arr_size(array) == expected;
}
static unsigned fixture_bit(const char *kind)
{
    if (strcmp(kind,"observed-positive") == 0) { return 1; }
    if (strcmp(kind,"observed-error") == 0) { return 2; }
    if (strcmp(kind,"observed-pagination") == 0) { return 4; }
    CHECK(strcmp(kind,"fault-injection") == 0 || strcmp(kind,"observed-native-rpc") == 0 ||
        strcmp(kind,"observed-alternative") == 0);
    return 0;
}
static bool field_type(const ApiField *field, yyjson_val *value)
{
    if (value == NULL) { return !field->required; }
    if (yyjson_is_null(value)) { return field->nullable; }
    switch (field->type) {
    case API_FIELD_STRING: return yyjson_is_str(value);
    case API_FIELD_UINT: return yyjson_is_uint(value);
    case API_FIELD_BOOL: return yyjson_is_bool(value);
    case API_FIELD_OBJECT: return yyjson_is_obj(value);
    case API_FIELD_ARRAY: return yyjson_is_arr(value);
    case API_FIELD_STRING_ARRAY: {
        if (!yyjson_is_arr(value)) { return false; }
        size_t i, count; yyjson_val *item;
        yyjson_arr_foreach(value,i,count,item) { if (!yyjson_is_str(item)) { return false; } }
        return true;
    }
    default: return false;
    }
}
static bool variables_match(const ApiContract *c, yyjson_val *variables)
{
    if (!yyjson_is_obj(variables)) { return c->variable_count == 0; }
    for (size_t i = 0; i < c->variable_count; ++i) {
        if (!field_type(&c->variables[i],yyjson_obj_get(variables,c->variables[i].name))) { return false; }
    }
    return true;
}
static bool header_present(yyjson_val *names, const char *name)
{
    size_t i, count; yyjson_val *item;
    if (!yyjson_is_arr(names)) { return false; }
    yyjson_arr_foreach(names,i,count,item) {
        if (yyjson_is_str(item) && _stricmp(yyjson_get_str(item),name) == 0) { return true; }
    }
    return false;
}
static void enabled_fixture(const ApiContract *c, yyjson_val *root, yyjson_val *entry)
{
    CHECK(strcmp(string(entry,"wire_operation"),c->wire_operation) == 0);
    yyjson_val *request = yyjson_obj_get(root,"request"), *body = yyjson_obj_get(request,"body");
    CHECK(strcmp(string(request,"method"),"POST") == 0);
    CHECK(strcmp(string(request,"url"),c->url) == 0);
    CHECK(strcmp(string(body,"operationName"),c->wire_operation) == 0);
    yyjson_val *hash = yyjson_ptr_get(body,"/extensions/persistedQuery/sha256Hash");
    CHECK(yyjson_is_str(hash) && strcmp(yyjson_get_str(hash),c->persisted_sha256) == 0);
    CHECK(number(yyjson_obj_get(yyjson_obj_get(body,"extensions"),"persistedQuery"),"version") == 1);
    CHECK(variables_match(c,yyjson_obj_get(body,"variables")));
    CHECK(!variables_match(c,NULL));
    yyjson_val *names = yyjson_obj_get(request,"header_names");
    for (size_t i = 0; i < c->header_count; ++i) {
        if (c->headers[i].required) { CHECK(header_present(names,c->headers[i].name)); }
    }
    CHECK(!header_present(names,"unobserved-header"));
    CHECK(number(entry,"original_response_bytes") <= c->response_cap);
    yyjson_val *response = yyjson_ptr_get(root,"/response/body");
    for (size_t i = 0; i < c->response_count; ++i) {
        CHECK(field_type(&c->response[i],yyjson_ptr_get(response,c->response[i].name)));
    }
    yyjson_val *typename = yyjson_ptr_get(response,"/data/albumUnion/__typename");
    const char *kind = string(entry,"kind");
    if (strcmp(kind,"observed-error") == 0) {
        CHECK(strcmp(yyjson_get_str(typename),"NotFound") == 0);
    } else {
        CHECK(strcmp(yyjson_get_str(typename),"Album") == 0);
        if (strcmp(kind,"observed-pagination") == 0) {
            CHECK(number(entry,"expected_count") == 0);
            CHECK(number(yyjson_obj_get(body,"variables"),"offset") >=
                yyjson_get_uint(yyjson_ptr_get(response,"/data/albumUnion/tracksV2/totalCount")));
        }
    }
}
void api_contract_manifest_tests(void)
{
    crypto_init(&crypto);
    size_t bytes = load("manifest.json",manifest_bytes,sizeof(manifest_bytes));
    yyjson_doc *manifest = parse(manifest_bytes,bytes,manifest_pool,sizeof(manifest_pool));
    yyjson_val *root = yyjson_doc_get_root(manifest), *operations = yyjson_obj_get(root,"operations");
    CHECK(number(root,"schema_version") == 1);
    CHECK(yyjson_is_arr(operations) && yyjson_arr_size(operations) == API_OPERATION_COUNT);
    unsigned fixture_coverage[API_OPERATION_COUNT] = {0};
    bool seen[API_OPERATION_COUNT] = {false};
    size_t i, maximum; yyjson_val *entry;
    yyjson_arr_foreach(operations,i,maximum,entry) {
        uint64_t id = number(entry,"id"); CHECK(id < API_OPERATION_COUNT && !seen[id]); seen[id] = true;
        const ApiContract *c = contract_inspect((ApiOperation)id);
        CHECK(strcmp(string(entry,"name"),c->name) == 0);
        CHECK(strcmp(string(entry,"source_sha256"),c->source_sha256) == 0);
        CHECK(strcmp(string(entry,"evidence_id"),c->evidence_id) == 0);
        CHECK(number(entry,"missing_proofs") == contract_missing_proofs(c));
        yyjson_val *enabled = yyjson_obj_get(entry,"enabled"); CHECK(yyjson_is_bool(enabled));
        CHECK(yyjson_get_bool(enabled) == (contract_validate(c) == API_CONTRACT_OK));
        if (c->wire_operation == NULL) { CHECK(yyjson_is_null(yyjson_obj_get(entry,"wire_operation"))); }
        else { CHECK(strcmp(string(entry,"wire_operation"),c->wire_operation) == 0); }
    }
    yyjson_val *fixtures = yyjson_obj_get(root,"fixtures");
    CHECK(yyjson_is_arr(fixtures) && yyjson_arr_size(fixtures) >= 4);
    yyjson_arr_foreach(fixtures,i,maximum,entry) {
        const char *file = string(entry,"file");
        CHECK(basename_only(file));
        for (size_t j = 0; j < i; ++j) {
            CHECK(strcmp(file,string(yyjson_arr_get(fixtures,j),"file")) != 0);
        }
        uint64_t id = number(entry,"operation"); CHECK(id < API_OPERATION_COUNT);
        fixture_coverage[id] |= fixture_bit(string(entry,"kind"));
        bytes = load(file,fixture_bytes,sizeof(fixture_bytes));
        CHECK(bytes <= API_CONTRACT_RESPONSE_CAP && bytes == number(entry,"bytes"));
        const char *sha = string(entry,"sha256"); CHECK(strlen(sha) == 64);
        CHECK(digest_matches(fixture_bytes,bytes,sha));
        /* A manifest hash protects the exact bytes, not just valid JSON. */
        fixture_bytes[0] ^= 1; CHECK(!digest_matches(fixture_bytes,bytes,sha)); fixture_bytes[0] ^= 1;
        yyjson_doc *doc = parse(fixture_bytes,bytes,fixture_pool,sizeof(fixture_pool));
        const char *pointer = string(entry,"count_pointer");
        uint64_t expected = number(entry,"expected_count");
        CHECK(count_matches(yyjson_doc_get_root(doc),pointer,expected));
        CHECK(!count_matches(yyjson_doc_get_root(doc),pointer,expected + 1));
        CHECK(!count_matches(yyjson_doc_get_root(doc),"/not-present",0));
        CHECK(strlen(string(entry,"provenance")) != 0);
        CHECK(strlen(string(entry,"evidence_id")) == 67);
        const ApiContract *c = contract_inspect((ApiOperation)id);
        const char *kind = string(entry,"kind");
        if (c->transport == API_PATHFINDER && strcmp(kind,"observed-native-rpc") != 0 &&
            strcmp(string(entry,"wire_operation"),c->wire_operation) == 0 &&
            (c->proofs & API_PROOF_VARIABLES) != 0) {
            CHECK(variables_match(c,yyjson_ptr_get(yyjson_doc_get_root(doc),"/request/body/variables")));
        }
        if (contract_validate(c) == API_CONTRACT_OK) { enabled_fixture(c,yyjson_doc_get_root(doc),entry); }
        yyjson_doc_free(doc);
    }
    for (unsigned op = 0; op < API_OPERATION_COUNT; ++op) {
        CHECK(seen[op]);
        if (contract_validate(contract_inspect((ApiOperation)op)) == API_CONTRACT_OK) {
            /* Fault injection/native IPC observations cannot enable HTTPS. */
            CHECK(fixture_coverage[op] == 7);
        }
    }
    CHECK(!basename_only("../outside.json") && !basename_only("C:\\secret.json"));
    CHECK(!basename_only("/outside.json") && !basename_only(""));
    static const char wrong_type[] = "{\"uri\":\"redacted\",\"locale\":\"\",\"offset\":true,\"limit\":2}";
    memcpy(fixture_bytes,wrong_type,sizeof(wrong_type));
    yyjson_doc *invalid = parse(fixture_bytes,sizeof(wrong_type)-1,fixture_pool,sizeof(fixture_pool));
    CHECK(!variables_match(contract_inspect(API_ALBUM),yyjson_doc_get_root(invalid)));
    yyjson_doc_free(invalid);
    yyjson_doc_free(manifest); CHECK(crypto_balanced(&crypto));
}
