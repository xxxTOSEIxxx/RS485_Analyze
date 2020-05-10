#include "chip.h"
#include "stdbool.h"
#include "string.h"


#define STX											( 0x02 )
#define ETX											( 0x03 )

#define UART_RING_BUFFER_SIZE		( 128 )												// UARTリングバッファサイズ
#define COMMAND_BUFFER_SIZE			( 512 )												// コマンドバッファサイズ

// ロータリースイッチのピン配置
#define ROTARY_SW_1_PIN					( 15 )													// ロータリースイッチ(BIT:1)
#define ROTARY_SW_2_PIN					(   1 )													// ロータリースイッチ(BIT:2)
#define ROTARY_SW_4_PIN					(   8 )													// ロータリースイッチ(BIT:4)
#define ROTARY_SW_8_PIN					(   9 )													// ロータリースイッチ(BIT:8)

// UART0（PC用）のピン配置
#define UART0_TXD_PIN            			( 10 )       											// UART0 TXD
#define UART0_RXD_PIN 						( 16 )													// UART0 RXD

// UART1（RS485 Master用）のピン配置
#define UART1_TXD_PIN            			(   6 )       											// UART1 TXD
#define UART1_RXD_PIN 						(   0 )													// UART1 RXD
#define UART1_DE_PIN 						( 14 )													// UART1 DE

// UART2（RS485 Slave用）のピン配置
#define UART2_TXD_PIN            			( 17 )       											// UART2 TXD
#define UART2_RXD_PIN 						( 13 )													// UART2 RXD
#define UART2_DE_PIN 						( 12 )													// UART2 DE

// コマンド解析結果種別
typedef enum
{
	COMMAND_RESULT_NONE = 0,								// コマンドまだ途中
	COMMAND_RESULT_ERROR,									// コマンド解析NG
	COMMAND_RESULT_SUCCESS								// コマンド解析OK
} COMMAND_RESULT_ENUM;

// コマンド解析状態
typedef enum
{
	ANALYZE_STATUS_STX = 0,									// STX待ち
	ANALYZE_STATUS_ETX,											// ETX待ち
} ANALYZE_STATUS_ENUM;


// UART情報テーブル
typedef struct
{
	LPC_USART_T 								*pUART;
    RINGBUFF_T              					tTxRing;														// 送信用リングバッファ構造体
    uint8_t                 						szTxBuff[ UART_RING_BUFFER_SIZE ];		// 送信用リングバッファ
    RINGBUFF_T              					tRxRing;														// 受信用リングバッファ構造体
    uint8_t                 						szRxBuff[ UART_RING_BUFFER_SIZE ];		// 受信用リングバッファ

    ANALYZE_STATUS_ENUM 			eAnalyzeStatus;											// 解析状態
    uint8_t											szCommand[ COMMAND_BUFFER_SIZE + 1 ];	// コマンドバッファ
    uint32_t										Index;
} UART_INFO_TABLE;

// 設定情報テーブル
typedef struct
{
	bool												bBitInvert;													// ビット反転フラグ（true：ビット反転する, false：ビット反転しない）
	uint32_t 										Baudrate;													// ボーレート
} CONFIG_INFO_TABLE;

// ロータリースイッチによる設定情報を定義
const CONFIG_INFO_TABLE	g_tConfigInfo[] =
{
		{	true		, 9600		},				// 0
		{	false		, 9600		},				// 1
		{	true		, 11400	},				// 2
		{	false		, 11400	},				// 3
		{	true		, 19200	},				// 4
		{	false		, 19200	},				// 5
		{	true		, 38400	},				// 6
		{	false		, 38400	},				// 7
		{	true		, 115200	},				// 8
		{	false		, 115200	},				// 9
};

// グローバル変数構造体
typedef struct
{
	CONFIG_INFO_TABLE					tConfigInfo;													// 設定情報
	UART_INFO_TABLE						tUART0;														// UART0
	UART_INFO_TABLE						tUART1;														// UART1
	UART_INFO_TABLE						tUART2;														// UART2

	uint8_t											szTemp[ COMMAND_BUFFER_SIZE + 1];	// 作業用バッファ
} GLOBAL_TABLE;
GLOBAL_TABLE       							g_Global;


