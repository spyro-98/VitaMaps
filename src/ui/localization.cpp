#include "ui/localization.h"

#include <psp2/libime.h>
#include <psp2/registrymgr.h>
#include <psp2/system_param.h>

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace vitamaps {
namespace {

enum class Language {
    Italian,
    English,
    Japanese,
    Korean,
    ChineseSimplified,
    ChineseTraditional,
    Russian,
};

constexpr std::size_t kTextCount = static_cast<std::size_t>(UiText::Count);
int g_setting = 0;
Language g_language = Language::English;

constexpr const char *kItalian[] = {
    "Inserimento testo", "Testo troppo lungo: accorcialo e conferma",
    "Conferma dalla tastiera Vita oppure chiudi per annullare",
    "Errore tastiera: 0x%08X",
    "Modalità pin: sposta la mappa e premi X",
    "Lista piena: massimo 64 pin", "Punto %u salvato in %s",
    "Modifica solo in RAM; salvataggio 0x%08X", "Ricerca già in corso",
    "Cerca coordinate, località o indirizzo",
    "Coordinate trovate: conferma il pin con X",
    "Trovato in locale: %s · conferma con X",
    "Impossibile avviare la ricerca", "Ricerca località in corso…",
    "Nessun risultato trovato", "Ricerca non disponibile senza connessione",
    "Ricerca fallita: 0x%08X HTTP %ld", "Cache: %s · conferma con X",
    "Trovato: %s · conferma con X", "Nuova lista", "Lista creata",
    "Impossibile creare la lista", "Rinomina lista", "Lista rinominata",
    "La lista unica non può essere eliminata",
    "Premi SELECT di nuovo per eliminare la lista", "Lista eliminata",
    "Punto centrato; X aggiunge una nuova tappa", "Rinomina punto",
    "Punto rinominato", "Premi QUADRATO di nuovo per eliminare il punto",
    "Punto eliminato", "Modalità pin terminata",
    "Centro %.5f, %.5f  z%.2f t%d  rot %.0f°", "online", "offline",
    "salva", "pin", "termina", "liste", "cerca", "opzioni", "zoom",
    "PIN → %s | %u punti | %s", "Liste e percorsi", "%u pin · %s%s",
    " · ATTIVA", "apri", "nuova", "rinomina", "elimina", "mappa",
    "%u tappe · linea d'aria %s", "%u tappe · linea d'aria %s · area %s",
    "Indirizzi e indicazioni stradali: backend non presente",
    "Lista vuota: torna alla mappa e premi X per inserire pin", "centra",
    "riordina", "VitaMaps - Impostazioni", "Stile della mappa", "Lingua UI",
    "Comportamento HUD", "Mirino centrale", "Unità di misura",
    "Animazioni interfaccia", "Log persistente su memoria Vita",
    "Auto: 2,5 s", "Sempre visibile", "Visibile", "Nascosto", "Imperiale",
    "Metrico", "Ridotte", "Fluide", "Attivo", "Disattivato", "Salvato",
    "Errore: 0x%08X", "seleziona/cambia", "cambia", "indietro", "esci",
    "Preferiti", "Punto %u", "Cache mappe", "Lettura…", "%s · %u tile",
    "Premi X di nuovo per cancellare", "Cancellazione…", "Cache cancellata",
    "Errore cache: 0x%08X", "cancella", "mostra/nascondi",
    "chiudi percorso", "apri percorso", "chiuso",
    "Servono almeno 3 punti per chiudere il percorso", "inizio", "nord",
    "ruota",
};

constexpr const char *kEnglish[] = {
    "Text entry", "Text too long: shorten it and confirm",
    "Confirm with the Vita keyboard or close to cancel",
    "Keyboard error: 0x%08X", "Pin mode: move the map and press X",
    "List full: maximum 64 pins", "Point %u saved in %s",
    "RAM-only change; save error 0x%08X", "Search already in progress",
    "Search coordinates, place, or address",
    "Coordinates found: confirm the pin with X",
    "Found locally: %s · confirm with X", "Unable to start search",
    "Searching for place…", "No results found",
    "Search unavailable without a connection",
    "Search failed: 0x%08X HTTP %ld", "Cache: %s · confirm with X",
    "Found: %s · confirm with X", "New list", "List created",
    "Unable to create list", "Rename list", "List renamed",
    "The only list cannot be deleted", "Press SELECT again to delete the list",
    "List deleted", "Point centered; X adds a new stop", "Rename point",
    "Point renamed", "Press SQUARE again to delete the point", "Point deleted",
    "Pin mode ended", "Center %.5f, %.5f  z%.2f t%d  rot %.0f°", "online",
    "offline", "save", "pin", "finish", "lists", "search", "options",
    "zoom", "PIN → %s | %u points | %s", "Lists and routes",
    "%u pins · %s%s", " · ACTIVE", "open", "new", "rename", "delete",
    "map", "%u stops · straight-line %s",
    "%u stops · straight-line %s · area %s",
    "Addresses and directions: backend unavailable",
    "Empty list: return to the map and press X to add pins", "center",
    "reorder", "VitaMaps - Settings", "Map style", "UI language",
    "HUD behavior", "Center crosshair", "Measurement units",
    "Interface animations", "Persistent logs on Vita storage", "Auto: 2.5 s",
    "Always visible", "Visible", "Hidden", "Imperial", "Metric", "Reduced",
    "Smooth", "Enabled", "Disabled", "Saved",
    "Error: 0x%08X", "select/change", "change", "back", "quit",
    "Favorites", "Point %u", "Map cache", "Reading…", "%s · %u tiles",
    "Press X again to clear", "Clearing…", "Cache cleared",
    "Cache error: 0x%08X", "clear", "show/hide", "close path",
    "open path", "closed", "At least 3 points are needed to close the path",
    "start", "north", "rotate",
};

constexpr const char *kJapanese[] = {
    "テキスト入力", "文字が長すぎます。短くして確定してください",
    "Vitaキーボードで確定、または閉じてキャンセル",
    "キーボードエラー: 0x%08X", "ピンモード: 地図を動かしてXを押す",
    "リスト上限: ピンは最大64個", "地点%uを%sに保存しました",
    "RAM内のみ変更。保存エラー 0x%08X", "検索中です",
    "座標・場所・住所を検索", "座標を検出しました。Xでピンを確定",
    "ローカル検索: %s · Xで確定", "検索を開始できません",
    "場所を検索中…", "結果がありません", "接続がないため検索できません",
    "検索エラー: 0x%08X HTTP %ld", "キャッシュ: %s · Xで確定",
    "検索結果: %s · Xで確定", "新しいリスト", "リストを作成しました",
    "リストを作成できません", "リスト名を変更", "リスト名を変更しました",
    "唯一のリストは削除できません", "もう一度SELECTを押すと削除します",
    "リストを削除しました", "地点を中央に移動。Xで次の地点を追加",
    "地点名を変更", "地点名を変更しました", "もう一度□を押すと地点を削除します",
    "地点を削除しました", "ピンモード終了",
    "中心 %.5f, %.5f  z%.2f t%d  回転 %.0f°", "オンライン", "オフライン",
    "保存", "ピン", "終了", "リスト", "検索", "設定", "ズーム",
    "PIN → %s | %u地点 | %s", "リストとルート", "%uピン · %s%s",
    " · 使用中", "開く", "新規", "名前変更", "削除", "地図",
    "%u地点 · 直線距離 %s", "%u地点 · 直線距離 %s · 面積 %s",
    "住所・経路案内: バックエンド未実装",
    "空のリストです。地図に戻りXでピンを追加", "中央へ", "並べ替え",
    "VitaMaps - 設定", "地図スタイル", "UI言語", "HUD表示",
    "中央の照準", "単位", "画面アニメーション", "Vitaストレージへのログ保存",
    "自動: 2.5秒", "常に表示", "表示", "非表示", "ヤード・ポンド法",
    "メートル法", "軽減", "滑らか", "有効", "無効", "保存しました",
    "エラー: 0x%08X", "選択/変更", "変更", "戻る", "終了",
    "お気に入り", "地点%u", "地図キャッシュ", "読み込み中…", "%s · %uタイル",
    "もう一度Xを押すと消去", "消去中…", "キャッシュを消去しました",
    "キャッシュエラー: 0x%08X", "消去", "表示/非表示", "経路を閉じる",
    "経路を開く", "閉鎖", "経路を閉じるには3地点以上必要です", "開始", "北",
    "回転",
};

constexpr const char *kKorean[] = {
    "텍스트 입력", "텍스트가 너무 깁니다. 줄인 뒤 확인하세요",
    "Vita 키보드에서 확인하거나 닫아서 취소하세요",
    "키보드 오류: 0x%08X", "핀 모드: 지도를 이동하고 X를 누르세요",
    "목록이 가득 찼습니다: 최대 64개 핀", "%u번 지점을 %s에 저장했습니다",
    "RAM에서만 변경됨; 저장 오류 0x%08X", "이미 검색 중입니다",
    "좌표, 장소 또는 주소 검색", "좌표를 찾았습니다. X로 핀을 확인하세요",
    "로컬 결과: %s · X로 확인", "검색을 시작할 수 없습니다",
    "장소 검색 중…", "검색 결과 없음", "연결 없이는 검색할 수 없습니다",
    "검색 실패: 0x%08X HTTP %ld", "캐시: %s · X로 확인",
    "찾음: %s · X로 확인", "새 목록", "목록을 만들었습니다",
    "목록을 만들 수 없습니다", "목록 이름 변경", "목록 이름을 변경했습니다",
    "유일한 목록은 삭제할 수 없습니다", "SELECT를 다시 누르면 목록을 삭제합니다",
    "목록을 삭제했습니다", "지점을 중앙에 놓았습니다. X로 다음 경유지 추가",
    "지점 이름 변경", "지점 이름을 변경했습니다", "□를 다시 누르면 지점을 삭제합니다",
    "지점을 삭제했습니다", "핀 모드 종료",
    "중앙 %.5f, %.5f  z%.2f t%d  회전 %.0f°", "온라인", "오프라인",
    "저장", "핀", "종료", "목록", "검색", "설정", "확대",
    "PIN → %s | %u개 지점 | %s", "목록 및 경로", "%u개 핀 · %s%s",
    " · 활성", "열기", "새로 만들기", "이름 변경", "삭제", "지도",
    "%u개 경유지 · 직선거리 %s", "%u개 경유지 · 직선거리 %s · 면적 %s",
    "주소 및 길 안내: 백엔드 없음", "빈 목록: 지도로 돌아가 X로 핀을 추가하세요",
    "중앙", "순서 변경", "VitaMaps - 설정", "지도 스타일", "UI 언어",
    "HUD 동작", "중앙 조준선", "측정 단위", "인터페이스 애니메이션",
    "Vita 저장소에 로그 저장", "자동: 2.5초", "항상 표시", "표시", "숨김",
    "야드파운드법", "미터법", "줄임", "부드럽게", "활성", "비활성", "저장됨",
    "오류: 0x%08X", "선택/변경", "변경", "뒤로", "종료",
    "즐겨찾기", "지점 %u", "지도 캐시", "읽는 중…", "%s · %u개 타일",
    "X를 다시 누르면 삭제", "삭제 중…", "캐시를 삭제했습니다",
    "캐시 오류: 0x%08X", "삭제", "표시/숨김", "경로 닫기", "경로 열기",
    "닫힘", "경로를 닫으려면 3개 이상의 지점이 필요합니다", "시작", "북쪽",
    "회전",
};

constexpr const char *kChineseSimplified[] = {
    "文本输入", "文本过长，请缩短后确认", "请在Vita键盘确认，或关闭以取消",
    "键盘错误: 0x%08X", "标记模式：移动地图并按X",
    "列表已满：最多64个标记", "地点%u已保存到%s",
    "仅在RAM中修改；保存错误 0x%08X", "搜索正在进行",
    "搜索坐标、地点或地址", "已找到坐标，按X确认标记",
    "本地找到: %s · 按X确认", "无法开始搜索", "正在搜索地点…",
    "未找到结果", "无网络连接，无法搜索", "搜索失败: 0x%08X HTTP %ld",
    "缓存: %s · 按X确认", "找到: %s · 按X确认", "新建列表",
    "列表已创建", "无法创建列表", "重命名列表", "列表已重命名",
    "不能删除唯一的列表", "再次按SELECT删除列表", "列表已删除",
    "地点已居中，按X添加下一站", "重命名地点", "地点已重命名",
    "再次按□删除地点", "地点已删除", "标记模式已结束",
    "中心 %.5f, %.5f  z%.2f t%d  旋转 %.0f°", "在线", "离线", "保存",
    "标记", "结束", "列表", "搜索", "设置", "缩放",
    "标记 → %s | %u个地点 | %s", "列表和路线", "%u个标记 · %s%s",
    " · 当前", "打开", "新建", "重命名", "删除", "地图",
    "%u站 · 直线距离 %s", "%u站 · 直线距离 %s · 面积 %s",
    "地址和路线：后端不可用", "列表为空：返回地图并按X添加标记",
    "居中", "排序", "VitaMaps - 设置", "地图样式", "UI语言", "HUD行为",
    "中心准星", "测量单位", "界面动画", "在Vita存储中保存日志",
    "自动: 2.5秒", "始终显示", "显示", "隐藏", "英制", "公制",
    "减少", "流畅", "启用", "禁用", "已保存",
    "错误: 0x%08X", "选择/更改", "更改", "返回", "退出",
    "收藏夹", "地点%u", "地图缓存", "读取中…", "%s · %u个瓦片",
    "再次按X清除", "正在清除…", "缓存已清除", "缓存错误: 0x%08X", "清除",
    "显示/隐藏", "闭合路径", "打开路径", "已闭合",
    "至少需要3个点才能闭合路径", "起点", "北", "旋转",
};

constexpr const char *kChineseTraditional[] = {
    "文字輸入", "文字過長，請縮短後確認", "請在Vita鍵盤確認，或關閉以取消",
    "鍵盤錯誤: 0x%08X", "標記模式：移動地圖並按X",
    "清單已滿：最多64個標記", "地點%u已儲存到%s",
    "僅在RAM中修改；儲存錯誤 0x%08X", "搜尋正在進行",
    "搜尋座標、地點或地址", "已找到座標，按X確認標記",
    "本機找到: %s · 按X確認", "無法開始搜尋", "正在搜尋地點…",
    "找不到結果", "無網路連線，無法搜尋", "搜尋失敗: 0x%08X HTTP %ld",
    "快取: %s · 按X確認", "找到: %s · 按X確認", "新增清單",
    "清單已建立", "無法建立清單", "重新命名清單", "清單已重新命名",
    "不能刪除唯一的清單", "再次按SELECT刪除清單", "清單已刪除",
    "地點已置中，按X新增下一站", "重新命名地點", "地點已重新命名",
    "再次按□刪除地點", "地點已刪除", "標記模式已結束",
    "中心 %.5f, %.5f  z%.2f t%d  旋轉 %.0f°", "線上", "離線", "儲存",
    "標記", "結束", "清單", "搜尋", "設定", "縮放",
    "標記 → %s | %u個地點 | %s", "清單與路線", "%u個標記 · %s%s",
    " · 目前", "開啟", "新增", "重新命名", "刪除", "地圖",
    "%u站 · 直線距離 %s", "%u站 · 直線距離 %s · 面積 %s",
    "地址與路線：後端不可用", "清單為空：返回地圖並按X新增標記",
    "置中", "排序", "VitaMaps - 設定", "地圖樣式", "UI語言", "HUD行為",
    "中心準星", "測量單位", "介面動畫", "在Vita儲存空間保存日誌",
    "自動: 2.5秒", "永遠顯示", "顯示", "隱藏", "英制", "公制",
    "減少", "流暢", "啟用", "停用", "已儲存",
    "錯誤: 0x%08X", "選擇/更改", "更改", "返回", "退出",
    "我的最愛", "地點%u", "地圖快取", "讀取中…", "%s · %u個圖磚",
    "再次按X清除", "正在清除…", "快取已清除", "快取錯誤: 0x%08X", "清除",
    "顯示/隱藏", "閉合路徑", "開啟路徑", "已閉合",
    "至少需要3個點才能閉合路徑", "起點", "北", "旋轉",
};

constexpr const char *kRussian[] = {
    "Ввод текста", "Текст слишком длинный: сократите и подтвердите",
    "Подтвердите на клавиатуре Vita или закройте для отмены",
    "Ошибка клавиатуры: 0x%08X", "Режим метки: переместите карту и нажмите X",
    "Список заполнен: максимум 64 метки", "Точка %u сохранена в %s",
    "Изменение только в ОЗУ; ошибка сохранения 0x%08X", "Поиск уже выполняется",
    "Поиск координат, места или адреса",
    "Координаты найдены: подтвердите метку кнопкой X",
    "Найдено локально: %s · подтвердите X", "Не удалось начать поиск",
    "Поиск места…", "Результаты не найдены", "Поиск недоступен без подключения",
    "Ошибка поиска: 0x%08X HTTP %ld", "Кэш: %s · подтвердите X",
    "Найдено: %s · подтвердите X", "Новый список", "Список создан",
    "Не удалось создать список", "Переименовать список", "Список переименован",
    "Нельзя удалить единственный список", "Нажмите SELECT ещё раз для удаления",
    "Список удалён", "Точка по центру; X добавляет следующую остановку",
    "Переименовать точку", "Точка переименована",
    "Нажмите КВАДРАТ ещё раз для удаления точки", "Точка удалена",
    "Режим метки завершён", "Центр %.5f, %.5f  z%.2f t%d  пов. %.0f°",
    "онлайн", "офлайн", "сохранить", "метка", "завершить", "списки",
    "поиск", "настройки", "масштаб", "PIN → %s | точек: %u | %s",
    "Списки и маршруты", "%u меток · %s%s", " · АКТИВЕН", "открыть",
    "новый", "переименовать", "удалить", "карта",
    "%u остановок · по прямой %s", "%u остановок · по прямой %s · площадь %s",
    "Адреса и маршруты: модуль недоступен",
    "Список пуст: вернитесь на карту и нажмите X для добавления", "центр",
    "порядок", "VitaMaps - Настройки", "Стиль карты", "Язык интерфейса",
    "Поведение HUD", "Центральный прицел", "Единицы измерения",
    "Анимации интерфейса", "Сохранять журнал на Vita", "Авто: 2,5 с",
    "Всегда видим", "Видим", "Скрыт", "Имперские", "Метрические",
    "Сокращённые", "Плавные", "Включено", "Отключено", "Сохранено",
    "Ошибка: 0x%08X", "выбор/изменение", "изменить", "назад", "выход",
    "Избранное", "Точка %u", "Кэш карт", "Чтение…", "%s · тайлов: %u",
    "Нажмите X ещё раз для очистки", "Очистка…", "Кэш очищен",
    "Ошибка кэша: 0x%08X", "очистить", "показать/скрыть",
    "замкнуть", "разомкнуть", "замкнут", "Для замыкания нужны 3 точки",
    "начало", "север", "вращать",
};

static_assert(std::size(kItalian) == kTextCount);
static_assert(std::size(kEnglish) == kTextCount);
static_assert(std::size(kJapanese) == kTextCount);
static_assert(std::size(kKorean) == kTextCount);
static_assert(std::size(kChineseSimplified) == kTextCount);
static_assert(std::size(kChineseTraditional) == kTextCount);
static_assert(std::size(kRussian) == kTextCount);

Language system_language() {
    int language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &language);
    switch (language) {
    case SCE_SYSTEM_PARAM_LANG_ITALIAN: return Language::Italian;
    case SCE_SYSTEM_PARAM_LANG_JAPANESE: return Language::Japanese;
    case SCE_SYSTEM_PARAM_LANG_KOREAN: return Language::Korean;
    case SCE_SYSTEM_PARAM_LANG_CHINESE_S: return Language::ChineseSimplified;
    case SCE_SYSTEM_PARAM_LANG_CHINESE_T: return Language::ChineseTraditional;
    case SCE_SYSTEM_PARAM_LANG_RUSSIAN: return Language::Russian;
    default: return Language::English;
    }
}

Language explicit_language(int setting) {
    switch (setting) {
    case 1: return Language::Italian;
    case 2: return Language::English;
    case 3: return Language::Japanese;
    case 4: return Language::Korean;
    case 5: return Language::ChineseSimplified;
    case 6: return Language::ChineseTraditional;
    case 7: return Language::Russian;
    default: return system_language();
    }
}

const char *const *table() {
    switch (g_language) {
    case Language::Italian: return kItalian;
    case Language::Japanese: return kJapanese;
    case Language::Korean: return kKorean;
    case Language::ChineseSimplified: return kChineseSimplified;
    case Language::ChineseTraditional: return kChineseTraditional;
    case Language::Russian: return kRussian;
    case Language::English: return kEnglish;
    }
    return kEnglish;
}

} // namespace

