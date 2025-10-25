#ifndef _NAN_VIRTUAL_IMPL
#define _NAN_VIRTUAL_IMPL

#include <stack>
#include <stdlib.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "mewlib.h"
// #include "mewall.hpp"
#include "mewall"
#include "mewmath.hpp"
#include "mewpack"
#include "mewtypes.h"
// #include "mewdll.hpp"
#include "isolate.hpp"
#include "mewallocator.hpp"
// todo replace to tiny
#include <variant>

#pragma region NOTES
/* // ! IMPORTANT 
  for dll libraries should create 'pipe' 
  `bool pipe_foo(VirtualMachine* vm);`
*/ 
#pragma endregion NOTES

#include <iostream>
#include <string>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

class DynamicLibrary {
private:
#ifdef _WIN32
  HMODULE handle_;
#else
  void* handle_;
#endif

public:
  DynamicLibrary() : handle_(nullptr) {}
  
  ~DynamicLibrary() {
    close();
  }
  
  bool open(const char* libraryPath) {
#ifdef _WIN32
    handle_ = LoadLibraryA(libraryPath);
#else
    handle_ = dlopen(libraryPath, RTLD_LAZY);
#endif
    return handle_ != nullptr;
  }
  
  void close() {
    if (handle_) {
#ifdef _WIN32
      FreeLibrary(handle_);
#else
      dlclose(handle_);
#endif
      handle_ = nullptr;
    }
  }
  
  template<typename T>
  T getFunction(const char* functionName) {
    if (!handle_) {
      return nullptr;
    }
    
#ifdef _WIN32
    FARPROC proc = GetProcAddress(handle_, functionName);
    if (!proc) {
      return nullptr;
    }
    return reinterpret_cast<T>(proc);
#else
    void* symbol = dlsym(handle_, functionName);
    if (!symbol) {
      return nullptr;
    }
    return reinterpret_cast<T*>(symbol);
#endif
  }
};


namespace Virtual {
  using byte = mew::byte;
  struct VirtualMachine;
  typedef void(*VM_Processor)(VirtualMachine&, byte*);

  enum Instruction: byte {
    Instruction_NONE = 0,
    Instruction_LDLL,
    Instruction_CALL,
    Instruction_PUSH,
    Instruction_POP,
    Instruction_RPOP,

    Instruction_ADD,
    Instruction_SUB,
    Instruction_MUL,
    Instruction_DIV,

    Instruction_INC,
    Instruction_DEC,

    Instruction_XOR,
    Instruction_OR,
    Instruction_NOT,
    Instruction_AND,
    Instruction_LS, // left shift
    Instruction_RS, // right shift

    Instruction_NUM,  // arg type | number
    Instruction_STRUCT,  // arg type | int
    Instruction_INT,  // arg type | int
    Instruction_FLT,  // arg type | float
    Instruction_DBL,  // arg type | double
    Instruction_UINT, // arg type | uint
    Instruction_BYTE, // arg type | char
    Instruction_MEM,  // arg type | memory
    Instruction_REG,  // arg type | memory
    Instruction_HEAP, // arg type | heap begin
    Instruction_ST,   // arg type | stack top w offset
    
    Instruction_JMP,
    Instruction_RET,
    Instruction_EXIT,
    Instruction_TEST,
    Instruction_JE,
    Instruction_JEL,
    Instruction_JEM,
    Instruction_JNE,
    Instruction_JL,
    Instruction_JM,
    Instruction_MOV,   // replace head data from stack to memory
    Instruction_SWAP,   // replace head data from stack to memory
    Instruction_MSET,

    Instruction_SWST,  // set used stream
    Instruction_WRITE, // write to used stream
    Instruction_READ,  // read used stream
    Instruction_WINE,  // write if not exist
    Instruction_OPEN,  // open file as destinator
    Instruction_CLOSE, // close file as destinator

    Instruction_LM,

    Instruction_PUTC,
    Instruction_PUTI,
    Instruction_PUTS,
    Instruction_GETCH,
    Intruction_GetVM,
    Intruction_GetIPTR,
    Instruction_MOVRDI,
    Instruction_DCALL, // dynamic library function call ~!see notes
  };

  #define VIRTUAL_VERSION (Instruction_PUTS*100)+0x55
  #define GrabFromVM(var) memcpy(&var, vm.begin, sizeof(var)); vm.begin += sizeof(var);

  struct VM_MANIFEST_FLAGS {
    bool has_debug: 1 = false;
    byte ch[3];
  }; // must be 4 byte;

  struct FuncExternalLink {
    u8 type: 1;
    const char* lib_name;
    const char* func_name;
  }; 

  struct CodeManifestExtended {
    VM_MANIFEST_FLAGS flags;
    mew::stack<FuncExternalLink> extern_links;
  };

  struct Code {
    u64 capacity;
    Instruction* playground;
    u64 data_size = 0;
    byte* data = nullptr;
    CodeManifestExtended cme;
  };

#pragma region FILE

  void Code_SaveToFile(const Code& code, std::ofstream& file) {
    /* manifest */
    VM_MANIFEST_FLAGS mflags = code.cme.flags;
    mew::writeBytes(file, (uint)VIRTUAL_VERSION);
    mew::writeBytes(file, mflags, sizeof(uint));
    /* data */
    mew::writeArray(file, code.playground, code.capacity);
    mew::writeArray(file, code.data, code.data_size);
  }

  void Code_SaveToFile(const Code& code, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    MewAssert(file.is_open());
    file.seekp(std::ios::beg);
    /* version */
    Code_SaveToFile(code, file);
    file.close();
  }

  void Code_SaveToFile(const Code& code, const char* path) {
    std::filesystem::path __path(path);
    if (!__path.is_absolute()) {
      __path = std::filesystem::absolute(__path.lexically_normal());
    }
    Code_SaveFromFile(code, __path);
  }

  Code* Code_LoadFromFile(std::ifstream& file) {
    /* manifest */
    int file_version = mew::readUInt64(file);
    if (file_version != VIRTUAL_VERSION) {
      MewWarn("file version not support (%i != %i)", file_version, VIRTUAL_VERSION); 
      return nullptr;
    }
    Code* code = new Code();
    VM_MANIFEST_FLAGS& mflags = code->cme.flags;
    mew::readBytes(file, mflags);
    /* data */
    code->capacity = mew::readArray(file, code->playground);
    code->data_size = mew::readArray(file, code->data);
    return code;
  }

