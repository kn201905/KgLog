#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <unistd.h>  // seeep() のため

#include "__general.h"
#include "KgLog.h"
#include "_KString.h"

#include <assert.h>


/////////////////////////////////////////////////////////////////////

KgLog::KgLog(const size_t bytes_buf, const size_t max_bytes_msg
		, const char* const pstr_fbody, const size_t bytes_file, const size_t bytes_fflush)
	: mc_pbuf_top{ new char[bytes_buf] }
	, mc_pbuf_tmnt{ mc_pbuf_top + bytes_buf }
	, mc_pbuf_top_padng{ mc_pbuf_tmnt - max_bytes_msg - 1 }  // -1 : 自動付加の末尾 LF
	, mc_max_bytes_msg{ max_bytes_msg }

	, mc_fbody_len{ strlen(pstr_fbody) }
	, mc_pstr_fname{ new char[mc_fbody_len + 15] }
	, mc_pstr_fname_MD{ mc_pstr_fname + mc_fbody_len + 1 }
	, mc_pstr_fname_HM{ mc_pstr_fname + mc_fbody_len + 6 }
	, mc_bytes_fsize{ bytes_file }
	, mc_bytes_fflush{ bytes_fflush }
{
	assert(max_bytes_msg < bytes_fflush);
	assert(bytes_buf > bytes_fflush * 3);

	// mc_pstr_fname の初期設定
	strncpy(mc_pstr_fname, pstr_fbody, mc_fbody_len);
	*(mc_pstr_fname_MD - 1) = '_';
	*(mc_pstr_fname_HM - 1) = '_';
	*(uint32_t*)(mc_pstr_fname_HM + 4) = KSTR4('.', 'l', 'o', 'g');
	*(mc_pstr_fname_HM + 8) = 0;

	// ファイル名の設定
	const time_t  ct = time(NULL);
	m_time_t_fname_cur = ct;  // mc_pstr_fname に対応する time_t値 を保存

	const tm* const  cp_tm = localtime(&ct);

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
	m_TA_top_dirty.m_ptr = mc_pbuf_top;
	m_TA_pos_next.m_ptr = mc_pbuf_top;

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
	}

	pthread_join(m_thrd_t, NULL);

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

	if (pgLog->mb_called_Signal_ThrdStop)
	{
		fwrite(s_cpstr_called_Signal_ThrdStrop, 1, CE_StrLen(s_cpstr_called_Signal_ThrdStrop), pgLog->m_pf_cur);
	}
	else
	{
		fwrite(s_cpstr_abort_Thrd, 1, CE_StrLen(s_cpstr_abort_Thrd), pgLog->m_pf_cur);
	}
	return  NULL;  // スレッドが終了される
}

// ------------------------------------------------------------------
// JS とは別スレッドで実行される（優先度が低いスレッド）

void  KgLog::LogThread()
{
	while (true)
	{
		sem_wait(&m_sem_TA);

		if (mb_IsUnderErr == EN_bErr::Err) { return; }  // スレッドを終了させる

		pthread_mutex_lock(&m_mutex_TA);
		const size_t  cLK_bytes_dirty = m_TA_bytes_dirty;
		if (cLK_bytes_dirty == 0)
		{
			pthread_mutex_unlock(&m_mutex_TA);
			
			if (mb_thrd_stop == false) { continue; }  // セマフォのカウントが残っていただけの場合

			// スレッドの終了通知を受け取ったと考えられる
			return;  // スレッドを終了させる
		}

		const char* const  cL_cptop_dirty = m_TA_top_dirty.m_ptr;
		char* const  cL_pos_next = m_TA_pos_next.Get_Ptr();
		if (cLK_bytes_dirty < mc_bytes_fflush && mb_thrd_stop == false && cL_cptop_dirty < cL_pos_next)
		{
			pthread_mutex_unlock(&m_mutex_TA);
			continue;
		}

		// ディスクへの書き込みを実行する
		if (cL_cptop_dirty < cL_pos_next)  // 通常処理
		{
			m_TA_top_dirty.m_ptr = cL_pos_next;
			m_TA_bytes_dirty = 0;
		}
		else  // cL_pos_next < cL_cptop_dirty のときの処理
		{
			m_TA_top_dirty.m_ptr = mc_pbuf_top;
			m_TA_bytes_dirty = m_TA_bytes_dirty_onTop;
			m_TA_bytes_dirty_onTop = 0;
		}
		pthread_mutex_unlock(&m_mutex_TA);

		// -------------------------------------
		// ディスクへの書き込みを実行する
		if (this->WriteToDisk(cL_cptop_dirty, cLK_bytes_dirty) == EN_bErr::Err) { return; }

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
KgLog::EN_bErr  KgLog::Write(const char* const p_cstr)  // p_cstr: null 文字で終わる文字列
{
	if (mb_IsUnderErr == EN_bErr::Err) { return  EN_bErr::Err; }

	pthread_mutex_lock(&m_mutex_TA);
	{
		// バッファに書き込みを実行する
		char*  p_atNull = stpncpy(m_TA_pos_next.m_ptr, p_cstr, mc_max_bytes_msg);
		*p_atNull++ = '\n';
		const size_t  cbytes_toDirty = p_atNull - m_TA_pos_next.m_ptr;

		if (cbytes_toDirty > mc_max_bytes_msg)  // p_cstr が指す文字列がオーバーフローを起こしている
		{
			pthread_mutex_unlock(&m_mutex_TA);

			mb_IsUnderErr = EN_bErr::Err;
			*(m_TA_pos_next.m_ptr + mc_max_bytes_msg) = 0;
			m_str_info = "cbytes_toDirty > mc_max_bytes_msg となりました。\n";
			return  EN_bErr::Err;
		}

		if (m_TA_top_dirty.Get_Ptr() <= m_TA_pos_next.m_ptr)  // m_TA_bytes_dirty に加算
		{
			m_TA_bytes_dirty += cbytes_toDirty;

			// 次に書き込むべき位置を設定
			if (p_atNull < mc_pbuf_top_padng)
			{
				m_TA_pos_next.m_ptr += cbytes_toDirty;
			}
			else  // mc_pbuf_top_padng <= p_atNull であるとき
			{
				m_TA_pos_next.m_ptr = mc_pbuf_top;
				m_TA_bytes_dirty_onTop = 0;  // 念の為
			}
		}
		else  // m_TA_pos_next.m_ptr <= m_TA_top_dirty.Get_Ptr() であるから、m_TA_bytes_dirty_onTop に加算
		{
			m_TA_bytes_dirty_onTop += cbytes_toDirty;

			// 次に書き込むべき位置をチェック
			m_TA_pos_next.m_ptr += cbytes_toDirty;
			if (m_TA_top_dirty.Get_Ptr() <= m_TA_pos_next.m_ptr + mc_max_bytes_msg)
			{
				pthread_mutex_unlock(&m_mutex_TA);

				mb_IsUnderErr = EN_bErr::Err;
				m_str_info = "書き込みバッファがオーバーフローしました。\n";
				return  EN_bErr::Err;
			}
		}
	}
	pthread_mutex_unlock(&m_mutex_TA);

	sem_post(&m_sem_TA);
	
	return  EN_bErr::OK;
}


/////////////////////////////////////////////////////////////////////

void  KgLog::Signal_ThrdStop()
{
	mb_thrd_stop = true;
	mb_called_Signal_ThrdStop = true;
	sem_post(&m_sem_TA);  // スレッドを return させるための措置
}
