#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <unistd.h>  // seeep() のため

#include "../../__common/__general.h"
#include "../../__common/_KString.h"
#include "../../__common/LogID.h"
#include "KgLog.h"

#include <assert.h>


/////////////////////////////////////////////////////////////////////

KgLog::KgLog(const size_t bytes_buf, const size_t max_bytes_msg
		, const char* const pstr_fbody, const size_t bytes_file, const size_t bytes_fflush
		, EN_FileType file_type)
	: mc_FileType{ file_type }

	, mc_pbuf_top{ new char[bytes_buf] }
	, mc_pbuf_tmnt{ mc_pbuf_top + bytes_buf }
	// 11: 時刻テキスト or UnixTime ,  3: logID + len
	, mc_pbuf_top_padng{ mc_pbuf_tmnt - max_bytes_msg - 1 - 11 - 3 }
	, mc_max_bytes_wrt{ max_bytes_msg + 1 }  // 末尾の \n を含めたバイト数

	, mc_fbody_len{ strlen(pstr_fbody) }
	, mc_pstr_fname{ new char[mc_fbody_len + 15] }
	, mc_pstr_fname_MD{ mc_pstr_fname + mc_fbody_len + 1 }
	, mc_pstr_fname_HM{ mc_pstr_fname + mc_fbody_len + 6 }
	, mc_bytes_fsize{ bytes_file }
	, mc_bytes_fflush{ bytes_fflush }
{
	assert(max_bytes_msg * 2 < bytes_fflush);
	assert(bytes_buf > bytes_fflush * 3);

	// mc_pstr_fname の初期設定
	G_StrCpy(mc_pstr_fname, pstr_fbody, mc_fbody_len);
	*(mc_pstr_fname_MD - 1) = '_';
	*(mc_pstr_fname_HM - 1) = '_';
	*(uint32_t*)(mc_pstr_fname_HM + 4) = KSTR4('.', 'l', 'o', 'g');
	*(mc_pstr_fname_HM + 8) = 0;

	// ファイル名の設定
	const time_t  ctime_t = time(NULL);
	m_time_t_fname_cur = ctime_t;  // mc_pstr_fname に対応する time_t値 を保存

	const tm* const  cp_tm = localtime(&ctime_t);

	const uint32_t  cmonth = cp_tm->tm_mon + 1;
	const uint32_t  cday = cp_tm->tm_mday;

	const uint32_t  chour = cp_tm->tm_hour;
	const uint32_t  cmin = cp_tm->tm_min;

	*(uint32_t*)(mc_pstr_fname_MD) = KSTR4(cmonth / 10, cmonth % 10, cday / 10, cday % 10) + 0x30303030;
	*(uint32_t*)(mc_pstr_fname_HM) = KSTR4(chour / 10, chour % 10, cmin / 10, cmin % 10) + 0x30303030;

	// エラーメッセージ初期化
	m_str_info = "KgLog::m_str_info: 情報なし\n";

	// ファイルオープン
	m_pf_cur = fopen(mc_pstr_fname, "a");
	if (m_pf_cur == NULL)
	{
		mb_IsUnderErr = EN_bErr::Err;
		m_str_info = "fopen() に失敗しました。\n";
		return;
	}

	// mutex の初期化
	if (pthread_mutex_init(&m_mutex_TA, NULL) != 0)
	{
		mb_IsUnderErr = EN_bErr::Err;
		m_str_info = "pthread_mutex_init() に失敗しました。\n";
		return;
	}

	// セマフォの初期化
	if (sem_init(&m_sem_TA, 0, 0) != 0)
	{
		mb_IsUnderErr = EN_bErr::Err;
		m_str_info = "sem_init() に失敗しました。\n";
		return;
	}

	// その他のメンバ変数の初期化
	m_TA_dirty.m_ptr = mc_pbuf_top;
	m_TA_pos_next.m_ptr = mc_pbuf_top;

	m_ui32_MMDD = KSTR4(cmonth / 10, cmonth % 10, cday / 10, cday % 10) + 0x30303030u;
	const uint32_t  csec = cp_tm->tm_sec;
	m_time_t_top_next_day = ctime_t - chour * 3600 - cmin * 60 - csec + 24 * 60 * 60;

	// 書き込みスレッド生成
	pthread_create(&m_thrd_t, NULL, MS_ThreadStart, this);
}

