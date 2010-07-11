/*
 * GroovyJITCompiler.h
 *
 *  Created on: 11 Jul 2010
 *      Author: chanwit
 */

#ifndef GROOVY_JIT_COMPILER_H
#define GROOVY_JIT_COMPILER_H

#include "j3/JavaJITCompiler.h"

using namespace j3;

namespace g7 {

class GroovyJITCompiler : public JavaJITCompiler {
public:
	GroovyJITCompiler(const std::string &ModuleID, bool trusted = false);
	~GroovyJITCompiler();
};

}

#endif /* GROOVY_JIT_COMPILER_H */
