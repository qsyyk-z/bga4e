#include "camera_cards.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "cJSON.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define TAG "CameraCards"

// 4.3C TF slot: CLK=12, CMD=11, D0=13 (1-bit SD_MMC, no LCD pin conflict).
#if defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C)
#define CAM_SD_CLK GPIO_NUM_12
#define CAM_SD_CMD GPIO_NUM_11
#define CAM_SD_D0 GPIO_NUM_13
#else
#define CAM_SD_CLK GPIO_NUM_NC
#define CAM_SD_CMD GPIO_NUM_NC
#define CAM_SD_D0 GPIO_NUM_NC
#endif

namespace {
constexpr const char* kPrimaryPath = "/sdcard/bga4e/cards.json";
constexpr const char* kFallbackPath = "/sdcard/cards.json";
std::mutex g_mutex;
sdmmc_card_t* g_card = nullptr;
}  // namespace

CameraCards& CameraCards::Instance() {
    static CameraCards instance;
    return instance;
}

bool CameraCards::Mount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (mounted_) return true;
#if !defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C)
    ESP_LOGW(TAG, "this board has no TF card pins configured");
    return false;
#else
    if (attempted_) return false;
    attempted_ = true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = 20000;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = CAM_SD_CLK;
    slot.cmd = CAM_SD_CMD;
    slot.d0 = CAM_SD_D0;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 4;
    mount_config.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_config, &g_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TF mount failed: %s (insert a FAT32 card)", esp_err_to_name(err));
        g_card = nullptr;
        return false;
    }
    mounted_ = true;
    ESP_LOGI(TAG, "TF mounted, camera JSON path %s", kPrimaryPath);
    return true;
#endif
}

namespace {
void AddName(cJSON* item, std::vector<std::string>* tiles) {
    if (item == nullptr || tiles == nullptr) return;
    if (cJSON_IsString(item)) {
        if (item->valuestring != nullptr && item->valuestring[0] != '\0') {
            tiles->push_back(item->valuestring);
        }
        return;
    }
    if (!cJSON_IsObject(item)) return;
    cJSON* name = cJSON_GetObjectItemCaseSensitive(item, "name");
    if (name == nullptr) name = cJSON_GetObjectItemCaseSensitive(item, "tile");
    if (cJSON_IsString(name) && name->valuestring != nullptr && name->valuestring[0] != '\0') {
        tiles->push_back(name->valuestring);
    }
}
}  // namespace

bool CameraCards::LoadMahjongHand(std::vector<std::string>* tiles, std::string* error) {
    if (tiles == nullptr) return false;
    tiles->clear();
    if (!Mount()) {
        if (error) *error = "TF 卡未挂载，请插入存有 bga4e/cards.json 的 FAT32 卡。";
        return false;
    }

    FILE* fp = fopen(kPrimaryPath, "rb");
    if (fp == nullptr) fp = fopen(kFallbackPath, "rb");
    if (fp == nullptr) {
        if (error) *error = "卡里没有 bga4e/cards.json。";
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 16 * 1024) {
        fclose(fp);
        if (error) *error = "cards.json 为空或过大。";
        return false;
    }
    std::string text(static_cast<size_t>(size), '\0');
    size_t read = fread(&text[0], 1, text.size(), fp);
    fclose(fp);
    text.resize(read);

    cJSON* root = cJSON_Parse(text.c_str());
    if (root == nullptr) {
        if (error) *error = "cards.json 不是合法 JSON。";
        return false;
    }

    // {"hand":[...]} 或 {"cards":[...]}
    cJSON* hand = cJSON_GetObjectItemCaseSensitive(root, "hand");
    if (hand == nullptr) hand = cJSON_GetObjectItemCaseSensitive(root, "cards");
    if (cJSON_IsArray(hand)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, hand) { AddName(item, tiles); }
    } else if (cJSON_IsArray(root)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root) { AddName(item, tiles); }
    } else {
        AddName(root, tiles);
    }
    cJSON_Delete(root);

    if (tiles->size() < 13 || tiles->size() > 14) {
        if (error) *error = "读到的牌数是 " + std::to_string(tiles->size()) +
                            "，麻将手牌需要 13 或 14 张。";
        tiles->clear();
        return false;
    }
    ESP_LOGI(TAG, "loaded %d tiles from camera JSON", static_cast<int>(tiles->size()));
    return true;
}
