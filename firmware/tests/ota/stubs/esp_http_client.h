#pragma once
#include "Arduino.h"
constexpr int ESP_OK=0, HTTP_TRANSPORT_OVER_SSL=1;
struct esp_http_client_config_t { const char *url; int (*crt_bundle_attach)(void *); int timeout_ms; bool disable_auto_redirect; int transport_type; };
namespace host {
inline std::string feed, image;
inline int requests=0, response=200, truncated=-1;
}
struct HttpStub { bool image; size_t at=0; };
using esp_http_client_handle_t=HttpStub *;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  assert(config->crt_bundle_attach && config->disable_auto_redirect && config->transport_type==HTTP_TRANSPORT_OVER_SSL);
  assert(std::string(config->url).find("https://uberclicker.com/flash/releases/")==0);
  ++host::requests; return new HttpStub{std::string(config->url).find("firmware.bin")!=std::string::npos};
}
inline int esp_http_client_cleanup(esp_http_client_handle_t h){delete h;return 0;}
inline int esp_http_client_open(esp_http_client_handle_t,int){return ESP_OK;}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){return h->image?host::image.size():host::feed.size();}
inline int esp_http_client_get_status_code(esp_http_client_handle_t){return host::response;}
inline int esp_http_client_read(esp_http_client_handle_t h,char *out,int size){
  auto &data=h->image?host::image:host::feed;
  if(h->image && host::truncated>=0 && h->at>=unsigned(host::truncated))return 0;
  size_t n=std::min({size_t(size),data.size()-h->at,size_t(1024)});
  memcpy(out,data.data()+h->at,n);h->at+=n;return int(n);
}
