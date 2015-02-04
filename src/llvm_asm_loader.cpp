
#include <iostream>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>

#include "error.hpp"
#include "instruction.hpp"
#include "llvm_asm_loader.hpp"
#include "util.hpp"
#include "vmachine.hpp"

using namespace usagi;

// コンストラクタ。
LlvmAsmLoader::LlvmAsmLoader(VMachine& vm_) :
  context(llvm::getGlobalContext()),
  vm(vm_) {
  }

// デストラクタ。
LlvmAsmLoader::~LlvmAsmLoader() {
  llvm::llvm_shutdown();
}

// LLVMのアセンブリファイルを読み込んで仮想マシンにロードする。
void LlvmAsmLoader::load_file(const std::string& filename) {
  llvm::SMDiagnostic error;
  llvm::Module* module = llvm::ParseAssemblyFile(filename, error, context);
  if (module == nullptr)
    throw_error_message(Error::PARSE, error.getMessage().str() + "[" + filename + "]");
  load_module(module);
}

// ロード済みの値とアドレスの対応関係を登録する。
void LlvmAsmLoader::assign_loaded(FunctionContext& fc, const llvm::Value* v) {
  // グローバル変数として確保されているか確認
  if (map_global.find(v) != map_global.end()) {
    // 登録済LLVM変数が再度登録されることはない
    assert(loaded_global.find(v) == loaded_global.end());
    loaded_global.insert(v);
    return;

  } else {
    assert(false);
  }
}

// LLVMの型に対応するオペランドを取得する。
int LlvmAsmLoader::assign_type(FunctionContext& fc, const llvm::Type* t, bool sign) {
  // ローカル定数に割り当てられている場合、そのまま戻す。
  if (fc.loaded_type.find(t) != fc.loaded_type.end()) {
    return fc.loaded_type.at(t);
  }

  // 新規割り当て
  vaddr_t type = load_type(t, sign);
  int k = fc.k.size();
  fc.loaded_type.insert(std::make_pair(t, -k - 1));
  fc.k.resize(k + sizeof(vaddr_t));
  *reinterpret_cast<vaddr_t*>(&fc.k.at(k)) = type;
  
  return -k - 1;
}

// LLVMの変数に対応する変数の番号を取得する。
int LlvmAsmLoader::assign_operand(FunctionContext& fc, const llvm::Value* v) {
  // 関数の引数は定数でもローカル変数に積むため、
  // ローカル変数判定を先に行う。
  // 既存のローカル変数の場合、その番号を戻す。
  auto sv_find = fc.stack_value.find(v);
  if (sv_find != fc.stack_value.end()) {
    return sv_find->second;
  }

  if (llvm::Constant::classof(v)) {
    assert(data_layout->getTypeAllocSize(v->getType()) != 0);
    assert(data_layout->getTypeStoreSize(v->getType()) ==
	   data_layout->getTypeAllocSize(v->getType()));
    
    // 既存の定数の場合、その番号を戻す。
    if (fc.loaded_value.find(v) != fc.loaded_value.end()) {
      return fc.loaded_value.at(v);
    }

    int size = data_layout->getTypeAllocSize(v->getType());
    // 既存の定数でない場合、末尾に割り当てる。
    int k = fc.k.size();
    ValueDest dst;
    dst.is_k = true;
    dst.addr.k = k;
    fc.k.resize(k + size);
    fc.loaded_value.insert(std::make_pair(v, -k - 1));
    load_constant(fc, dst, static_cast<const llvm::Constant*>(v));

    return -k - 1;

  } else {
    // 既存のローカル変数でない場合、末尾に割り当てている。
    fc.stack_value.insert(std::make_pair(v, fc.stack_sum));

    assert(data_layout->getTypeAllocSize(v->getType()) != 0);
    assert(data_layout->getTypeStoreSize(v->getType()) ==
	   data_layout->getTypeAllocSize(v->getType()));
    fc.stack_sum += data_layout->getTypeStoreSize(v->getType());

    return fc.stack_value.find(v)->second;
  }
  
  return 0;
}

// 値の格納先ValueDestから実際の格納位置のポインタを取得する。
uint8_t* LlvmAsmLoader::get_ptr_by_dest(FunctionContext& fc, ValueDest dst) {
  if (dst.is_k) {
    return &fc.k.at(dst.addr.k);
  } else {
    return dst.addr.ptr;
  }
}

// LLVMの定数(配列)を仮想マシンにロードする。
void LlvmAsmLoader::load_array(FunctionContext& fc, ValueDest dst, const llvm::ConstantArray* src) {
  // Typeの要素数とOperandsの要素数は同じはず
  assert(src->getType()->getNumElements() == src->getNumOperands());
  
  // 書き込み
  int one_size = data_layout->getTypeAllocSize(src->getType()->getElementType());
  for (unsigned int i = 0; i < src->getNumOperands(); i ++) {
    load_constant(fc, relocate_dest(dst, i * one_size), src->getOperand(i));
  }
}

// LLVMの定数を仮想マシンにロードする。
void LlvmAsmLoader::load_constant(FunctionContext& fc, ValueDest dst, const llvm::Constant* src) {
  
  // 値の型に合わせて分岐
  switch(src->getValueID()) {
    //case llvm::Value::ArgumentVal: {} break;
    //case llvm::Value::BasicBlockVal: {} break;
  case llvm::Value::FunctionVal: {
    const llvm::Function* func = static_cast<const llvm::Function*>(src);
    // まだ確保されていない関数
    if (map_func.find(func) == map_func.end()) {
      vaddr_t addr = vm.reserve_func_addr();
      map_func.insert(std::make_pair(func, addr));
      load_function(func);
    }
    *reinterpret_cast<vaddr_t*>(get_ptr_by_dest(fc, dst)) = map_func.at(func);
  } return;

    //case llvm::Value::GlobalAliasVal: {} break;
  case llvm::Value::GlobalVariableVal: {
    assert(map_global.find(src) != map_global.end());
    *reinterpret_cast<vaddr_t*>(get_ptr_by_dest(fc, dst)) = map_global.at(src);
  } break;

  case llvm::Value::UndefValueVal: {
    // 値が不定なので初期化を行わない
  } break;

    //case llvm::Value::BlockAddressVal: {} break;
  case llvm::Value::ConstantExprVal: {
    load_expr(fc, dst, static_cast<const llvm::ConstantExpr*>(src));
  } break;
    
  case llvm::Value::ConstantAggregateZeroVal: {
    load_zero(fc, dst, static_cast<const llvm::ConstantAggregateZero*>(src));
  } break;

  case llvm::Value::ConstantDataArrayVal: {
    load_data(fc, dst, static_cast<const llvm::ConstantDataArray*>(src));
  } break;

    //case llvm::Value::ConstantDataVectorVal: {} break;
  case llvm::Value::ConstantIntVal: {
    load_int(fc, dst, static_cast<const llvm::ConstantInt*>(src));
  } break;

  case llvm::Value::ConstantFPVal: {
    load_float(fc, dst, static_cast<const llvm::ConstantFP*>(src));
  } break;

  case llvm::Value::ConstantArrayVal: {
    load_array(fc, dst, static_cast<const llvm::ConstantArray*>(src));
  } break;

  case llvm::Value::ConstantStructVal: {
    load_struct(fc, dst, static_cast<const llvm::ConstantStruct*>(src));
  } break;

    //case llvm::Value::ConstantVectorVal: {} break;
  case llvm::Value::ConstantPointerNullVal: {
    *reinterpret_cast<vaddr_t*>(get_ptr_by_dest(fc, dst)) = VADDR_NULL;
  } break;

    //case llvm::Value::MDNodeVal: {} break;
    //case llvm::Value::MDStringVal: {} break;
    //case llvm::Value::InlineAsmVal: {} break;
    //case llvm::Value::InstructionVal: {} break;
  default: {
    print_debug("unsupport type : %d\n", src->getValueID());
    src->dump();

    throw_error(Error::UNSUPPORT);
  } break;
  }
}

