#pragma once
#include <windows.h>

#pragma warning(disable:4201) // nameless unions
#pragma warning(disable:4121) // alignment of a member was sensitive to packing (i don't care)

#include <Rtl.Types.h>
//#include <Rtl.Memory.h>
#include <Rtl.Strings.h>
#include <Rtl.Kernel.h>
#include <Rtl.Hook.h>
#include <Rtl.Options.h>
#include <Rtl.Thunks.h>
#include <Rtl.Misc.h>

#pragma comment(linker,"/merge:.CRT=.data")
