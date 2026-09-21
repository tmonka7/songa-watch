#include "watch_svc/svc_i18n.h"
#include "watch_svc/svc_event.h"

#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "i18n";

static watch_lang_t s_lang = WATCH_LANG_EN;

/* Designated initialisers, so the tables cannot silently drift out of step
 * with the enum: a new STR_* that nobody fills in is NULL, and i18n_in()
 * falls back to English rather than drawing a blank label.
 * tools/check_consistency.ps1 fails the build check if any id is missing
 * from either table. */

static const char *const s_en[STR_COUNT] = {
    [STR_BACK]                = "Back",
    [STR_OK]                  = "OK",
    [STR_CANCEL]              = "Cancel",
    [STR_ON]                  = "On",
    [STR_OFF]                 = "Off",
    [STR_YES]                 = "Yes",
    [STR_NO]                  = "No",
    [STR_CONNECTED]           = "Connected",
    [STR_NOT_CONNECTED]       = "Not Connected",
    [STR_CONNECTING]          = "Connecting",
    [STR_DISCONNECT]          = "Disconnect",
    [STR_CONNECT]             = "Connect",
    [STR_SCANNING]            = "Scanning",
    [STR_NONE]                = "None",
    [STR_UNKNOWN]             = "Unknown",
    [STR_LOADING]             = "Loading",
    [STR_ERROR]               = "Error",
    [STR_RETRY]               = "Retry",
    [STR_SAVE]                = "Save",
    [STR_CLOSE]               = "Close",
    [STR_START]               = "Start",
    [STR_STOP]                = "Stop",
    [STR_ENABLED]             = "Enabled",
    [STR_DISABLED]            = "Disabled",
    [STR_NOT_AVAILABLE]       = "Not available",

    [STR_STEPS]               = "Steps",
    [STR_TEMPERATURE]         = "Temperature",
    [STR_BATTERY]             = "Battery",
    [STR_ACTIVITY]            = "Activity",
    [STR_GOAL]                = "Goal",
    [STR_TODAY]               = "Today",

    [STR_APPS]                = "Applications",
    [STR_AI_VISION]           = "AI Vision",
    [STR_SENSOR]              = "Sensor",
    [STR_CAMERA]              = "Camera",
    [STR_AUDIO]               = "Audio",
    [STR_WIFI]                = "Wi-Fi",
    [STR_BLUETOOTH]           = "Bluetooth",
    [STR_SD_CARD]             = "SD Card",
    [STR_SETTINGS]            = "Settings",
    [STR_MORE]                = "More",

    [STR_MOTION_SENSOR]       = "Motion Sensor",
    [STR_ACCELEROMETER]       = "Accelerometer",
    [STR_GYROSCOPE]           = "Gyroscope",
    [STR_MOTION_GRAPH]        = "Motion Graph",
    [STR_ACCEL]               = "Accel",
    [STR_GYRO]                = "Gyro",
    [STR_SENSOR_MISSING]      = "No motion sensor",

    [STR_VOLTAGE]             = "Voltage",
    [STR_CURRENT]             = "Current",
    [STR_POWER]               = "Power",
    [STR_CHARGING]            = "Charging",
    [STR_POWER_SAVING]        = "Power Saving",
    [STR_SYSTEM_VOLTAGE]      = "System",
    [STR_CHARGE_STATE]        = "Charge State",
    [STR_NO_BATTERY]          = "No battery",
    [STR_USB_POWER]           = "USB Power",

    [STR_SIGNAL]              = "Signal",
    [STR_IP_ADDRESS]          = "IP",
    [STR_SCAN_NETWORKS]       = "Scan Networks",
    [STR_AVAILABLE_NETWORKS]  = "Available Networks",
    [STR_ADD_NETWORK]         = "Add Network",
    [STR_PASSWORD]            = "Password",
    [STR_NETWORK_SETTINGS]    = "Network Settings",
    [STR_FORGET]              = "Forget",
    [STR_NO_NETWORKS]         = "No networks found",

    [STR_DEVICES]             = "Devices",
    [STR_SCAN]                = "Scan",
    [STR_NO_DEVICES]          = "No devices found",

    [STR_RECORDING]           = "Recording",
    [STR_MIC_LEVEL]           = "Mic Level",
    [STR_VOLUME]              = "Volume",
    [STR_MIC_GAIN]            = "Mic Gain",
    [STR_PLAY]                = "Play",
    [STR_RECORD]              = "Record",

    [STR_USED]                = "Used",
    [STR_FREE]                = "Free",
    [STR_TOTAL]               = "Total",
    [STR_IMAGES]              = "Images",
    [STR_VIDEOS]              = "Videos",
    [STR_OTHERS]              = "Others",
    [STR_BROWSE_FILES]        = "Browse Files",
    [STR_NO_CARD]             = "No card inserted",
    [STR_STORAGE]             = "Storage",

    [STR_PHOTO]               = "Photo",
    [STR_VIDEO]               = "Video",
    [STR_CAPTURE]             = "Capture",
    [STR_NO_CAMERA]           = "No camera",
    [STR_CAMERA_HINT]         = "Connect an Arducam Mega",
    [STR_OBJECTS]             = "Objects",
    [STR_DETECTION_DETAIL]    = "Detection Details",
    [STR_CONFIDENCE]          = "Confidence",
    [STR_NO_DETECTIONS]       = "Nothing detected",
    [STR_OBJECT]              = "Object",

    [STR_DISPLAY]             = "Display",
    [STR_SENSORS]             = "Sensors",
    [STR_SYSTEM]              = "System",
    [STR_ABOUT]               = "About",
    [STR_OTA_UPDATE]          = "OTA Update",
    [STR_POWER_OFF]           = "Power Off",
    [STR_BRIGHTNESS]          = "Brightness",
    [STR_AUTO_TIMEOUT]        = "Auto Timeout",
    [STR_ALWAYS_ON]           = "Always On Display",
    [STR_WATCH_FACE]          = "Watch Face",
    [STR_LANGUAGE]            = "Language",
    [STR_TIME]                = "Time",
    [STR_FACTORY_RESET]       = "Factory Reset",

    [STR_VERSION]             = "Version",
    [STR_BUILD]               = "Build",
    [STR_CHIP]                = "Chip",
    [STR_FLASH]               = "Flash",
    [STR_RAM]                 = "RAM",
    [STR_FREE_HEAP]           = "Free heap",
    [STR_UPTIME]              = "Uptime",

    [STR_NEW_VERSION]         = "New Version",
    [STR_UPDATE_AVAILABLE]    = "Update available",
    [STR_DOWNLOAD]            = "Download",
    [STR_LATER]               = "Later",
    [STR_CHECKING]            = "Checking",
    [STR_UP_TO_DATE]          = "Up to date",
    [STR_INSTALLING]          = "Installing",
    [STR_REBOOTING]           = "Rebooting",
    [STR_UPDATE_FAILED]       = "Update failed",
    [STR_NEEDS_WIFI]          = "Wi-Fi required",

    [STR_SLIDE_TO_POWER_OFF]  = "Slide to power off",
    [STR_RESTART]             = "Restart",

    [STR_OBD2]                = "OBD2",
    [STR_OBD2_TITLE]          = "Car Diagnostics",
    [STR_LIVE_DATA]           = "Live Data",
    [STR_TROUBLE_CODES]       = "Trouble Codes",
    [STR_FREEZE_FRAME]        = "Freeze Frame",
    [STR_VEHICLE_STATUS]      = "Vehicle Status",
    [STR_ADAPTER]             = "Adapter",
    [STR_NO_ADAPTER]          = "No adapter",
    [STR_SEARCHING_ADAPTER]   = "Searching adapter",
    [STR_ENGINE_LOAD]         = "Engine Load",
    [STR_COOLANT_TEMP]        = "Coolant Temp",
    [STR_RPM]                 = "RPM",
    [STR_VEHICLE_SPEED]       = "Vehicle Speed",
    [STR_INTAKE_TEMP]         = "Intake Temp",
    [STR_FUEL_LEVEL]          = "Fuel Level",
    [STR_THROTTLE]            = "Throttle",
    [STR_CLEAR_CODES]         = "Clear Codes",
    [STR_NO_CODES]            = "No trouble codes",
    [STR_STORED]              = "Stored",
    [STR_PENDING]             = "Pending",
    [STR_ENGINE]              = "Engine",
    [STR_ABS]                 = "ABS",
    [STR_SRS]                 = "SRS",
    [STR_TRANSMISSION]        = "Transmission",
    [STR_STATUS_OK]           = "OK",
    [STR_STATUS_WARNING]      = "Warning",
    [STR_STATUS_FAULT]        = "Fault",
    [STR_NO_FREEZE_FRAME]     = "No freeze frame data",
    [STR_DRIVE_SAFER]         = "Drive Safer",

    [STR_TIME_SETTINGS]       = "Time Settings",
    [STR_DATE]                = "Date",
    [STR_24_HOUR]             = "24-hour clock",
    [STR_TIME_ZONE]           = "Time Zone",
    [STR_SYNC_NETWORK]        = "Sync with network",
    [STR_SET_MANUALLY]        = "Set manually",
    [STR_HOUR]                = "Hour",
    [STR_MINUTE]              = "Minute",
    [STR_YEAR]                = "Year",
    [STR_MONTH]               = "Month",
    [STR_DAY]                 = "Day",

    [STR_ENGLISH]             = "English",
    [STR_JAPANESE]            = "Japanese",
};