// LLVMの定数(DataArray)を仮想マシンにロードする。
void LlvmAsmLoader::load_data(FunctionContext& fc, ValueDest dst, const llvm::ConstantDataArray* src) {
  memcpy(get_ptr_by_dest(fc, dst), src->getRawDataValues().data(),
	 data_layout->getTypeAllocSize(src->getType()));
}

// LLVMの定数(Expr)を仮想マシンにロードする。
void LlvmAsmLoader::load_expr(FunctionContext& fc, ValueDest dst, const llvm::ConstantExpr* src) {
  switch(src->getOpcode()) {
    // case llvm::Instruction::Trunc:
    // case llvm::Instruction::ZExt:
    // case llvm::Instruction::SExt:
    // case llvm::Instruction::FPTrunc:
    // case llvm::Instruction::FPExt:
    // case llvm::Instruction::UIToFP:
    // case llvm::Instruction::SIToFP:
    // case llvm::Instruction::FPToUI:
    // case llvm::Instruction::FPToSI:
    // case llvm::Instruction::PtrToInt:
    // case llvm::Instruction::IntToPtr:

  case llvm::Instruction::BitCast: {
    // 変換元の定数を読み込む
    int k = assign_operand(fc, src->getOperand(0));
    // 変換先を0埋め
    memset(get_ptr_by_dest(fc, dst), 0, data_layout->getTypeAllocSize(src->getType()));
    // データをそのままコピー
    uint8_t* src_ptr;
    if (map_global.find(src->getOperand(0)) != map_global.end()) {
      src_ptr = vm.get_raw_addr(map_global.at(src->getOperand(0)));
    } else {
      src_ptr = fc.k.data() - k - 1;
    }
    size_t size = data_layout->getTypeAllocSize(src->getOperand(0)->getType());
    if (size > data_layout->getTypeAllocSize(src->getType())) {
      size = data_layout->getTypeAllocSize(src->getType());
    }
    
    memcpy(get_ptr_by_dest(fc, dst), src_ptr, size);
  } break;

    // case llvm::Instruction::AddrSpaceCast: {} break; //CastOps
    // case llvm::Instruction::Select: {} break; //Select
    // case llvm::Instruction::InsertElement: {} break; // InsertElement
    // case llvm::Instruction::ExtractElement: {} break; // ExtractElement
    // case llvm::Instruction::InsertValue: {} break; // InsertValue
    // case llvm::Instruction::ExtractValue: {} break; // ExtractValue
    // case llvm::Instruction::ShuffleVector: {} break; // ShuffleVector
    
  case llvm::Instruction::GetElementPtr: {
    const llvm::Value* tv = src->getOperand(0);
    assert(llvm::Constant::classof(tv)); // オペランドは定数のはず。
    // ポインタのアドレスを取得。
    //vaddr_t target_value = load_constant(static_cast<const llvm::Constant*>(tv));
    assert(map_global.find(tv) != map_global.end());
    vaddr_t target_value = map_global.at(tv);
    llvm::Type* op_type = tv->getType()->getPointerElementType();
    assert(op_type->isSized()); // サイズが確定可能と想定
    int delta = 0;
    for (unsigned int i = 1, num_operands = src->getNumOperands(); i < num_operands; i ++) {
      // 2つ目以降のオペランドは数値のはず。
      assert(llvm::ConstantInt::classof(src->getOperand(i)));
      const llvm::ConstantInt* op = static_cast<const llvm::ConstantInt*>(src->getOperand(i));
      assert(data_layout->getTypeStoreSize(op_type) != 0);
      assert(data_layout->getTypeStoreSize(op_type) ==
	     data_layout->getTypeAllocSize(op_type));

      if (i == 1) {
	// 先頭の番号は最初のポインタを基準としたアドレスの計算
	delta += (uint32_t)data_layout->getTypeStoreSize(op_type) * op->getSExtValue();

      } else if (llvm::SequentialType::classof(op_type)) {
	// pointer, array, vectorの場合、中身の型を見る。
	op_type = static_cast<const llvm::SequentialType*>(op_type)->getElementType();
	delta += (uint32_t)data_layout->getTypeStoreSize(op_type) * op->getSExtValue();

      } else if (llvm::StructType::classof(op_type)) {
	// 構造体型の中身を見る。
	unsigned int j = 0;
	for (j = 0; j < op->getZExtValue(); j ++) {
	  llvm::Type* in_type = static_cast<const llvm::StructType*>(op_type)->getElementType(j);
	  delta += (uint32_t)data_layout->getTypeStoreSize(in_type);
	}
	op_type = static_cast<const llvm::StructType*>(op_type)->getElementType(j);

      } else {
	assert(false);
      }
    }
    // TODO 範囲チェック
    *reinterpret_cast<vaddr_t*>(get_ptr_by_dest(fc, dst)) = target_value + delta;
  } break;

    // case Instruction::ICmp:
    // case Instruction::FCmp: {} break; // Cmp

  default: {
    print_debug("unsupport expr : %d %s\n", src->getOpcode(), src->getOpcodeName());
    throw_error(Error::UNSUPPORT);
  } break;
  }
}

// LLVMの定数(Floating-point)を仮想マシンにロードする。
void LlvmAsmLoader::load_float(FunctionContext& fc, ValueDest dst, const llvm::ConstantFP* src) {
  switch(src->getType()->getTypeID()) {
  case llvm::Type::FloatTyID: {
    *reinterpret_cast<float*>(get_ptr_by_dest(fc, dst)) = src->getValueAPF().convertToFloat();
  } break;

  case llvm::Type::DoubleTyID: {
    *reinterpret_cast<double*>(get_ptr_by_dest(fc, dst)) = src->getValueAPF().convertToDouble();
  } break;

  default: {
    print_debug("unsupport type : %d\n", src->getType()->getTypeID());
    src->dump();
    throw_error(Error::UNSUPPORT);
  } break;
  };
}

