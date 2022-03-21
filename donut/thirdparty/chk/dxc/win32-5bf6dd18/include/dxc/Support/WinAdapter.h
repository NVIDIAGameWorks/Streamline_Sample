//===- WinAdapter.h - Windows Adapter for non-Windows platforms -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines Windows-specific types, macros, and SAL annotations used
// in the codebase for non-Windows platforms.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_WIN_ADAPTER_H
#define LLVM_SUPPORT_WIN_ADAPTER_H

#ifndef _WIN32

#ifdef __cplusplus
#include <atomic>
#include <cassert>
#include <climits>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>
#endif // __cplusplus

//===----------------------------------------------------------------------===//
//
//                             Begin: Macro Definitions
//
//===----------------------------------------------------------------------===//
#define C_ASSERT(expr) static_assert((expr), "")
#define ATLASSERT assert

#define ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))

#define _countof(a) (sizeof(a) / sizeof(*(a)))

#define __declspec(x)

#define uuid(id)

#define STDMETHODCALLTYPE
#define STDAPI extern "C" HRESULT STDAPICALLTYPE
#define STDAPI_(type) extern "C" type STDAPICALLTYPE
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE

#define UNREFERENCED_PARAMETER(P) (void)(P)

#define RtlEqualMemory(Destination, Source, Length)                            \
  (!memcmp((Destination), (Source), (Length)))
#define RtlMoveMemory(Destination, Source, Length)                             \
  memmove((Destination), (Source), (Length))
#define RtlCopyMemory(Destination, Source, Length)                             \
  memcpy((Destination), (Source), (Length))
#define RtlFillMemory(Destination, Length, Fill)                               \
  memset((Destination), (Fill), (Length))
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define MoveMemory RtlMoveMemory
#define CopyMemory RtlCopyMemory
#define FillMemory RtlFillMemory
#define ZeroMemory RtlZeroMemory

#define FALSE 0
#define TRUE 1

#define REGDB_E_CLASSNOTREG 1

// We ignore the code page completely on Linux.
#define GetConsoleOutputCP() 0

#define _HRESULT_TYPEDEF_(_sc) ((HRESULT)_sc)
#define DISP_E_BADINDEX _HRESULT_TYPEDEF_(0x8002000BL)

// This is an unsafe conversion. If needed, we can later implement a safe
// conversion that throws exceptions for overflow cases.
#define UIntToInt(uint_arg, int_ptr_arg) *int_ptr_arg = uint_arg

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

// Use errno to implement {Get|Set}LastError
#define GetLastError() errno
#define SetLastError(ERR) errno = ERR

// Map these errors to equivalent errnos.
#define ERROR_SUCCESS 0L
#define ERROR_OUT_OF_STRUCTURES ENOMEM
#define ERROR_UNHANDLED_EXCEPTION EINTR
#define ERROR_NOT_FOUND ENOTSUP
#define ERROR_NOT_CAPABLE EPERM
#define ERROR_FILE_NOT_FOUND ENOENT
#define ERROR_IO_DEVICE EIO
#define ERROR_INVALID_HANDLE EBADF

// Used by HRESULT <--> WIN32 error code conversion
#define SEVERITY_ERROR 1
#define FACILITY_WIN32 7
#define HRESULT_CODE(hr) ((hr)&0xFFFF)
#define MAKE_HRESULT(severity, facility, code)                                 \
  ((HRESULT)(((unsigned long)(severity) << 31) |                               \
             ((unsigned long)(facility) << 16) | ((unsigned long)(code))))

#define FILE_TYPE_UNKNOWN 0x0000
#define FILE_TYPE_DISK 0x0001
#define FILE_TYPE_CHAR 0x0002
#define FILE_TYPE_PIPE 0x0003
#define FILE_TYPE_REMOTE 0x8000

#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// STGTY ENUMS
#define STGTY_STORAGE 1
#define STGTY_STREAM 2
#define STGTY_LOCKBYTES 3
#define STGTY_PROPERTY 4

// Storage errors
#define STG_E_INVALIDFUNCTION 1L
#define STG_E_ACCESSDENIED 2L

#define STREAM_SEEK_SET 0
#define STREAM_SEEK_CUR 1
#define STREAM_SEEK_END 2