// ------------------------------------------------------------------

KgLog::~KgLog()
{
	if (mb_called_Signal_ThrdStop == false)
	{
		mb_thrd_stop = true;
		sem_post(&m_sem_TA);  // スレッドを return させるための措置
		pthread_join(m_thrd_t, NULL);
	}

	// リソース解放
	pthread_mutex_destroy(&m_mutex_TA);
	sem_destroy(&m_sem_TA);

	if (m_pf_cur != NULL) { fclose(m_pf_cur); }
	delete  mc_pstr_fname;

	delete[]  mc_pbuf_top;
}


/////////////////////////////////////////////////////////////////////

static const char* const  s_cpstr_called_Signal_ThrdStrop = "Signal_ThrdStop() が call されました。\n";
static const char* const  s_cpstr_abort_Thrd = "Signal_ThrdStop() が call されずにスレッドが終了しました。\n";

void*  KgLog::MS_ThreadStart(void* arg_pKgLog)
{
	KgLog*  pgLog = (KgLog*)arg_pKgLog;
/*
	// スレッドの detach を行う
	if (pthread_detach(pgLog->m_thrd_t) != 0)
	{
		pgLog->mb_IsUnderErr = EN_bErr::Err;
		pgLog->m_str_info = "detach() 失敗\n";
		return  NULL;
	}
*/
	pgLog->LogThread();

	if (pgLog->mb_IsUnderErr == EN_bErr::OK)
	{
		if (pgLog->mc_FileType == EN_FileType::Text)
		{
			if (pgLog->mb_called_Signal_ThrdStop)
			{
				fwrite(s_cpstr_called_Signal_ThrdStrop, 1, CE_StrLen(s_cpstr_called_Signal_ThrdStrop), pgLog->m_pf_cur);
			}
			else
			{
				fwrite(s_cpstr_abort_Thrd, 1, CE_StrLen(s_cpstr_abort_Thrd), pgLog->m_pf_cur);
			}
		}
		else  // pgLog->mb_IsUnderErr == EN_bErr::OK かつ、バイナリファイルの処理の場合
		{
			// Unix Time の書き込み
			const uint8_t  log_id = N_LogID::EN_UNIX_TIME;
			fwrite(&log_id, 1, 1, pgLog->m_pf_cur);
			uint16_t  len = 8;
			fwrite(&len, 1, 2, pgLog->m_pf_cur);
			const time_t  ctime = time(NULL);
			fwrite(&ctime, 1, 8, pgLog->m_pf_cur);

			// close ID の書き込み
			uint8_t  close_id;
			if (pgLog->mb_called_Signal_ThrdStop)
			{ close_id = N_LogID::EN_CLOSE_Normal; }
			else
			{ close_id = N_LogID::EN_CLOSE_Abort; }
			fwrite(&close_id, 1, 1, pgLog->m_pf_cur);
			len = 0;
			fwrite(&len, 1, 2, pgLog->m_pf_cur);
		}
	}
	
	return  NULL;  // スレッドが終了される
}

// ------------------------------------------------------------------
// JS とは別スレッドで実行される（優先度が低いスレッド）

void  KgLog::LogThread()
{
	while (true)
	{
		sem_wait(&m_sem_TA);  // 書き込み合図を受け取るセマフォ

		if (mb_IsUnderErr == EN_bErr::Err) { return; }  // スレッドを終了させる

		pthread_mutex_lock(&m_mutex_TA);
		const size_t  cLK_bytes_dirty = m_TA_dirty.m_bytes;
		if (cLK_bytes_dirty == 0)
		{
			pthread_mutex_unlock(&m_mutex_TA);
			
			if (mb_thrd_stop == false) { continue; }  // セマフォのカウントが残っていただけの場合

			// スレッドの終了通知を受け取ったと考えられる
			return;  // スレッドを終了させる
		}

		const char* const  cLK_cptop_dirty = m_TA_dirty.m_ptr;
		char* const  cLK_pos_next = m_TA_pos_next.Get_Ptr();
		if (cLK_bytes_dirty < mc_bytes_fflush && mb_thrd_stop == false && cLK_cptop_dirty < cLK_pos_next)
		{
			// ディスクへの書き出しが必要でない場合
			pthread_mutex_unlock(&m_mutex_TA);
			continue;
		}
		pthread_mutex_unlock(&m_mutex_TA);

		// -------------------------------------
		// ディスクへの書き込みを実行する
		if (this->WriteToDisk(cLK_cptop_dirty, cLK_bytes_dirty) == EN_bErr::Err) { return; }

		pthread_mutex_lock(&m_mutex_TA);
		
		if (cLK_cptop_dirty < cLK_pos_next)  // 通常処理
		{
			m_TA_dirty.m_ptr += cLK_bytes_dirty;
			m_TA_dirty.m_bytes -= cLK_bytes_dirty;
		}
		else  // cLK_pos_next < cLK_cptop_dirty のときの処理
		{
			m_TA_dirty.m_ptr = mc_pbuf_top;
			m_TA_dirty.m_bytes = m_TA_dirty.m_bytes_onTop;
			m_TA_dirty.m_bytes_onTop = 0;
		}

		pthread_mutex_unlock(&m_mutex_TA);

		if (mb_thrd_stop) { return; }  // このスレッドは終了される
	}
}

