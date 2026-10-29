#include <esp_app_desc.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>
#define STRINGIFY_INNER(x) #x
#define STRINGIFY(x) STRINGIFY_INNER(x)

// Override IDF's weak descriptor so OTA validates the release's actual identity.
const __attribute__((section(".rodata_desc"))) esp_app_desc_t esp_app_desc = {
    .magic_word = ESP_APP_DESC_MAGIC_WORD,
    .version = UBERCLICKER_VERSION,
    .project_name = "UberClicker",
    .time = __TIME__,
    .date = __DATE__,
    .idf_ver = STRINGIFY(ESP_IDF_VERSION_MAJOR) "." STRINGIFY(ESP_IDF_VERSION_MINOR) "." STRINGIFY(ESP_IDF_VERSION_PATCH),
    .min_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MIN_FULL,
    .max_efuse_blk_rev_full = CONFIG_ESP_EFUSE_BLOCK_REV_MAX_FULL,
    .mmu_page_size = 31 - __builtin_clz(CONFIG_MMU_PAGE_SIZE),
};
_Static_assert(sizeof(UBERCLICKER_VERSION) <= sizeof(esp_app_desc.version), "Firmware version must fit the OTA descriptor");
