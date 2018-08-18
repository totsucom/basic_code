/*
 * TWELITE BLUE/RED (MONOSTICK含む)用ひな形プログラム
 * SDK バージョン 2018-05
 *
 * できること
 * ・UART(パソコン側とのシリアル通信)
 * ・他のTWELITEとの無線通信
 *
 * オマケ的な
 * ・無線受信時にLEDを光らせる
 *
 * その他の機能は自前で追加してください
 */

#include <AppHardwareApi.h>
#include "utils.h"
#include "ToCoNet.h"
#include "serial.h"
#include "string.h"
#include "sprintf.h"
#include "ToCoNet_mod_prototype.h"  // ToCoNet モジュール定義(無線で使う)


#define LED         16              // モノスティックの場合のLED
//#define LED         9               // デジタル出力4にLEDをつないだ場合

// 無線通信用パラメータ(通信先と合わせておくこと)
#define APP_ID      0x67720103
#define CHANNEL     18

static uint32 u32Seq = 0;           // 送信パケットのシーケンス番号


#define UART_BAUD 115200 	        // シリアルのボーレート
static tsFILE sSerStream;           // シリアル用ストリーム
static tsSerialPortSetup sSerPort;  // シリアルポートデスクリプタ

// シリアルにメッセージを出力する
#define serialPrintf(...) vfPrintf(&sSerStream, LB __VA_ARGS__)

// 無線で送信する
static bool_t sendBroadcast(char *p)
{
    tsTxDataApp tsTx;
    memset(&tsTx, 0, sizeof(tsTxDataApp));

    tsTx.u32SrcAddr = ToCoNet_u32GetSerial();//チップのS/N
    tsTx.u32DstAddr = TOCONET_MAC_ADDR_BROADCAST;

    u32Seq++;
    tsTx.bAckReq = FALSE;
    tsTx.u8Retry = 0x02; // 送信失敗時は 2回再送
    tsTx.u8CbId = u32Seq & 0xFF;
    tsTx.u8Seq = u32Seq & 0xFF;
    tsTx.u8Cmd = TOCONET_PACKET_CMD_APP_DATA;

    // ペイロードを作成
    memcpy(tsTx.auData, p, strlen(p));
    tsTx.u8Len = strlen(p);

    // 送信
    return ToCoNet_bMacTxReq(&tsTx);
}

// デバッグ出力用に UART を初期化
static void vSerialInit() {
    static uint8 au8SerialTxBuffer[96];
    static uint8 au8SerialRxBuffer[32];

    sSerPort.pu8SerialRxQueueBuffer = au8SerialRxBuffer;
    sSerPort.pu8SerialTxQueueBuffer = au8SerialTxBuffer;
    sSerPort.u32BaudRate = UART_BAUD;
    sSerPort.u16AHI_UART_RTS_LOW = 0xffff;
    sSerPort.u16AHI_UART_RTS_HIGH = 0xffff;
    sSerPort.u16SerialRxQueueSize = sizeof(au8SerialRxBuffer);
    sSerPort.u16SerialTxQueueSize = sizeof(au8SerialTxBuffer);
    sSerPort.u8SerialPort = E_AHI_UART_0;
    sSerPort.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
    SERIAL_vInit(&sSerPort);

    sSerStream.bPutChar = SERIAL_bTxChar;
    sSerStream.u8Device = E_AHI_UART_0;
}