// ------------------------------------------------------------------
// psrc から cbytes をディスクへの書き込む。新規ファイルへの移行をチェック
// KgLog のバッファ関連のメンバ変数は操作しない

KgLog::EN_bErr  KgLog::WriteToDisk(const char* const psrc, const size_t cbytes)
{
	const size_t  cbyte_wrt = fwrite(psrc, 1, cbytes, m_pf_cur);
	if (cbyte_wrt < cbytes)  // エラーハンドリング
	{
		mb_IsUnderErr = EN_bErr::Err;
		m_str_info = "KgLog::LogThread(): fwrite() に失敗しました。\n";
		return  EN_bErr::Err;  // 戻り値は mb_IsUnderErr
	}
/*
	if (fflush(m_pf_cur) != 0)
	{
		mb_IsUnderErr = EN_bErr::Err;
		m_str_info = "KgLog::LogThread(): fflush() に失敗しました。\n";
		return EN_bErr::Err;  // このスレッドは終了される
	}
*/
	// ディスク書き込みバイト数のチェック
	m_bytes_wrtn_curFile += cbytes;
	if (m_bytes_wrtn_curFile > mc_bytes_fsize)
	{
		const time_t  ct_now = time(NULL);
		// 現在のファイル作成時刻と２分以内の場合は、新規ファイルを作成しない
		if (ct_now - m_time_t_fname_cur < 120) { return  EN_bErr::OK; }

		// 新しいファイルを作成する
		fclose(m_pf_cur);
		m_bytes_wrtn_curFile = 0;

		// ファイル名の設定
		m_time_t_fname_cur = ct_now;

		const tm* const  cp_tm = localtime(&ct_now);

		const uint32_t  cmonth = cp_tm->tm_mon + 1;
		const uint32_t  cday = cp_tm->tm_mday;

		const uint32_t  chour = cp_tm->tm_hour;
		const uint32_t  cmin = cp_tm->tm_min;

		*(uint32_t*)(mc_pstr_fname_MD) = KSTR4(cmonth / 10, cmonth % 10, cday / 10, cday % 10) + 0x30303030;
		*(uint32_t*)(mc_pstr_fname_HM) = KSTR4(chour / 10, chour % 10, cmin / 10, cmin % 10) + 0x30303030;

		// ファイルオープン
		m_pf_cur = fopen(mc_pstr_fname, "a");
		if (m_pf_cur == NULL)
		{
			mb_IsUnderErr = EN_bErr::Err;
			m_str_info = "fopen() に失敗しました。\n";
			return  EN_bErr::Err;
		}
	}

	return  EN_bErr::OK;
}


