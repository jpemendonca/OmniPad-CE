#include "ui_text.h"

#ifdef __PROSPERO__
int sceSystemServiceParamGetInt(int param_id, int *value);
#define SYSTEM_PARAM_LANG 1
#endif

static const char *const k_codes[UI_LANG_COUNT] = { "en", "pt", "es", "fr", "de", "ru", "ja" };

static const char *const k_text[UI_LANG_COUNT][UI_COUNT] = {
    [UI_EN] = {
        [UI_RUNNING] = "OmniPad %s running. Dashboard: Media tab",
        [UI_RUNNING_IP] = "OmniPad %s running. Dashboard: Media tab, or http://%s:%d on your phone",
        [UI_PAD_CONNECTED] = "OmniPad: %s connected (player %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: controller disconnected (player %d)",
        [UI_CONTROLLER] = "controller",
        [UI_BT_READY] = "OmniPad: Bluetooth adapter ready. Put the controller in pairing mode (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: controller paired. Press a button on it",
        [UI_BT_LOCKED] = "OmniPad: the Bluetooth adapter locked up. Unplug it and plug it again",
        [UI_BT_NO_START] = "OmniPad: the Bluetooth adapter did not start. Unplug and plug it again, or see the dashboard",
        [UI_JC_NEED_RIGHT] = "OmniPad: left Joy-Con connected. Now hold the sync button on the right one until it connects",
        [UI_JC_NEED_LEFT] = "OmniPad: right Joy-Con connected. Now hold the sync button on the left one until it connects",
    },
    [UI_PT] = {
        [UI_RUNNING] = "OmniPad %s ativo. Painel: aba Midia",
        [UI_RUNNING_IP] = "OmniPad %s ativo. Painel: aba Midia, ou http://%s:%d no celular",
        [UI_PAD_CONNECTED] = "OmniPad: %s conectado (jogador %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: controle desconectado (jogador %d)",
        [UI_CONTROLLER] = "controle",
        [UI_BT_READY] = "OmniPad: adaptador Bluetooth pronto. Coloque o controle em pareamento (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: controle pareado. Aperte um botao nele",
        [UI_BT_LOCKED] = "OmniPad: o adaptador Bluetooth travou. Tire e plugue de novo",
        [UI_BT_NO_START] = "OmniPad: o adaptador Bluetooth nao iniciou. Tire e plugue de novo, ou veja o painel",
        [UI_JC_NEED_RIGHT] = "OmniPad: Joy-Con esquerdo conectado. Agora segure o botao de sincronizar do direito ate conectar",
        [UI_JC_NEED_LEFT] = "OmniPad: Joy-Con direito conectado. Agora segure o botao de sincronizar do esquerdo ate conectar",
    },
    [UI_ES] = {
        [UI_RUNNING] = "OmniPad %s activo. Panel: pestaña Multimedia",
        [UI_RUNNING_IP] = "OmniPad %s activo. Panel: pestaña Multimedia, o http://%s:%d en el móvil",
        [UI_PAD_CONNECTED] = "OmniPad: %s conectado (jugador %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: mando desconectado (jugador %d)",
        [UI_CONTROLLER] = "mando",
        [UI_BT_READY] = "OmniPad: adaptador Bluetooth listo. Pon el mando en modo de emparejamiento (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: mando emparejado. Pulsa un botón en él",
        [UI_BT_LOCKED] = "OmniPad: el adaptador Bluetooth se bloqueó. Desconéctalo y vuelve a conectarlo",
        [UI_BT_NO_START] = "OmniPad: el adaptador Bluetooth no arrancó. Desconéctalo y vuelve a conectarlo, o mira el panel",
        [UI_JC_NEED_RIGHT] = "OmniPad: Joy-Con izquierdo conectado. Ahora mantén el botón de sincronización del derecho hasta que conecte",
        [UI_JC_NEED_LEFT] = "OmniPad: Joy-Con derecho conectado. Ahora mantén el botón de sincronización del izquierdo hasta que conecte",
    },
    [UI_FR] = {
        [UI_RUNNING] = "OmniPad %s actif. Tableau de bord : onglet Multimédia",
        [UI_RUNNING_IP] = "OmniPad %s actif. Tableau de bord : onglet Multimédia, ou http://%s:%d sur votre téléphone",
        [UI_PAD_CONNECTED] = "OmniPad : %s connecté (joueur %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad : manette déconnectée (joueur %d)",
        [UI_CONTROLLER] = "manette",
        [UI_BT_READY] = "OmniPad : adaptateur Bluetooth prêt. Mettez la manette en mode d'appairage (DS4 : Share + PS)",
        [UI_BT_PAIRED] = "OmniPad : manette appairée. Appuyez sur un de ses boutons",
        [UI_BT_LOCKED] = "OmniPad : l'adaptateur Bluetooth est bloqué. Débranchez-le puis rebranchez-le",
        [UI_BT_NO_START] = "OmniPad : l'adaptateur Bluetooth n'a pas démarré. Débranchez-le puis rebranchez-le, ou consultez le tableau de bord",
        [UI_JC_NEED_RIGHT] = "OmniPad : Joy-Con gauche connecté. Maintenez le bouton de synchronisation du droit jusqu'à la connexion",
        [UI_JC_NEED_LEFT] = "OmniPad : Joy-Con droit connecté. Maintenez le bouton de synchronisation du gauche jusqu'à la connexion",
    },
    [UI_DE] = {
        [UI_RUNNING] = "OmniPad %s läuft. Dashboard: Tab Medien",
        [UI_RUNNING_IP] = "OmniPad %s läuft. Dashboard: Tab Medien oder http://%s:%d auf dem Handy",
        [UI_PAD_CONNECTED] = "OmniPad: %s verbunden (Spieler %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: Controller getrennt (Spieler %d)",
        [UI_CONTROLLER] = "Controller",
        [UI_BT_READY] = "OmniPad: Bluetooth-Adapter bereit. Versetze den Controller in den Kopplungsmodus (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: Controller gekoppelt. Drücke eine Taste darauf",
        [UI_BT_LOCKED] = "OmniPad: Der Bluetooth-Adapter hängt. Zieh ihn ab und steck ihn wieder ein",
        [UI_BT_NO_START] = "OmniPad: Der Bluetooth-Adapter ist nicht gestartet. Zieh ihn ab und steck ihn wieder ein oder sieh im Dashboard nach",
        [UI_JC_NEED_RIGHT] = "OmniPad: Linker Joy-Con verbunden. Halte jetzt die Sync-Taste am rechten, bis er verbunden ist",
        [UI_JC_NEED_LEFT] = "OmniPad: Rechter Joy-Con verbunden. Halte jetzt die Sync-Taste am linken, bis er verbunden ist",
    },
    [UI_RU] = {
        [UI_RUNNING] = "OmniPad %s запущен. Панель: вкладка «Мультимедиа»",
        [UI_RUNNING_IP] = "OmniPad %s запущен. Панель: вкладка «Мультимедиа» или http://%s:%d на телефоне",
        [UI_PAD_CONNECTED] = "OmniPad: %s подключён (игрок %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: контроллер отключён (игрок %d)",
        [UI_CONTROLLER] = "контроллер",
        [UI_BT_READY] = "OmniPad: адаптер Bluetooth готов. Переведите контроллер в режим сопряжения (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: контроллер сопряжён. Нажмите на нём кнопку",
        [UI_BT_LOCKED] = "OmniPad: адаптер Bluetooth завис. Отключите его и подключите снова",
        [UI_BT_NO_START] = "OmniPad: адаптер Bluetooth не запустился. Отключите его и подключите снова или откройте панель",
        [UI_JC_NEED_RIGHT] = "OmniPad: левый Joy-Con подключён. Теперь удерживайте кнопку синхронизации на правом до подключения",
        [UI_JC_NEED_LEFT] = "OmniPad: правый Joy-Con подключён. Теперь удерживайте кнопку синхронизации на левом до подключения",
    },
    [UI_JA] = {
        [UI_RUNNING] = "OmniPad %s 起動中。ダッシュボード: メディアタブ",
        [UI_RUNNING_IP] = "OmniPad %s 起動中。ダッシュボード: メディアタブ、またはスマホで http://%s:%d",
        [UI_PAD_CONNECTED] = "OmniPad: %s を接続しました (プレイヤー %d)",
        [UI_PAD_DISCONNECTED] = "OmniPad: コントローラーを切断しました (プレイヤー %d)",
        [UI_CONTROLLER] = "コントローラー",
        [UI_BT_READY] = "OmniPad: Bluetoothアダプターの準備ができました。コントローラーをペアリングモードにしてください (DS4: Share + PS)",
        [UI_BT_PAIRED] = "OmniPad: コントローラーをペアリングしました。ボタンを押してください",
        [UI_BT_LOCKED] = "OmniPad: Bluetoothアダプターが応答しません。抜き差ししてください",
        [UI_BT_NO_START] = "OmniPad: Bluetoothアダプターを起動できませんでした。抜き差しするか、ダッシュボードを確認してください",
        [UI_JC_NEED_RIGHT] = "OmniPad: 左のJoy-Conを接続しました。右のJoy-Conのシンクロボタンを接続するまで長押ししてください",
        [UI_JC_NEED_LEFT] = "OmniPad: 右のJoy-Conを接続しました。左のJoy-Conのシンクロボタンを接続するまで長押ししてください",
    },
};

ui_lang_t ui_lang_from_system(int system_lang)
{
    switch (system_lang) {
    case 0:                     return UI_JA;
    case 2: case 22:            return UI_FR;   /* France, Canada */
    case 3: case 20:            return UI_ES;   /* Spain, Latin America */
    case 4:                     return UI_DE;
    case 7: case 17:            return UI_PT;   /* Portugal, Brazil */
    case 8:                     return UI_RU;
    default:                    return UI_EN;
    }
}

static ui_lang_t console_lang(void)
{
    static int lang = -1;
#ifdef __PROSPERO__
    int system_lang;
    if (lang < 0) lang = sceSystemServiceParamGetInt(SYSTEM_PARAM_LANG, &system_lang) == 0
                         ? ui_lang_from_system(system_lang) : UI_EN;
#else
    if (lang < 0) lang = UI_EN;
#endif
    return (ui_lang_t)lang;
}

const char *ui_text_in(ui_lang_t lang, ui_msg_t msg)
{
    return k_text[lang][msg];
}

const char *ui_text(ui_msg_t msg)
{
    return k_text[console_lang()][msg];
}

const char *ui_lang_code(void)
{
    return k_codes[console_lang()];
}
