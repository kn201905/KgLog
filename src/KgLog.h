#pragma  once

#include <cstdint>  // 数値型 uint64_t など
#include <semaphore.h>
#include <pthread.h>


// １回あたりに出力されるログの最大バイト数が分かっているものを記録するクラス

class  KgLog
{
public:
	enum class  EN_bErr : bool { Err = true, OK = false };

	// ====================================
	// bytes_buf : バッファ全体のサイズ
	// max_bytes_msg : Write() で送られるメッセージの最大バイト数（末尾付加の \n は除く）
	// str_fbody : ファイル名は str_fbody_MMDD_HHMM.log(_#) となる
	// bytes_file : ログファイル１つの最大サイズ（これを超えると、新規ファイルが作成される）
	// bytes_fflush : このバイト数を超えると、fwrite() が実行される
	//【注意１】max_bytes_msg < byte_fflush であること（そうでない場合、assert が発生する）
	//【注意２】書き出し効率を考慮して、bytes_buf > bytes_fflush * 3 であること（そうでない場合、assert が発生する）
	KgLog (size_t bytes_buf, size_t max_bytes_msg, const char* str_fbody, size_t bytes_file, size_t bytes_fflush);
	~KgLog();

public:
	// ====================================
	// 戻り値は mb_IsUnderErr
	//【注意】自動的に「\n」が最後に付加される
	EN_bErr  Write(const char* p_cstr);  // p_cstr: null 文字で終わる文字列

	// int  Get_InfoCode();  // 将来、実装予定
	const char*  Get_StrInfo() { return  m_str_info; }
	EN_bErr  IsUnderErr() { return  mb_IsUnderErr; }

	// fwrite() スレッドを停止する
	void  Signal_ThrdStop();


private:
	// ====================================
	// pthread_create() が、静的関数を必要とするため、その仲介役となる関数
	static void*  MS_ThreadStart(void* arg_pKgLog);

	// 実際のログを記録するスレッド
	void  LogThread();
	// m_pbuf_top_dirty から、bytes だけ書き出す。戻り値は mb_IsUnderErr
	EN_bErr  WriteToDisk(const char* const psrc, size_t bytes);

	// ====================================
	EN_bErr  mb_IsUnderErr = EN_bErr::OK;  // エラー発生中に、Log の書き込みを行わないようにするためのフラグ
	bool  mb_thrd_stop = false;  // スレッドを終了させるフラグ。KgLog::LogThread() で検査される
	bool  mb_called_Signal_ThrdStop = false;

	char* const  mc_pbuf_top;
	char* const  mc_pbuf_tmnt;
	char* const  mc_pbuf_top_padng;  // = mc_pbuf_tmnt - MAX_BYTES_MSG_KgLog - 1
	const size_t  mc_max_bytes_msg;  // 末尾の \n を除いたバイト数
	
	// >>>>>>>>>>>>>>>>>>>>>>>>>
	// 排他制御対象
	class
	{
		friend  KgLog::KgLog(size_t, size_t, const char*, size_t, size_t);
		friend  void  KgLog::LogThread();
		// --------------
		char*  m_ptr;
	public:
		const char*  Get_Ptr() { return  m_ptr; }
	} m_TA_top_dirty;  // ディスクへの書き込み待ちをしている先頭

	class
	{
		friend  KgLog::KgLog(size_t, size_t, const char*, size_t, size_t);
		friend  KgLog::EN_bErr  KgLog::Write(const char* const p_cstr);
		// --------------
		char*  m_ptr;
	public:
		char*  Get_Ptr() { return  m_ptr; }
	} m_TA_pos_next;  // バッファ内の、次にデータを書き込むべき位置

	size_t  m_TA_bytes_dirty = 0;
	size_t  m_TA_bytes_dirty_onTop = 0;  // m_TA_pbuf_top_next < m_TA_top_dirty.m_ptr であるときに利用される
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
};