/////////////////////////////////////////////////////////////////////
// JS と同じスレッドで実行される（最優先スレッド）
// 戻り値は Err or OK
KgLog::EN_bErr  KgLog::WriteTxt(const char* const p_cstr)  // p_cstr: null 文字で終わる文字列
{
	if (mb_IsUnderErr == EN_bErr::Err) { return  EN_bErr::Err; }

	// バッファに書き込みを実行する
	char* const  pc_next = m_TA_pos_next.Get_Ptr();
	char* const  pc_atNull = G_StrCpy(pc_next, p_cstr, mc_max_bytes_wrt);
	if (*pc_atNull != 0)  // p_cstr が指す文字列がオーバーフローを起こしている
	{
		mb_IsUnderErr = EN_bErr::Err;
		*pc_atNull = 0;  // オーバーフローを起こした文字列を一応保存しておく
		m_str_info = "p_cstr の文字数がオーバーしています。；\n";
		return  EN_bErr::Err;
	}

	*pc_atNull = '\n';  // \0 を \n に書き換えておく
	const size_t  cbytes_wrt = pc_atNull - pc_next + 1;

	const EN_bErr  cret_val = this->Adv_TA_pos_next_onLocked(cbytes_wrt);
	sem_post(&m_sem_TA);
	
	return  cret_val;
}


// ------------------------------------------------------------------

KgLog::EN_bErr  KgLog::WriteTxt_with_HrTime(const char* const p_cstr)  // p_cstr: null 文字で終わる文字列
{
	if (mb_IsUnderErr == EN_bErr::Err) { return  EN_bErr::Err; }

	// -----------------------------
	// 記録する時刻文字列の生成
	uint64_t  ui64_hour;
	uint64_t  ui64_min;

	const time_t  ctime_t = time(NULL);
	if (ctime_t >= m_time_t_top_next_day)
	{
		const tm* const  cp_tm = localtime(&ctime_t);

		const uint32_t  cmonth = cp_tm->tm_mon + 1;
		const uint32_t  cday = cp_tm->tm_mday;
		ui64_hour = cp_tm->tm_hour;  // JST による hour になっている
		ui64_min = cp_tm->tm_min;
		const uint32_t  csec = cp_tm->tm_sec;

		m_ui32_MMDD = KSTR4(cmonth / 10, cmonth % 10, cday / 10, cday % 10) + 0x30303030u;
		m_time_t_top_next_day = ctime_t - ui64_hour * 3600 - ui64_min * 60 - csec + 24 * 60 * 60;
	}
	else
	{
		ui64_min = uint64_t(ctime_t) / 60;
		ui64_hour = ((ui64_min / 60) + 9) % 24;  // +9 は JST にするため
		ui64_min %= 60;
	}
	
	// バッファに HrTime を書き込む
	char* const  cpnext = m_TA_pos_next.Get_Ptr();
	*(uint32_t*)cpnext = m_ui32_MMDD;
	*(uint64_t*)(cpnext + 4) = 0x20'3030'3a30'302d
			+ ((ui64_min % 10) << 40) + ((ui64_min / 10) << 32) + ((ui64_hour % 10) << 16) + ((ui64_hour / 10) << 8);

	// p_cstr の内容を書き込む
	char* const  pc_atNull = G_StrCpy(cpnext + 11, p_cstr, mc_max_bytes_wrt);

	if (*pc_atNull != 0)  // p_cstr が指す文字列がオーバーフローを起こしている
	{
		mb_IsUnderErr = EN_bErr::Err;
		*pc_atNull = 0;  // オーバーフローを起こした文字列を一応保存しておく
		m_str_info = "KgLog::Write_HrTime() ▶ p_cstr の文字数がオーバーしています。；\n";
		return  EN_bErr::Err;
	}

	*pc_atNull = '\n';  // \0 を \n に書き換えておく

	const EN_bErr  cret_val = this->Adv_TA_pos_next_onLocked(pc_atNull - cpnext + 1);
	sem_post(&m_sem_TA);
	
	return  cret_val;
}


// ------------------------------------------------------------------
// ディスクに記録するのは、len + 14（11(UinixTime) +3(logID,len)）bytes となる

