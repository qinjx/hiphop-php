/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010- Facebook, Inc. (http://www.facebook.com)         |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#include <runtime/base/hphp_system.h>
#include <runtime/base/compiler_id.h>

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////


#ifdef HPHP_VERSION
#undef HPHP_VERSION
#endif
#define HPHP_VERSION(v) return #v;
const char* getHphpCompilerVersion() {
#include "../../version"
}

const char* getHphpCompilerId() {
#ifdef COMPILER_ID
  return COMPILER_ID;
#else
  return "";
#endif
}

///////////////////////////////////////////////////////////////////////////////
}