#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <functional>
#include <vector>
namespace host {
struct Stop {};
struct Queue { std::vector<uint8_t> bytes; bool full=false; };
inline uint32_t now=0;
inline bool failQueue=false, failTask=false;
inline unsigned allocations=0, deletions=0;
inline void (*task)(void *)=nullptr;
inline std::function<void()> onDelay, onDisplay;
inline std::vector<uint32_t> starts;
inline std::vector<uint8_t> firstPixels;
inline uint8_t bitmapFirst=0;
}
using QueueHandle_t=host::Queue *;
constexpr int pdPASS=1, OUTPUT=1, HIGH=1, HSPI=1, MSBFIRST=1, SPI_MODE0=0;
constexpr uint32_t portMAX_DELAY=0xffffffff;
inline uint32_t millis(){return host::now;}
inline uint32_t pdMS_TO_TICKS(uint32_t value){return value;}
inline void vTaskDelay(uint32_t value){host::now+=value;if(host::onDelay)host::onDelay();}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline QueueHandle_t xQueueCreate(unsigned length,size_t size){
 assert(length==1);if(host::failQueue)return nullptr;
 ++host::allocations;auto q=new host::Queue;q->bytes.resize(size);return q;
}
inline void vQueueDelete(QueueHandle_t q){++host::deletions;delete q;}
inline int xQueueOverwrite(QueueHandle_t q,const void *data){memcpy(q->bytes.data(),data,q->bytes.size());q->full=true;return pdPASS;}
inline int xQueueReceive(QueueHandle_t q,void *data,uint32_t timeout){
 if(!q->full){if(timeout==portMAX_DELAY)throw host::Stop{};return 0;}
 memcpy(data,q->bytes.data(),q->bytes.size());q->full=false;return pdPASS;
}
inline int xTaskCreate(void(*task)(void *),const char *,unsigned,void *,unsigned,void *){
 if (host::failTask)
   return 0;
 host::task = task;
 return pdPASS;
}
struct SPISettings{SPISettings(int,int,int){}};
struct SPIClass{explicit SPIClass(int){};void begin(int,int,int,int){}};
