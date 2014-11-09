#pragma once

#include <string>
#include <sstream>
#include <iostream>

namespace usagi {
  class Value;

  class Util {
  public:
    /**
     * 数字を10進数表現で文字列に変換。
     * @param v 変換元数値
     * @return 変換後文字列
     */
    template<class T> static inline std::string num2dec_str(T v) {
      return std::to_string(v);
    }

    /**
     * 数字を16進数表現で文字列に変換。
     * @param v 変換元数値
     * @return 変換後文字列
     */
    template<class T> static inline std::string num2hex_str(T v) {
      std::ostringstream os;
      os << std::hex << v;
      return os.str();
    }

    /**
     * 未実装機能を表すメソッド。
     * 画面にメッセージを出力する。
     * @param mesg 画面に出力するメッセージ
     */
#ifdef NDEBUG
#define fixme(mesg) //
#else
#define fixme(mesg) Util::_fixme(__LINE__, __FILE__, mesg);
#endif
    static void _fixme(long line, const char* file, std::string mesg);

    /**
     * デバッグ情報出力用関数
     */
#ifdef NDEBUG
#define print_debug(...) //
#else
#define print_debug(...) fprintf(stderr, "\x1b[36mdebug\x1b[39m [" __FILE__ "] " __VA_ARGS__)
#endif
  };
}