#define HEAP_NO_SERIALIZE 1

#define MB_ERR_INVALID_CHARS 0x00000008 // error for invalid chars

#define _atoi64 atoll
#define sprintf_s snprintf
#define _strdup strdup
#define vsprintf_s vsprintf
#define strcat_s strcat

#define OutputDebugStringW(msg) fputws(msg, stderr)

#define OutputDebugStringA(msg) fputs(msg, stderr)
#define OutputDebugFormatA(...) fprintf(stderr, __VA_ARGS__)

// Event Tracing for Windows (ETW) provides application programmers the ability
// to start and stop event tracing sessions, instrument an application to
// provide trace events, and consume trace events.
#define DxcEtw_DXCompilerCreateInstance_Start()
#define DxcEtw_DXCompilerCreateInstance_Stop(hr)
#define DxcEtw_DXCompilerCompile_Start()
#define DxcEtw_DXCompilerCompile_Stop(hr)
#define DxcEtw_DXCompilerDisassemble_Start()
#define DxcEtw_DXCompilerDisassemble_Stop(hr)
#define DxcEtw_DXCompilerPreprocess_Start()
#define DxcEtw_DXCompilerPreprocess_Stop(hr)
#define DxcEtw_DxcValidation_Start()
#define DxcEtw_DxcValidation_Stop(hr)

#define UInt32Add UIntAdd
#define Int32ToUInt32 IntToUInt

//===--------------------- HRESULT Related Macros -------------------------===//

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)

#define E_ABORT (HRESULT)0x80004004
#define E_ACCESSDENIED (HRESULT)0x80070005
#define E_FAIL (HRESULT)0x80004005
#define E_HANDLE (HRESULT)0x80070006
#define E_INVALIDARG (HRESULT)0x80070057
#define E_NOINTERFACE (HRESULT)0x80004002
#define E_NOTIMPL (HRESULT)0x80004001
#define E_OUTOFMEMORY (HRESULT)0x8007000E
#define E_POINTER (HRESULT)0x80004003
#define E_UNEXPECTED (HRESULT)0x8000FFFF

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define DXC_FAILED(hr) (((HRESULT)(hr)) < 0)

#define HRESULT_FROM_WIN32(x)                                                  \
  (HRESULT)(x) <= 0 ? (HRESULT)(x)                                             \
                    : (HRESULT)(((x)&0x0000FFFF) | (7 << 16) | 0x80000000)

//===----------------------------------------------------------------------===//
//
//                         Begin: Disable SAL Annotations
//
//===----------------------------------------------------------------------===//
#define _In_
#define _In_z_
#define _In_opt_
#define _In_opt_count_(size)
#define _In_opt_z_
#define _In_reads_(size)
#define _In_reads_bytes_(size)
#define _In_reads_bytes_opt_(size)
#define _In_reads_opt_(size)
#define _In_reads_to_ptr_(ptr)
#define _In_count_(size)
#define _In_range_(lb, ub)
#define _In_bytecount_(size)
#define _In_NLS_string_(size)
#define __in_bcount(size)

#define _Out_
#define _Out_bytecap_(nbytes)
#define _Out_writes_to_opt_(a, b)
#define _Outptr_
#define _Outptr_opt_
#define _Outptr_opt_result_z_
#define _Out_opt_
#define _Out_writes_(size)
#define _Out_write_bytes_(size)
#define _Out_writes_z_(size)
#define _Out_writes_all_(size)
#define _Out_writes_bytes_(size)
#define _Outref_result_buffer_(size)
#define _Outptr_result_buffer_(size)
#define _Out_cap_(size)
#define _Out_cap_x_(size)
#define _Out_range_(lb, ub)
#define _Outptr_result_z_
#define _Outptr_result_buffer_maybenull_(ptr)
#define _Outptr_result_maybenull_
#define _Outptr_result_nullonfailure_

#define __out_ecount_part(a, b)

#define _Inout_
#define _Inout_z_
#define _Inout_opt_
#define _Inout_cap_(size)
#define _Inout_count_(size)
#define _Inout_count_c_(size)
#define _Inout_opt_count_c_(size)
#define _Inout_bytecount_c_(size)
#define _Inout_opt_bytecount_c_(size)

