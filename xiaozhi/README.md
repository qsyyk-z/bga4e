# 灏忔櫤 + 4.3C 楹诲皢鍔╂墜鎺ュ叆

鎶?BGA4E 鍚庣浣滀负鈥滈夯灏嗙墝灞€宸ュ叿鈥濇寕鍒板皬鏅虹殑 MCP 涓婏紝骞跺湪 **ESP32-S3-Touch-LCD-4.3C锛?00脳480锛?* 涓婄敤 LVGL 鏄剧ず鐗屽眬杈呭姪鐣岄潰銆?
閫傜敤宸ョ▼锛?
```text
D:\M08\AiXiaoZhi_Hiwonder_Sensor
```

- xiaoZhi `PROJECT_VER 1.8.9`
- `main/idf_component.yml` 瑕佹眰 ESP-IDF >= 5.4.0
- 宸茬敤 **ESP-IDF 5.4.4** 缂栬瘧閫氳繃

## 1. 鍔熻兘

```text
鐢ㄦ埛璇磋瘽
  鈫?灏忔櫤鍞ら啋 + ASR
  鈫?灏忔櫤浜戠 LLM
      鈹溾攢 璋冪敤 MCP 宸ュ叿锛歡et_state / get_advice / set_hand / record_discard
      鈫?HTTP
  鈫?BGA4E FastAPI 鍚庣锛圫QLite + 鍚戝惉/杩涘紶锛?  鈫?宸ュ叿杩斿洖涓枃
  鈫?灏忔櫤 LLM 缁勭粐璇█ + TTS 鎾姤
```

4.3C 灞忓箷鏄剧ず锛?
- 褰撳墠鐜╁銆佸贰鐩€佺墝澧?- 鍥涘寮冪墝
- 鏈€杩戝嚭鐗?- 鎴戠殑 13/14 寮犳墜鐗?- 绛栫暐寤鸿锛氭墦鍝紶銆佺悊鐢便€佸悜鍚?杩涘紶
- 椤堕儴鎸夐挳锛氬埛鏂?/ 寤鸿 / 璇煶
- BOOT 閿垨鈥滆闊斥€?鈥滅墝灞€鈥濇寜閽垏鎹㈢墝妗屽拰瀵硅瘽椤?
## 2. 鏂囦欢娓呭崟

闇€瑕佹嫹璐濆埌灏忔櫤宸ョ▼锛?
```text
main/boards/common/mahjong_mcp.cc
main/boards/common/mahjong_mcp.h
main/boards/common/mahjong_ui.cc
main/boards/common/mahjong_ui.h

main/boards/waveshare-s3-touch-lcd-4.3c/config.h
main/boards/waveshare-s3-touch-lcd-4.3c/config.json
main/boards/waveshare-s3-touch-lcd-4.3c/esp32_s3_touch_lcd_4.3c.cc

components/custom_io_expander_ch32v003/
```

`main/CMakeLists.txt` 宸茬粡鐢?`file(GLOB boards/common/*.cc)` 鏀堕泦 common 婧愭枃浠讹紝鎵€浠?`mahjong_mcp.cc` 鍜?`mahjong_ui.cc` **涓嶉渶瑕佹墜鍔ㄥ姞 CMake**銆?
## 3. 鏉跨骇閰嶇疆

### 3.1 Kconfig

`main/Kconfig.projbuild` 鐨?board choice 涓姞鍏ワ細

```kconfig
config BOARD_TYPE_ESP32S3_Touch_LCD_4_3C
    bool "Waveshare ESP32-S3-Touch-LCD-4.3C"
    depends on IDF_TARGET_ESP32S3
```

骞跺湪鏂囦欢鏈熬鍔犲叆锛?
```kconfig
menu "BGA4E Mahjong Assistant"
    config BGA4E_BACKEND_URL
        string "BGA4E backend base URL"
        default "http://192.168.1.100:8000"

    config BGA4E_PLAYER
        int "BGA4E player index (0-3)"
        range 0 3
        default 0
endmenu
```

### 3.2 CMake

`main/CMakeLists.txt` 鐨?board 鏄犲皠涓姞鍏ワ細

```cmake
elseif(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C)
    set(BOARD_TYPE "waveshare-s3-touch-lcd-4.3c")
```

### 3.3 鍒嗗尯琛?
4.3C 鍥轰欢鍖呭惈璇煶妯″瀷鍜?LVGL锛?M app 鍒嗗尯鍙墿 2%锛?
```text
partitions/v1/16m.csv
```

鏀规垚鍙?7M锛?
```csv
ota_0,    app,  ota_0,   0x100000,  7M,
ota_1,    app,  ota_1,   0x800000,  7M,
```

褰撳墠瀹炴祴锛?
```text
AiXiaoZhi_Hiwonder.bin = 0x5dcdf0
app partition 0x700000
16% free
```

### 3.4 缂栬瘧閰嶇疆

```bash
idf.py menuconfig
# Board Type -> Waveshare ESP32-S3-Touch-LCD-4.3C
# BGA4E Mahjong Assistant -> 鍚庣 IP 鍜岀帺瀹跺骇浣?idf.py build
```