// LLVMの関数を仮想マシンにロードする。
void LlvmAsmLoader::load_function(const llvm::Function* function) {
  if (function->isDeclaration()) {
    // VM組み込み関数/ライブラリなど外部の関数
    // 名前を持つはず
    assert(function->hasName());
    
    vaddr_t addr = map_func.at(function);
    vm.deploy_function(function->getName(),
		       load_type(function->getReturnType(), false),
		       addr);

  } else {
    // 通常の関数(VMで解釈、実行する)
    FuncStore::NormalProp prop;

    // 定数
    std::vector<uint8_t> k;
    // 定数の最初に0x00(false)と0xff(true)をロードできるように確保しておく
    k.push_back(0x00);
    k.push_back(0xff);
    // 変数
    std::map<const llvm::Value*, int> stack_values;
    
    // ブロックとそれに割り当てる名前
    std::map<const llvm::BasicBlock*, unsigned int> block_alias;
    // ブロック名とそれの開始位置
    std::map<unsigned int, unsigned int> block_start;

    FunctionContext fc = {prop.code, k, stack_values, 0};
    
    // 引数を変数の先頭に登録
    for (auto arg = function->getArgumentList().begin();
	 arg != function->getArgumentList().end(); arg ++) {
      assert(arg->hasName()); // 名前を持つはず
      stack_values.insert(std::make_pair(arg, fc.stack_sum));

      assert(data_layout->getTypeAllocSize(arg->getType()) != 0);
      assert(data_layout->getTypeStoreSize(arg->getType()) ==
	     data_layout->getTypeAllocSize(arg->getType()));
      fc.stack_sum += data_layout->getTypeAllocSize(arg->getType());
    }

    // ブロックの名称を最初に作っておく
    unsigned int alias = 0;
    for (auto b = function->begin(); b != function->end(); b ++) {
      block_alias.insert(std::make_pair(b, alias ++));
    }

    for (auto block = function->begin(); block != function->end(); block ++) {
      // ブロックの開始位置を格納しておく
      block_start.insert(std::make_pair(block_alias.at(block), fc.code.size()));

      // 命令を解析する
      for (auto i = block->begin(); i != block->end(); i ++) {
	i->dump();
	// LLVMに対応した命令に置き換え
	switch(i->getOpcode()) {
	case llvm::Instruction::Ret: {
	  const llvm::ReturnInst& inst = static_cast<const llvm::ReturnInst&>(*i);
	  if (inst.getReturnValue() == nullptr) {
	    // 戻り値がない場合、0xff..
	    push_code(fc, Opcode::RETURN, FILL_OPERAND);

	  } else {
	    // 戻り値がある場合、A = 1, B = 値
	    push_code(fc, Opcode::RETURN,
		      assign_operand(fc, inst.getReturnValue()));
	  }
	} break;

	case llvm::Instruction::Call: {
	  const llvm::CallInst& inst = static_cast<const llvm::CallInst&>(*i);
	  // インラインアセンブラ未対応
	  if (inst.isInlineAsm()) throw_error(Error::UNSUPPORT);
	  if (!inst.getType()->isVoidTy()) {
	    // 戻り値の型
	    push_code(fc, Opcode::SET_TYPE,
		      assign_type(fc, inst.getType()));
	    // 出力先
	    push_code(fc, Opcode::SET_OUTPUT,
		      assign_operand(fc, &inst));
	  }

	  // CALL命令、関数
	  push_code(fc, Opcode::CALL,
		    assign_operand(fc, inst.getCalledValue()));
	  // 戻り値の型、格納先の命令を追加
	  push_code(fc, Opcode::EXTRA, FILL_OPERAND);
	  push_code(fc, Opcode::EXTRA, FILL_OPERAND);

	  // 引数部分の命令(引数の型、引数〜)を追加
	  for (unsigned int arg_idx = 0, num = inst.getNumArgOperands();
	       arg_idx < num; arg_idx ++) {
	    push_code(fc, Opcode::EXTRA,
		      assign_type(fc, inst.getArgOperand(arg_idx)->getType()));
	    push_code(fc, Opcode::EXTRA,
		      assign_operand(fc, inst.getArgOperand(arg_idx)));
	  }
	} break;

	case llvm::Instruction::Br: {
	  const llvm::IndirectBrInst& inst = static_cast<const llvm::IndirectBrInst&>(*i);
	  if (inst.getNumDestinations() == 0) {
	    // 無条件分岐の場合、無条件jump先の命令を追加
	    push_code(fc, Opcode::JUMP,
		      block_alias.at(inst.getDestination(-1)));

	  } else {
	    // 条件分岐
	    assert(inst.getNumDestinations() == 2);
	    push_code(fc, Opcode::TEST,
		      assign_operand(fc, inst.getAddress()));
	    // cond == true の場合のジャンプ先
	    push_code(fc, Opcode::EXTRA,
		      block_alias.at(inst.getSuccessor(1)));
	    // cond != true の場合のジャンプ先
	    push_code(fc, Opcode::JUMP,
		      block_alias.at(inst.getSuccessor(0)));
	  }
	} break;

	case llvm::Instruction::Switch: {
	  const llvm::SwitchInst& inst = static_cast<const llvm::SwitchInst&>(*i);
	  // set_type <intty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getCondition()->getType()));
	  // set_value <value>
	  push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getCondition()));
	  for (auto it = inst.case_begin(); it != inst.case_end(); it ++) {
	    // test_eq <val>
	    push_code(fc, Opcode::TEST_EQ, assign_operand(fc, it.getCaseValue()));
	    // extra <dest>
	    push_code(fc, Opcode::EXTRA, block_alias.at(it.getCaseSuccessor()));
	  }
	  // jump <defaultdest>
	  push_code(fc, Opcode::JUMP, block_alias.at(inst.getDefaultDest()));
	} break;

	case llvm::Instruction::Unreachable: {
	  // const llvm::UnreachableInst& inst = static_cast<const llvm::Unreachable&>(*i);
	  // Unreachableはコンパイラの最適化のためだけにあるので、何も実行しなくて良い。
	} break;

	case llvm::Instruction::PHI: {
	  const llvm::PHINode& inst = static_cast<const llvm::PHINode&>(*i);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE,
		    assign_type(fc, inst.getType()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT,
		    assign_operand(fc, &inst));
	  // phi <val0>
	  // extra <label0>
	  // extra <val1>
	  // extra <label1>…
	  for (unsigned int i = 0, num = inst.getNumIncomingValues(); i < num; i ++) {
	    push_code(fc, (i == 0 ? Opcode::PHI : Opcode::EXTRA),
		      assign_operand(fc, inst.getIncomingValue(i)));
	    push_code(fc, Opcode::EXTRA,
		      block_alias.at(inst.getIncomingBlock(i)));
	  }
	} break;

	  /**
	   * 加減乗除などの２項演算子命令作成マクロ
	   * @param opcode オペコード
	   * @param sign 符号考慮の場合true
	   */
#define M_BIN_OPERATOR(opcode, sign)				\
	  const llvm::BinaryOperator& inst =			\
	    static_cast<const llvm::BinaryOperator&>(*i);	\
	  assert(inst.getNumOperands() == 2);			\
	  /* set_type <ty> */					\
	  push_code(fc, Opcode::SET_TYPE,			\
		    assign_type(fc, inst.getType(), sign));	\
	  /* set_output <result> */				\
	  push_code(fc, Opcode::SET_OUTPUT,			\
		    assign_operand(fc, &inst));			\
	  /* set_value <op1> */					\
	  push_code(fc, Opcode::SET_VALUE,			\
		    assign_operand(fc, inst.getOperand(0)));	\
	  /* opcode <op2> */					\
	  push_code(fc, (opcode),				\
		    assign_operand(fc, inst.getOperand(1)))

	case llvm::Instruction::Add:
	case llvm::Instruction::FAdd: {
	  // add命令は符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::ADD, true);
	} break;

	case llvm::Instruction::Sub:
	case llvm::Instruction::FSub: {
	  // sub命令は符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::SUB, true);
	} break;

	case llvm::Instruction::Mul:
	case llvm::Instruction::FMul: {
	  // mul命令は符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::MUL, true);
	} break;

	case llvm::Instruction::UDiv: {
	  // 符号考慮なしのdiv
	  M_BIN_OPERATOR(Opcode::DIV, false);
	} break;

	case llvm::Instruction::SDiv:
	case llvm::Instruction::FDiv: {
	  // 符号考慮ありのdiv
	  M_BIN_OPERATOR(Opcode::DIV, true);
	} break;

	case llvm::Instruction::URem: {
	  // 符号考慮なしのrem
	  M_BIN_OPERATOR(Opcode::REM, false);
	} break;

	case llvm::Instruction::SRem:
	case llvm::Instruction::FRem: {
	  // 符号考慮ありのrem
	  M_BIN_OPERATOR(Opcode::REM, true);
	} break;

	case llvm::Instruction::Shl: {
	  // 左リシフトは符号に関係ないので考慮バージョンのみ実装
	  M_BIN_OPERATOR(Opcode::SHL, true);
	} break;

	case llvm::Instruction::LShr: {
	  // 符号考慮なし右シフト
	  M_BIN_OPERATOR(Opcode::SHR, false);
	} break;

	case llvm::Instruction::AShr: {
	  // 符号考慮あり右シフト
	  M_BIN_OPERATOR(Opcode::SHR, true);
	} break;

	case llvm::Instruction::And: {
	  // andは符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::AND, true);
	} break;

	case llvm::Instruction::Or: {
	  // orは符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::OR, true);
	} break;

	case llvm::Instruction::Xor: {
	  // xorは符号考慮バージョンのみ
	  M_BIN_OPERATOR(Opcode::XOR, true);
	} break;

#undef M_BIN_OPERATOR
	  
	case llvm::Instruction::Alloca: {
	  const llvm::AllocaInst& inst = static_cast<const llvm::AllocaInst&>(*i);
	  // set_type <type>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getAllocatedType()));
	  // set_align <alignment>
	  push_code(fc, Opcode::SET_ALIGN, inst.getAlignment());
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	  // alloca <NumElements>
	  push_code(fc, Opcode::ALLOCA, assign_operand(fc, inst.getArraySize()));
	} break;
	  
	case llvm::Instruction::Load: {
	  const llvm::LoadInst& inst = static_cast<const llvm::LoadInst&>(*i);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE,
		    assign_type(fc, inst.getPointerOperand()->getType()));
	  // set_align <alignment>
	  push_code(fc, Opcode::SET_ALIGN,
		    inst.getAlignment());
	  // set_adr <pointer>
	  push_code(fc, Opcode::SET_ADR,
		    assign_operand(fc, inst.getPointerOperand()));
	  // load <result>
	  push_code(fc, Opcode::LOAD,
		    assign_operand(fc, &inst));
	} break;

	case llvm::Instruction::Store: {
	  const llvm::StoreInst& inst = static_cast<const llvm::StoreInst&>(*i);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE,
		    assign_type(fc, inst.getValueOperand()->getType()));
	  // set_align <alignment>
	  push_code(fc, Opcode::SET_ALIGN,
		    inst.getAlignment());
	  // set_adr <pointer>
	  push_code(fc, Opcode::SET_ADR,
		    assign_operand(fc, inst.getPointerOperand()));
	  // store <value>
	  push_code(fc, Opcode::STORE,
		    assign_operand(fc, inst.getValueOperand()));
	} break;

	case llvm::Instruction::GetElementPtr: {
	  const llvm::GetElementPtrInst& inst = static_cast<const llvm::GetElementPtrInst&>(*i);
	  // set_ptr <ptrval>
	  push_code(fc, Opcode::SET_ADR,
		    assign_operand(fc, inst.getPointerOperand()));

	  llvm::Type* op_type = inst.getPointerOperandType()->getPointerElementType();
	  for (unsigned int i = 1, num = inst.getNumOperands(); i < num; i ++) {
	    if (i == 1) {
	      // set_type <ty>
	      push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getOperand(i)->getType()));
	      // set_value <idx>
	      push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(i)));
	      // mul_adr <>
	      assert(data_layout->getTypeAllocSize(op_type) != 0);
	      assert(data_layout->getTypeStoreSize(op_type) ==
		     data_layout->getTypeAllocSize(op_type));
	      push_code(fc, Opcode::MUL_ADR, data_layout->getTypeAllocSize(op_type));
	      
	    } else if (llvm::SequentialType::classof(op_type)) {
	      op_type = static_cast<const llvm::SequentialType*>(op_type)->getElementType();
	      // set_type <ty>
	      push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getOperand(i)->getType()));
	      // set_value <idx>
	      push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(i)));
	      // mul_adr <>
	      assert(data_layout->getTypeAllocSize(op_type) != 0);
	      assert(data_layout->getTypeStoreSize(op_type) ==
		     data_layout->getTypeAllocSize(op_type));
	      push_code(fc, Opcode::MUL_ADR, data_layout->getTypeAllocSize(op_type));

	    } else if (llvm::StructType::classof(op_type)) {
	      unsigned int j = 0;
	      int size_sum = 0;
	      // int系のはず
	      assert(llvm::ConstantInt::classof(inst.getOperand(i)));
	      for (j = 0;
		   j < static_cast<const llvm::ConstantInt*>(inst.getOperand(i))->getZExtValue();
		   j ++) {
		llvm::Type* in_type =
		  static_cast<const llvm::StructType*>(op_type)->getElementType(j);
		size_sum += data_layout->getTypeStoreSize(in_type);
	      }
	      // add_adr <>
	      op_type = static_cast<const llvm::StructType*>(op_type)->getElementType(j);
	      push_code(fc, Opcode::ADD_ADR, size_sum);

	    } else {
	      assert(false);
	    }
	  }
	  // get_adr <result>
	  push_code(fc, Opcode::GET_ADR,
		    assign_operand(fc, &inst));
	} break;

	case llvm::Instruction::Trunc:
	case llvm::Instruction::ZExt:
	case llvm::Instruction::FPTrunc:
	case llvm::Instruction::FPExt:
	case llvm::Instruction::FPToUI:
	case llvm::Instruction::UIToFP:
	case llvm::Instruction::PtrToInt:
	case llvm::Instruction::IntToPtr: {
	  const llvm::CastInst& inst = static_cast<const llvm::CastInst&>(*i);
	  assert(inst.getNumOperands() == 1);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getSrcTy()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	  // set_value <value>
	  push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(0)));
	  // typecast <ty2>
	  push_code(fc, Opcode::TYPE_CAST, assign_type(fc, inst.getDestTy()));
	} break;

	case llvm::Instruction::SExt:
	case llvm::Instruction::FPToSI:
	case llvm::Instruction::SIToFP: {
	  const llvm::SExtInst& inst = static_cast<const llvm::SExtInst&>(*i);
	  assert(inst.getNumOperands() == 1);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getSrcTy(), true));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	  // set_value <value>
	  push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(0)));
	  // typecast <ty2>
	  push_code(fc, Opcode::TYPE_CAST, assign_type(fc, inst.getDestTy(), true));
	} break;

	case llvm::Instruction::BitCast: {
	  const llvm::BitCastInst& inst = static_cast<const llvm::BitCastInst&>(*i);
	  assert(inst.getNumOperands() == 1);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getSrcTy()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	  // set_value <value>
	  push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(0)));
	  // bitcast <ty2>
	  push_code(fc, Opcode::BIT_CAST, assign_type(fc, inst.getDestTy()));
	} break;

	case llvm::Instruction::ICmp: {
	  const llvm::ICmpInst& inst = static_cast<const llvm::ICmpInst&>(*i);
	  assert(inst.isIntPredicate());
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE,
		    assign_type(fc, inst.getOperand(0)->getType(), inst.isSigned()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT,
		    assign_operand(fc, &inst));

	  switch(inst.getPredicate()){
	    /**
	     * icmpの比較演算を作るためのマクロ
	     * @param PRE CmpInst::Predicateのメンバ
	     * @param OPC VMの命令
	     * @param FOP 比較対象左辺値
	     * @param SOP 比較対象右辺値
	     */
#define M_ICMP_OPERATOR(PRE, OPC, FOP, SOP)				\
	    case llvm::CmpInst::Predicate::PRE: {			\
	      push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(FOP))); \
	      push_code(fc, Opcode::OPC, assign_operand(fc, inst.getOperand(SOP))); \
	    } break;

	    M_ICMP_OPERATOR(ICMP_EQ, EQUAL, 0, 1); // =
	    M_ICMP_OPERATOR(ICMP_NE, NOT_EQUAL, 0, 1); // !=
	    M_ICMP_OPERATOR(ICMP_UGT, GREATER, 0, 1);  // > (unsigned)
	    M_ICMP_OPERATOR(ICMP_UGE, GREATER_EQUAL, 0, 1); // >= (unsigned)
	    M_ICMP_OPERATOR(ICMP_ULT, GREATER_EQUAL, 1, 0); // < (unsigned)
	    M_ICMP_OPERATOR(ICMP_ULE, GREATER, 1, 0); // <= (unsigned)
	    M_ICMP_OPERATOR(ICMP_SGT, GREATER, 0, 1);  // > (signed)
	    M_ICMP_OPERATOR(ICMP_SGE, GREATER_EQUAL, 0, 1); // >= (signed)
	    M_ICMP_OPERATOR(ICMP_SLT, GREATER_EQUAL, 1, 0); // < (signed)
	    M_ICMP_OPERATOR(ICMP_SLE, GREATER, 1, 0); // <= (signed)
