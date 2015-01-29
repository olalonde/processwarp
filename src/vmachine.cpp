
#include <cstring>
#include <memory>

#include <ffi.h>
#include <dlfcn.h>
#include <unistd.h>

#include "error.hpp"
#include "func_store.hpp"
#include "instruction.hpp"
#include "stackinfo.hpp"
#include "type_based.hpp"
#include "util.hpp"
#include "vmachine.hpp"

using namespace usagi;

static TypeBased* TYPE_BASES[] = {
  nullptr, // 0
  nullptr, // 1 void
  new TypePointer(), // 2 pointer
  nullptr, // 3 function
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr, // 10
  new TypeExtended<int8_t>(), // 11 8bit整数型
  new TypeExtended<int16_t>(), // 12 16bit整数型
  new TypeExtended<int32_t>(), // 13 32bit整数型
  new TypeExtended<int64_t>(), // 14 64bit整数型
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr, // 20
  new TypeExtended<uint8_t>(), // 21 8bit整数型
  new TypeExtended<uint16_t>(), // 22 16bit整数型
  new TypeExtended<uint32_t>(), // 23 32bit整数型
  new TypeExtended<uint64_t>(), // 24 64bit整数型
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr, // 30
  nullptr, // 31
  new TypeExtended<float>(), // 32 float
  new TypeExtended<double>(), // 33 double
  nullptr, // 34
  nullptr, // 35 quad
};

struct OperandRet {
  DataStore& data;
  vaddr_t addr;
  uint8_t* cache;
};

struct OperandParam {
  DataStore& stack;
  DataStore& k;
  VMemory& vmemory;
};

inline uint8_t* get_cache(vaddr_t addr, VMemory& vmemory) {
  DataStore& store = vmemory.get_data(addr);
  return store.head.get() + VMemory::get_addr_lower(addr);
}

inline FuncStore& get_function(instruction_t code, OperandParam& param) {
  int operand = Instruction::get_operand(code);
  // 関数は定数領域に置かれているはず
  assert((operand & HEAD_OPERAND) != 0);
  vaddr_t addr = *reinterpret_cast<vaddr_t*>(param.k.head.get() + (FILL_OPERAND - operand));
  return param.vmemory.get_func(addr);
}

inline OperandRet get_operand(instruction_t code, OperandParam& param) {
  int operand = Instruction::get_operand(code);
  print_debug("get_operand %d\n", operand);
  if ((operand & HEAD_OPERAND) != 0) {
    vaddr_t position = (FILL_OPERAND - operand);
    assert(position < param.k.size);
    // 定数の場合1の補数表現からの復元
    return {param.k, param.k.addr + position, param.k.head.get() + position};
    
  } else {
    assert(operand < param.stack.size);
    return {param.stack, param.stack.addr + operand, param.stack.head.get() + operand};
  }
}

inline TypeStore& get_type(instruction_t code, OperandParam& param) {
  int operand = Instruction::get_operand(code);
  // 型は定数領域に置かれているはず
  assert((operand & HEAD_OPERAND) != 0);

  vaddr_t addr = *reinterpret_cast<vaddr_t*>(param.k.head.get() + (FILL_OPERAND - operand));
  return param.vmemory.get_type(addr);
}

// コンストラクタ。
VMachine::VMachine() :
  status(SETUP) {
}

