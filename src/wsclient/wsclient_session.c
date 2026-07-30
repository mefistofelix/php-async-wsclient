/*
  +----------------------------------------------------------------------+
  | TrueAsync WebSocket Client                                           |
  +----------------------------------------------------------------------+
*/

#include "php.h"
#include "ext/random/php_random_csprng.h"
#include "main/php_streams.h"

#include "wsclient/wsclient_session.h"

static ssize_t wsclient_recv_callback(wslay_event_context_ptr context, uint8_t *buffer,
                                      size_t length, int flags, void *user_data)
{
    (void) flags;
    wsclient_session_t *const session = user_data;
    const size_t available = session->input_len - session->input_pos;
    if (available == 0) {
        wslay_event_set_error(context, WSLAY_ERR_WOULDBLOCK);
        return -1;
    }
    const size_t count = available < length ? available : length;
    memcpy(buffer, session->input + session->input_pos, count);
    session->input_pos += count;
    return (ssize_t) count;
}

static ssize_t wsclient_send_callback(wslay_event_context_ptr context, const uint8_t *buffer,
                                      size_t length, int flags, void *user_data)
{
    (void) flags;
    wsclient_session_t *const session = user_data;
    const ssize_t result = php_stream_write(session->stream, (const char *) buffer, length);
    if (result <= 0) {
        wslay_event_set_error(context, WSLAY_ERR_CALLBACK_FAILURE);
    }
    return result;
}

static int wsclient_genmask_callback(wslay_event_context_ptr context, uint8_t *buffer,
                                     size_t length, void *user_data)
{
    (void) context;
    (void) user_data;
    return php_random_bytes_throw(buffer, length) == SUCCESS ? 0 : -1;
}

static void wsclient_on_message(wslay_event_context_ptr context,
                                const struct wslay_event_on_msg_recv_arg *message,
                                void *user_data)
{
    (void) context;
    wsclient_session_t *const session = user_data;
    wsclient_message_node_t *const node = ecalloc(1, sizeof(*node));
    node->binary = message->opcode == WSLAY_BINARY_FRAME;
    node->data = zend_string_init((const char *) message->msg, message->msg_length, 0);
    if (session->messages_tail != NULL) {
        session->messages_tail->next = node;
    } else {
        session->messages_head = node;
    }
    session->messages_tail = node;
}

wsclient_session_t *wsclient_session_create(php_stream *const stream)
{
    if (stream == NULL) return NULL;
    wsclient_session_t *const session = ecalloc(1, sizeof(*session));
    session->stream = stream;
    const struct wslay_event_callbacks callbacks = {
        .recv_callback = wsclient_recv_callback,
        .send_callback = wsclient_send_callback,
        .genmask_callback = wsclient_genmask_callback,
        .on_msg_recv_callback = wsclient_on_message,
    };
    if (wslay_event_context_client_init(&session->context, &callbacks, session) != 0) {
        efree(session);
        return NULL;
    }
    wslay_event_config_set_max_recv_msg_length(session->context, 1024 * 1024);
    return session;
}

void wsclient_session_destroy(wsclient_session_t *const session)
{
    if (session == NULL) return;
    if (session->context != NULL) wslay_event_context_free(session->context);
    while (session->messages_head != NULL) {
        wsclient_message_node_t *const node = session->messages_head;
        session->messages_head = node->next;
        zend_string_release(node->data);
        efree(node);
    }
    efree(session);
}

int wsclient_session_feed(wsclient_session_t *const session, const uint8_t *data, size_t len)
{
    session->input = data;
    session->input_len = len;
    session->input_pos = 0;
    const int result = wslay_event_recv(session->context);
    session->input = NULL;
    session->input_len = 0;
    session->input_pos = 0;
    return result;
}

wsclient_message_node_t *wsclient_session_pop(wsclient_session_t *const session)
{
    wsclient_message_node_t *const node = session->messages_head;
    if (node != NULL) {
        session->messages_head = node->next;
        if (session->messages_head == NULL) session->messages_tail = NULL;
        node->next = NULL;
    }
    return node;
}

int wsclient_session_send(wsclient_session_t *const session, const uint8_t opcode,
                          const char *const data, const size_t len)
{
    const struct wslay_event_msg message = {
        .opcode = opcode,
        .msg = (const uint8_t *) data,
        .msg_length = len,
    };
    if (wslay_event_queue_msg(session->context, &message) != 0) {
        return -1;
    }
    return wslay_event_send(session->context);
}