#undef M_ICMP_OPERATOR

	  default: {
	    assert(false);
	  } break;
	  }
	} break;

	case llvm::Instruction::FCmp: {
	  const llvm::FCmpInst& inst = static_cast<const llvm::FCmpInst&>(*i);
	  assert(inst.isFPPredicate());
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getOperand(0)->getType()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));

	  switch(inst.getPredicate()){
	    /**
	     * fcmpの比較演算を作るためのマクロ
	     * @param PRE CmpInst::Predicateのメンバ
	     * @param OPC VMの命令
	     * @param FOP 比較対象左辺値
	     * @param SOP 比較対象右辺値
	     */
#define M_FCMP_OPERATOR1(PRE, OPC, FOP, SOP)				\
	    case llvm::CmpInst::PRE: {					\
	      push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(FOP))); \
	      push_code(fc, Opcode::OPC, assign_operand(fc, inst.getOperand(SOP))); \
	    } break;

	    M_FCMP_OPERATOR1(FCMP_OEQ, EQUAL, 0, 1); // =
	    M_FCMP_OPERATOR1(FCMP_OGT, GREATER, 0, 1);  // >
	    M_FCMP_OPERATOR1(FCMP_OGE, GREATER_EQUAL, 0, 1); // >=
	    M_FCMP_OPERATOR1(FCMP_OLT, GREATER_EQUAL, 1, 0); // <
	    M_FCMP_OPERATOR1(FCMP_OLE, GREATER, 1, 0); // <=
	    M_FCMP_OPERATOR1(FCMP_ONE, NOT_EQUAL, 0, 1); // !=
	    M_FCMP_OPERATOR1(FCMP_ORD, NOT_NANS, 0, 1); // !isnan(v) && !isnan(A)