/* Japanese. Restricted to lv_font_source_han_sans_sc_16_cjk's glyph set -
 * see tools/check_i18n_font.ps1. Where the font has no kanji for a term
 * (voltage, fuel, device, warning) the katakana or a synonym is used; those
 * substitutions are listed in docs/i18n.md. */
static const char *const s_jp[STR_COUNT] = {
    [STR_BACK]                = "戻る",
    [STR_OK]                  = "OK",
    [STR_CANCEL]              = "取消",
    [STR_ON]                  = "オン",
    [STR_OFF]                 = "オフ",
    [STR_YES]                 = "はい",
    [STR_NO]                  = "いいえ",
    [STR_CONNECTED]           = "接続済",
    [STR_NOT_CONNECTED]       = "未接続",
    [STR_CONNECTING]          = "接続中",
    [STR_DISCONNECT]          = "切断",
    [STR_CONNECT]             = "接続",
    [STR_SCANNING]            = "スキャン中",
    [STR_NONE]                = "なし",
    [STR_UNKNOWN]             = "不明",
    [STR_LOADING]             = "読込中",
    [STR_ERROR]               = "エラー",
    [STR_RETRY]               = "再試行",
    [STR_SAVE]                = "保存",
    [STR_CLOSE]               = "閉じる",
    [STR_START]               = "開始",
    [STR_STOP]                = "停止",
    [STR_ENABLED]             = "オン",
    [STR_DISABLED]            = "オフ",
    [STR_NOT_AVAILABLE]       = "利用不可",

    [STR_STEPS]               = "歩数",
    [STR_TEMPERATURE]         = "温度",
    [STR_BATTERY]             = "電池",
    [STR_ACTIVITY]            = "活動",
    [STR_GOAL]                = "目標",
    [STR_TODAY]               = "今日",

    [STR_APPS]                = "アプリ",
    [STR_AI_VISION]           = "AI映像",
    [STR_SENSOR]              = "センサー",
    [STR_CAMERA]              = "カメラ",
    [STR_AUDIO]               = "音声",
    [STR_WIFI]                = "Wi-Fi",
    [STR_BLUETOOTH]           = "Bluetooth",
    [STR_SD_CARD]             = "SDカード",
    [STR_SETTINGS]            = "設定",
    [STR_MORE]                = "その他",

    [STR_MOTION_SENSOR]       = "動作センサー",
    [STR_ACCELEROMETER]       = "加速度計",
    [STR_GYROSCOPE]           = "ジャイロ",
    [STR_MOTION_GRAPH]        = "動作グラフ",
    [STR_ACCEL]               = "加速度",
    [STR_GYRO]                = "ジャイロ",
    [STR_SENSOR_MISSING]      = "センサーなし",

    [STR_VOLTAGE]             = "電位",
    [STR_CURRENT]             = "電流",
    [STR_POWER]               = "電力",
    [STR_CHARGING]            = "充電中",
    [STR_POWER_SAVING]        = "節電",
    [STR_SYSTEM_VOLTAGE]      = "システム",
    [STR_CHARGE_STATE]        = "充電状態",
    [STR_NO_BATTERY]          = "電池なし",
    [STR_USB_POWER]           = "USB電源",

    [STR_SIGNAL]              = "信号",
    [STR_IP_ADDRESS]          = "IPアドレス",
    [STR_SCAN_NETWORKS]       = "ネットワーク検出",
    [STR_AVAILABLE_NETWORKS]  = "利用できるネットワーク",
    [STR_ADD_NETWORK]         = "ネットワーク追加",
    [STR_PASSWORD]            = "パスワード",
    [STR_NETWORK_SETTINGS]    = "ネットワーク設定",
    [STR_FORGET]              = "削除",
    [STR_NO_NETWORKS]         = "ネットワークなし",

    [STR_DEVICES]             = "デバイス",
    [STR_SCAN]                = "スキャン",
    [STR_NO_DEVICES]          = "デバイスなし",

    [STR_RECORDING]           = "録音中",
    [STR_MIC_LEVEL]           = "マイク入力",
    [STR_VOLUME]              = "音量",
    [STR_MIC_GAIN]            = "マイク感度",
    [STR_PLAY]                = "再生",
    [STR_RECORD]              = "録音",

    [STR_USED]                = "使用",
    [STR_FREE]                = "空き",
    [STR_TOTAL]               = "合計",
    [STR_IMAGES]              = "画像",
    [STR_VIDEOS]              = "動画",
    [STR_OTHERS]              = "その他",
    [STR_BROWSE_FILES]        = "ファイル表示",
    [STR_NO_CARD]             = "カードなし",
    [STR_STORAGE]             = "ストレージ",

    [STR_PHOTO]               = "写真",
    [STR_VIDEO]               = "動画",
    [STR_CAPTURE]             = "記録",
    [STR_NO_CAMERA]           = "カメラなし",
    [STR_CAMERA_HINT]         = "カメラを接続してください",
    [STR_OBJECTS]             = "物体",
    [STR_DETECTION_DETAIL]    = "検出の詳細",
    [STR_CONFIDENCE]          = "確度",
    [STR_NO_DETECTIONS]       = "検出なし",
    [STR_OBJECT]              = "物体",

    [STR_DISPLAY]             = "画面",
    [STR_SENSORS]             = "センサー",
    [STR_SYSTEM]              = "システム",
    [STR_ABOUT]               = "情報",
    [STR_OTA_UPDATE]          = "更新",
    [STR_POWER_OFF]           = "電源を切る",
    [STR_BRIGHTNESS]          = "明るさ",
    [STR_AUTO_TIMEOUT]        = "自動オフ",
    [STR_ALWAYS_ON]           = "常時表示",
    [STR_WATCH_FACE]          = "時計表示",
    [STR_LANGUAGE]            = "言語",
    [STR_TIME]                = "時刻",
    [STR_FACTORY_RESET]       = "初期化",

    [STR_VERSION]             = "バージョン",
    [STR_BUILD]               = "ビルド",
    [STR_CHIP]                = "チップ",
    [STR_FLASH]               = "フラッシュ",
    [STR_RAM]                 = "RAM",
    [STR_FREE_HEAP]           = "空きメモリ",
    [STR_UPTIME]              = "起動時間",

    [STR_NEW_VERSION]         = "新バージョン",
    [STR_UPDATE_AVAILABLE]    = "更新があります",
    [STR_DOWNLOAD]            = "ダウンロード",
    [STR_LATER]               = "あとで",
    [STR_CHECKING]            = "確認中",
    [STR_UP_TO_DATE]          = "最新です",
    [STR_INSTALLING]          = "インストール中",
    [STR_REBOOTING]           = "再起動中",
    [STR_UPDATE_FAILED]       = "更新に失敗",
    [STR_NEEDS_WIFI]          = "Wi-Fiが必要です",

    [STR_SLIDE_TO_POWER_OFF]  = "スライドで電源を切る",
    [STR_RESTART]             = "再起動",

    [STR_OBD2]                = "OBD2",
    [STR_OBD2_TITLE]          = "車両チェック",
    [STR_LIVE_DATA]           = "走行データ",
    [STR_TROUBLE_CODES]       = "故障コード",
    [STR_FREEZE_FRAME]        = "フリーズフレーム",
    [STR_VEHICLE_STATUS]      = "車両状態",
    [STR_ADAPTER]             = "アダプター",
    [STR_NO_ADAPTER]          = "アダプターなし",
    [STR_SEARCHING_ADAPTER]   = "アダプター検出中",
    [STR_ENGINE_LOAD]         = "エンジン負荷",
    [STR_COOLANT_TEMP]        = "水温",
    [STR_RPM]                 = "回転数",
    [STR_VEHICLE_SPEED]       = "車速",
    [STR_INTAKE_TEMP]         = "インテーク温度",
    [STR_FUEL_LEVEL]          = "ガソリン残量",
    [STR_THROTTLE]            = "スロットル",
    [STR_CLEAR_CODES]         = "コード消去",
    [STR_NO_CODES]            = "コードなし",
    [STR_STORED]              = "記録済",
    [STR_PENDING]             = "保留",
    [STR_ENGINE]              = "エンジン",
    [STR_ABS]                 = "ABS",
    [STR_SRS]                 = "SRS",
    [STR_TRANSMISSION]        = "変速機",
    [STR_STATUS_OK]           = "正常",
    [STR_STATUS_WARNING]      = "注意",
    [STR_STATUS_FAULT]        = "異常",
    [STR_NO_FREEZE_FRAME]     = "データなし",
    [STR_DRIVE_SAFER]         = "安全運転",

    [STR_TIME_SETTINGS]       = "時刻設定",
    [STR_DATE]                = "日付",
    [STR_24_HOUR]             = "24時間表示",
    [STR_TIME_ZONE]           = "時間帯",
    [STR_SYNC_NETWORK]        = "ネットワーク同期",
    [STR_SET_MANUALLY]        = "手動設定",
    [STR_HOUR]                = "時",
    [STR_MINUTE]              = "分",
    [STR_YEAR]                = "年",
    [STR_MONTH]               = "月",
    [STR_DAY]                 = "日",

    [STR_ENGLISH]             = "English",
    [STR_JAPANESE]            = "日本語",
};

