/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "ext/standard/base64.h"
#include "ext/standard/sha1.h"

#include "wsclient/wsclient_handshake.h"

#include <string.h>

bool wsclient_compute_accept(const char *const key, const size_t key_len,
                             char out[WSCLIENT_ACCEPT_LENGTH])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    PHP_SHA1_CTX context;
    unsigned char digest[PHP_SHA1_DIGEST_LENGTH];

    if (key == NULL || key_len != WSCLIENT_KEY_LENGTH || out == NULL) {
        return false;
    }

    PHP_SHA1Init(&context);
    PHP_SHA1Update(&context, (const unsigned char *) key, key_len);
    PHP_SHA1Update(&context, (const unsigned char *) guid, sizeof(guid) - 1);
    PHP_SHA1Final(digest, &context);

    zend_string *const encoded = php_base64_encode(digest, sizeof(digest));
    if (encoded == NULL || ZSTR_LEN(encoded) != WSCLIENT_ACCEPT_LENGTH) {
        if (encoded != NULL) {
            zend_string_release(encoded);
        }
        return false;
    }

    memcpy(out, ZSTR_VAL(encoded), WSCLIENT_ACCEPT_LENGTH);
    zend_string_release(encoded);
    return true;
}