#undef M_FCMP_OPERATOR1

#define M_FCMP_OPERATOR2(PRE, OPC, FOP, SOP)				\
	    case llvm::CmpInst::PRE: {					\
	    push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(FOP))); \
	    push_code(fc, Opcode::OR_NANS, assign_operand(fc, inst.getOperand(SOP))); \
	    push_code(fc, Opcode::OPC, assign_operand(fc, inst.getOperand(SOP))); \
	  } break;

	    M_FCMP_OPERATOR2(FCMP_UEQ, EQUAL, 0, 1); // =
	    M_FCMP_OPERATOR2(FCMP_UGT, GREATER, 0, 1);  // >
	    M_FCMP_OPERATOR2(FCMP_UGE, GREATER_EQUAL, 0, 1); // >=
	    M_FCMP_OPERATOR2(FCMP_ULT, GREATER_EQUAL, 1, 0); // <
	    M_FCMP_OPERATOR2(FCMP_ULE, GREATER, 1, 0); // <=
	    M_FCMP_OPERATOR2(FCMP_UNE, NOT_EQUAL, 0, 1); // !=
#undef M_FCMP_OPERATOR2

	  case llvm::CmpInst::FCMP_UNO: { // isnan(v) || isnan(A)
	    push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getOperand(0)));
	    push_code(fc, Opcode::OR_NANS, assign_operand(fc, inst.getOperand(1))); \
	    // OR_NANSを使い比較不能かどうか調べ、pc+1分、NOPを埋めることで都合をつける。
	    push_code(fc, Opcode::NOP, 0);
	  } break;

	  case llvm::CmpInst::FCMP_FALSE: { // false
	    // set_output <result>
	    push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	    // set_value 定数
	    push_code(fc, Opcode::SET_VALUE, -1); // 0x00をk(0)に割り当てておく
	    // copy sizeof(<ty>)
	    push_code(fc, Opcode::COPY, 1);
	  } break;

	  case llvm::CmpInst::FCMP_TRUE: { // true
	    // set_output <result>
	    push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	    // set_value 定数
	    push_code(fc, Opcode::SET_VALUE, -2); // 0xffをk(1)に割り当てておく
	    // copy sizeof(<ty>)
	    push_code(fc, Opcode::COPY, 1);
	  } break;

	  default: {
	    print_debug("predicate %d\n", inst.getPredicate());
	    assert(false);
	  } break;
	  }
	} break;

	case llvm::Instruction::Select: {
	  const llvm::SelectInst& inst = static_cast<const llvm::SelectInst&>(*i);
	  // set_type <ty>
	  push_code(fc, Opcode::SET_TYPE, assign_type(fc, inst.getTrueValue()->getType()));
	  // set_output <result>
	  push_code(fc, Opcode::SET_OUTPUT, assign_operand(fc, &inst));
	  // set_value <cond>
	  push_code(fc, Opcode::SET_VALUE, assign_operand(fc, inst.getCondition()));
	  // select <val1>
	  push_code(fc, Opcode::SELECT, assign_operand(fc, inst.getTrueValue()));
	  // extra <val2>
	  push_code(fc, Opcode::EXTRA, assign_operand(fc, inst.getFalseValue()));
	} break;

	default: {
	  print_debug("unsupport instruction : %s\n", i->getOpcodeName());
	  throw_error_message(Error::UNSUPPORT, "instruction:" + Util::num2dec_str(i->getOpcode()));
	} break;
	};
      }
    }

    // TEST/JUMP命令のジャンプ先をラベルから開始位置に書き換える
    for (unsigned int pc = 0, size = fc.code.size(); pc < size; pc ++) {
      instruction_t code = fc.code.at(pc);

      switch (Instruction::get_opcode(code)) {
#define M_REPLACE_LABEL(code_at) 					\
	{								\
	  instruction_t& target = fc.code.at(code_at);			\
	  instruction_t label   = Instruction::get_operand(target);	\
	  if (label != FILL_OPERAND) {					\
	    target = Instruction::rewrite_operand(target,		\
						  block_start.at(label)); \
	  }								\
	}

      case Opcode::CALL: {
	M_REPLACE_LABEL(pc + 1);
	M_REPLACE_LABEL(pc + 2);
	pc += 2;
      } break;

      case Opcode::TEST: {
	M_REPLACE_LABEL(pc + 1);
	pc += 1;
      } break;

      case Opcode::TEST_EQ: {
	M_REPLACE_LABEL(pc + 1);
	pc += 1;
      } break;

      case Opcode::JUMP: {
	M_REPLACE_LABEL(pc);
      } break;

      case Opcode::PHI: {
	if (Instruction::get_opcode(fc.code.at(pc + 1)) == Opcode::EXTRA) {
	  M_REPLACE_LABEL(pc + 1);
	  pc += 1;
	}
	while (Instruction::get_opcode(fc.code.at(pc + 1)) == Opcode::EXTRA &&
	       Instruction::get_opcode(fc.code.at(pc + 2)) == Opcode::EXTRA) {
	  M_REPLACE_LABEL(pc + 2);
	  pc += 2;
	}
      } break;
#undef M_REPLACE_LABEL
      }
    }

    prop.is_var_arg = function->isVarArg();
    prop.arg_num    = function->arg_size();
    prop.stack_size = fc.stack_sum;

    // 定数領域を作成
    prop.k = vm.v_malloc(k.size(), true);
    vm.v_memcpy(prop.k, k.data(), k.size());

    // 定数、変数の数がオペランドで表現可能な上限を超えた場合エラー
    if (stack_values.size() > ((FILL_OPERAND >> 1) - 1) ||
	k.size() > ((FILL_OPERAND >> 1) - 1)) {
      throw_error_message(Error::TOO_MANY_VALUE, function->getName().str());
    }

    vaddr_t addr = map_func.at(function);
    vm.deploy_function_normal(function->getName().str(),
			      load_type(function->getReturnType(), false),
			      prop, addr);
  }
}