//-----------------------------------------------------------------------------
// UART0 割込み処理
//-----------------------------------------------------------------------------
void UART0_IRQHandler(void)
{
    // 受信した場合は受信用バッファに格納し、送信用バッファにデータがある場合は送信をおこなう
    Chip_UART_IRQRBHandler( g_Global.tUART0.pUART, &g_Global.tUART0.tRxRing, &g_Global.tUART0.tTxRing );
}


//-----------------------------------------------------------------------------
// UART1 割込み処理
//-----------------------------------------------------------------------------
void UART1_IRQHandler(void)
{
    // 受信した場合は受信用バッファに格納し、送信用バッファにデータがある場合は送信をおこなう
    Chip_UART_IRQRBHandler( g_Global.tUART1.pUART, &g_Global.tUART1.tRxRing, &g_Global.tUART1.tTxRing );
}


//-----------------------------------------------------------------------------
// UART2 割込み処理
//-----------------------------------------------------------------------------
void UART2_IRQHandler(void)
{
    // 受信した場合は受信用バッファに格納し、送信用バッファにデータがある場合は送信をおこなう
    Chip_UART_IRQRBHandler( g_Global.tUART2.pUART, &g_Global.tUART2.tRxRing, &g_Global.tUART2.tTxRing );
}

//-----------------------------------------------------------------------------
// ロータリースイッチの設定によってモードを決定する
//-----------------------------------------------------------------------------
void GetMode(CONFIG_INFO_TABLE *ptConfigInfo)
{
	uint8_t					Mode = 0;

	// ロータリースイッチ(BIT:1)の状態
	if (Chip_GPIO_GetPinState(LPC_GPIO_PORT,0, ROTARY_SW_1_PIN) == false)
	{
		Mode += 1;
	}
	// ロータリースイッチ(BIT:2)の状態
	if (Chip_GPIO_GetPinState(LPC_GPIO_PORT,0, ROTARY_SW_2_PIN) == false)
	{
		Mode += 2;
	}
	// ロータリースイッチ(BIT:4)の状態
	if (Chip_GPIO_GetPinState(LPC_GPIO_PORT,0, ROTARY_SW_4_PIN) == false)
	{
		Mode += 4;
	}
	// ロータリースイッチ(BIT:8)の状態
	if (Chip_GPIO_GetPinState(LPC_GPIO_PORT,0, ROTARY_SW_8_PIN) == false)
	{
		Mode += 8;
	}

	// ロータリースイッチによる設定情報を取得
	*ptConfigInfo = g_tConfigInfo[Mode];
}