  Code* Code_LoadFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    MewAssert(file.is_open());
    file >> std::noskipws;
    Code* code = Code_LoadFromFile(file);
    file.close();
    return code;
  }

  Code* Code_LoadFromFile(const char* path) {
    std::filesystem::path __path(path);
    if (!__path.is_absolute()) {
      __path = std::filesystem::absolute(__path.lexically_normal());
    }
    return Code_LoadFromFile(__path);
  }

#pragma region VM
  enum VM_Status: byte {
    VM_Status_Panding = 0,
    VM_Status_Execute = 1 << 1,
    VM_Status_Ret     = 1 << 2,
    VM_Status_Error   = 1 << 3,
  };
  
  enum VM_TestStatus: byte {
    VM_TestStatus_Skip = 0,
    VM_TestStatus_Equal = 1 << 1,
    VM_TestStatus_Less  = 1 << 2,
    VM_TestStatus_More  = 1 << 3,
    VM_TestStatus_EqualMore  = VM_TestStatus_Equal | VM_TestStatus_More,
    VM_TestStatus_EqualLess  = VM_TestStatus_Equal | VM_TestStatus_Less,
  };
  
  enum VM_flags {
    None = 0,
    HeapLockExecute = 1 << 1,
  };

  template<u64 size>
  struct VM_Register {
    byte data[size];
  };
  
  enum struct VM_RegType: byte {
    None, R, RX, DX, FX, RDI
  };

#ifdef _WIN32
  typedef HMODULE handle_t;
#else
  typedef void* handle_t;
#endif


  typedef u64(*vm_dll_pipe_fn)(VirtualMachine* vm);

#pragma pack(push, 4)
  struct VM_DEBUG {
    byte last_head_byte = 0;
    char* last_fn = 0;
  };
  struct VirtualMachine {
    FILE* std_in = stdin;
    FILE* std_out = stdout;
    Isolate fs;
    Code* src;
    VM_DEBUG debug;
    VM_Register<4> _r[5];                       // 4*5(20)
    VM_Register<4> _fx[5];                      // 4*5(20)
    VM_Register<8> _rx[5];                      // 8*5(40)
    VM_Register<8> _dx[5];                      // 8*5(40)
    u64 capacity;                            // 8byte
    FILE *r_stream;                             // 8byte
    byte *memory, *heap,
        *begin, *end;                           // 4x8byte(24byte) 
    struct TestStatus {
      bytepartf(skip)
      bytepartf(equal)
      bytepartf(less)
      bytepartf(more)
    } test;                                     // 1byte
    VM_Status status;                           // 1byte
    struct Flags {
      bytepartt(heap_lock_execute)
      bytepartf(use_debug)
      bytepartf(use_isolate)
      bytepartf(in_neib_ctx)
    } flags;                                    // 1byte
    byte _pad0[1];
    mew::stack<u8, mew::MidAllocator<u8>> stack;                 // 24byte
    u64 rdi = 0;
    mew::stack<byte *, mew::MidAllocator<byte*>> begin_stack;        // 24byte             // 24byte
    mew::stack<Code*> libs;                 // 24byte
    mew::stack<handle_t> dll_handles;
    std::unordered_map<const char*, vm_dll_pipe_fn> dll_pipes;
    u64 process_cycle = 0;

    byte* getRegister(VM_RegType rt, byte idx, u64* size = nullptr) {
      MewUserAssert(idx < 5, "undefined register idx");
      switch (rt) {
        case VM_RegType::R: 
          if (!size) {*size = 4;}
          return this->_r[idx].data;     
        case VM_RegType::RX: 
          if (!size) {*size = 8;}
          return this->_rx[idx].data;
        case VM_RegType::FX: 
          if (!size) {*size = 4;}
          return this->_fx[idx].data;
        case VM_RegType::DX: 
          if (!size) {*size = 8;}
          return this->_dx[idx].data;
        case VM_RegType::RDI:
          return (byte*)&this->rdi;
        default: return nullptr;
      }
    }
  };                                            // 368byte 
