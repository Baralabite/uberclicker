#pragma once
#include "Arduino.h"
#include "esp_http_client.h"
#include "esp_app_desc.h"
using esp_partition_type_t=uint8_t;
using esp_partition_subtype_t=uint8_t;
struct esp_partition_t { uint8_t type,subtype;uint32_t address,size; };
inline esp_partition_t parts[]={{1,2,0x9000,0x5000},{1,0,0xe000,0x2000},{0,16,0x10000,0x1f0000},{0,17,0x200000,0x1f0000},{1,3,0x3f0000,0x10000}};
using esp_ota_handle_t=unsigned;
using esp_ota_img_states_t=int;
constexpr int ESP_OTA_IMG_PENDING_VERIFY=1;
namespace host {
inline int begins=0,aborts=0,ends=0,boots=0,confirmed=0,rolledBack=0,otaState=0;
inline bool badImage=false;
inline std::string written;
}
inline const esp_partition_t *esp_partition_find_first(uint8_t type,uint8_t subtype,const char *) { for(auto &p:parts)if(p.type==type && p.subtype==subtype)return &p;return nullptr; }
inline const esp_partition_t *esp_ota_get_running_partition(){return parts+2;}
inline const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *){return parts+3;}
inline int esp_ota_begin(const esp_partition_t *p,uint32_t size,esp_ota_handle_t *handle){assert(p==parts+3 && size<=p->size);++host::begins;*handle=1;host::written.clear();return ESP_OK;}
inline int esp_ota_write(esp_ota_handle_t,const void *data,size_t size){host::written.append((const char *)data,size);return ESP_OK;}
inline int esp_ota_abort(esp_ota_handle_t){++host::aborts;return ESP_OK;}
inline int esp_ota_end(esp_ota_handle_t){++host::ends;return host::badImage?-1:ESP_OK;}
inline int esp_ota_get_partition_description(const esp_partition_t *,esp_app_desc_t *desc){memcpy(desc->version,host::written.data()+48,32);memcpy(desc->project_name,host::written.data()+80,32);return ESP_OK;}
inline int esp_ota_set_boot_partition(const esp_partition_t *p){assert(p==parts+3);++host::boots;return ESP_OK;}
inline int esp_ota_get_state_partition(const esp_partition_t *,int *state){*state=host::otaState;return ESP_OK;}
inline int esp_ota_mark_app_valid_cancel_rollback(){++host::confirmed;return ESP_OK;}
inline int esp_ota_mark_app_invalid_rollback_and_reboot(){++host::rolledBack;return ESP_OK;}