//-----------------------------------------------------------------------------
// UARTの初期化
//-----------------------------------------------------------------------------
void UART_Init(GLOBAL_TABLE	*ptGlobal)
{
	// リングバッファ初期化
    RingBuffer_Init( &ptGlobal->tUART0.tRxRing, ptGlobal->tUART0.szRxBuff, 1, UART_RING_BUFFER_SIZE );      // UART0で使用する受信用リングバッファを生成
    RingBuffer_Init( &ptGlobal->tUART0.tTxRing, ptGlobal->tUART0.szTxBuff, 1, UART_RING_BUFFER_SIZE );     	 // UART0で使用する送信用リングバッファを生成
    RingBuffer_Init( &ptGlobal->tUART1.tRxRing, ptGlobal->tUART1.szRxBuff, 1, UART_RING_BUFFER_SIZE );      // UART1で使用する受信用リングバッファを生成
    RingBuffer_Init( &ptGlobal->tUART1.tTxRing, ptGlobal->tUART1.szTxBuff, 1, UART_RING_BUFFER_SIZE );     	 // UART1で使用する送信用リングバッファを生成
    RingBuffer_Init( &ptGlobal->tUART2.tRxRing, ptGlobal->tUART2.szRxBuff, 1, UART_RING_BUFFER_SIZE );      // UART2で使用する受信用リングバッファを生成
    RingBuffer_Init( &ptGlobal->tUART2.tTxRing, ptGlobal->tUART2.szTxBuff, 1, UART_RING_BUFFER_SIZE );     	 // UART2で使用する送信用リングバッファを生成

	// UART0・UART1・UART2で使用するピンを設定
    Chip_Clock_EnablePeriphClock( SYSCTL_CLOCK_SWM );
    Chip_SWM_MovablePinAssign( SWM_U0_TXD_O, UART0_TXD_PIN );                   	// UART0_TXD
    Chip_SWM_MovablePinAssign( SWM_U0_RXD_I, UART0_RXD_PIN );                      // UART0_RXD
    Chip_SWM_MovablePinAssign( SWM_U1_TXD_O, UART1_TXD_PIN );                   	// UART1_TXD
    Chip_SWM_MovablePinAssign( SWM_U1_RXD_I, UART1_RXD_PIN );                      // UART1_RXD
    Chip_SWM_MovablePinAssign( SWM_U2_TXD_O, UART2_TXD_PIN );                   	// UART2_TXD
    Chip_SWM_MovablePinAssign( SWM_U2_RXD_I, UART2_RXD_PIN );                      // UART2_RXD
    Chip_Clock_DisablePeriphClock( SYSCTL_CLOCK_SWM );

    // UARTベースクロックを設定
    // ※UARTs need a base clock 16x faster than the baud rate, so if you	need a 115.2Kbps baud rate, you will need a clock rate of at least (115.2K * 16).
    //Chip_Clock_SetUSARTNBaseClockRate((115200 * 16 ), true);
    Chip_Clock_SetUSARTNBaseClockRate((ptGlobal->tConfigInfo.Baudrate * 16 ), true);

    // UART0の設定（PC用）
    ptGlobal->tUART0.pUART = LPC_USART0;
    Chip_UART_Init( ptGlobal->tUART0.pUART );                                                                        		// UART0を有効にする
    Chip_UART_ConfigData( ptGlobal->tUART0.pUART, (UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1) );
    Chip_UART_SetBaud( ptGlobal->tUART0.pUART, ptGlobal->tConfigInfo.Baudrate );                  // UART0のボーレートを設定
    Chip_UART_Enable( ptGlobal->tUART0.pUART );                                                          				// UART0の受信を有効にする
    Chip_UART_TXEnable( ptGlobal->tUART0.pUART );                                                        				// UART0の送信を有効にする
    Chip_UART_IntEnable( ptGlobal->tUART0.pUART, UART_INTEN_RXRDY );                                 // UART0受信割込みを有効にする
    Chip_UART_IntDisable( ptGlobal->tUART0.pUART, UART_INTEN_TXRDY );                                 // UART0送信用割込みを無効にする（※UART API内部で有効／無効にしている）

    // UART1の設定（RS485 Master用）
    ptGlobal->tUART1.pUART = LPC_USART1;
    Chip_UART_Init( ptGlobal->tUART1.pUART );                                                                        		// UART1を有効にする
    Chip_UART_ConfigData( ptGlobal->tUART1.pUART, (UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1) );
    Chip_UART_SetBaud( ptGlobal->tUART1.pUART, ptGlobal->tConfigInfo.Baudrate );                  // UART1のボーレートを設定
    Chip_UART_Enable( ptGlobal->tUART1.pUART );                                                          				// UART1の受信を有効にする
    Chip_UART_TXEnable( ptGlobal->tUART1.pUART );                                                        				// UART1の送信を有効にする
    Chip_UART_IntEnable( ptGlobal->tUART1.pUART, UART_INTEN_RXRDY );                                 // UART1受信割込みを有効にする
    Chip_UART_IntDisable( ptGlobal->tUART1.pUART, UART_INTEN_TXRDY );                                 // UART1送信用割込みを無効にする（※UART API内部で有効／無効にしている）

    // UART2の設定（RS485 Slave用）
    ptGlobal->tUART2.pUART = LPC_USART2;
    Chip_UART_Init( ptGlobal->tUART2.pUART );                                                                        		// UART2を有効にする
    Chip_UART_ConfigData( ptGlobal->tUART2.pUART, (UART_CFG_DATALEN_8 | UART_CFG_PARITY_NONE | UART_CFG_STOPLEN_1) );
    Chip_UART_SetBaud( ptGlobal->tUART2.pUART, ptGlobal->tConfigInfo.Baudrate );     				// UART2のボーレートを設定
    Chip_UART_Enable( ptGlobal->tUART2.pUART );                                                          				// UART2の受信を有効にする
    Chip_UART_TXEnable( ptGlobal->tUART2.pUART );                                                        				// UART2の送信を有効にする
    Chip_UART_IntEnable( ptGlobal->tUART2.pUART, UART_INTEN_RXRDY );                                 // UART2受信割込みを有効にする
    Chip_UART_IntDisable( ptGlobal->tUART2.pUART, UART_INTEN_TXRDY );                                 // UART2送信用割込みを無効にする（※UART API内部で有効／無効にしている）

    // UART0・UART1・UART2の割込みを有効にする
    NVIC_EnableIRQ( UART0_IRQn );                                                           										// UART0割込みを有効にする
    NVIC_EnableIRQ( UART1_IRQn );                                                           										// UART1割込みを有効にする
    NVIC_EnableIRQ( UART2_IRQn );                                                           										// UART2割込みを有効にする
}

