#pragma  once

#include <cstdint>  // 数値型 uint64_t など
#include <semaphore.h>
#include <pthread.h>


// １回あたりに出力されるログの最大バイト数が分かっているものを記録するクラス

class  KgLog
{
public:
	enum class  EN_bErr : bool { Err = true, OK = false };
	enum class  EN_FileType : int { Text, Binary };

	// ====================================
	// bytes_buf : バッファ全体のサイズ
	// max_bytes_msg : Write() で送られるメッセージの最大バイト数（末尾付加の \n は除く）
	// str_fbody : ファイル名は str_fbody_MMDD_HHMM.log(_#) となる
	// bytes_file : ログファイル１つの最大サイズ（これを超えると、新規ファイルが作成される）
	// bytes_fflush : このバイト数を超えると、fwrite() が実行される
	//【注意１】書き出し効率を考慮して、max_bytes_msg * 2 < byte_fflush であること（そうでない場合、assert が発生する）
	//【注意２】書き出し効率を考慮して、bytes_buf > bytes_fflush * 3 であること（そうでない場合、assert が発生する）
	KgLog (size_t bytes_buf, size_t max_bytes_msg, const char* str_fbody, size_t bytes_file, size_t bytes_fflush,
			EN_FileType file_type = EN_FileType::Text);
	~KgLog();

public:
	// ====================================
	// 戻り値は mb_IsUnderErr
	//【注意】自動的に「\n」が最後に付加される
	// cp_cstr: null 文字で終わる文字列
	EN_bErr  WriteTxt(const char* cp_cstr);
	EN_bErr  WriteTxt_with_HrTime(const char* cp_cstr);  // 11文字の時刻が付与される
	// 万一、データエンコードエラーがあっても、後続するデータが読めるように、bytes を記録することにする
	// bytes には、logID の１バイトは含まない
	EN_bErr  Write_with_UnixTime(uint8_t logID, const void* cp_data, uint16_t bytes);

	// 以下の２つの関数は、データフォーマットは書き込み側に任せるものとなる
	uint8_t*  Get_pos_next() {
		if (mb_IsUnderErr == EN_bErr::Err) { return  NULL; }
		return  (uint8_t*)m_TA_pos_next.Get_Ptr();
	};
	EN_bErr  Adv_pos_next(uint16_t bytes);  // 書き込み最大サイズは 32Kbytes まで

	// int  Get_InfoCode();  // 将来、実装予定
	const char*  Get_StrInfo() { return  m_str_info; }
	EN_bErr  IsUnderErr() { return  mb_IsUnderErr; }

	// fwrite() スレッドを停止する
	EN_bErr  Signal_ThrdStop();


private:
	// ====================================
	EN_bErr  WriteToBuffer();

	// ------------------------------------
	// pthread_create() が、静的関数を必要とするため、その仲介役となる関数
	static void*  MS_ThreadStart(void* arg_pKgLog);

	// 実際のログを記録するスレッド
	void  LogThread();
	// m_pbuf_top_dirty から、bytes だけ書き出す。戻り値は mb_IsUnderErr
	EN_bErr  WriteToDisk(const char* const psrc, size_t bytes);

	// データを書き込んだあとにコールされる関数
	EN_bErr  Adv_TA_pos_next_onLocked(size_t bytes);

	// ====================================
	const EN_FileType  mc_FileType;

	EN_bErr  mb_IsUnderErr = EN_bErr::OK;  // エラー発生中に、Log の書き込みを行わないようにするためのフラグ
	bool  mb_thrd_stop = false;  // スレッドを終了させるフラグ。KgLog::LogThread() で検査される
	bool  mb_called_Signal_ThrdStop = false;

	char* const  mc_pbuf_top;
	char* const  mc_pbuf_tmnt;
	// 以下の「11」は、「MMDD_HH:MM_」の 11文字分
	char* const  mc_pbuf_top_padng;  // = mc_pbuf_tmnt - MAX_BYTES_MSG_KgLog - 1 - 11
	const size_t  mc_max_bytes_wrt;  // 末尾の \n を含めたバイト数（max_bytes_msg + 1）
	
	// >>>>>>>>>>>>>>>>>>>>>>>>>
	// 排他制御対象
	class
	{
		friend  KgLog::KgLog(size_t, size_t, const char*, size_t, size_t, EN_FileType);
		friend  void  KgLog::LogThread();
		friend  KgLog::EN_bErr  KgLog::Adv_TA_pos_next_onLocked(size_t);
		// --------------
		char*  m_ptr;
		size_t  m_bytes = 0;
		size_t  m_bytes_onTop = 0;  // m_TA_dirty.m_ptr < m_TA_top_dirty.m_ptr であるときに利用される
	public:
		const char*  Get_Ptr() { return  m_ptr; }
	} m_TA_dirty;  // ディスクへの書き込み待ちをしている先頭

	class
	{
		friend  KgLog::KgLog(size_t, size_t, const char*, size_t, size_t, EN_FileType);
		friend  KgLog::EN_bErr  KgLog::Adv_TA_pos_next_onLocked(size_t);
		// --------------
		char*  m_ptr;
	public:
		char*  Get_Ptr() { return  m_ptr; }  // JS スレッド側では、読み出しは常に安全
	} m_TA_pos_next;  // バッファ内の、次にデータを書き込むべき位置
	// >>>>>>>>>>>>>>>>>>>>>>>>>

	// --------
	const size_t  mc_fbody_len;

	char* const  mc_pstr_fname;  // 現在のファイル名。動的確保した領域を指している
	char* const  mc_pstr_fname_MD;
	char* const  mc_pstr_fname_HM;

	time_t  m_time_t_fname_cur = 0;  // 現在の mc_pstr_fname に対応する time_t値

	// --------
	const size_t  mc_bytes_fsize;
	const size_t  mc_bytes_fflush;  // fflush を実行するサイズ

	FILE*  m_pf_cur = NULL;  // 現在書き込み中のファイル（fopen で mode a で開いたもの）
	size_t  m_bytes_wrtn_curFile = 0;  // m_pf_cur に対して書き込みがなされたバイト数

	// ----------------
	pthread_t  m_thrd_t;  // ファイルを書き込むスレッド
	pthread_mutex_t  m_mutex_TA;
	sem_t  m_sem_TA;

	// ----------------
	const char*  m_str_info = NULL;

	// ====================================
	// ログに出力する時刻に関するメンバ変数
	uint32_t  m_ui32_MMDD;
	time_t  m_time_t_top_next_day;  // 次の日の 0時0分0秒 における値
};

