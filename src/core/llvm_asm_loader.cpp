
#include <iostream>
#include <llvm/AsmParser/Parser.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>

#include "error.hpp"
#include "llvm_asm_loader.hpp"
#include "util.hpp"
#include "vmachine.hpp"

using namespace usagi;

/**
 * 値がロード済みかチェックし該当した場合、既存のアドレスを戻すマクロ
 * @param value チェック対象の値
 */
#define check_same_value(loaded, value) auto exist = loaded.find(value); if (exist != loaded.end())  return &(exist->second)

// コンストラクタ。
LlvmAsmLoader::LlvmAsmLoader(VMachine& vmachine_) :
  context(llvm::getGlobalContext()),
  vmachine(vmachine_) {
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
    throw_error_message(Error::PARSE, error.getMessage().str());
  load(module);
}

// LLVMのアセンブリファイルを読み込んで仮想マシンにロードする。
/*
void LlvmAsmLoader::load_string(const std::string& src) {
  llvm::SMDiagnostic error;
  llvm::Module* module = llvm::ParseAssemblyString(src, error, context);
  if (module == nullptr)
    throw_error(Error::PARSE, error.getMessage().str());
  load(module);
}
//*/

// ロード済みの値とアドレスの対応関係を登録する。
Value* LlvmAsmLoader::assign_loaded(const llvm::Value* v, const Value& src) {
  // 登録済LLVM変数が再度登録されることはない
  assert(loaded.find(v) == loaded.end());

  // 値が未登録なので登録する
  return &(loaded.insert(std::make_pair(v, src)).first->second);
}

// LLVMの変数に対応する変数の番号を取得する。
int LlvmAsmLoader::assign_operand(FunctionContext& fc, const llvm::Value* v) {
  // 関数の引数は定数でもローカル変数に積むため、
  // ローカル変数判定を先に行う。
  // 既存のローカル変数の場合、その番号を戻す。
  for (unsigned int i = 0, size = fc.values.size(); i < size; i ++) {
    if (v == fc.values.at(i)) {
      return i;
    }
  }

  if (llvm::Constant::classof(v)) {
    // llvm::Constant->vaddr_tに変換
    Value* value = load(static_cast<const llvm::Constant*>(v));

    // 既存の定数の場合、その番号を戻す。
    for (int i = 0, size = fc.k.size(); i < size; i ++) {
      if (value == fc.k.at(i)) {
	// 定数の場合は- i -1
	return -1 - i;
      }
    }

    // 既存の定数でない場合、末尾に割り当てる。
    fc.k.push_back(value);
    return -fc.k.size();

  } else {    
    // 既存のローカル変数でない場合、末尾に割り当ている。
    fc.values.push_back(v);
    return fc.values.size() - 1;
  }
}

// LLVMの定数を仮想マシンにロードする。
Value* LlvmAsmLoader::load(const llvm::Constant* constant) {
  check_same_value(loaded, constant);
  
  // 値の型に合わせて分岐
  switch(constant->getValueID()) {
    //case llvm::Value::ArgumentVal: {} break;
    //case llvm::Value::BasicBlockVal: {} break;
  case llvm::Value::FunctionVal:
    return load(static_cast<const llvm::Function*>(constant));

    //case llvm::Value::GlobalAliasVal: {} break;
    //case llvm::Value::GlobalVariableVal: {} break;
    //case llvm::Value::UndefValueVal: {} break;
    //case llvm::Value::BlockAddressVal: {} break;
  case llvm::Value::ConstantExprVal:
    return load(static_cast<const llvm::ConstantExpr*>(constant));

    //case llvm::Value::ConstantAggregateZeroVal: {} break;
  case llvm::Value::ConstantDataArrayVal:
    return load(static_cast<const llvm::ConstantDataArray*>(constant));

    //case llvm::Value::ConstantDataVectorVal: {} break;
    //case llvm::Value::ConstantIntVal: {} break;
    //case llvm::Value::ConstantFPVal: {} break;
    //case llvm::Value::ConstantArrayVal: {} break;
    //case llvm::Value::ConstantStructVal: {} break;
    //case llvm::Value::ConstantVectorVal: {} break;
    //case llvm::Value::ConstantPointerNullVal: {} break;
    //case llvm::Value::MDNodeVal: {} break;
    //case llvm::Value::MDStringVal: {} break;
    //case llvm::Value::InlineAsmVal: {} break;
    //case llvm::Value::InstructionVal: {} break;
  default: {
    print_debug("unsupport type : %d\n", constant->getValueID());
    throw_error(Error::UNSUPPORT);
  } break;
  }
}

