package cgosymbolizer

import (
	"runtime"
)

/*
#cgo CFLAGS: -g1
#include "cgosymbolizer_darwin.h"
*/
import "C"

func init() {
	C.cgo_init()
	const structVersion = 0
	runtime.SetCgoTraceback(structVersion, C.cgo_traceback, C.cgo_context, C.cgo_symbolizer)
}
