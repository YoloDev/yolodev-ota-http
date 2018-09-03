#include "mgos_yolodev_ota_http.h"
#include "common/cs_dbg.h"
#include "mgos_event.h"
#include "mgos_mongoose.h"
#include "mgos_yolodev_ota.h"
#include "mongoose.h"

bool is_http_request(const char *uri) {
  if (uri == NULL) {
    return false;
  }

  if (!(*(uri++) == 'h' && *(uri++) == 't' && *(uri++) == 't' &&
        *(uri++) == 'p')) {
    return false;
  }

  if (*uri == 's') {
    uri++;
  }

  if (!(*(uri++) == ':' && *(uri++) == '/' && *(uri++) == '/')) {
    return false;
  }

  return true;
}

struct update_request_data {
  uint32_t crc32;
  uint32_t crc32_data;
  struct update_context *context;
};

static void http_ev(struct mg_connection *cn, int ev, void *ev_data,
                    void *user_data) {
  if (user_data == NULL) {
    return;
  }

  struct update_request_data *data = (struct update_request_data *)user_data;

  switch (ev) {
    case MG_EV_HTTP_CHUNK: {
      struct http_message *msg = (struct http_message *)ev_data;
      cn->flags |= MG_F_DELETE_CHUNK;
      if (updater_process(data->context, msg->body.p, msg->body.len) < 0) {
        // Errored, maybe log?
        LOG(LL_WARN,
            ("Failed in update_process: %s", data->context->status_msg));
        cn->flags |= MG_F_CLOSE_IMMEDIATELY;
        updater_finish(data->context);
        free(data);
        return;
      }

      break;
    }

    case MG_EV_HTTP_REPLY: {
      struct http_message *msg = (struct http_message *)ev_data;
      cn->flags |= MG_F_CLOSE_IMMEDIATELY;
      bool finalize = true;
      if (msg->body.len > 0) {
        if (updater_process(data->context, msg->body.p, msg->body.len) < 0) {
          finalize = false;
        }
      }

      if (finalize) {
        updater_finalize(data->context);
      }

      free(data);
      break;
    }
  }
}

static void yolodev_ota_request_ev(int ev, void *ev_data, void *userdata) {
  struct yolodev_ota_request *data = (struct yolodev_ota_request *)ev_data;
  LOG(LL_DEBUG, ("New OTA request: %s", data->uri));
  if (!is_http_request(data->uri)) {
    LOG(LL_DEBUG, ("Request is not a HTTP request, skipping: %s", data->uri));
  }

  LOG(LL_INFO, ("New HTTP OTA request, starting download: %s", data->uri));
  struct update_request_data *request_data =
      malloc(sizeof(struct update_request_data));
  request_data->crc32 = data->crc32;
  request_data->crc32_data = 0;
  request_data->context = data->updater_context;

  // struct mg_connect_opts opts;
  // memset(&opts, 0, sizeof(opts));
  // opts.user_data = request_data;

  mg_connect_http(mgos_get_mgr(), http_ev, request_data, data->uri, NULL, NULL);

  (void)ev;
  (void)userdata;
}

bool mgos_yolodev_ota_http_init(void) {
  mgos_event_add_handler(YOLODEV_OTA_REQUEST, yolodev_ota_request_ev, NULL);

  return true;
}