// LLVMの定数(DataArray)を仮想マシンにロードする。
Value* LlvmAsmLoader::load(const llvm::ConstantDataArray* data_array) {
  check_same_value(loaded, data_array);

  return assign_loaded(data_array,
		       vmachine.create_value_by_array(data_array->getElementByteSize(),
						      data_array->getNumElements(),
						      data_array->getRawDataValues().data()));
}

// LLVMの定数(Expr)を仮想マシンにロードする。
Value* LlvmAsmLoader::load(const llvm::ConstantExpr* expr) {
  check_same_value(loaded, expr);

  switch(expr->getOpcode()) {
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
    // case llvm::Instruction::BitCast:
    // case llvm::Instruction::AddrSpaceCast: {} break; //CastOps
    // case llvm::Instruction::Select: {} break; //Select
    // case llvm::Instruction::InsertElement: {} break; // InsertElement
    // case llvm::Instruction::ExtractElement: {} break; // ExtractElement
    // case llvm::Instruction::InsertValue: {} break; // InsertValue
    // case llvm::Instruction::ExtractValue: {} break; // ExtractValue
    // case llvm::Instruction::ShuffleVector: {} break; // ShuffleVector
  case llvm::Instruction::GetElementPtr: {
    const llvm::Value* tv = expr->getOperand(0);
    assert(llvm::Constant::classof(tv)); // オペランドは定数のはず。
    Value* target_value = load(static_cast<const llvm::Constant*>(tv));
    assert(target_value->is_pointer); // 1つ目のオペランドはポインタのはず。
    llvm::Type* op_type = tv->getType()->getPointerElementType();
    assert(op_type->isSized()); // サイズが確定可能と想定
    int delta = 0;
    // delta = Σ(size(n) * operand(n))を計算
    for (unsigned int i = 1, num_operands = expr->getNumOperands(); i < num_operands; i ++) {
      // 2つ目以降のオペランドは数値のはず。
      assert(llvm::ConstantInt::classof(expr->getOperand(i)));
      const llvm::ConstantInt* op = static_cast<const llvm::ConstantInt*>(expr->getOperand(i));
      delta += (uint32_t)data_layout->getTypeStoreSize(op_type) * op->getSExtValue();
      print_debug("(%d|%d) x %lld\n",
		  (uint32_t)data_layout->getTypeStoreSize(op_type),
		  (uint32_t)data_layout->getTypeAllocSize(op_type),
		  op->getSExtValue());
      if (llvm::SequentialType::classof(op_type)) {
	// pointer, array, vectorの場合、中身の型を見る。
	op_type = static_cast<const llvm::SequentialType*>(op_type)->getElementType();

      } else if (llvm::StructType::classof(op_type)) {
	// structの場合、n番目の要素の型を見る。
	op_type = static_cast<const llvm::StructType*>(op_type)->getElementType(op->getZExtValue());
      }
    }
    return assign_loaded(expr, vmachine.create_pointer(target_value, delta));
  } break;

    // case Instruction::ICmp:
    // case Instruction::FCmp: {} break; // Cmp
    //default: {} break; // Binary
  default: {
    print_debug("unsupport expr : %d %s\n", expr->getOpcode(), expr->getOpcodeName());
    throw_error(Error::UNSUPPORT);
  } break;
  }
}