#define _Ret_maybenull_
#define _Ret_notnull_
#define _Ret_opt_

#define _Use_decl_annotations_
#define _Analysis_assume_(expr)
#define _Analysis_assume_nullterminated_(x)
#define _Success_(expr)

#define __inexpressible_readableTo(size)
#define __inexpressible_writableTo(size)

#define _Printf_format_string_
#define _Null_terminated_
#define __fallthrough

#define _Field_size_(size)
#define _Field_size_full_(size)
#define _Field_size_opt_(size)
#define _Post_writable_byte_size_(size)
#define _Post_readable_byte_size_(size)
#define __drv_allocatesMem(mem)

#define _COM_Outptr_
#define _COM_Outptr_opt_
#define _COM_Outptr_result_maybenull_

#define _Null_
#define _Notnull_
#define _Maybenull_

#define _Outptr_result_bytebuffer_(size)

#define __debugbreak()

// GCC produces erros on calling convention attributes.
#ifdef __GNUC__
#define __cdecl
#define __CRTDECL
#define __stdcall
#define __vectorcall
#define __thiscall
#define __fastcall
#define __clrcall
#endif // __GNUC__

//===----------------------------------------------------------------------===//
//
//                             Begin: Type Definitions
//
//===----------------------------------------------------------------------===//

#ifdef __cplusplus

typedef unsigned char BYTE;
typedef unsigned char *LPBYTE;

typedef BYTE BOOLEAN;
typedef BOOLEAN *PBOOLEAN;

typedef bool BOOL;
typedef BOOL *LPBOOL;

typedef int INT;
typedef long LONG;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef long long LONGLONG;
typedef long long LONG_PTR;
typedef unsigned long long ULONGLONG;

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef DWORD *LPDWORD;

typedef uint32_t UINT32;
typedef uint64_t UINT64;

typedef signed char INT8, *PINT8;
typedef signed int INT32, *PINT32;

typedef size_t SIZE_T;
typedef const char *LPCSTR;
typedef const char *PCSTR;

typedef int errno_t;

typedef wchar_t WCHAR;
typedef wchar_t *LPWSTR;
typedef wchar_t *PWCHAR;
typedef const wchar_t *LPCWSTR;
typedef const wchar_t *PCWSTR;

typedef WCHAR OLECHAR;
typedef OLECHAR *BSTR;
typedef OLECHAR *LPOLESTR;
typedef char *LPSTR;

typedef void *LPVOID;
typedef const void *LPCVOID;

typedef std::nullptr_t nullptr_t;

typedef signed int HRESULT;

//===--------------------- Handle Types -----------------------------------===//

typedef void *HANDLE;

#define DECLARE_HANDLE(name)                                                   \
  struct name##__ {                                                            \
    int unused;                                                                \
  };                                                                           \
  typedef struct name##__ *name
DECLARE_HANDLE(HINSTANCE);

typedef void *HMODULE;

#define STD_INPUT_HANDLE ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE ((DWORD)-12)

//===--------------------- Struct Types -----------------------------------===//

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _BY_HANDLE_FILE_INFORMATION {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD dwVolumeSerialNumber;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD nNumberOfLinks;
  DWORD nFileIndexHigh;
  DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *PBY_HANDLE_FILE_INFORMATION,
    *LPBY_HANDLE_FILE_INFORMATION;

typedef struct _WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[260];
  WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER;

typedef LARGE_INTEGER *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef ULARGE_INTEGER *PULARGE_INTEGER;

typedef struct tagSTATSTG {
  LPOLESTR pwcsName;
  DWORD type;
  ULARGE_INTEGER cbSize;
  FILETIME mtime;
  FILETIME ctime;
  FILETIME atime;
  DWORD grfMode;
  DWORD grfLocksSupported;
  CLSID clsid;
  DWORD grfStateBits;
  DWORD reserved;
} STATSTG;

enum tagSTATFLAG {
  STATFLAG_DEFAULT = 0,
  STATFLAG_NONAME = 1,
  STATFLAG_NOOPEN = 2
};

// TODO: More definitions will be added here.

#endif // __cplusplus

#endif // _WIN32

#endif // LLVM_SUPPORT_WIN_ADAPTER_H