//-----------------------------------------------------------------------------
// エラー内容を送信
//-----------------------------------------------------------------------------
void SendError(const char *pszErrorMsg, UART_INFO_TABLE *ptSendUartInfo)
{
	uint8_t						stx = 0x02;
	uint8_t						etx = 0x03;

	Chip_UART_SendBlocking(ptSendUartInfo->pUART, &stx, 1);
	Chip_UART_SendBlocking(ptSendUartInfo->pUART, pszErrorMsg, strlen(pszErrorMsg));
	Chip_UART_SendBlocking(ptSendUartInfo->pUART, &etx, 2);
}

//-----------------------------------------------------------------------------
// 受信したコマンドを送信
//-----------------------------------------------------------------------------
void SendCommand(UART_INFO_TABLE *ptRecvUartInfo, UART_INFO_TABLE *ptSendUartInfo)
{
	// 受信データをチェック
	if (ptRecvUartInfo->Index > 0)
	{
		// 受信データを送信先UARTへ送信
		Chip_UART_SendBlocking(ptSendUartInfo->pUART, ptRecvUartInfo->szCommand, ptRecvUartInfo->Index);
		memset(ptRecvUartInfo->szCommand, 0x00, sizeof(ptRecvUartInfo->szCommand));
		ptRecvUartInfo->Index = 0;
	}
}