// VM命令を実行する。
void VMachine::execute(int max_clock) {
  Thread& thread = *(threads.front().get());
 re_entry: {
    // ciが1段の場合、終了
    if (thread.stackinfos.size() == 1) return;

    StackInfo& stackinfo = *(thread.stackinfos.back().get());
    resolve_stackinfo_cache(&stackinfo);

    const FuncStore& func = *stackinfo.func_cache;
    const std::vector<instruction_t>& insts = func.normal_prop.code;
    DataStore& k = vmemory.get_data(func.normal_prop.k);
    OperandParam op_param = {*stackinfo.stack_cache, k, vmemory};

    for (; max_clock > 0; max_clock --) {
      instruction_t code = insts.at(stackinfo.pc);
      print_debug("pc:%d, k:%ld, insts:%ld, code:%08x %s\n",
		  stackinfo.pc, k.size / sizeof(vaddr_t),
		  insts.size(), code, Util::code2str(code).c_str());
      //usleep(100000);///< TODO

      // call命令の判定(call命令の場合falseに変える)
      bool is_tailcall = true;

      switch (static_cast<uint8_t>(Instruction::get_opcode(code))) {
      case Opcode::NOP: {
	// 何もしない命令
      } break;
	
      case Opcode::CALL:
	is_tailcall = false;
	
      case Opcode::TAILCALL: {
	std::unique_ptr<StackInfo> new_stackinfo;
	FuncStore& new_func = get_function(code, op_param);

	assert(!is_tailcall); // TODO 動きを確認する。

	// スタックのサイズの有無により作りを変える
	new_stackinfo.reset
	  (new StackInfo(new_func.addr,
			 // tailcallの場合、戻り値の格納先を現行のものから引き継ぐ
			 is_tailcall ? stackinfo.ret_addr : stackinfo.output,
			 Instruction::get_operand(insts.at(stackinfo.pc + 1)),
			 Instruction::get_operand(insts.at(stackinfo.pc + 2)),
			 (new_func.normal_prop.stack_size != 0 ?
			  vmemory.alloc_data(new_func.normal_prop.stack_size, false).addr :
			  VADDR_NON)));

	// 引数を集める
	int args = 0;
	int written_size = 0;
	instruction_t type_inst;
	instruction_t value_inst;
	std::vector<uint8_t> work; // 可変長引数、ネイティブメソッド用引数を一時的に格納する領域
	while (stackinfo.pc + 4 + args * 2 < insts.size() &&
	       Instruction::get_opcode(type_inst  = insts.at(stackinfo.pc + 3 + args * 2))
	       == Opcode::EXTRA &&
	       Instruction::get_opcode(value_inst = insts.at(stackinfo.pc + 4 + args * 2))
	       == Opcode::EXTRA) {

	  const TypeStore& type  = get_type(type_inst, op_param);
	  OperandRet value = get_operand(value_inst, op_param);

	  if (new_func.type == FuncType::FC_NORMAL &&
	      args < new_func.normal_prop.arg_num) {
	    // 通常の引数はスタックの先頭にコピー
	    memcpy(new_stackinfo->stack_cache->head.get() + written_size, value.cache, type.size);
	    written_size += type.size;

	  } else {
	    // 可変長引数、ネイティブメソッド用引数は一時領域に格納
	    std::size_t dest = work.size();
	    work.resize(dest + sizeof(vaddr_t) + type.size);
	    memcpy(work.data() + dest,                   &type.addr,  sizeof(vaddr_t));
	    memcpy(work.data() + dest + sizeof(vaddr_t), value.cache, type.size);
	  }
	  
	  args += 1;
	}

	// pcの書き換え
	stackinfo.pc += args * 2 + 2;

	if (new_func.type == FuncType::FC_NORMAL) {
	  // 可変長引数でない場合、引数の数をチェック
	  if (args < new_func.normal_prop.arg_num ||
	      (!new_func.normal_prop.is_var_arg && args != new_func.normal_prop.arg_num))
	    throw_error(Error::TYPE_VIOLATION);

	  // 可変長引数分がある場合、別領域を作成
	  new_stackinfo->var_arg = v_malloc(work.size(), false);
	  new_stackinfo->alloca_addrs.push_back(new_stackinfo->var_arg);
	  v_memcpy(new_stackinfo->var_arg, work.data(), work.size());
	  
	  if (is_tailcall) {
	    // 末尾再帰の場合、既存のstackinfoを削除
	    // 次の命令はRETURNのはず
	    assert(Instruction::get_opcode(insts.at(stackinfo.pc + 2)) == Opcode::RETURN);
	    thread.stackinfos.pop_back();
	  } else {
	    stackinfo.pc ++;
	    assert(false);
	    // 末尾再帰でない場合、callinfosを追加
	  }
	  thread.stackinfos.push_back(std::unique_ptr<StackInfo>(new_stackinfo.release()));
	  goto re_entry;

	} else if (func.type == FuncType::FC_INTRINSIC) {
	  // VM組み込み関数の呼び出し
	  assert(false);
	  assert(func.intrinsic != nullptr);
	  func.intrinsic(*this, thread, stackinfo.output_cache, work);

	} else { // func.type == FuncType::EXTERNAL
	  // 関数のロードを行っていない場合、ロードする
	  if (new_func.external == nullptr) {
	    new_func.external = get_external_func(new_func.name);
	  }

	  // 関数の呼び出し
	  call_external(new_func.external, new_func.ret_type, stackinfo.output_cache, work);
	}
	
      } break;

      case Opcode::RETURN: {
	StackInfo& upperinfo = *(thread.stackinfos.at(thread.stackinfos.size() - 2).get());

	if (Instruction::get_operand(code) == FILL_OPERAND) {
	  // 戻り値がないので何もしない

	} else {
	  // 戻り値を設定する
	  OperandRet operand = get_operand(code, op_param);
	  
	  stackinfo.type_cache1->copy(upperinfo.output_cache, operand.cache);
	}
	// スタック領域を開放
	vmemory.free(stackinfo.stack);

	// alloca領域を開放
	for (vaddr_t addr : stackinfo.alloca_addrs) {
	  vmemory.free(addr);
	}
	
	// 1段上のスタックのpcを設定(normal_pc)
	upperinfo.pc = stackinfo.normal_pc;

	// stackinfoを1つ除去してre_entryに移動
	thread.stackinfos.pop_back();
	goto re_entry;
      } break;

      case Opcode::SET_TYPE: {
	TypeStore& store = get_type(code, op_param);
	stackinfo.type = store.addr;
	if (store.addr < sizeof(TYPE_BASES) / sizeof(TYPE_BASES[0])) {
	  stackinfo.type_cache1 = TYPE_BASES[store.addr];
	  if (stackinfo.type_cache1 == nullptr) {
	    assert(false); // TODO 未対応の型
	  }

	} else {
	  assert(false); // 拡張型
	}
	stackinfo.type_cache2 = &store;
      } break;

      case Opcode::SET_OUTPUT: {
	OperandRet operand = get_operand(code, op_param);
	stackinfo.output       = operand.addr;
	stackinfo.output_cache = operand.cache;
      } break;

      case Opcode::SET_VALUE: {
	OperandRet operand = get_operand(code, op_param);
	stackinfo.value       = operand.addr;
	stackinfo.value_cache = operand.cache;
      } break;

#define M_BINARY_OPERATOR(name, op)				\
	case Opcode::name: {					\
	  OperandRet operand = get_operand(code, op_param);	\
	  stackinfo.type_cache1->op(stackinfo.output_cache,	\
				    stackinfo.value_cache,	\
				    operand.cache);		\
	} break;

	M_BINARY_OPERATOR(ADD, op_add); // 加算
	M_BINARY_OPERATOR(SUB, op_sub); // 減算
	M_BINARY_OPERATOR(MUL, op_mul); // 乗算
	M_BINARY_OPERATOR(DIV, op_div); // 除算
	M_BINARY_OPERATOR(REM, op_rem); // 剰余
	M_BINARY_OPERATOR(SHL, op_shl); // 左シフト
	M_BINARY_OPERATOR(SHR, op_shr); // 右シフト
	M_BINARY_OPERATOR(AND, op_and); // and
	M_BINARY_OPERATOR(OR,  op_or);  // or
	M_BINARY_OPERATOR(XOR, op_xor); // xor

#undef M_BINARY_OPERATOR

      case Opcode::SET_ADR: {
	OperandRet operand = get_operand(code, op_param);
	stackinfo.address = *reinterpret_cast<vaddr_t*>(operand.cache);
	stackinfo.address_cache = get_cache(stackinfo.address, vmemory);
	print_debug("address = %016llx\n", stackinfo.address);
      } break;

      case Opcode::SET_ALIGN: {
	int operand = Instruction::get_operand_value(code);
	stackinfo.alignment = operand;
      } break;

      case Opcode::ADD_ADR: {
	int operand = Instruction::get_operand_value(code);
	stackinfo.address += operand;
	stackinfo.address_cache += operand;
	print_debug("+%d address = %016llx\n", operand, stackinfo.address);
      } break;

      case Opcode::MUL_ADR: {
	int operand = Instruction::get_operand_value(code);
	const vm_int_t diff = operand * stackinfo.type_cache1->get(stackinfo.value_cache);
	stackinfo.address += diff;
	stackinfo.address_cache += diff;
	print_debug("+%d * %lld address = %16llx\n",
		    operand, stackinfo.type_cache1->get(stackinfo.value_cache), stackinfo.address);
      } break;

      case Opcode::GET_ADR: {
	OperandRet operand = get_operand(code, op_param);
	*reinterpret_cast<vaddr_t*>(operand.cache) = stackinfo.address;
	print_debug("*%016llx = %016llx\n", operand.addr, stackinfo.address);
      } break;

      case Opcode::LOAD: {
	OperandRet operand = get_operand(code, op_param);
	memcpy(operand.cache, stackinfo.address_cache, stackinfo.type_cache2->size);
	print_debug("*%016llx = *%016llx(size = %ld)\n",
		    operand.addr, stackinfo.address, stackinfo.type_cache2->size);
      } break;

      case Opcode::STORE: {
	OperandRet operand = get_operand(code, op_param);
	memcpy(stackinfo.address_cache, operand.cache, stackinfo.type_cache2->size);
      } break;

      case Opcode::JUMP: {
	stackinfo.phi0 = stackinfo.phi1;
	stackinfo.phi1 = stackinfo.pc = Instruction::get_operand(code);
	continue;
      } break;

      case Opcode::PHI: {
	instruction_t code2 = insts.at(stackinfo.pc + 1);
	int count = 0;
	while ((Instruction::get_opcode(code) == Opcode::PHI ||
		Instruction::get_opcode(code) == Opcode::EXTRA)) {
	  // PHI命令はEXTRA含め、偶数個
	  if (Instruction::get_opcode(code2) != Opcode::EXTRA) {
	    throw_error(Error::INST_VIOLATION);
	  }

	  if (stackinfo.phi0 == Instruction::get_operand(code2)) {
	    OperandRet operand = get_operand(code, op_param);
	    stackinfo.type_cache1->copy(stackinfo.output_cache, operand.cache);
	  }
	  count += 2;
	  code  = insts.at(stackinfo.pc + count);
	  code2 = insts.at(stackinfo.pc + count + 1);
	}
	stackinfo.pc += count - 1;
		      
      } break;

      case Opcode::TYPE_CAST: {
	TypeStore& type = get_type(code, op_param);
	stackinfo.type_cache1->type_cast(stackinfo.output_cache,
					 type.addr,
					 stackinfo.value_cache);
      } break;

      case Opcode::BIT_CAST: {
	TypeStore& type = get_type(code, op_param);
	stackinfo.type_cache1->bit_cast(stackinfo.output_cache,
					type.size,
					stackinfo.value_cache);
      } break;

      default: {
	// EXTRAARGを含む想定外の命令
	throw_error_message(Error::INST_VIOLATION, Util::num2hex_str(insts.at(stackinfo.pc)));
      } break;
      }
      
      stackinfo.pc ++;
    }
  }
}