// LLVMの関数を仮想マシンにロードする。
Value* LlvmAsmLoader::load(const llvm::Function* function) {
  check_same_value(loaded, function);

  // 命令配列
  std::vector<uint32_t> code;
  // 定数
  std::vector<Value*> k;
  // 変数
  std::vector<const llvm::Value*> values;

  FunctionContext fc = {code, k, values };

  if (!function->isDeclaration()) {
    // 引数を変数の先頭に登録
    for (auto arg = function->getArgumentList().begin();
	 arg != function->getArgumentList().end(); arg ++) {
      assert(arg->hasName()); // 名前を持つはず
      const llvm::Value* arg_val = function->getValueSymbolTable().lookup(arg->getName());
      assert(arg_val != nullptr);
      values.push_back(arg_val);
    }

    for (auto block = function->begin(); block != function->end(); block ++) {
      for (auto i = block->begin(); i != block->end(); i ++) {
	// LLVMに対応した命令に置き換え
	switch(i->getOpcode()) {
	case llvm::Instruction::Ret: {
	  // Ret命令の追加
	  const llvm::ReturnInst& inst = static_cast<const llvm::ReturnInst&>(*i);
	  push_codeA(fc, Opcode::RETURN,
		     assign_operand(fc, inst.getReturnValue()));
	} break;

	case llvm::Instruction::Call: {
	  const llvm::CallInst& inst = static_cast<const llvm::CallInst&>(*i);
	  // インラインアセンブラ未対応
	  if (inst.isInlineAsm()) throw_error(Error::UNSUPPORT);
	  if (inst.isTailCall()) {
	    // 末尾再帰の場合、関数、引数の数、引数、、、の命令を追加
	    push_codeAB(fc, Opcode::TAILCALL,
			assign_operand(fc, inst.getCalledFunction()),
			inst.getNumArgOperands());
	    unsigned int arg_idx = 0;
	    while(arg_idx < inst.getNumArgOperands()) {
	      int op_a = assign_operand(fc, inst.getArgOperand(arg_idx ++));
	      int op_b = (arg_idx < inst.getNumArgOperands() ?
			  assign_operand(fc, inst.getArgOperand(arg_idx ++)) : 0);
	      push_codeAB(fc, Opcode::EXTRAARG2, op_a, op_b);
	    }

	  } else if (inst.getNumArgOperands() == 0) {
	    // 引数が0個の場合、戻り値格納アドレス、関数の命令を追加
	    push_codeAB(fc, Opcode::CALL,
			assign_operand(fc, &inst),
			assign_operand(fc, inst.getCalledFunction()));

	  } else {
	    // 引数がある場合、戻り値格納アドレス、関数、引数の数、引数、、、の命令を追加
	    push_codeAB(fc, Opcode::CALLX,
			assign_operand(fc, &inst),
			assign_operand(fc, inst.getCalledFunction()));
	    unsigned int arg_idx = 0;
	    while(arg_idx < inst.getNumArgOperands()) {
	      int op_a = (arg_idx == 0 ?
			  inst.getNumArgOperands() :
			  assign_operand(fc, inst.getArgOperand(arg_idx ++)));
	      int op_b = (arg_idx < inst.getNumArgOperands() ?
			  assign_operand(fc, inst.getArgOperand(arg_idx ++)) : 0);
	      push_codeAB(fc, Opcode::EXTRAARG2, op_a, op_b);
	    }
	  }
	} break;

	default: {
	  print_debug("unsupport instruction : %s\n", i->getOpcodeName());
	  throw_error(Error::UNSUPPORT);
	} break;
	};
      }
    }
  }

  return assign_loaded(function,
		       vmachine.create_function(function->isVarArg(),
						function->arg_size(),
						values.size(), code, k));
}

// LLVMのモジュールを仮想マシンにロードする。
void LlvmAsmLoader::load(const llvm::Module* module) {
  /// @todo DataLayout, Tripleのチェック
  print_debug("DataLayout: %s\n", module->getDataLayoutStr().c_str());
  print_debug("Triple: %s\n",     module->getTargetTriple().c_str());
  
  // データレイアウトの取得
  data_layout = module->getDataLayout();
  // 大域変数の読み込み
  for (auto gl = module->global_begin(); gl != module->global_end(); gl ++) {
    /// @todo スレッドローカル、セクション、公開の扱い
    vmachine.set_global_value(gl->getName().str(), load(gl));
  }

  // 関数の読み込み
  for (auto fn = module->begin(); fn != module->end(); fn ++) {
    /// @todo スレッドローカル、セクション、公開の扱い
    vmachine.set_global_value(fn->getName().str(), load(fn));    
  }

  // AliasListは未対応
  if (module->getAliasList().size() != 0) {
    throw_error(Error::UNSUPPORT);
  }
}

// LLVMの大域変数を仮想マシンにロードする。
Value* LlvmAsmLoader::load(const llvm::GlobalVariable* variable) {
  check_same_value(loaded, variable);

  if (variable->hasInitializer()) {
    return assign_loaded(variable,
			 vmachine.create_pointer(load(variable->getInitializer()), 0));
  } else {
    assert(false); // あっている？
    return assign_loaded(variable,
			 vmachine.create_null());
  }
}

// 現在解析中の関数の命令配列に命令を追記する。
void LlvmAsmLoader::push_codeA(FunctionContext& fc, Opcode opcode, int a) {
  assert((a <<  0 & ~0x00ffffff) == 0);

  uint32_t v =
    (static_cast<uint32_t>(opcode) << 24 & 0xff000000) |
    (static_cast<uint32_t>(a)      <<  0 & 0x00ffffff);

  fc.code.push_back(v);
}

// 現在解析中の関数の命令配列に命令を追記する。
void LlvmAsmLoader::push_codeAB(FunctionContext& fc, Opcode opcode, int a, int b) {
  assert((a << 12 & ~0x00fff000) == 0 || ((-a-1) << 12 & ~0x00fff000) == 0);
  assert((b <<  0 & ~0x00000fff) == 0 || ((-b-1) <<  0 & ~0x00000fff) == 0);

  uint32_t v  =
    (static_cast<uint32_t>(opcode) << 24 & 0xff000000) |
    (static_cast<uint32_t>(a)      << 12 & 0x00fff000) |
    (static_cast<uint32_t>(b)      <<  0 & 0x00000fff);

  fc.code.push_back(v);
}