#pragma pack(pop)

  u64 VM_OpenDll(VirtualMachine& vm, const char* name) {
    handle_t handle_;
    #ifdef _WIN32
      handle_ = LoadLibraryA(libraryPath);
    #else
      handle_ = dlopen(libraryPath, RTLD_LAZY);
    #endif
    MewForUserAssert(handle_ != nullptr, "cant open library (%s)", name);
    return vm.dll_handles.push(handle_);
  }

  void VM_CloseDlls(VirtualMachine& vm) {
    for (int i = 0; i < vm.dll_handles.count(); ++i) {
      auto handle_ = vm.dll_handles.at(i);
#ifdef _WIN32
      FreeLibrary(handle_);
#else
      dlclose(handle_);
#endif
    }
  }

  vm_dll_pipe_fn VM_GetDllPipeFunction(VirtualMachine& vm, u64 dll_idx, const char* name) {
    auto it = vm.dll_pipes.find(name);
    if (it != vm.dll_pipes.end()) {return it->second;}
    MewForUserAssert(vm.dll_handles.has(dll_idx), "cant find library by identifier(%i), maybe library wasnt loaded", dll_idx);
#ifdef _WIN32
    FARPROC proc = GetProcAddress(handle_, functionName);
#else
    void* proc = dlsym(handle_, functionName);
#endif
    if (!proc) {
      MewWarn("cant find function(%s) from library\n", name);
      return nullptr;
    }
    vm_dll_pipe_fn fn = (vm_dll_pipe_fn)(proc);
    vm.dll_pipes.insert({name, fn});
    return fn;
  }

  struct Code_AData {
    u64 size;
  };

  u64 Code_CountAData(Code& code) {
    if (code.adata == nullptr) { return 0; }
    Code_AData* adata = (Code_AData*)code.adata;
    u64 size_couter = 0;
    for (int i = 0; i < code.adata_count; ++i) {
      auto& local = adata[i];
      size_couter += local.size;
    }
    return size_couter;
  }
  
  void a() {
    sizeof(VirtualMachine);
  }

  #ifndef VM_ALLOC_ALIGN
    #define VM_ALLOC_ALIGN 512
  #endif
  #ifndef VM_MINHEAP_ALIGN
    #define VM_MINHEAP_ALIGN 128
  #endif
  #ifndef VM_CODE_ALIGN
    #define VM_CODE_ALIGN 8
  #endif
  #define __VM_ALIGN(_val, _align) (((int)((_val) / (_align)) + 1) * (_align))


  void Alloc(VirtualMachine& vm) {
    if (vm.memory != nullptr) {
      free(vm.memory);
    }
    vm.memory = new byte[VM_ALLOC_ALIGN];
    memset(vm.memory, Instruction_NONE, VM_ALLOC_ALIGN);
    vm.capacity = VM_ALLOC_ALIGN;
  }

  void Alloc(VirtualMachine& vm, Code& code) {
    u64 adata_count = Code_CountAData(code);
    u64 size = __VM_ALIGN(code.capacity+code.data_size+adata_count, VM_ALLOC_ALIGN);
    if ((size - code.capacity - code.data_size) <= 0) {
      size += VM_MINHEAP_ALIGN;
    }
    vm.memory = new byte[size];
    memset(vm.memory, Instruction_NONE, size);
    vm.capacity = size;
  }

  u32 DeclareProccessor(VirtualMachine& vm, VM_Processor proc) {
    MewNotImpl();
    // vm.procs.push_back(proc);
    // return vm.procs.size()-1;
  }

  void LoadMemory(VirtualMachine& vm, Code& code) {
    memcpy(vm.memory, code.playground, code.capacity);
    // todo load from .nlib file 
    for (int i = 0; i < code.cme.libs.size(); ++i) {
      Code* lib = Code_LoadFromFile(code.cme.libs.at(i));
      vm.libs.push(lib);
    }
  }

  void VM_ManualPush(VirtualMachine& vm, u32 x) {
    vm.stack.push(x);
  }

  void VM_Push(VirtualMachine& vm, byte head_byte, u32 number) {
    switch (head_byte) {
      case 0:
      case Instruction_FLT:
      case Instruction_NUM: {
        vm.stack.push(number);
      } break;
      case Instruction_MEM: {
        MewUserAssert(vm.heap+number < vm.end, "out of memory");
        byte* pointer = vm.heap+number;
        u32 x; memcpy(&x, pointer, sizeof(x));
        vm.stack.push(x, vm.rdi);
      } break;
      case Instruction_REG: {
        MewUserAssert(vm.heap+number < vm.end, "out of memory");
        vm.stack.push(number, vm.rdi);
      } break;
      case Instruction_ST: {
        MewUserAssert(vm.stack.has(number), "out of stack");
        vm.stack.push(vm.stack.at((int)number), vm.rdi);
      } break;
      default: MewNot(); break;
    }
  }

  byte* VM_GetReg(VirtualMachine& vm, u64* size = nullptr) {
    Virtual::VM_RegType rtype = (Virtual::VM_RegType)(*vm.begin++);
    byte ridx = *vm.begin++;
    return vm.getRegister(rtype, ridx);
  }

  void VM_Push(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    Instruction head_byte = (Instruction)*vm.begin++;
    switch (head_byte) {
      case 0:
      case Instruction_FLT:
      case Instruction_NUM: {
        u32 number = 0;
        memcpy(&number, vm.begin, sizeof(number));
        vm.stack.push(number);
        vm.begin += sizeof(number);
      } break;
      case Instruction_STRUCT: {
        auto arg = VM_GetArg(vm);
        vm.stack.push_array(arg.data, arg.size);
      } break;
      case Instruction_BYTE: { // <value:8>
        u8 number = 0;
        memcpy(&number, vm.begin, sizeof(number));
        vm.stack.push(number);
        vm.begin += sizeof(number);
      } break;
      case Instruction_MEM: { // <offset:32>
        u32 number = 0;
        memcpy(&number, vm.begin, sizeof(number));
        MewUserAssert(vm.heap+number < vm.end, "out of memory");
        byte* pointer = vm.heap+number;
        u32 x; memcpy(&x, pointer, sizeof(x));
        vm.stack.push(x);
        vm.begin += sizeof(number);
      } break;
      case Instruction_REG: { 
        u64 size;
        byte* reg = VM_GetReg(vm, &size);
        MewUserAssert(reg != nullptr, "invalid register");
        vm.stack.push((u32)*reg);
        if (size == 8) {
          vm.stack.push((u32)*(reg+sizeof(u32)));
        }
      } break;
      case Instruction_ST: { // offset:4 + arg
        int offset = 0; // byte offset
        GrabFromVM(offset);
        MewUserAssert(vm.stack.size() < offset, "out of stack");
        auto arg = VM_GetArg(vm);
        MewUserAssert(0 < vm.stack.size()-offset && vm.stack.size() < offset+arg.size, "out of stack");
        byte* value = vm.stack.begin()+(vm.stack.size() - offset - 1);
        memcpy(value, arg.data, arg.size);
      } break;
      default: MewNot(); break;
    }
  }
  
  void VM_Pop(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    MewAssert(!vm.stack.empty());
    vm.stack.asc_pop(1);
  }

  void VM_RPop(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    MewAssert(!vm.stack.empty());
    u64 size;
    u8* raw_reg = VM_GetReg(vm, &size);
    if (size == 4) {
      u32* value = (u32*)&vm.stack.top(vm.rdi+sizeof(u32));
      u32* reg = (u32*)raw_reg;
      *reg = *value;
      vm.stack.asc_pop(sizeof(u32));
    } else if (size == 8) {
      u64* value = (u64*)&vm.stack.top(vm.rdi+sizeof(u64));
      u64* reg = (u64*)raw_reg;
      *reg = *value;
      vm.stack.asc_pop(sizeof(u64));
    }
  }
  
  void VM_StackTop(VirtualMachine& vm, byte type, u32* x, byte** mem = nullptr) {
    switch (type) {
      case 0:
      case Instruction_FLT:
      case Instruction_ST:
      case Instruction_NUM: {
        MewUserAssert(!vm.stack.empty(), "stack is empty");
        u32 _top = vm.stack.top(vm.rdi);
        memmove(x, &_top, sizeof(_top));
      } break;
      case Instruction_MEM: {
        MewUserAssert(!vm.stack.empty(), "stack is empty");
        u32 _top = vm.stack.top(vm.rdi);
        u32 offset = _top;
        MewUserAssert(vm.heap+offset < vm.end, "out of memory");
        byte* pointer = vm.heap+offset;
        if (mem != nullptr) {
          *mem = pointer;
        }
        memmove(x, pointer, sizeof(*x));
      } break;

      default: MewNot(); break;
    }
  }

  void VM_SwitchContext(VirtualMachine& vm, Code* code) {
    vm.flags.in_neib_ctx = true;
  }

  void VM_ManualCall(VirtualMachine& vm, int libIDX, const char* fname) {
    Code* lib = vm.libs.at(libIDX);
    u64 offset = lib->find_label(fname);
    MewUserAssert(offset != -1, "undefined function");
    vm.begin_stack.push(vm.begin);
    vm.begin = (byte*)lib->playground+offset;
    VM_SwitchContext(vm, lib);
  }

  void VM_Call(VirtualMachine& vm) {
    u64 offset;
    GrabFromVM(offset);
    vm.begin_stack.push(vm.begin);
    vm.begin = vm.memory + offset;
    MewUserAssert(vm.begin <= vm.end, "segmentation fault, cant call out of code");
  }

  void VM_MathBase(VirtualMachine& vm, u32* x, u32* y, byte** mem = nullptr) {
    byte type_x = *vm.begin++;
    byte type_y = *vm.begin++;
    vm.rdi += sizeof(u32);
    VM_StackTop(vm, type_x, x, mem);
    vm.rdi -= sizeof(u32);
    VM_StackTop(vm, type_y, y);
  }

  int VM_GetOffset(VirtualMachine& vm) {
    int offset;
    memcpy(&offset, vm.begin, sizeof(int)); vm.begin += sizeof(int);
    return offset/4;
  }