// 外部の関数を呼び出す。
void VMachine::call_external(external_func_t func,
			     vaddr_t ret_type,
			     uint8_t* ret_addr,
			     std::vector<uint8_t>& args) {
  print_debug("call_external\n");

  // 戻り値の型変換
  ffi_type* ffi_ret_type = nullptr;
  switch(ret_type) {
  case BasicType::TY_UI8:  ffi_ret_type = &ffi_type_uint8;  break;
  case BasicType::TY_UI16: ffi_ret_type = &ffi_type_uint16; break;
  case BasicType::TY_UI32: ffi_ret_type = &ffi_type_uint32; break;
  case BasicType::TY_UI64: ffi_ret_type = &ffi_type_uint64; break;
  case BasicType::TY_SI8:  ffi_ret_type = &ffi_type_sint8;  break;
  case BasicType::TY_SI16: ffi_ret_type = &ffi_type_sint16; break;
  case BasicType::TY_SI32: ffi_ret_type = &ffi_type_sint32; break;
  case BasicType::TY_SI64: ffi_ret_type = &ffi_type_sint64; break;

  default: {
    fixme(Util::vaddr2str(ret_type));
    assert(false); // TODO 他の型の対応
  } break;
  }

  // 引数の型変換 & ポインタ格納
  std::vector<ffi_type*> ffi_arg_types;
  std::vector<void*> ffi_args;

  int seek = 0;
  while(seek < args.size()) {
    TypeStore& type = vmemory.get_type(*reinterpret_cast<vaddr_t*>(args.data() + seek));

    switch(type.addr) {
    case BasicType::TY_POINTER: {
      ffi_arg_types.push_back(&ffi_type_pointer);
      vaddr_t addr = *reinterpret_cast<vaddr_t*>(args.data() + seek + sizeof(vaddr_t));
      DataStore& pointed = vmemory.get_data(addr);
      *reinterpret_cast<void**>(args.data() + seek + sizeof(vaddr_t)) =
	pointed.head.get() + VMemory::get_addr_lower(addr);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_UI8: {
      ffi_arg_types.push_back(&ffi_type_uint8);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_UI16: {
      ffi_arg_types.push_back(&ffi_type_uint16);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_UI32: {
      ffi_arg_types.push_back(&ffi_type_uint32);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_UI64: {
      ffi_arg_types.push_back(&ffi_type_uint64);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;
      
    case BasicType::TY_SI8: {
      ffi_arg_types.push_back(&ffi_type_sint8);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_SI16: {
      ffi_arg_types.push_back(&ffi_type_sint16);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_SI32: {
      ffi_arg_types.push_back(&ffi_type_sint32);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    case BasicType::TY_SI64: {
      ffi_arg_types.push_back(&ffi_type_sint64);
      ffi_args.push_back(args.data() + seek + sizeof(vaddr_t));
    } break;

    default: {
      fixme(Util::vaddr2str(type.addr));
      assert(false); // TODO 他の型の対応
    } break;
    }
    
    seek += sizeof(vaddr_t) + type.size;
  }

  // libffiの準備
  ffi_cif cif;
  ffi_status status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI, ffi_args.size(),
				   ffi_ret_type, ffi_arg_types.data());

  if (status != FFI_OK) {
    throw_error_message(Error::EXT_CALL, Util::num2hex_str(status));
  }

  // メソッド呼び出し
  ffi_call(&cif, func, ret_addr, ffi_args.data());
}

// 型のサイズと最大アライメントを計算する。
std::pair<size_t, unsigned int> VMachine::calc_type_size(const std::vector<vaddr_t>& member) {
  size_t size = 0;
  unsigned int max_alignment = 0;
  unsigned int odd;

  for (int i = 0, member_size = member.size(); i < member_size; i ++) {
    TypeStore& type = vmemory.get_type(member.at(i));
    // メンバ中で一番大きなアライメントを保持
    if (type.alignment > max_alignment) max_alignment = type.alignment;
    // パディングを計算する
    if ((odd = size % type.alignment) != 0) size = size - odd + type.alignment;
    // サイズ分を追加
    size += type.size;
  }
  // 一番大きなアライメントで最後に調整
  if ((odd = size % max_alignment) != 0) size = size - odd + max_alignment;

  return std::make_pair(size, max_alignment);
}

// 型のサイズと最大アライメントを計算する。
std::pair<size_t, unsigned int> VMachine::calc_type_size(vaddr_t type) {
  TypeStore& t = vmemory.get_type(type);
  
  return std::make_pair(t.size, t.alignment);
}

// VMの終了処理を行う。
void VMachine::close() {
  // ロードした外部のライブラリを閉じる
  /*
    for (auto it = ext_libs.begin(); it != ext_libs.end(); it ++) {
    dlclose(*it);
    }
  //*/
}

// 基本型情報を作成する。
TypeStore& VMachine::create_type(BasicType type) {
  // 型のアドレスを持つValueを作成
  print_debug("create_type(basic)\n");
  print_debug("\taddr\t:%016llx\n", type);
  
  return vmemory.get_type(type);
}

// 配列型情報を作成する。
TypeStore& VMachine::create_type(vaddr_t element, unsigned int num) {
  // サイズ、アライメントを計算
  std::vector<vaddr_t> member;
  member.push_back(element);
  std::pair<size_t, unsigned int> info = calc_type_size(member);
  
  // 領域を確保
  return vmemory.alloc_type(info.first * num, info.second, element, num);
}

// 複合型情報を作成する。
TypeStore& VMachine::create_type(const std::vector<vaddr_t>& member) {
  // 領域を確保
  std::pair<size_t, unsigned int> info = calc_type_size(member);
  return vmemory.alloc_type(info.first, info.second, member);
}

// ネイティブ関数を指定アドレスに展開する。
void VMachine::deploy_function(const std::string& name, vaddr_t ret_type, vaddr_t addr) {
  auto ifunc = intrinsic_funcs.find(name);
  if (ifunc == intrinsic_funcs.end()) {
    // 組み込み関数に名前がなかった場合、ライブラリ関数として展開。
    deploy_function_external(name, ret_type, addr);

  } else {
    // 組み込み関数に名前があった場合組み込み関数として展開。
    deploy_function_intrinsic(name, ret_type, addr);
  }
}

// ライブラリ関数を指定アドレスに展開する。
void VMachine::deploy_function_external(const std::string& name, vaddr_t ret_type, vaddr_t addr) {
  auto ifunc = intrinsic_funcs.find(name);

  // VM組み込み関数と同じ名前は使えない
  if (ifunc != intrinsic_funcs.end()) {
    throw_error_message(Error::EXT_LIBRARY, name);
  }

  // 関数領域を確保
  vmemory.alloc_func(symbols.get(name), ret_type, addr);
}

// VM組み込み関数を指定アドレスに展開する。
void VMachine::deploy_function_intrinsic(const std::string& name, vaddr_t ret_type, vaddr_t addr) {
  auto ifunc = intrinsic_funcs.find(name);

  // VM組み込み関数に指定の名前の関数がない。
  if (ifunc == intrinsic_funcs.end()) {
    throw_error_message(Error::EXT_LIBRARY, name);
  }

  // 関数領域を確保
  vmemory.alloc_func(symbols.get(name), ret_type, ifunc->second, addr);
}

// 通常の関数(VMで解釈、実行する)を指定アドレスに展開する。
void VMachine::deploy_function_normal(const std::string& name,
				      vaddr_t ret_type,
				      const FuncStore::NormalProp& prop,
				      vaddr_t addr) {
  // 関数領域を確保
  vmemory.alloc_func(symbols.get(name), ret_type, prop, addr);
}

// ライブラリなど、外部の関数へのポインタを取得する。
external_func_t VMachine::get_external_func(const Symbols::Symbol& name) {
  print_debug("get external func:%s\n", name.str().c_str());
  external_func_t func = reinterpret_cast<external_func_t>(dlsym(RTLD_NEXT, name.str().c_str()));

  // エラーを確認
  char* error;
  if ((error = dlerror()) != nullptr) {
    throw_error_message(Error::EXT_LIBRARY, error);
  }

  return func;
}

// 仮想アドレスに相当する実アドレスを取得する。
uint8_t* VMachine::get_raw_addr(vaddr_t addr) {
  DataStore& store = vmemory.get_data(addr);
  // アクセス違反を確認する
  if (VMemory::get_addr_lower(addr) > store.size) {
    throw_error_message(Error::SEGMENT_FAULT, Util::vaddr2str(addr));
  }
  return reinterpret_cast<uint8_t*>(store.head.get() + VMemory::get_addr_lower(addr));
}

// StackInfoのキャッシュを解決し、実行前の状態にする。
void VMachine::resolve_stackinfo_cache(StackInfo* target) {
  // 関数
  if (target->func != VADDR_NON) {
    target->func_cache = &vmemory.get_func(target->func);
  } else {
    target->func_cache = nullptr;
  }
  // スタック領域
  if (target->stack != VADDR_NON) {
    target->stack_cache = &vmemory.get_data(target->stack);
  } else {
    target->stack_cache = nullptr;
  }
  // 操作対象の型
  if (target->type != VADDR_NON) {
    target->type_cache2 = &vmemory.get_type(target->type);
    if (target->type < sizeof(TYPE_BASES) / sizeof(TYPE_BASES[0])) {
      target->type_cache1 = TYPE_BASES[target->type];
      if (target->type_cache1 == nullptr) {
	assert(false); // TODO 未対応の型
      }
    } else {
      assert(false); // 拡張型
    }
  } else {
    target->type_cache1 = nullptr;
    target->type_cache2 = nullptr;
  }
  // 格納先アドレス
  if (target->output != VADDR_NON) {
    target->output_cache = get_cache(target->output, vmemory);
  } else {
    target->output_cache = nullptr;
  }
  // 値レジスタ
  if (target->value != VADDR_NON) {
    target->value_cache = get_cache(target->value, vmemory);
  } else {
    target->value_cache = nullptr;
  }
  // アドレスレジスタ
  if (target->address != VADDR_NON) {
    target->address_cache = get_cache(target->address, vmemory);
  } else {
    target->address_cache = nullptr;
  }
}

// 関数のアドレスを予約する。
vaddr_t VMachine::reserve_func_addr() {
  return vmemory.reserve_func_addr();
}

// VMの初期設定をする。
void VMachine::run(std::vector<std::string> args) {
  // 最初のスレッドを作成
  Thread* init_thread;
  threads.push_back(std::unique_ptr<Thread>(init_thread = new Thread()));
  
  // スレッドを初期化

  // main関数の取得
  auto it_main_func = globals.find(&symbols.get("main"));
  if (it_main_func == globals.end())
    throw_error_message(Error::SYM_NOT_FOUND, "main");
  FuncStore& main_func = vmemory.get_func(it_main_func->second);

  // main関数用のスタックを確保する
  DataStore& main_stack = vmemory.alloc_data(main_func.normal_prop.stack_size, false);
  
  // maink関数の内容に応じて、init_stackを作成する
  DataStore* init_stack;
  if (main_func.normal_prop.arg_num == 2) {
    // main関数の戻り値と引数を格納するのに必要な領域サイズを計算
    const size_t ret_size = calc_type_size(main_func.ret_type).first;
    size_t init_stack_size = ret_size + sizeof(vaddr_t) * args.size();
    for (unsigned int i = 0, arg_size = args.size(); i < arg_size; i ++) {
      init_stack_size += args.at(i).length() + 1;
    }
    // 領域を確保
    init_stack = &vmemory.alloc_data(init_stack_size, false);

    // init_stack_dataにmain関数の戻り値、argvとして渡すポインタの配列、引数文字列、、を格納する
    vaddr_t sum = ret_size + sizeof(vaddr_t) * args.size();
    for (unsigned int i = 0, arg_size = args.size(); i < arg_size; i ++) {
      vaddr_t addr = init_stack->addr + sum;
      memcpy(init_stack->head.get() + ret_size + sizeof(vaddr_t) * i,
	     &addr, sizeof(vaddr_t));
      memcpy(init_stack->head.get() + sum,
	     args.at(i).c_str(), args.at(i).length() + 1);
      sum += args.at(i).length() + 1;
    }
    // main関数のスタックの先頭にargc, argvを格納する
    vm_int_t argc = args.size();
    vaddr_t  argv = init_stack->addr + ret_size;
    memcpy(main_stack.head.get(), &argc, sizeof(argc));
    memcpy(main_stack.head.get() + 4, &argv, sizeof(argv));
    
  } else if (main_func.normal_prop.arg_num != 0) {
    // int main()でもint main(int, char**)でもないようだ
    throw_error(Error::SPEC_VIOLATION);

  } else {
    // int main()の場合は引数を設定しない
    // main関数の戻り値格納先を確保する
    init_stack = &vmemory.alloc_data(calc_type_size(main_func.ret_type).first, false);
  }

  // mainのreturnを受け取るためのスタックを1段確保する
  StackInfo* init_stackinfo = new StackInfo(VADDR_NON, VADDR_NON, 0, 0, init_stack->addr);
  init_stackinfo->output = init_stack->addr;
  init_stackinfo->output_cache = init_stack->head.get();
  init_thread->stackinfos.push_back(std::unique_ptr<StackInfo>(init_stackinfo));
  
  StackInfo* main_stackinfo = new StackInfo(main_func.addr,
					    init_stack->addr,
					    0, 0, main_stack.addr);
  init_thread->stackinfos.push_back(std::unique_ptr<StackInfo>(main_stackinfo));

  status = ACTIVE;
}

// 大域変数のアドレスを設定する。
void VMachine::set_global_value(const std::string& name, vaddr_t addr) {
  /// @todo 同一の名前が再利用されないことの確認
  globals.insert(std::make_pair(&symbols.get(name), addr));
}

// VMの初期設定をする。
void VMachine::setup() {
  // BasicTypeのメンバをsize0, alignment0のTY_VOIDにしておくことで、getelementptrの
  // 計算で余計に計算しても問題が起きない
  std::vector<vaddr_t> basic_type_dummy;
  basic_type_dummy.push_back(BasicType::TY_VOID);

#define M_ALLOC_BASIC_TYPE(s, a, t)			\
  vmemory.alloc_type((s), (a), basic_type_dummy, (t));	\
  intrinsic_addrs.insert(t)

  // 基本型を登録
  M_ALLOC_BASIC_TYPE(0,  0,  BasicType::TY_VOID);
  M_ALLOC_BASIC_TYPE(8,  8,  BasicType::TY_POINTER);
  M_ALLOC_BASIC_TYPE(8,  8,  BasicType::TY_FUNCTION);
  M_ALLOC_BASIC_TYPE(1,  1,  BasicType::TY_UI8);
  M_ALLOC_BASIC_TYPE(2,  2,  BasicType::TY_UI16);
  M_ALLOC_BASIC_TYPE(4,  4,  BasicType::TY_UI32);
  M_ALLOC_BASIC_TYPE(8,  8,  BasicType::TY_UI64);
  M_ALLOC_BASIC_TYPE(1,  1,  BasicType::TY_SI8);
  M_ALLOC_BASIC_TYPE(2,  2,  BasicType::TY_SI16);
  M_ALLOC_BASIC_TYPE(4,  4,  BasicType::TY_SI32);
  M_ALLOC_BASIC_TYPE(8,  8,  BasicType::TY_SI64);
  M_ALLOC_BASIC_TYPE(4,  4,  BasicType::TY_F32);
  M_ALLOC_BASIC_TYPE(8,  8,  BasicType::TY_F64);
  M_ALLOC_BASIC_TYPE(16, 16, BasicType::TY_F128);

#undef M_ALLOC_BASIC_TYPE

  // Cの標準ライブラリをロード
  /*
    void* dl_handle = dlopen(, RTLD_LAZY);
    if (!dl_handle) {
    throw_error_message(Error::EXT_LIBRARY, dlerror());
    }
  //*/
}

// ワープ後のVMの設定をする。
void VMachine::setup_continuous() {
}

// データ領域を確保する。
vaddr_t VMachine::v_malloc(size_t size, bool is_const) {
  return vmemory.alloc_data(size, is_const).addr;
}

// データ領域へ実データをコピーする。
void VMachine::v_memcpy(vaddr_t dst, void* src, size_t n) {
  DataStore& store = vmemory.get_data(dst);
  // アクセス違反をチェック
  if (VMemory::get_addr_lower(dst) + n > store.size) {
    throw_error_message(Error::SEGMENT_FAULT, Util::vaddr2str(dst));
  }
  memcpy(store.head.get(), src, n);
}

// データ領域を指定の数値で埋める。
void VMachine::v_memset(vaddr_t dst, int c, size_t len) {
  DataStore& store = vmemory.get_data(dst);
  // アクセス違反をチェック
  if (VMemory::get_addr_lower(dst) + len > store.size) {
    throw_error_message(Error::SEGMENT_FAULT, Util::vaddr2str(dst));
  }
  memset(store.head.get(), c, len);
}