// LLVMの大域変数を仮想マシンにロードする。
void LlvmAsmLoader::load_globals(const llvm::Module::GlobalListType& variables) {
  // 実際の値をロードする前に割当先アドレスを決定する。
  // 定数の合計サイズを計算する
  vaddr_t sum = 0;
  for (auto gl = variables.begin(); gl != variables.end(); gl ++) {
    // 割り当てサイズの確認
    assert(data_layout->getTypeAllocSize(gl->getType()->getElementType()) != 0);
    assert(data_layout->getTypeAllocSize(gl->getType()->getElementType()) ==
	   data_layout->getTypeStoreSize(gl->getType()->getElementType()));

    if (gl->isConstant()) {
      // 定数の場合、仮のアドレスを割り当てる
      map_global.insert(std::make_pair(gl, sum));
      sum += data_layout->getTypeAllocSize(gl->getType()->getElementType());

    } else {
      // 変数の場合、それぞれのアドレスを確保する
      size_t size = data_layout->getTypeAllocSize(gl->getType()->getElementType());
      vaddr_t new_addr = vm.v_malloc(size, false);
      map_global.insert(std::make_pair(gl, new_addr));
    }
  }

  // 定数領域を割り当て
  vaddr_t global_addr = vm.v_malloc(sum, true);
  // 割り当てたアドレスを元に仮のアドレスから実際のアドレスに変更する
  for (auto it = map_global.begin(); it != map_global.end(); it ++) {
    if (static_cast<const llvm::GlobalVariable*>(it->first)->isConstant()) {
      it->second += global_addr;
    }
  }

  // 読み込み用にダミーの格納先を作成
  FuncStore::NormalProp prop;
  std::vector<uint8_t> k;
  std::map<const llvm::Value*, int> stack_values;
  FunctionContext fc = {prop.code, k, stack_values, 0};
  // 初期値がある場合は値をロードする
  for (auto it = map_global.begin(); it != map_global.end(); it ++) {
    const llvm::GlobalVariable* gl =
      static_cast<const llvm::GlobalVariable*>(it->first);
    if (gl->hasInitializer()) {
      ValueDest dst;
      dst.is_k = false;
      dst.addr.ptr = vm.get_raw_addr(it->second);
      load_constant(fc, dst, gl->getInitializer());
    }
  }
  // ダミー定数領域には書き込まれていないはず
  assert(k.size() == 0);
}