static const char *const *const s_tables[WATCH_LANG_COUNT] = {
    [WATCH_LANG_EN] = s_en,
    [WATCH_LANG_JP] = s_jp,
};

const char *i18n_in(i18n_id_t id, watch_lang_t lang)
{
    if (id < 0 || id >= STR_COUNT) {
        return "?";
    }
    if (lang < 0 || lang >= WATCH_LANG_COUNT) {
        lang = WATCH_LANG_EN;
    }
    const char *s = s_tables[lang][id];
    if (s == NULL) {
        /* Untranslated entry: fall back to English rather than showing a
         * blank label. */
        s = s_en[id];
    }
    return (s != NULL) ? s : "?";
}

const char *i18n(i18n_id_t id)
{
    return i18n_in(id, s_lang);
}

void i18n_set_lang(watch_lang_t lang)
{
    if (lang < 0 || lang >= WATCH_LANG_COUNT || lang == s_lang) {
        return;
    }
    s_lang = lang;
    ESP_LOGI(TAG, "language -> %s", (lang == WATCH_LANG_JP) ? "ja" : "en");
    svc_event_post(WATCH_EV_LANG_CHANGED, &lang, sizeof(lang));
}

watch_lang_t i18n_get_lang(void)
{
    return s_lang;
}

bool i18n_needs_cjk_font(void)
{
    return s_lang == WATCH_LANG_JP;
}