#pragma region VM_ARG 
  typedef struct {
    VM_RegType type;
    byte idx;
  } VM_REG_INFO;

  class VM_ARG {
  public:
    VM_ARG() {}
    byte* data;
    byte* data2;
    u32 size;

    byte type;
    int& getInt() {
      return (int&)(*this->data);
    }
    lli& getLong() {
      return (lli&)(*this->data);
    }
    float& getFloat() {
      return (float&)(*this->data);
    }
    double& getDouble() {
      return (double&)(*this->data);
    }
    byte getByte() {
      return (byte)(*this->data);
    }

    byte* getMem() {
      return this->data;
    }

    static void do_math(VM_ARG& a, mew::asgio fn, bool depr_float = false) { 
      switch (a.type) {
        case Instruction_ST: mew::gen_asgio(fn, a.getInt()); break;
        case Instruction_REG: {
          VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
          switch (ri->type) {
            case VM_RegType::R: mew::gen_asgio(fn, a.getInt()); break;
            case VM_RegType::RX: mew::gen_asgio(fn, a.getLong()); break;
            case VM_RegType::FX: if (!depr_float) mew::gen_asgio(fn, a.getFloat()); break;
            case VM_RegType::DX: if (!depr_float) mew::gen_asgio(fn, a.getDouble()); break;
            default: MewUserAssert(false, "undefined reg type");
          }
          break;
        }
        default: MewUserAssert(false, "undefined arg type");
      } 
    }

    static void do_math(VM_ARG& a, VM_ARG& b, mew::adgio fn, bool depr_float = false) {
      switch (a.type) {
        case Instruction_ST: {
          switch (b.type) {
            case Instruction_ST: mew::gen_adgio(fn, a.getInt(), b.getInt()); break;
            case Instruction_REG: {
              VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
              switch (ri->type) {
                case VM_RegType::R: mew::gen_adgio(fn, a.getInt(), b.getInt()); break;
                case VM_RegType::RX: mew::gen_adgio(fn, a.getInt(), b.getLong()); break;
                case VM_RegType::FX: if (!depr_float) mew::gen_adgio(fn, a.getInt(), b.getFloat()); break;
                case VM_RegType::DX: if (!depr_float) mew::gen_adgio(fn, a.getInt(), b.getDouble()); break;
                default: MewUserAssert(false, "undefined reg type");
              }
            } break;
            case Instruction_NUM: {
              mew::gen_adgio(fn, a.getInt(), b.getInt());
            } break;
          }
        } break;
        case Instruction_REG: {
          VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
          switch (ri->type) {
            case VM_RegType::R: { 
              switch (b.type) {
                case Instruction_ST: mew::gen_adgio(fn, a.getInt(), b.getInt()); break;
                case Instruction_REG: {
                  VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
                  switch (ri->type) {
                    case VM_RegType::R: mew::gen_adgio(fn, a.getInt(), b.getInt()); break;
                    case VM_RegType::RX: mew::gen_adgio(fn, a.getInt(), b.getLong()); break;
                    case VM_RegType::FX: if (!depr_float) mew::gen_adgio(fn, a.getInt(), b.getFloat()); break;
                    case VM_RegType::DX: if (!depr_float) mew::gen_adgio(fn, a.getInt(), b.getDouble()); break;
                    default: MewUserAssert(false, "undefined reg type");
                  }
                } break;
                case Instruction_NUM: {
                  mew::gen_adgio(fn, a.getInt(), b.getInt());
                } break;
              }
            } break;
            case VM_RegType::RX: { 
              switch (b.type) {
                case Instruction_ST: mew::gen_adgio(fn, a.getLong(), b.getInt()); break;
                case Instruction_REG: {
                  VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
                  switch (ri->type) {
                    case VM_RegType::R: mew::gen_adgio(fn, a.getLong(), b.getInt()); break;
                    case VM_RegType::RX: mew::gen_adgio(fn, a.getLong(), b.getLong()); break;
                    case VM_RegType::FX: if (!depr_float) mew::gen_adgio(fn, a.getLong(), b.getFloat()); break;
                    case VM_RegType::DX: if (!depr_float) mew::gen_adgio(fn, a.getLong(), b.getDouble()); break;
                    default: MewUserAssert(false, "undefined reg type");
                  }
                } break;
                case Instruction_NUM: {
                  mew::gen_adgio(fn, a.getLong(), b.getInt());
                } break;
              }
            } break;
            case VM_RegType::FX: { 
              if (depr_float) break;
              switch (b.type) {
                case Instruction_ST: mew::gen_adgio(fn, a.getFloat(), b.getInt()); break;
                case Instruction_REG: {
                  VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
                  switch (ri->type) {
                    case VM_RegType::R: mew::gen_adgio(fn, a.getFloat(), b.getInt()); break;
                    case VM_RegType::RX: mew::gen_adgio(fn, a.getFloat(), b.getLong()); break;
                    case VM_RegType::FX: mew::gen_adgio(fn, a.getFloat(), b.getFloat()); break;
                    case VM_RegType::DX: mew::gen_adgio(fn, a.getFloat(), b.getDouble()); break;
                    default: MewUserAssert(false, "undefined reg type");
                  }
                } break;
                case Instruction_NUM: {
                  mew::gen_adgio(fn, a.getFloat(), b.getInt());
                } break;
              }
            } break;
            case VM_RegType::DX: { 
              if (depr_float) break;
              switch (b.type) {
                case Instruction_ST: mew::gen_adgio(fn, a.getDouble(), b.getInt()); break;
                case Instruction_REG: {
                  VM_REG_INFO* ri = (VM_REG_INFO*)a.data2;
                  switch (ri->type) {
                    case VM_RegType::R: mew::gen_adgio(fn, a.getDouble(), b.getInt()); break;
                    case VM_RegType::RX: mew::gen_adgio(fn, a.getDouble(), b.getLong()); break;
                    case VM_RegType::FX: mew::gen_adgio(fn, a.getDouble(), b.getFloat()); break;
                    case VM_RegType::DX: mew::gen_adgio(fn, a.getDouble(), b.getDouble()); break;
                    default: MewUserAssert(false, "undefined reg type");
                  }
                } break;
                case Instruction_NUM: {
                  mew::gen_adgio(fn, a.getDouble(), b.getInt());
                } break;
              }
            } break;
            default: MewUserAssert(false, "undefined reg type");
          }
        } break;
        default: MewUserAssert(false, "undefined arg type");
      } 
    }

    VM_ARG& operator++() {
      do_math(*this, mew::aginc);
      return *this;
    }

    VM_ARG& operator--() {
      do_math(*this, mew::agdec);
      return *this;
    }

    static void mov(VM_ARG& a, VM_ARG& b) {
      do_math(a, b, mew::agmov);
    }
    static void swap(VM_ARG& a, VM_ARG& b) {
      do_math(a, b, mew::agswap);
    }
  };

  VM_ARG& operator+(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agadd);
    return a;
  }
  VM_ARG& operator-(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agsub);
    return a;
  }
  VM_ARG& operator/(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agdiv);
    return a;
  }
  VM_ARG& operator*(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agmul);
    return a;
  }
  VM_ARG& operator>>(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agrs, true);
    return a;
  }
  VM_ARG& operator<<(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agls, true);
    return a;
  }
  VM_ARG& operator^(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agxor, true);
    return a;
  }
  VM_ARG& operator~(VM_ARG& a) {
    VM_ARG::do_math(a, mew::agnot, true);
    return a;
  }
  VM_ARG& operator|(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agor, true);
    return a;
  }
  VM_ARG& operator&(VM_ARG& a, VM_ARG& b) {
    VM_ARG::do_math(a, b, mew::agand, true);
    return a;
  }

  VM_ARG VM_GetArg(VirtualMachine& vm) {
    byte type = *vm.begin++;
    switch (type) {
      case Instruction_ST: {
        u32 offset;
        GrabFromVM(offset);
        VM_ARG arg;
        MewUserAssert(vm.stack.size() < offset, "out of stack");
        byte* num = vm.stack.begin()+(vm.stack.size() - 1 - offset);
        arg.data = num;
        arg.type = type;
        arg.size = sizeof(u32);
        return arg;
      };
      case Instruction_REG: {
        byte rtype = *vm.begin++;
        byte ridx = *vm.begin++;
        u64 size;
        VM_ARG arg;
        arg.data = vm.getRegister((VM_RegType)rtype, ridx, &size);
        VM_REG_INFO* ri = new VM_REG_INFO();
        ri->idx = ridx;
        ri->type = (VM_RegType)rtype;
        arg.data2 = (byte*)ri;
        arg.type = type;
        arg.size = (u32)size;
        return arg;
      }
      case Instruction_NUM: {
        s32 num;
        GrabFromVM(num);
        VM_ARG arg;
        arg.data = (byte*)num;
        arg.type = type;
        arg.size = sizeof(num);
        return arg;
      }
      case Instruction_MEM: {
        u64 offset;
        GrabFromVM(offset);
        MewUserAssert(vm.heap+offset < vm.end, "out of memory");
        byte* pointer = vm.heap+offset;
        u64 size;
        GrabFromVM(size);
        MewUserAssert(pointer+size < vm.end, "out of memory");
        VM_ARG arg;
        arg.data = pointer;
        arg.type = type;
        arg.size = size;
        return arg;
      }
    
      default: MewUserAssert(false, "undefined arg type");
    }
  }