// LLVMの定数(Int)を仮想マシンにロードする。
void LlvmAsmLoader::load_int(FunctionContext& fc, ValueDest dst, const llvm::ConstantInt* src) {
  if (src->getBitWidth() <= 0 || 64 < src->getBitWidth()) {
    print_debug("unsupport bit width : %d\n", src->getBitWidth());
    throw_error(Error::UNSUPPORT);
  }

  memcpy(get_ptr_by_dest(fc, dst), src->getValue().getRawData(),
	 ((src->getBitWidth() - 1) / 8) + 1);
}

// LLVMのモジュールを仮想マシンにロードする。
void LlvmAsmLoader::load_module(llvm::Module* module) {
  /// todo: DataLayout, Tripleのチェック
  print_debug("DataLayout: %s\n", module->getDataLayoutStr().c_str());
  print_debug("Triple: %s\n",     module->getTargetTriple().c_str());
  
  // データレイアウトの取得
  data_layout = module->getDataLayout();
  // 大域変数の読み込み
  load_globals(module->getGlobalList());

  // 関数のアドレスを予約しておく
  for (auto fn = module->begin(); fn != module->end(); fn ++) {
    vaddr_t addr = vm.reserve_func_addr();
    map_func.insert(std::make_pair(fn, addr));
    /// todo: スレッドローカル、セクション、公開の扱い
    vm.set_global_value(fn->getName().str(), addr);
  }

  // 関数の読み込み
  for (auto fn = module->begin(); fn != module->end(); fn ++) {
    load_function(fn);
  }

  // AliasListは未対応
  if (module->getAliasList().size() != 0) {
    throw_error(Error::UNSUPPORT);
  }

  // デバッグ用にダンプを出力
  std::set<vaddr_t> all = vm.vmemory.get_alladdr();
  for (auto it = all.begin(); it != all.end(); it ++) {
    vaddr_t addr = *it;
    if (VMemory::addr_is_func(addr)) {
      FuncStore& func = vm.vmemory.get_func(addr);
      if (func.type == FuncType::FC_NORMAL) {
	const FuncStore::NormalProp& prop = func.normal_prop;
	print_debug("func(normal):\t%016" PRIx64 "\n", addr);
	print_debug("\tname:\t%s\n", func.name.str().c_str());
	print_debug("\tis_var_arg:\t%d\n", prop.is_var_arg);
	print_debug("\targ_num:\t%d\n", prop.arg_num);
	print_debug("\tstack_size:\t%d\n", prop.stack_size);
	print_debug("\tcode:(%ld)\n", prop.code.size());
	for (auto it = prop.code.begin(); it != prop.code.end(); it++) {
	  print_debug("\t\t%08x  %s\n", *it, Util::code2str(*it).c_str());
	}
	print_debug("\tk:\t%016" PRIx64 "\n", prop.k);
	print_debug("\tret_type\t%016" PRIx64 "\n", func.ret_type);

      } else if (func.type == FuncType::FC_INTRINSIC) {
	print_debug("func(intrinsic):\t%016" PRIx64 "\n", addr);
	print_debug("\tname:\t%s\n", func.name.str().c_str());

      } else { // FC_EXTERNAL
	print_debug("func(external):\t%016" PRIx64 "\n", addr);
	print_debug("\tname:\t%s\n", func.name.str().c_str());
      }

    } else if (VMemory::addr_is_type(addr)) {
      TypeStore& type = vm.vmemory.get_type(addr);
      print_debug("type:\t%016" PRIx64 "\n", addr);
      switch(type.kind) {
      case TypeKind::TK_BASIC: break;
      case TypeKind::TK_STRUCT: {
	for (auto it = type.member.begin(); it != type.member.end(); it ++)
	  print_debug("\t%016" PRIx64 "\n", *it);
      } break;
      case TypeKind::TK_ARRAY: 
	print_debug("\t[%016" PRIx64 " x %d]\n", type.element, type.num);
	break;
      case TypeKind::TK_VECTOR:
	print_debug("\t<%016" PRIx64 " x %d>\n", type.element, type.num);
	break;
      default: assert(false);
      }

    } else { // data
      DataStore& data = vm.vmemory.get_data(addr);
      print_debug("data:\t%016" PRIx64 "\n", addr);
      print_debug("\tsize:\t%ld\n", data.size);
      for (unsigned int i = 0; i < data.size; i += 4) {
	switch(data.size - i) {
	case 1: {
	  print_debug("\t%d\t%02x          - %c\n", i,
		      0xff & data.head[i    ],
		      (' ' <= static_cast<const char>(data.head[i    ]) &&
		       static_cast<const char>(data.head[i    ]) <= '~' ?
		       static_cast<const char>(data.head[i    ]) : ' '));
	} break;

	case 2: {
	  print_debug("\t%d\t%02x %02x       - %c%c\n", i,
		      0xff & data.head[i    ], 0xff & data.head[i + 1],
		      (' ' <= static_cast<const char>(data.head[i    ]) &&
		       static_cast<const char>(data.head[i    ]) <= '~' ?
		       static_cast<const char>(data.head[i    ]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 1]) &&
		       static_cast<const char>(data.head[i + 1]) <= '~' ?
		       static_cast<const char>(data.head[i + 1]) : ' '));
	} break;

	case 3: {
	  print_debug("\t%d\t%02x %02x %02x    - %c%c%c\n", i,
		      0xff & data.head[i    ], 0xff & data.head[i + 1],
		      0xff & data.head[i + 2],
		      (' ' <= static_cast<const char>(data.head[i    ]) &&
		       static_cast<const char>(data.head[i    ]) <= '~' ?
		       static_cast<const char>(data.head[i    ]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 1]) &&
		       static_cast<const char>(data.head[i + 1]) <= '~' ?
		       static_cast<const char>(data.head[i + 1]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 2]) &&
		       static_cast<const char>(data.head[i + 2]) <= '~' ?
		       static_cast<const char>(data.head[i + 2]) : ' '));
	} break;

	default: {
	  print_debug("\t%d\t%02x %02x %02x %02x - %c%c%c%c\n", i,
		      0xff & data.head[i    ], 0xff & data.head[i + 1],
		      0xff & data.head[i + 2], 0xff & data.head[i + 3],
		      (' ' <= static_cast<const char>(data.head[i    ]) &&
		       static_cast<const char>(data.head[i    ]) <= '~' ?
		       static_cast<const char>(data.head[i    ]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 1]) &&
		       static_cast<const char>(data.head[i + 1]) <= '~' ?
		       static_cast<const char>(data.head[i + 1]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 2]) &&
		       static_cast<const char>(data.head[i + 2]) <= '~' ?
		       static_cast<const char>(data.head[i + 2]) : ' '),
		      (' ' <= static_cast<const char>(data.head[i + 3]) &&
		       static_cast<const char>(data.head[i + 3]) <= '~' ?
		       static_cast<const char>(data.head[i + 3]) : ' '));
	} break;
	}
      }
    }
  }
}

