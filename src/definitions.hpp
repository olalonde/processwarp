#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace usagi {
  class VMachine;
  class Thread;

  /** 通信プロトコルのメジャーバージョン */
  static const int PROTOCOL_MAJOR_VERSION = 0;
  /** 通信プロトコルのマイナーバージョン */
  static const int PROTOCOL_MINOR_VERSION = 1;

  /** 仮想アドレス */
  typedef std::uint64_t vaddr_t;

  /** VM組み込み関数の型 */
  typedef size_t (*intrinsic_func_t)(VMachine&, Thread&, uint8_t*, std::vector<uint8_t>&);

  /** システム中で扱う最長のuint */
  typedef std::uint64_t longest_uint_t;

  /** システム中で扱う最長のint */
  typedef std::uint64_t longest_int_t;

  /** vmで扱うint */
  typedef std::int64_t vm_int_t;

  /** 最長のuintを1うめしたもの */
  static longest_uint_t LONGEST_UINT_FILL = 0xFFFFFFFFFFFFFFFF;

  /** ライブラリなど外部関数の関数の型 */
  typedef void (*external_func_t)();

  /** NULLのアドレス */
  static const vaddr_t VADDR_NULL = 0x00000000;
  /** アドレスに何も割り当てられていない状態の定数 */
  static const vaddr_t VADDR_NON  = 0x00000000;

  /** 命令 */
  typedef std::uint32_t instruction_t;

  /** スタックの作業用バッファサイズ */
  static const int STACK_BUFFER_SIZE = 2;

  /** 仮想プロセスID */
  typedef std::string vpid_t;

  /** オペランドの最大値 */
  static const instruction_t FILL_OPERAND = 0x03FFFFFF;
  static const instruction_t HEAD_OPERAND = 0x02000000;

  /** VM内のint相当のint型 */
  typedef int64_t vm_int_t;
  /** VM内のint相当のint型 */
  typedef uint64_t vm_uint_t;

  /** メモリの内容ごとに割り当てるアドレスの判定フラグ */
  enum AddrType : vaddr_t {
    AD_TYPE     = 0x0000000000000000, ///< 型 or スタックの相対アドレス
    AD_VALUE_08 = 0x1000000000000000, ///< 0〜255Byte空間
    AD_VALUE_16 = 0x2000000000000000, ///< 256〜65KByte空間
    AD_VALUE_24 = 0x3000000000000000, ///< 65K〜16MByte空間
    AD_VALUE_32 = 0x4000000000000000, ///< 16M〜4GByte空間
    AD_VALUE_40 = 0x5000000000000000, ///< 4G〜1TByte空間
    AD_VALUE_48 = 0x6000000000000000, ///< 1T〜256TByte空間
    AD_CONSTANT = 0x8000000000000000, ///< 定数判定フラグ
    AD_FUNCTION = 0xF000000000000000, ///< 関数
    AD_MASK     = 0xF000000000000000,
  };

  /** プロセスID */
  typedef std::string pid_t;

  /** 関数のタイプ */
  enum FuncType : uint8_t {
    FC_NORMAL       = 0x01, ///< 通常の関数(VMで解釈、実行する)
    FC_INTRINSIC    = 0x02, ///< VM組み込み関数
    FC_EXTERNAL     = 0x03, ///< ライブラリなど外部の関数
  };

  /** 基本型に予約するアドレス */
  enum BasicType : vaddr_t {
    TY_VOID     = 0x0000000000000001, ///< void型
    TY_POINTER  = 0x0000000000000002, ///< ポインタ
    TY_FUNCTION = 0x0000000000000003, ///< 関数
    TY_UI8      = 0x0000000000000011, ///< 8bit整数型
    TY_UI16     = 0x0000000000000012, ///< 16bit整数型
    TY_UI32     = 0x0000000000000013, ///< 32bit整数型
    TY_UI64     = 0x0000000000000014, ///< 64bit整数型
    TY_SI8      = 0x0000000000000021, ///< 8bit整数型
    TY_SI16     = 0x0000000000000022, ///< 16bit整数型
    TY_SI32     = 0x0000000000000023, ///< 32bit整数型
    TY_SI64     = 0x0000000000000024, ///< 64bit整数型
    // TY_F16
    TY_F32      = 0x0000000000000032, ///< 単精度浮動小数点型
    TY_F64      = 0x0000000000000033, ///< 倍精度浮動小数点型
    // TY_F80
    TY_F128     = 0x0000000000000035, ///< 四倍精度浮動小数点型
    TY_MAX      = 0x00000000000000FF, ///< 基本型最大値
  };
  
  /** 仮想マシンのOpcode */
  enum Opcode : uint8_t {
    // ----------------------------------------------------------------------
    // name		args	description
    // ----------------------------------------------------------------------
    NOP = 0,
      EXTRA,
      CALL,
      TAILCALL,
      RETURN,
      UNWIND,
      SET_TYPE,
      SET_OUTPUT,
      SET_VALUE,
      ADD,
      SUB, // 10
      MUL,
      DIV,
      REM,
      SHL,
      SHR,
      AND,
      OR,
      XOR,
      COPY,
      SET_ADR, // 20
      SET_ALIGN,
      ADD_ADR,
      MUL_ADR,
      GET_ADR,
      LOAD,
      STORE,
      ALLOC,
      FREE,
      ALLOCA,
      TEST, // 30
      TEST_EQ,
      JUMP,
      PHI,
      TYPE_CAST,
      BIT_CAST,
      EQUAL,
      NOT_EQUAL,
      GREATER,
      GREATER_EQUAL,
      NANS, // 40
      OR_NANS,
      SELECT,
      VA_ARG,
  };
}