褰撳墠 `sdkconfig` 宸插垏鎹负锛?
```text
CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_4_3C=y
CONFIG_BGA4E_BACKEND_URL="http://192.168.1.100:8000"
CONFIG_BGA4E_PLAYER=0
```

鐑у綍锛?
```bash
idf.py -p COMx flash monitor
```

## 4. 鍚姩鍚庣

```powershell
cd M08\bga4e\backend
.\.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

缃戦〉閲屽厛鏂板缓涓€灞€锛屾垨鐐光€滅敓鎴愭紨绀虹墝灞€鈥濄€?
纭繚锛?
- 灏忔櫤鍜?PC 鍦ㄥ悓涓€灞€鍩熺綉锛?- Windows 闃茬伀澧欐斁琛?8000锛?- `BGA4E_BACKEND_URL` 鐢?PC 灞€鍩熺綉 IP锛屼笉瑕佺敤 `127.0.0.1`銆?
## 5. MCP 宸ュ叿

| 宸ュ叿 | 浣滅敤 | 鐢ㄦ埛璇存硶 |
| --- | --- | --- |
| `self.mahjong.get_state` | 鑾峰彇鐗屽眬銆佹墜鐗屻€佸純鐗屻€佺墝澧?| 鈥滅幇鍦ㄧ墝灞€鎬庝箞鏍凤紵鈥?|
| `self.mahjong.get_advice` | 鎵撳摢寮犮€佺悊鐢便€佸悜鍚?| 鈥滄垜璇ユ墦鍝紶锛熲€?|
| `self.mahjong.set_hand` | 褰曞叆鎵嬬墝 | 鈥滄垜鐨勬墜鐗屾槸鈥︹€︹€?|
| `self.mahjong.record_discard` | 璁板綍鎵撳嚭鐨勭墝 | 鈥滆涓€涓嬶紝鎴戞墦涓変竾鈥?|

`get_advice` 鍥哄畾 `use_llm=false`锛氬悗绔畻鐗岋紝灏忔櫤 LLM 璐熻矗璇磋瘽銆?
## 6. LVGL 鐣岄潰

`mahjong_ui.cc` 鍒涘缓 800脳480 鐨勭嫭绔?LVGL screen锛?
```text
鈹屸攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?BGA4E 楹诲皢鍔╂墜   宸茶繛鎺?     [鍒锋柊][寤鸿][璇煶] 鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?褰撳墠锛氳タ瀹?路 绗?8 宸?路 鐗屽 42 鈹?绛栫暐寤鸿       鈹?鈹?                             鈹?寤鸿鎵?涓変竾     鈹?鈹?鍚勫寮冪墝锛?                   鈹?鐞嗙敱鈥︹€?        鈹?鈹?涓滃锛氣€? 鍗楀锛氣€?             鈹?鍚戝惉 1 路 8 寮?  鈹?鈹?瑗垮锛氣€? 鍖楀锛氣€?             鈹?              鈹?鈹?鏈€杩戝嚭鐗岋細浜斾竾                 鈹?              鈹?鈹溾攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹粹攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?鈹?鎵嬬墝  [涓€涓嘳[浜屼竾][涓変竾] 鈥?                   鈹?鈹斺攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹?```

- 姣?1.5 绉掓鏌ヤ竴娆″埛鏂?寤鸿璇锋眰锛?- 鐐瑰嚮鈥滃埛鏂扳€濇媺鍙?`/api/current/summary`锛?- 鐐瑰嚮鈥滃缓璁€濇媺鍙?`/api/current/advice`锛?- BOOT 閿€佸睆骞曗€滆闊斥€?鈥滅墝灞€鈥濇寜閽兘鑳藉垏鎹㈣亰澶╅〉鍜岀墝灞€椤碉紱
- 涓枃浣跨敤 `font_puhui_20_4`/`font_puhui_30_4`銆?
## 7. 娉ㄦ剰

- 4.3C 鐨?RGB 灞忋€丟T911 瑙︽懜銆丆H32V003 IO 鎵╁睍銆丒S7210/ES8311 閮芥寜寰洩瀹樻柟鏉跨骇浠ｇ爜閫傞厤锛涘鏋滃疄闄呮帓绾?鐗堟湰涓嶅悓锛屽厛鏍稿鍘熺悊鍥俱€?- 褰撳墠鍙仛浜?**缂栬瘧楠岃瘉**锛岃繕娌℃湁鍦ㄧ湡瀹?4.3C 鏉垮瓙涓婄儳褰曢獙璇佽Е鎽告柟鍚戙€佽儗鍏夈€侀煶棰戙€?- 鎽勫儚澶磋瘑鍒粛鐢?BGA4E 鐨?CardNet 鎴栨憚鍍忓ご鑺傜偣璐熻矗锛涜繖涓€鐗堣礋璐ｂ€滃ぇ灞?+ 璇煶 + LLM 寤鸿鈥濄€?- 鍚庣 `current` 鎺ュ彛浠?`updated_at` 鏈€鏂颁竴灞€涓哄噯锛涘灞€鍚屾椂寮€鏃跺缓璁厛鍥哄畾涓€灞€銆?