// LLVMの定数(struct)を仮想マシンにロードする。
void LlvmAsmLoader::load_struct(FunctionContext& fc, ValueDest dst, const llvm::ConstantStruct* src) {
  // Typeの要素数とOperandsの要素数は同じはず
  assert(src->getType()->getNumElements() == src->getNumOperands());
  
  // 書き込み
  int sum_size = 0;
  for (unsigned int i = 0; i < src->getNumOperands(); i ++) {
    int one_size = data_layout->getTypeAllocSize(src->getOperand(i)->getType());
    load_constant(fc, relocate_dest(dst, sum_size), src->getOperand(i));
    sum_size += one_size;
  }
}

// LLVMの型を仮想マシンにロードする。
vaddr_t LlvmAsmLoader::load_type(const llvm::Type* type, bool sign) {
  // 既存の値の場合、それを戻す。
  auto exist = loaded_type.find(type);
  if (exist != loaded_type.end()) {
    return exist->second;
  }

  // 基本型の判定
  BasicType addr;
  switch(type->getTypeID()) {
    // 1:1t対応するもの
  case llvm::Type::VoidTyID:        addr = BasicType::TY_VOID;      break;
  case llvm::Type::FloatTyID:       addr = BasicType::TY_F32;       break;
  case llvm::Type::DoubleTyID:      addr = BasicType::TY_F64;       break;
  case llvm::Type::FunctionTyID:    addr = BasicType::TY_FUNCTION;  break;
  case llvm::Type::PointerTyID:     addr = BasicType::TY_POINTER;   break;

  case llvm::Type::IntegerTyID: {
    // intはサイズごとに切り替え
    if (sign) {
      if (type->getIntegerBitWidth() <= 8) {
	addr = BasicType::TY_SI8;
      } else if (type->getIntegerBitWidth() <= 16) {
	addr = BasicType::TY_SI16;
      } else if (type->getIntegerBitWidth() <= 32) {
	addr = BasicType::TY_SI32;
      } else if (type->getIntegerBitWidth() <= 64) {
	addr = BasicType::TY_SI64;
      } else {
	throw_error_message(Error::UNSUPPORT,
			    "integer bit-width:" +
			    Util::num2dec_str(type->getIntegerBitWidth()));
      }
    } else {
      if (type->getIntegerBitWidth() <= 8) {
	addr = BasicType::TY_UI8;
      } else if (type->getIntegerBitWidth() <= 16) {
	addr = BasicType::TY_UI16;
      } else if (type->getIntegerBitWidth() <= 32) {
	addr = BasicType::TY_UI32;
      } else if (type->getIntegerBitWidth() <= 64) {
	addr = BasicType::TY_UI64;
      } else {
	throw_error_message(Error::UNSUPPORT,
			    "integer bit-width:" +
			    Util::num2dec_str(type->getIntegerBitWidth()));
      }
    }
  } break;

  case llvm::Type::StructTyID: {
    // 構造体独自に判定してしまう
    std::vector<vaddr_t> member;
    for (int i = 0, size = type->getStructNumElements(); i < size; i ++) {
      member.push_back(load_type(type->getStructElementType(i), false));
    }
    TypeStore& store = vm.create_type_struct(member);
    loaded_type.insert(std::make_pair(type, store.addr));
    return store.addr;
  } break;

  case llvm::Type::ArrayTyID: {
    TypeStore& store =
      vm.create_type_array(load_type(type->getArrayElementType(), false),
			   type->getArrayNumElements());
    loaded_type.insert(std::make_pair(type, store.addr));
    return store.addr;
  } break;

  case llvm::Type::VectorTyID: {
    TypeStore& store =
      vm.create_type_vector(load_type(type->getVectorElementType(), false),
			    type->getVectorNumElements());
    loaded_type.insert(std::make_pair(type, store.addr));
    return store.addr;

  } break;

  default:
    type->dump();
    throw_error_message(Error::UNSUPPORT, "type:" + Util::num2dec_str(type->getTypeID()));
    break;
  }
  
  // 基本型をvmachineから払い出し、キャッシュに登録
  TypeStore& store = vm.create_type_basic(addr);
  loaded_type.insert(std::make_pair(type, store.addr));
  return store.addr;
}

// LLVMの定数(0うめ領域)を仮想マシンにロードする。
void LlvmAsmLoader::load_zero(FunctionContext& fc, ValueDest dst, const llvm::ConstantAggregateZero* src) {
  // 領域サイズを取得
  assert(data_layout->getTypeAllocSize(src->getType()) != 0);
  assert(data_layout->getTypeStoreSize(src->getType()) ==
	 data_layout->getTypeAllocSize(src->getType()));
  unsigned int size = data_layout->getTypeAllocSize(src->getType());

  // 0クリア
  memset(get_ptr_by_dest(fc, dst), 0, size);
}

// 現在解析中の関数の命令配列に命令を追記する。
void LlvmAsmLoader::push_code(FunctionContext& fc, Opcode opcode, int operand) {
  fc.code.push_back(Instruction::make_instruction(opcode, operand));
  print_debug("push code %02x %08x(%d)\n", opcode, operand, operand);
}

// 現在あるValueDestを元に、相対位置を変化させたValueDestを作成する。
LlvmAsmLoader::ValueDest LlvmAsmLoader::relocate_dest(ValueDest dst, int diff) {
  if (dst.is_k) {
    dst.addr.k += diff;
  } else {
    dst.addr.ptr += diff;
  }
  return dst;
}