// ユーザ定義のイベントハンドラ
// ウィンドウズのwndProc()みたいなもん
// 比較的重めの処理を書いてもいいけどブロックしてはいけません
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32 u32evarg)
{
    static int count = 0;

	// 起動時
	if (eEvent == E_EVENT_START_UP) {
        //起動時メッセージ出力など最初にやりたいことをここに記述
        //ハードウェアの初期化はここではなくcbAppColdStart()で

    }
    // 無線データ受信
    else if (eEvent == E_ORDER_KICK) {
        //軽い処理であればcbToCoNet_vRxEvent()内に書いてもいいが
        //割り込みを邪魔しないためにも、直接通信に関係ないことは
        //cbToCoNet_vRxEvent()内ではなく、ここに書くべきである

        vPortSetLo(LED);    // LED ON処理
        count = 20;
    }
	// 4ms毎タイマー
    else if (eEvent == E_EVENT_TICK_TIMER) {

        // LED OFF処理
        if (count > 0) {
            count--;
        } else {
            vPortSetHi(LED);
        }

        // シリアル入力チェック
		while (!SERIAL_bRxQueueEmpty(sSerPort.u8SerialPort))
		{
			// FIFOキューから１バイトずつ取り出して処理する。
			int16 i16Char = SERIAL_i16RxChar(sSerPort.u8SerialPort);

            switch(i16Char & 255)
            {
            case 's':
                //Sが入力されたら送信
                sendBroadcast("HELLO TWELITE!");
                break;
            }
		}
	}
}

// 電源オンによるスタート
void cbAppColdStart(bool_t bAfterAhiInit)
{
	if (!bAfterAhiInit) {
        // 必要モジュール登録手続き
        ToCoNet_REG_MOD_ALL();
	} else {
        // SPRINTF 初期化
        SPRINTF_vInit128();

        // ToCoNet パラメータ
        sToCoNet_AppContext.u32AppId = APP_ID;
        sToCoNet_AppContext.u8Channel = CHANNEL;
        sToCoNet_AppContext.bRxOnIdle = TRUE; // アイドル時にも受信
        u32Seq = 0;

        // ユーザ定義のイベントハンドラを登録
        ToCoNet_Event_Register_State_Machine(vProcessEvCore);

		// シリアル出力用
		vSerialInit();
		ToCoNet_vDebugInit(&sSerStream);
		ToCoNet_vDebugLevel(0);

        // IOポート初期化
        vPortAsOutput(LED);
        vPortSetHi(LED);

        // MAC 層開始
        ToCoNet_vMacStart();
	}
}

// スリープからの復帰
void cbAppWarmStart(bool_t bAfterAhiInit)
{
    //今回は使わない
}

// ネットワークイベント発生時
void cbToCoNet_vNwkEvent(teEvent eEvent, uint32 u32arg)
{
	switch(eEvent) {
	default:
		break;
	}
}

// パケット受信時
void cbToCoNet_vRxEvent(tsRxDataApp *pRx)
{
    static uint32 u32SrcAddrPrev = 0;
    static uint8 u8seqPrev = 0xFF;

    // 前回と同一の送信元＋シーケンス番号のパケットなら受け流す
    if (pRx->u32SrcAddr == u32SrcAddrPrev && pRx->u8Seq == u8seqPrev) {
        return;
    }

    // ここではそのままシリアルへ転送
    char buf[64];
    int len = (pRx->u8Len < sizeof(buf)) ? pRx->u8Len : sizeof(buf)-1;
    memcpy(buf, pRx->auData, len);
    buf[len] = '\0';
    serialPrintf("%s\n", buf);

    u32SrcAddrPrev = pRx->u32SrcAddr;
    u8seqPrev = pRx->u8Seq;

    // 受信したことをvProcessEvCore()関数に知らせる
    ToCoNet_Event_Process(E_ORDER_KICK, 0, vProcessEvCore);
}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8 u8CbId, uint8 bStatus)
{
    //送信したよ！
    serialPrintf(">> SENT %s seq=%08X\n", bStatus ? "OK" : "NG", u32Seq);
}

// ハードウェア割り込み発生後（遅延呼び出し）
void cbToCoNet_vHwEvent(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
    //割り込みに対する処理は通常ここで行う。
}

// ハードウェア割り込み発生時
uint8 cbToCoNet_u8HwInt(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
    //割り込みで最初に呼ばれる。最短で返さないといけない。
	return FALSE;//FALSEによりcbToCoNet_vHwEvent()が呼ばれる
}

// メイン
void cbToCoNet_vMain(void)
{
}