//-----------------------------------------------------------------------------
// 受信したデータを解析
//-----------------------------------------------------------------------------
void Analyze(uint8_t *pszRecv, int iRecv, UART_INFO_TABLE *ptRecvUartInfo, UART_INFO_TABLE *ptSendUartInfo)
{
#if 1
	Chip_UART_SendBlocking(ptSendUartInfo->pUART, pszRecv, iRecv);
#else
	int				i = 0;
	uint8_t			ch = 0x00;

	for(i = 0 ; i < iRecv ; i++)
	{
		// ビット反転あり？
		if (g_Global.tConfigInfo.bBitInvert == true)
		{
			ch = ~pszRecv[i];
		}
		else
		{
			ch = pszRecv[i];
		}

		// STX待ち状態の場合
		if (ptRecvUartInfo->eAnalyzeStatus == ANALYZE_STATUS_STX)
		{
			// STXの場合（正常）
			if (ch == STX)
			{
				memset(ptRecvUartInfo->szCommand, 0x00, sizeof(ptRecvUartInfo->szCommand));
				ptRecvUartInfo->Index = 0;
				ptRecvUartInfo->szCommand[ptRecvUartInfo->Index++] = ch;

				// EXT待ち状態に変更
				ptRecvUartInfo->eAnalyzeStatus = ANALYZE_STATUS_ETX;
			}
		}
		// ETX待ち状態の場合
		else if (ptRecvUartInfo->eAnalyzeStatus == ANALYZE_STATUS_ETX)
		{
			// STXの場合（異常）
			if (ch == STX)
			{
				memset(ptRecvUartInfo->szCommand, 0x00, sizeof(ptRecvUartInfo->szCommand));
				ptRecvUartInfo->Index = 0;
				ptRecvUartInfo->szCommand[ptRecvUartInfo->Index++] = ch;

				// EXT待ち状態に変更
				ptRecvUartInfo->eAnalyzeStatus = ANALYZE_STATUS_ETX;
			}
			// ETXの場合（正常）
			else if (ch == ETX)
			{
				ptRecvUartInfo->szCommand[ptRecvUartInfo->Index++] = ch;

				// 受信したコマンドを送信
				SendCommand(ptRecvUartInfo,ptSendUartInfo);

				// STX待ち状態に変更
				ptRecvUartInfo->eAnalyzeStatus = ANALYZE_STATUS_STX;
			}
			else
			{
				ptRecvUartInfo->szCommand[ptRecvUartInfo->Index++] = ch;
				if (ptRecvUartInfo->Index >= COMMAND_BUFFER_SIZE)
				{
					// 受信したコマンドを送信（一旦送信）
					SendCommand(ptRecvUartInfo,ptSendUartInfo);
				}
			}
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// UART 処理
//-----------------------------------------------------------------------------
bool UART_Proc(UART_INFO_TABLE *ptRecvUartInfo, UART_INFO_TABLE *ptSendUartInfo)
{
	int						iRecv = 0;
	int						iRead = 0;


	// 受信バッファフル？
	if (RingBuffer_IsFull(&ptRecvUartInfo->tRxRing))
	{
		// エラー
		SendError("RingBuffer Full Error", ptSendUartInfo);
		return false;
	}

	// 受信データあり？
	iRecv = RingBuffer_GetCount(&ptRecvUartInfo->tRxRing);
	if (iRecv > 0)
	{
		// 受信データを取得
		memset(g_Global.szTemp,0x00,sizeof(g_Global.szTemp));
		iRead = Chip_UART_ReadRB(ptRecvUartInfo->pUART, &ptRecvUartInfo->tRxRing,g_Global.szTemp, iRecv );
		if (iRead != iRecv)
		{
			// エラー
			SendError("Read Size Error", ptSendUartInfo);
			return false;
		}

		// 受信データ解析
		Analyze(g_Global.szTemp, iRecv, ptRecvUartInfo, ptSendUartInfo);
	}

	return true;
}

//-----------------------------------------------------------------------------
// メイン処理
//-----------------------------------------------------------------------------
int main(void)
{
	memset(&g_Global, 0x00, sizeof(g_Global));

	// システムコアのクロックレート更新
	SystemCoreClockUpdate();

	// I/Oとして使用するPINの設定
    Chip_GPIO_Init(LPC_GPIO_PORT);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 0, ROTARY_SW_1_PIN);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 0, ROTARY_SW_2_PIN);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 0, ROTARY_SW_4_PIN);
    Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, 0, ROTARY_SW_8_PIN);
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, 0, UART1_DE_PIN);
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, 0, UART2_DE_PIN);

    // ロータリースイッチの設定によってモードを決定する
    GetMode(&g_Global.tConfigInfo);

    // UARTの初期化
    UART_Init(&g_Global);

    // ループ
    while(1)
    {
    	// UART1（RS485 Master用） の受信データチェック
    	UART_Proc(&g_Global.tUART1,&g_Global.tUART0);

    	// UART2（RS485 Slave用）の受信データチェック
    	UART_Proc(&g_Global.tUART2,&g_Global.tUART0);
    }
}