void ui_localization_init(int language_setting) {
    g_setting = std::clamp(language_setting, 0, kUiLanguageSettingCount - 1);
    g_language = explicit_language(g_setting);
}

int ui_language_setting() { return g_setting; }

const char *ui_text(UiText text) {
    const std::size_t index = static_cast<std::size_t>(text);
    return index < kTextCount ? table()[index] : "";
}

const char *ui_language_name(int language_setting) {
    const int setting = std::clamp(language_setting, 0,
                                   kUiLanguageSettingCount - 1);
    static constexpr const char *names[][8] = {
        {"Sistema", "Italiano", "Inglese", "Giapponese", "Coreano",
         "Cinese semplificato", "Cinese tradizionale", "Russo"},
        {"System", "Italian", "English", "Japanese", "Korean",
         "Simplified Chinese", "Traditional Chinese", "Russian"},
        {"システム", "イタリア語", "英語", "日本語", "韓国語",
         "簡体字中国語", "繁体字中国語", "ロシア語"},
        {"시스템", "이탈리아어", "영어", "일본어", "한국어",
         "중국어 간체", "중국어 번체", "러시아어"},
        {"系统", "意大利语", "英语", "日语", "韩语", "简体中文",
         "繁体中文", "俄语"},
        {"系統", "義大利語", "英語", "日語", "韓語", "簡體中文",
         "繁體中文", "俄語"},
        {"Система", "Итальянский", "Английский", "Японский", "Корейский",
         "Китайский упрощённый", "Китайский традиционный", "Русский"},
    };
    return names[static_cast<int>(g_language)][setting];
}

const char *ui_language_http_tag() {
    switch (g_language) {
    case Language::Italian: return "it,en";
    case Language::Japanese: return "ja,en";
    case Language::Korean: return "ko,en";
    case Language::ChineseSimplified: return "zh-CN,en";
    case Language::ChineseTraditional: return "zh-TW,en";
    case Language::Russian: return "ru,en";
    case Language::English: return "en";
    }
    return "en";
}

unsigned int ui_language_ime_mask() {
    switch (g_language) {
    case Language::Italian: return SCE_IME_LANGUAGE_ITALIAN;
    case Language::Japanese: return SCE_IME_LANGUAGE_JAPANESE;
    case Language::Korean: return SCE_IME_LANGUAGE_KOREAN;
    case Language::ChineseSimplified: return SCE_IME_LANGUAGE_SIMPLIFIED_CHINESE;
    case Language::ChineseTraditional: return SCE_IME_LANGUAGE_TRADITIONAL_CHINESE;
    case Language::Russian: return SCE_IME_LANGUAGE_RUSSIAN;
    case Language::English: return SCE_IME_LANGUAGE_ENGLISH;
    }
    return SCE_IME_LANGUAGE_ENGLISH;
}

} // namespace vitamaps