#pragma endregion VM_ARG

  void VM_MovRDI(VirtualMachine& vm) {
    int offset = VM_GetOffset(vm);
    vm.rdi = offset;
  }

  void VM_Add(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a + b;
  }

  void VM_Sub(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a - b;
  }
  
  void VM_Mul(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a * b;
  }
  void VM_Div(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a / b;
  }
  
  void VM_Inc(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    ++a;
  }

  void VM_Dec(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    --a;
  }

  void VM_Xor(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a ^ b;
  }

  void VM_Or(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a | b;
  }

  void VM_Not(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    ~a;
  }
  
  void VM_And(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a & b;
  }

  void VM_LS(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a << b;
  }

  void VM_RS(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    a >> b;
  }
  
  void VM_ManualJmp(VirtualMachine& vm, u32 offset) {
    MewUserAssert(MEW_IN_RANGE(vm.memory, vm.end, vm.begin+offset), 
      "out of memory");
    vm.begin = vm.memory + offset;
    MewUserAssert(vm.begin <= vm.end, "segmentation fault, cant call out of code");
  }

  
  void VM_Jmp(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 offset;
    GrabFromVM(offset);
    vm.begin = vm.memory + offset;
    MewUserAssert(vm.begin <= vm.end, "segmentation fault, cant call out of code");
  }

  void VM_Ret(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    if (vm.begin_stack.empty()) {
      vm.status = VM_Status_Ret; return;
    }
    byte* begin = vm.begin_stack.top();
    vm.begin = begin;
    vm.begin_stack.pop();
  }

  void VM_Test(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u32 x, y;
    vm.test = {0};
    VM_MathBase(vm, (u32*)&x, (u32*)&y);
    int result = memcmp(&x, &y, sizeof(x));
    if (result > 0) {
      vm.test.more = 1;
    } else if (result < 0) {
      vm.test.less = 1;
    } else {
      vm.test.equal = 1;
    }
  }

  void VM_JE(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (vm.test.equal) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }
  void VM_JEL(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (vm.test.equal || vm.test.less) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }
  void VM_JEM(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (vm.test.equal || vm.test.more) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }
  void VM_JL(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (vm.test.less) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }
  void VM_JM(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (vm.test.more) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }
  void VM_JNE(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int offset; 
    memcpy(&offset, vm.begin, sizeof(int));
    if (!vm.test.equal) {
      VM_ManualJmp(vm, offset);
    } else {
      vm.begin += sizeof(int);
    }
  }

  void VM_Mov(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    VM_ARG::mov(a, b);
  }

  void VM_Swap(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    VM_ARG::swap(a, b);
  }

  void VM_MSet(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 x; /* start */
    u64 y; /* size  */
    u64 z; /* value */
    GrabFromVM(x);
    GrabFromVM(y);
    GrabFromVM(z);
    MewUserAssert(vm.heap+x < vm.end, "out of memory");
    MewUserAssert(vm.heap+x+y < vm.end, "out of memory");
    memset(vm.heap+x, z, y);
  }

  void VM_Putc(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    wchar_t long_char;
    memcpy(&long_char, vm.begin, sizeof(wchar_t)); vm.begin+=sizeof(wchar_t);
    fputwc(long_char, vm.std_out);
  }
  
  void VM_Puti(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto x = VM_GetArg(vm);
    int xi = x.getInt();
    char str[12] = {0};
    mew::_itoa10(xi, str);
    fputs(str, vm.std_out);
  }

  void VM_Puts(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 offset;
    GrabFromVM(offset);
    MewUserAssert(vm.heap+offset < vm.end, "out of memory");
    byte* pointer = vm.heap+offset;
    char* begin = (char*)pointer;
    while (*(begin) != 0) {
      putchar(*(begin++));
    }
  }

  void VM_Getch(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    int& a = VM_GetArg(vm).getInt();
    a = mew::wait_char();
  }
  
  // 
  void VM_LM(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto a = VM_GetArg(vm);
    auto b = VM_GetArg(vm);
    VM_ARG::mov(a, b);
  }

  void VM_Open(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 offset;
    GrabFromVM(offset);
    MewUserAssert(vm.heap+offset < vm.end, "out of memory");
    byte* path = vm.heap+offset;
    u32 descr = vm.fs.Open((const char*)path);
    VM_ManualPush(vm, descr);
  }

  void VM_Close(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto descr_arg = VM_GetArg(vm);
    u32 descr = (u32)descr_arg.getLong();
    vm.fs.Close(descr);
  }

  void VM_Wine(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 offset;
    GrabFromVM(offset);
    MewUserAssert(vm.heap+offset < vm.end, "out of memory");
    byte* path = vm.heap+offset;
    vm.fs.CreateFileIfNotExist((const char*)path);
  }
  
  void VM_Write(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u32 descr;
    GrabFromVM(descr);
    auto content = VM_GetArg(vm);
    u8* raw_content = content.getMem();
    u64 size = content.size;
    vm.fs.WriteToFile(descr, raw_content, size);
  }

  void VM_Read(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u32 descr;
    GrabFromVM(descr);
    auto dest = VM_GetArg(vm);
    u8* raw_dest = dest.getMem();
    u64 size = dest.size;
    vm.fs.ReadFromFile(descr, raw_dest, size);
  }
  
  void VM_GetIternalPointer(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    auto _from = VM_GetArg(vm);
    auto _size = VM_GetArg(vm);
    auto _where = VM_GetArg(vm);
    u8* from = (u8*)_from.getLong();
    u64 size = _size.getLong();
    u8* where = (u8*)_where.getLong();
    memcpy(where, from, size);
  }
  
  // put into rx4 vm pointer;
  void VM_GetVM(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 rx_size;
    auto rx4 = vm.getRegister(VM_RegType::RX, 4, &rx_size);
    byte* vm_ptr = (byte*)&vm;
    memcpy(rx4, vm_ptr, rx_size);
  }
  
  void VM_DCALL(VirtualMachine& vm) {
    vm.debug.last_fn = (char*)__func__;
    u64 lib_idx;
    GrabFromVM(lib_idx);
    u64 offset;
    GrabFromVM(offset);
    MewUserAssert(vm.heap+offset < vm.end, "out of memory");
    auto name = (const char*)vm.heap+offset;
    auto proc = VM_GetDllPipeFunction(vm, lib_idx, name);
    auto result = proc(&vm);
    vm.stack.push(result);
  }

  void RunLine(VirtualMachine& vm) {
    byte head_byte = *vm.begin++;
    vm.debug.last_head_byte = head_byte;
    if (vm.flags.heap_lock_execute) { 
      MewAssert(vm.begin < vm.heap);
    }
    switch (head_byte) {
      case Instruction_NONE: break;
      case Instruction_PUSH: {
        VM_Push(vm);
      } break;
      case Instruction_POP: {
        VM_Pop(vm);
      } break;
      case Instruction_RPOP: {
        VM_RPop(vm);
      } break;
      case Instruction_ADD: {
        VM_Add(vm);
      } break;
      case Instruction_SUB: {
        VM_Sub(vm);
      } break;
      case Instruction_MUL: {
        VM_Mul(vm);
      } break;
      case Instruction_DIV: {
        VM_Div(vm);
      } break;
      case Instruction_INC: {
        VM_Inc(vm);
      } break;
      case Instruction_DEC: {
        VM_Dec(vm);
      } break;
      case Instruction_XOR: {
        VM_Xor(vm);
      } break;
      case Instruction_OR: {
        VM_Or(vm);
      } break;
      case Instruction_NOT: {
        VM_Not(vm);
      } break;
      case Instruction_LS: {
        VM_LS(vm);
      } break;
      case Instruction_RS: {
        VM_RS(vm);
      } break;
      case Instruction_JMP: {
        VM_Jmp(vm);
      } break;
      case Instruction_RET: {
        VM_Ret(vm);
      } break;
      case Instruction_TEST: {
        VM_Test(vm);
      } break;
      case Instruction_JE: {
        VM_JE(vm);
      } break;
      case Instruction_JEL: {
        VM_JEL(vm);
      } break;
      case Instruction_JEM: {
        VM_JEM(vm);
      } break;
      case Instruction_JL: {
        VM_JL(vm);
      } break;
      case Instruction_JM: {
        VM_JM(vm);
      } break;
      case Instruction_JNE: {
        VM_JNE(vm);
      } break;
      case Instruction_MOV: {
        VM_Mov(vm);
      } break;
      case Instruction_SWAP: {
        VM_Swap(vm);
      } break;
      case Instruction_MSET: {
        VM_MSet(vm);
      } break;
      case Instruction_PUTC: {
        VM_Putc(vm);
      } break;
      case Instruction_PUTI: {
        VM_Puti(vm);
      } break;
      case Instruction_PUTS: {
        VM_Puts(vm);
      } break;
      case Instruction_GETCH: {
        VM_Getch(vm);
      } break;
      case Instruction_MOVRDI: {
        VM_MovRDI(vm);
      } break;
      case Instruction_CALL: {
        VM_Call(vm);
      } break;
      case Instruction_WINE: {
        VM_Wine(vm);
      } break;
      case Instruction_WRITE: {
        VM_Write(vm);
      } break;
      case Instruction_READ: {
        VM_Read(vm);
      } break;
      case Intruction_GetVM: {
        VM_GetVM(vm);
      } break;
      case Intruction_GetIPTR: {
        VM_GetIternalPointer(vm);
      } break;
      case Instruction_OPEN: {
        VM_Open(vm);
      } break;
      case Instruction_CLOSE: {
        VM_Close(vm);
      } break;
      case Instruction_LM: {
        VM_LM(vm);
      } break;
      case Instruction_DCALL: {
        VM_DCALL(vm);
      } break;
      case Instruction_EXIT: {
        vm.status = VM_Status_Ret;
      } break;
      default: MewUserAssert(false, "unsupported instruction");
    }
  }

  int Run(VirtualMachine& vm, Code& code) {
    u64 code_size = __VM_ALIGN(code.capacity, VM_CODE_ALIGN);
    MewAssert(vm.capacity > code_size);
    byte* begin = vm.memory;
    byte* end   = begin+vm.capacity;
    byte* alloc_space = begin+code_size+1;
    vm.flags.use_debug = code.cme.flags.has_debug;
    vm.src = &code;
    vm.heap = alloc_space;
    vm.begin = begin;
    vm.end = end;
    vm.status = VM_Status_Execute;
    if (code.data != nullptr) {
      memcpy(vm.heap, code.data, code.data_size*sizeof(*code.data));
    }
    if (code.adata != nullptr) {
      memset(vm.heap+code.data_size, 0, vm.capacity-(code.capacity+code.data_size));
    }
    while (vm.begin < vm.end && vm.status != VM_Status_Ret) {
      ++vm.process_cycle; RunLine(vm);
    }
    vm.status = VM_Status_Panding;
    if (vm.stack.empty()) {
      return 0;
    }
    return vm.stack.top();
  }

  int Execute(VirtualMachine& vm, Code& code) {
    Alloc(vm, code);
    LoadMemory(vm, code);
    return Run(vm, code);
  }
  
  int Execute(Code& code) {
    VirtualMachine vm;
    Alloc(vm, code);
    LoadMemory(vm, code);
    return Run(vm, code);
  }

  int Execute(const char* path) {
    Code* code = Code_LoadFromFile(path);
    return Execute(*code);
  }

  class VM_Async {
  public:
    struct ExecuteInfo {
      enum struct Status {
        Execute, Errored, Done
      } status;
      int result;
    }; 
  private:
    mew::stack<VirtualMachine*> m_vms;
    mew::stack<ExecuteInfo> m_execs;
  public:
    VM_Async() { }

    void hardStop() {
      for (int i = 0; i < m_vms.size(); ++i) {
        delete m_vms[i];
      }
      m_vms.clear();
      m_execs.clear();
    }
    
    int Append(Code& code) {
      VirtualMachine* vm = new VirtualMachine();
      Alloc(*vm, code);
      LoadMemory(*vm, code);
      u64 code_size = __VM_ALIGN(code.capacity, VM_CODE_ALIGN);
      MewAssert(vm->capacity > code_size);
      vm->flags.use_debug = code.cme.flags.has_debug;
      vm->src = &code;
      vm->heap = vm->begin+code_size+1;
      vm->begin = vm->memory;
      vm->end = vm->begin+vm->capacity;
      vm->status = VM_Status_Execute;
      if (code.data != nullptr) {
        memcpy(vm->heap, code.data, code.data_size*sizeof(*code.data));
      }
      m_execs.push((ExecuteInfo){ExecuteInfo::Status::Execute, -1});
      return m_vms.push(vm);
    }

    bool is_ends(int id) {
      return m_vms[id]->status == VM_Status_Ret;
    }

    int get_status_code(int id) {
      return m_execs[id].result;
    }
        
    VirtualMachine* GetById(int id) {
      return m_vms[id];
    }

    void ExecuteStep() {
      for (int i = 0; i < m_vms.size(); ++i) {
        m_execs[i].result = this->Run(*m_vms[i], *m_vms[i]->src);
        if (m_vms[i]->status == VM_Status_Panding) {
          m_execs[i].status = ExecuteInfo::Status::Done;
        }
        if (m_vms[i]->status == VM_Status_Error) {
          m_execs[i].status = ExecuteInfo::Status::Errored;
        }
      }
    }
    
    int Run(VirtualMachine& vm, Code& code) {
      if (!(vm.begin < vm.end && vm.status != VM_Status_Ret)) {
        vm.status = VM_Status_Panding;
        return vm.stack.top();
      }
      ++vm.process_cycle;
      try {
        RunLine(vm);
      } catch(std::exception& e) {
        u64 cursor = vm.capacity - (u64)(vm.end-vm.begin);
        for (int i = 0; i < code.cme.debug.size(); ++i) {
          if (code.cme.debug[i].cursor >= cursor) {
            fprintf(vm.std_out, "\n[DEBUG_ERROR] at (%i) in (%s)\n", code.cme.debug[i].line, vm.debug.last_fn);
            vm.status = VM_Status_Error;
            break;
          }
        }
      }
      return -1;
    }
  };

  class CodeBuilder {
  public:
    struct untyped_pair {
      byte* data;
      byte size;
    };
    static const u64 alloc_size = 8;
  private:
    u64 capacity, size, _data_size = 0;
    mew::stack<u64> _adatas;
    byte* code = nullptr, *data = nullptr;
    u64 stack_head = 0;
  public:
    CodeBuilder(): capacity(alloc_size), size(0), 
      code((byte*)realloc(NULL, alloc_size)), _data_size(0) { memset(code, 0, alloc_size); }

    inline u64 code_size() const noexcept {
      return size;
    }

    inline u64 cursor() const noexcept {
      return size;
    }

    inline u64 data_size() const noexcept {
      return _data_size;
    }

    template<typename K>
    u64 push(K& value) {
      *this
        << Instruction_PUSH 
        << Instruction_NUM
        << Instruction_STRUCT
        << sizeof(value);
      insert(value);
      return cursor();
    }

    inline u64 putRegister(VM_REG_INFO reg) {
      *this
        << Instruction_REG
        << (byte)reg.type
        << (byte)reg.idx;
      return cursor();
    }

    inline u64 putNumber(s32 num) {
      *this
        << Instruction_NUM
        << num;
      return cursor();
    }

    inline u64 putMem(u64 offset, u64 size) {
      *this
        << Instruction_MEM
        << offset
        << size;
      return cursor();
    }

    inline u64 putByte(u8 num) {
      *this
        << Instruction_BYTE
        << num;
      return cursor();
    }

    inline u64 putRdiOffset(u64 offset) {
      *this
        << Instruction_ST
        << offset;
      return cursor();
    }
    
    void Upsize(u64 _size = alloc_size) {
      byte* __temp_p = (byte*)realloc(code, capacity+_size);
      code = __temp_p;
      capacity += _size;
    }
    
    void UpsizeIfNeeds(u64 needs_size) {
      if (size+needs_size > capacity) {
        Upsize(needs_size);
      }
    }
        
    void AddData(byte* row, u64 size) {
      u64 __new_size = _data_size+size;
      data = (byte*)realloc(data, __new_size);
      memcpy(data+_data_size, row, size);
      _data_size = __new_size;
    }

    CodeBuilder& operator+=(const char* text) {
      AddData((byte*)text, strlen(text));
      return *this;
    }

    friend CodeBuilder& operator<<(CodeBuilder& cb, byte i) {
      cb.UpsizeIfNeeds(sizeof(i));
      cb.code[cb.size++] = i;
      return cb;
    } 

    friend CodeBuilder& operator<<(CodeBuilder& cb, u32 i) {
      cb.UpsizeIfNeeds(sizeof(i));
      memcpy(cb.code+cb.size, &i, sizeof(i));
      cb.size += sizeof(i);
      return cb;
    }

    CodeBuilder& putU64(u64 i) {
      UpsizeIfNeeds(sizeof(i));
      memcpy(code+size, &i, sizeof(i));
      size += sizeof(i);
      return *this;
    }

    friend CodeBuilder& operator<<(CodeBuilder& cb, Instruction i) {
      cb.UpsizeIfNeeds(sizeof(i));
      cb.code[cb.size++] = i;
      return cb;
    }
    friend CodeBuilder& operator<<(CodeBuilder& cb, int i) {
      cb.UpsizeIfNeeds(sizeof(i));
      memcpy(cb.code+cb.size, &i, sizeof(i));
      cb.size += sizeof(i);
      return cb;
    }
    friend CodeBuilder& operator<<(CodeBuilder& cb, size_t i) {
      cb.UpsizeIfNeeds(sizeof(i));
      memcpy(cb.code+cb.size, &i, sizeof(i));
      cb.size += sizeof(i);
      return cb;
    }
    
    template<typename K>
    CodeBuilder& insert(K& value) {
      UpsizeIfNeeds(sizeof(value));
      memcpy(code+size, &value, sizeof(value));
      size += sizeof(value);
      return *this;
    }

    friend CodeBuilder& operator<<(CodeBuilder& cb, untyped_pair i) {
      cb.UpsizeIfNeeds(i.size);
      memcpy(cb.code+cb.size, &i.data, i.size);
      cb.size += i.size;
      return cb;
    }

    friend CodeBuilder& operator>>(CodeBuilder& cb, untyped_pair i) {
      cb.UpsizeIfNeeds(i.size);
      memmove(cb.code+i.size, cb.code, cb.size);
      memcpy(cb.code, &i.data, i.size);
      cb.size += i.size;
      return cb;
    }

    void push_adata(u64 size) {
      _adatas.push(size);
    }

    Code* operator*() {
      Code* c = new Code();
      c->capacity   = size;
      c->playground = (Instruction*)(code);
      c->data_size  = _data_size;
      c->data       = data;
      return c;
    }
    Code operator*(int) {
      Code c;
      c.capacity    = size;
      c.playground  = (Instruction*)code;
      c.data_size   = _data_size;
      c.data        = data;
      return c;
    }

    byte* at(int idx) {
      u32 real_idx = (size + idx) % size;
      MewAssert(real_idx < size);
      return (code+real_idx);
    }

    byte* operator[](int idx) {
      return at(idx);
    }

    void force_data(u32 _size) {
      byte* _ndata = new byte[_data_size+_size];
      memcpy(_ndata, data, _data_size);
      _data_size += _size;
      data = _ndata;
    }
    
  };

  constexpr u64 GetVersion() {
    return VIRTUAL_VERSION;
  }

  #undef VIRTUAL_VERSION
#include "mewpop"
}
namespace Tests {
  bool test_Virtual() {
    try {
      using namespace Virtual;
      CodeBuilder builder;
      builder << Instruction_PUTS;
      builder << (u32)0U;
      builder << Instruction_EXIT;
      builder += "hellow word";
      Code* code = *builder;
      Code_SaveFromFile(*code, "./hellow_word.nb");
      // printf("[%u|%u]\n", code->capacity, code->data_size);
      Execute("./hellow_word.nb");
    } catch (std::exception e) {
      MewPrintError(e);
      return false;
    }
    return true;
  }
}

#endif