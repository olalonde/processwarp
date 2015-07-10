#pragma once

#include "definitions.hpp"

namespace processwarp {
  class BuiltinWarp {
  public:

    /**
     * This function register function that will be called at after warp.
     * parameter from src;
     * vaddr_t function
     */
    static BuiltinPost at_after_warp(Process& proc, Thread& thread, BuiltinFuncParam p,
				     vaddr_t dst, std::vector<uint8_t>& src);

    /**
     * This function register function that will be called at befor warp.
     * parameter from src;
     * vaddr_t function
     */
    static BuiltinPost at_befor_warp(Process& proc, Thread& thread, BuiltinFuncParam p,
				     vaddr_t dst, std::vector<uint8_t>& src);

    /**
     * This function check to warp is requested.
     */
    static BuiltinPost poll_warp_request(Process& proc, Thread& thread, BuiltinFuncParam p,
					 vaddr_t dst, std::vector<uint8_t>& src);

    /**
     * This function register library functions in virtual machine.
     * @param vm target virtual machine for regist on.
     */
    static void regist(Process& vm);

    /**
     * This function set a parameter to warp function.
     * paramter from src;
     * i32 key
     * i32 value
     */
    static BuiltinPost set_processwarp_param(Process& proc, Thread& thread, BuiltinFuncParam p,
					     vaddr_t dst, std::vector<uint8_t>& src);
  };
}
