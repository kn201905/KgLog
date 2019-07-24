#pragma once

//////////////////////////////////////////////////////////////////////////
// 文字列バッファサイズのカウント

// CE_StrBuf は「/0」を含む
constexpr int  CE_StrBuf(const char* const cpcstr)
{
	const char*  pbuf = cpcstr;
	while (*pbuf++ != 0);
	return  (pbuf - cpcstr);
}

// CE_StrLen_wo0 は「/0」はカウントに入れない
constexpr int  CE_StrLen(const char * const cpcstr)
{
	const char*  pbuf = cpcstr;
	while (*pbuf != 0) { pbuf++; }
	return  (pbuf - cpcstr);
}

// 関数の引数に constexpr を利用したい場合は以下を利用する
// 1. constexpr 関数の戻り値を定数化するには、constexpr 変数で受ける必要がある
// 2. 引数に const char* const を利用すると、constexpr としてコンパイルされない
// 3. CE_PSTR には、constexpr const char* const を渡したほうがよい（cpp ファイルで static 宣言したものを想定）
//　　constexpr がなくてもコンパイル時コンパイルをしてくれるが、エディタ上で警告が表示される
// 注意：Debug 版では、オーバーヘッドが大きくなるため注意
#define CE_F_StrBuf(CE_PSTR) ([&](){constexpr int ret_val = CE_StrBuf(CE_PSTR); return ret_val;}())
#define CE_F_StrLen(CE_PSTR) ([&](){constexpr int ret_val = CE_StrLen(CE_PSTR); return ret_val;}())