KgLog::EN_bErr  KgLog::Write_with_UnixTime(const uint8_t logID, const void* const cp_data, const uint16_t len)
{
	if (mb_IsUnderErr == EN_bErr::Err) { return  EN_bErr::Err; }

	// UNIX タイムを書き込む
	char* const  cpnext = m_TA_pos_next.Get_Ptr();
	*cpnext = N_LogID::EN_UNIX_TIME;  // logID
	*(uint16_t*)(cpnext + 1) = 8;  // len
	*(time_t*)(cpnext + 3) = time(NULL);

	// データを書き込む
	size_t  bytes_wrt;
	*(uint8_t*)(cpnext + 11) = logID;
	if (len <= mc_max_bytes_wrt) // オーバーフローなし
	{
		*(uint16_t*)(cpnext + 12) = len;
		memcpy(cpnext + 14, cp_data, len);
		bytes_wrt = 14 + len;
	}
	else  // オーバーフローあり
	{
		*(uint16_t*)(cpnext + 12) = mc_max_bytes_wrt | N_LogID::EN_FLG_DataSz_PADover;
		memcpy(cpnext + 14, cp_data, mc_max_bytes_wrt);
		bytes_wrt = 14 + mc_max_bytes_wrt;
	}

	const EN_bErr  cret_val = this->Adv_TA_pos_next_onLocked(bytes_wrt);
	sem_post(&m_sem_TA);
	
	return  cret_val;
}

// ------------------------------------------------------------------
// m_TA_pos_next.m_ptr と m_TA_bytes_dirty or m_TA_bytes_dirty_onTop が書き換えられる

KgLog::EN_bErr  KgLog::Adv_TA_pos_next_onLocked(const size_t bytes)
{
	pthread_mutex_lock(&m_mutex_TA);

	// 次に書き込むべき位置の更新
	if (m_TA_dirty.m_ptr <= m_TA_pos_next.m_ptr)  // m_TA_bytes_dirty に加算
	{
		m_TA_dirty.m_bytes += bytes;
		m_TA_pos_next.m_ptr += bytes;

		// 次に書き込むべき位置を設定
		if (mc_pbuf_top_padng <= m_TA_pos_next.m_ptr)
		{
			m_TA_pos_next.m_ptr = mc_pbuf_top;
			m_TA_dirty.m_bytes_onTop = 0;  // 念の為
		}
	}
	else  // m_TA_pos_next.m_ptr < m_TA_dirty.m_ptr であるから、m_TA_bytes_dirty_onTop に加算
	{
		m_TA_dirty.m_bytes_onTop += bytes;
		m_TA_pos_next.m_ptr += bytes;

		// 次に書き込むべき位置をチェック（マイナス値にはならないはずだが、念の為に int64_t にしておく）
		const int64_t  crmn_bytes_buf = m_TA_dirty.m_ptr - m_TA_pos_next.m_ptr;
		if (crmn_bytes_buf < (int64_t)mc_max_bytes_wrt)
		{
			// 書き込みバッファがオーバーフローを起こしたとき（通常は起こり得ないはず）
			// ファイルタイプがテキストであっても、0, 0, 0 の３バイトを書き込んでマークとする
			if (crmn_bytes_buf >= 3)
			{
				*m_TA_pos_next.m_ptr = N_LogID::EN_ERR_BUF_Overflow;
				*(uint16_t*)(m_TA_pos_next.m_ptr + 1) = 0;

				m_TA_pos_next.m_ptr += 3;
				m_TA_dirty.m_bytes_onTop += 3;
			}
			mb_IsUnderErr = EN_bErr::Err;
			m_str_info = "KgLog::Adv_TA_pos_next_onLocked() ▶ 書き込みバッファがオーバーフローしました。\n";
			pthread_mutex_unlock(&m_mutex_TA);
			return  EN_bErr::Err;
		}
	}
	pthread_mutex_unlock(&m_mutex_TA);
	return  EN_bErr::OK;
}

// ------------------------------------------------------------------

KgLog::EN_bErr  KgLog::Adv_pos_next(const uint16_t bytes)
{
	if (mb_IsUnderErr == EN_bErr::Err) { return  EN_bErr::Err; }

	if (bytes > mc_max_bytes_wrt)
	{
		m_str_info = "KgLog::Write_pos_next() ▶ 書き込みバイト数がオーバーしています。；\n";
		return  EN_bErr::Err;
	}

	const EN_bErr  cret_val = this->Adv_TA_pos_next_onLocked(bytes);
	sem_post(&m_sem_TA);
	
	return  cret_val;
}


/////////////////////////////////////////////////////////////////////

KgLog::EN_bErr  KgLog::Signal_ThrdStop()
{
	mb_thrd_stop = true;
	mb_called_Signal_ThrdStop = true;
	sem_post(&m_sem_TA);  // スレッドを return させるための措置
	pthread_join(m_thrd_t, NULL);

	return  mb_IsUnderErr;
}
