// Copyright (C) 2024 - 2025 Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
// Contributors: KC, LT
// Maintainer: LT

#pragma once
#pragma warning(disable : 4996)

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

// KEEP THE EMPTY LINE ABOVE SO CLANGFORMAT DOES NOT REORDER THE HEADER BELOW
#  include <errhandlingapi.h>
#else
#  include <dlfcn.h>
#endif

#include <algorithm>
#include <any>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Temporary, for debugging
#include <iostream>

#if FlexmlRT_IS_STATIC_LIB == 1
#  pragma message("FlexmlRT_IS_STATIC_LIB is set to 1 (static linking)")
#else
#  pragma message("FlexmlRT_IS_STATIC_LIB is set to 0 (dynamic linking)")
#endif

#ifdef _WIN32
#  if FlexmlRT_IS_STATIC_LIB == 0
#    ifdef FLEXMLRT_EXPORT
#      define FLEXMLRT_DLL __declspec(dllexport)
#    else
#      define FLEXMLRT_DLL __declspec(dllimport)
#    endif
#  else // FlexmlRT_IS_STATIC_LIB == 1
#    define FLEXMLRT_DLL
#  endif
#  define FLEXMLRT_LIB_NAME "flexmlrt.dll"
#else
#  define FLEXMLRT_DLL
#  define FLEXMLRT_LIB_NAME "libflexmlrt.so"
#endif

// Define this macro to use dlmopen instead of dlopen
// #define FLEXML_LOADER_USE_DLMOPEN 1

// Define this macro to turn on debug messages
// #define FLEXML_LOADER_DEBUG 1

// Define this macro to use in-library destructor
// #define FLEXML_CLIENT_USE_IN_LIBRARY_DESTRUCTOR 1

// Prints debug messages to stdout if FLEXML_LOADER_DEBUG is on.
#ifdef FLEXML_LOADER_DEBUG
#  include <iostream>

#  define FLEXML_LOADER_DEBUG_PRINT(str)                                       \
    do {                                                                       \
      std::cout << "DEBUG: xilinx_apps loader: " << str << std::endl;          \
    } while (false)
#else
#  define FLEXML_LOADER_DEBUG_PRINT(str)
#endif

#define MAJOR_VER 3
#define MINOR_VER 0

extern "C" FLEXMLRT_DLL void
flexml_rt_executeRunnerNew(uint64_t modelId, unsigned clusterId,
                           const char* serializedArgs);
extern "C" FLEXMLRT_DLL char* flexml_rt_paramLoader(uint64_t modelId,
                                                    const char* fileName,
                                                    const char* key,
                                                    const char* transform);

namespace xrt {
class bo;
class device;
class hw_context;
} // namespace xrt
namespace flexmlrt::client {

class ImplBase;
class ErtTensorTypeImplBase;
struct TensorMetadataType;
namespace impl {

/**
 * @brief %Exception class for shared library dynamic loading errors
 *
 * This exception class is derived from `std::exception` and provides the
 * standard @ref what() member function. An object of this class is constructed
 * with an error message string, which is stored internally and retrieved with
 * the @ref what() member function.
 */
class LoaderException : public std::exception {
  std::string message;

public:
  /**
   * Constructs an Exception object.
   *
   * @param msg an error message string, which is copied and stored internal to
   * the object
   */
  LoaderException(std::string msg) : message(std::move(msg)) {}

  /**
   * Returns the error message string passed to the constructor.
   *
   * @return the error message string
   */
  // Lint: not compatible with C++11
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  const char* what() const noexcept override { return message.c_str(); }
};

inline std::string getLibPath(const std::string& modelPath) {
  // check if library is installed in a conda or python venv
  const char* condaPath = std::getenv("CONDA_PREFIX");
  const char* pyvenvPath = std::getenv("VIRTUAL_ENV");
  if (condaPath) {
    std::filesystem::path flexmlrtLibPath;
#ifdef _WIN32
    flexmlrtLibPath = std::filesystem::path(std::string(condaPath)) / "Lib" /
                      "site-packages" / "flexmlrt" / "lib" / FLEXMLRT_LIB_NAME;
#else
    flexmlrtLibPath = std::filesystem::path(std::string(condaPath)) / "lib" /
                      "python3.10" / "site-packages" / "flexmlrt" / "lib" /
                      FLEXMLRT_LIB_NAME;
    if (!std::filesystem::exists(flexmlrtLibPath)) {
      // try python3.12 path
      flexmlrtLibPath = std::filesystem::path(std::string(condaPath)) / "lib" /
                        "python3.12" / "site-packages" / "flexmlrt" / "lib" /
                        FLEXMLRT_LIB_NAME;
    }
#endif
    return flexmlrtLibPath.string();
  } else if (pyvenvPath) {
    auto libPath = std::filesystem::path(std::string(pyvenvPath)) / "lib" /
                   "python3.10" / "site-packages" / "flexmlrt" / "lib" /
                   FLEXMLRT_LIB_NAME;
    return libPath.string();
  } else {
    // assume library is in the same directory as modelPath
    auto libPath =
        std::filesystem::path(modelPath).parent_path() / FLEXMLRT_LIB_NAME;
    return libPath.string();
  }
}

#if FlexmlRT_IS_STATIC_LIB == 1
extern "C" ErtTensorTypeImplBase*
flexml_client_ErtTensorType_createImpl(const TensorMetadataType metadata);
extern "C" void
flexml_client_ErtTensorType_destroyImpl(ErtTensorTypeImplBase* pImpl);
extern "C" ImplBase* flexml_client_Model_createImpl(const void* options,
                                                    std::size_t optionsSize);
extern "C" void flexml_client_Model_destroyImpl(ImplBase* pImpl);

// Static library version for both Windows and Linux
// In this case, we do not use dynamic loading, but instead use a static
// registry of function pointers to retrieve the functions.
template <typename FuncType>
FuncType getStaticFunction(const std::string& funcName, bool silent = false) {
  // For static linking, we need a registry of function pointers
  static std::map<std::string, void*> staticFunctionRegistry = {
      {"flexml_client_ErtTensorType_createImpl",
       reinterpret_cast<void*>(flexml_client_ErtTensorType_createImpl)},
      {"flexml_client_ErtTensorType_destroyImpl",
       reinterpret_cast<void*>(flexml_client_ErtTensorType_destroyImpl)},
      {"flexml_client_Model_createImpl",
       reinterpret_cast<void*>(flexml_client_Model_createImpl)},
      {"flexml_client_Model_destroyImpl",
       reinterpret_cast<void*>(flexml_client_Model_destroyImpl)}};

  FLEXML_LOADER_DEBUG_PRINT("Static funcName=" << funcName);

  // Initialize registry if empty (this would be populated by static library
  // initialization)
  if (staticFunctionRegistry.empty()) {
    // This would be populated by the static library's initialization code
    // For now, return nullptr to indicate static functions not registered
    if (!silent) {
      std::ostringstream oss;
      oss << "ERROR: Static function registry not initialized for function: "
          << funcName << std::endl;
      throw LoaderException(oss.str());
    }
    return nullptr;
  }

  auto it = staticFunctionRegistry.find(funcName);
  if (it == staticFunctionRegistry.end()) {
    if (!silent) {
      std::ostringstream oss;
      oss << "ERROR: Static function not found: " << funcName << std::endl;
      throw LoaderException(oss.str());
    }
    return nullptr;
  }

  return reinterpret_cast<FuncType>(it->second);
}
#endif

#ifdef _WIN32

// Name of the model .so inside the .flexml directory
const std::string modelLibName = "libflexml_usermodel.dll";

inline std::string getWinErrorStr(DWORD errorId) {
  LPVOID bufptr = nullptr;
  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&bufptr, 0, NULL);

  std::string errStr;
  if (bufptr) {
    errStr = reinterpret_cast<char*>(bufptr);
    LocalFree(bufptr);
  }
  return errStr;
}

template <typename FuncType>
FuncType getDynamicFunction(const std::string& libName,
                            const std::string& funcName, bool silent = false) {
  FLEXML_LOADER_DEBUG_PRINT("libName=" << libName << ", funcName=" << funcName);

  // open the library

  HMODULE handle = nullptr;
  SetLastError(0);
  std::string winLibName = libName;
  std::replace(winLibName.begin(), winLibName.end(), '/', '\\');
  handle = LoadLibraryA(winLibName.c_str());

  // To consider: save the handle for each libName in a map.  Calling dlopen
  // multiple times increases the ref count, requiring an equal number of
  // dlclose, but in the context of an application that runs forever, leaving
  // the .so open doesn't matter.

  FLEXML_LOADER_DEBUG_PRINT("after LoadLibrary, handle=0x" << std::hex << handle
                                                           << std::dec);
  if (handle == nullptr) {
    if (silent)
      return nullptr;
    FLEXML_LOADER_DEBUG_PRINT("inside handle==nullptr");
    std::ostringstream oss;
    oss << "ERROR: Cannot open library " << winLibName << ": "
        << getWinErrorStr(GetLastError()) << "." << std::endl;
    FLEXML_LOADER_DEBUG_PRINT("after oss filling");
    throw LoaderException(oss.str());
  }

  // load the symbol
  FLEXML_LOADER_DEBUG_PRINT("after handle==nullptr check");
  SetLastError(0); // reset errors
  FLEXML_LOADER_DEBUG_PRINT("before GetProcAddress");
  FARPROC pFunc = GetProcAddress(handle, funcName.c_str());
  FLEXML_LOADER_DEBUG_PRINT("after GetProcAddress");
  if (pFunc == nullptr) {
    if (silent)
      return nullptr;
    FLEXML_LOADER_DEBUG_PRINT("inside null pFunc");
    std::ostringstream oss;
    oss << "ERROR: Cannot load symbol '" << funcName
        << "': " << getWinErrorStr(GetLastError())
        << ".  Please recompile the model." << std::endl;
    FLEXML_LOADER_DEBUG_PRINT("after 2nd oss filling");
    throw LoaderException(oss.str());
  }
  FLEXML_LOADER_DEBUG_PRINT("before return");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<FuncType>(pFunc);
}

#else
// Name of the model .so inside the .flexml directory
const std::string modelLibName = "libflexml_usermodel.so";

template <typename FuncType>
FuncType getDynamicFunction(const std::string& libName,
                            const std::string& funcName, bool silent = false) {
  FLEXML_LOADER_DEBUG_PRINT("libName=" << libName << ", funcName=" << funcName);

  // open the library

  void* handle =
#  ifdef FLEXML_LOADER_USE_DLMOPEN
      dlmopen(LM_ID_NEWLM, libName.c_str(), RTLD_LAZY);
#  else
      // We need RTLD_LOCAL to confine the symbols of the loaded library.
      // Otherwise, when calling this a second time to load another model,
      // it would resolve to the symbols from the first model.
      dlopen(libName.c_str(), RTLD_LAZY | RTLD_LOCAL);
#  endif
  // To consider: save the handle for each libName in a map.  Calling dlopen
  // multiple times increases the ref count, requiring an equal number of
  // dlclose, but in the context of an application that runs forever, leaving
  // the .so open doesn't matter.

  FLEXML_LOADER_DEBUG_PRINT("after dlopen, handle=0x" << std::hex << handle
                                                      << std::dec);
  if (handle == nullptr) {
    if (silent)
      return nullptr;
    FLEXML_LOADER_DEBUG_PRINT("inside handle==nullptr");
    std::ostringstream oss;
    oss << "ERROR: Cannot open library " << libName << ": " << dlerror() << "."
        << std::endl;
    FLEXML_LOADER_DEBUG_PRINT("after oss filling");
    throw LoaderException(oss.str());
  }

  // load the symbol
  FLEXML_LOADER_DEBUG_PRINT("after handle==nullptr check");
  dlerror(); // reset errors
  FLEXML_LOADER_DEBUG_PRINT("before dlsym");
  void* pFunc = dlsym(handle, funcName.c_str());
  FLEXML_LOADER_DEBUG_PRINT("after dlsym");
  const char* dlsymError2 = dlerror();
  if (dlsymError2 != nullptr) {
    if (silent)
      return nullptr;
    FLEXML_LOADER_DEBUG_PRINT("inside dlsym_error2");
    std::ostringstream oss;
    oss << "ERROR: Cannot load symbol '" << funcName << "': " << dlsymError2
        << ".  Please recompile the model." << std::endl;
    FLEXML_LOADER_DEBUG_PRINT("after 2nd oss filling");
    throw LoaderException(oss.str());
  }
  FLEXML_LOADER_DEBUG_PRINT("before return");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return reinterpret_cast<FuncType>(pFunc);
}
#endif // _WIN32

} // namespace impl

// #############################################################################

/**
 * @brief %Exception class for FlexML API run-time errors
 *
 * This exception class is derived from `std::exception` and provides the
 * standard @ref what() member function. An object of this class is constructed
 * with an error message string, which is stored internally and retrieved with
 * the @ref what() member function.
 */
class Exception : public std::exception {
  std::string message;

public:
  /**
   * Constructs an Exception object.
   *
   * @param msg an error message string, which is copied and stored internal to
   * the object
   */
  Exception(std::string msg) : message(std::move(msg)) {}

  /**
   * Returns the error message string passed to the constructor.
   *
   * @return the error message string
   */
  // Lint: not compatible with C++11
  // NOLINTNEXTLINE(modernize-use-nodiscard)
  const char* what() const noexcept override { return message.c_str(); }
};

enum class ErrorCode : int {
  NoError = 0,
  IncompatibleInput = 1,
  IncompatibleOutput = 2,
  WrongNumInputs = 3,
  WrongNumOutputs = 4
};

class BadArgsException : public Exception {

  static std::string getString(int rc) {
    std::string codeStr;
    switch (ErrorCode(rc)) {
    case ErrorCode::NoError:
      codeStr = "No error";
      break;
    case ErrorCode::IncompatibleInput:
      codeStr = "Incompatible input shape";
      break;
    case ErrorCode::IncompatibleOutput:
      codeStr = "Incompatible output shape";
      break;
    case ErrorCode::WrongNumInputs:
      codeStr = "Wrong number of inputs";
      break;
    case ErrorCode::WrongNumOutputs:
      codeStr = "Wrong number of outputs";
      break;
    }

    return std::string("Model argument error: ") + codeStr;
  }

public:
  BadArgsException(int rc) : Exception(getString(rc)) {}
};

/**
 * @brief Enum class representing data types for FlexML API
 */
enum class DataType {
  Float32 = 0,  // float
  Int8 = 1,     // int8_t
  UInt8 = 2,    // uint8_t
  Int16 = 3,    // int16_t
  UInt16 = 4,   // uint16_t
  BFloat16 = 5, // bfloat16_t
  Bool = 6,     // bool
  Float16 = 7,  // float16_t
};

/**
 * @brief Enum class representing data formats for FlexML API
 */
enum class DataFormat {
  NCHW = 0,   // NCHW format
  NHWC = 1,   // NHWC format
  NHW = 2,    // NHW format
  HCWNC8 = 3, // HCWNC8 format
  HCWNC4 = 4, // HCWNC4 format
  GENERIC = 5 // Generic format, used for custom formats
};

/**
 * @brief Enum class representing Rounding Modes for FlexML API
 */
enum class RoundingMode {
  ROUND_TO_NEAREST_EVEN = 0, // Round to nearest
  TRUNCATION = 1             // Round towards zero
};

inline std::string toString(DataType type) {
  switch (type) {
  case DataType::Float32:
    return "fp32";
  case DataType::BFloat16:
    return "bf16";
  case DataType::Int8:
    return "int8";
  case DataType::UInt8:
    return "uint8";
  case DataType::Int16:
    return "int16";
  case DataType::UInt16:
    return "uint16";
  case DataType::Bool:
    return "bool";
  case DataType::Float16:
    return "fp16";
  default:
    return "UNKNOWN";
  }
}
inline std::string toString(DataFormat format) {
  switch (format) {
  case DataFormat::NCHW:
    return "NCHW";
  case DataFormat::NHWC:
    return "NHWC";
  case DataFormat::NHW:
    return "NHW";
  case DataFormat::HCWNC8:
    return "HCWNC8";
  case DataFormat::HCWNC4:
    return "HCWNC4";
  case DataFormat::GENERIC:
    return "GENERIC";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Struct containing FlexML API Input/Ouput buffer type for
 * general External RunTime (ERT) integration TODO: to be deprecated
 */
struct ErtIoType {
  /**
   * Pointer to the buffer data
   */
  void* data;

  /**
   * Buffer name in the Model ADF graph. This is used to verify node names from
   * ORT matches port names in the codegen ADF by Flexml Backend
   */
  std::string name;

  /**
   * Buffer index from external runtime to match against the argument index
   * of forward function
   */
  int idx;

  /**
   * Total size of valid data or allocated space in buffer data pointer
   */
  size_t size;

  /**
   * Data type of buffer elements
   */
  std::string type;
};

/**
 * @brief Struct containing FlexML API Input/Ouput buffer type for
 * general External RunTime (ERT) integration TODO: to be deprecated
 */
struct ErtIoTypeNew {
  /**
   * Pointer to the buffer data
   */
  void* data = nullptr;

  /**
   * Buffer name in the Model ADF graph. This is used to verify node names from
   * ORT matches port names in the codegen ADF by Flexml Backend
   */
  std::string name;

  /**
   * Buffer index from external runtime to match against the argument index
   * of forward function
   */
  int idx;

  /**
   * Total size of valid data or allocated space in buffer data pointer
   */
  size_t size;

  /**
   * Data type of buffer elements
   */
  std::string type;

  /**
   * Shape of buffer element
   */
  std::vector<std::int64_t> shape;

  /**
   * offset of the buffer data
   * NOTE: Used only for weight buffers
   */
  size_t offset = 0;
};

/** * @brief Struct containing metadata for a tensor buffer
 *
 * This struct contains metadata about a tensor buffer, including its size,
 * data type, shape, strides, format, and quantization parameters.
 */
struct TensorMetadataType {
  /**
   * Buffer name in the Model ADF graph. This is used to verify node names from
   * ORT matches port names in the codegen ADF by Flexml Backend
   */
  std::string name;

  /**
   * Total size of valid data or allocated space in buffer data pointer
   */
  size_t size;

  /**
   * Data type of buffer elements
   */
  DataType type;

  /**
   * Shape of buffer element
   */
  std::vector<std::uint32_t> shape;

  /**
   * Strides for each dimension in shape
   */
  std::vector<uint32_t> strides;

  /**
   * Format of the buffer data
   * data formats, such as "NHWC", "NCHW", etc.
   */
  DataFormat format;

  /**
   * Dimension permutation order for the buffer data for GENERIC format w.r.t.
   * the CPU Tensor format This is used to specify the order of dimensions for
   * the expected format.
   * - CPU Tensor formatOrder will be the reference contigous sequence.
   * - HW Tensor will be a permutation of the contigous sequence.
   * For example, if the CPU format is "ABCD", the formatOrder will be set to
   * {0, 1, 2, 3} and for HW Tensor format "ADBC", it will be set to {0, 3, 1,
   * 2}.
   */
  std::vector<std::uint32_t> formatOrder;

  /**
   * Scale factor, zero point and rounding mode for quantization
   * NOTE: Used only for quantized buffers
   */
  double scaleFactor;
  int zeroPoint;
  RoundingMode roundingMode;
};

/**
 * @brief Abstract base class for ErtTensorType implementation
 */
class ErtTensorTypeImplBase {
public:
  virtual ~ErtTensorTypeImplBase() = default;
  virtual const xrt::bo& getBo() const = 0;
  virtual xrt::bo& getMutableBo() = 0;
  virtual void setBo(const xrt::bo& bo) = 0;
  virtual const TensorMetadataType& getMetadata() const = 0;
  virtual const xrt::device& getDevice() const = 0;
  virtual const xrt::hw_context& getHwContext() const = 0;
};

/**
 * @brief Struct containing FlexML API Input/Ouput tensor type for
 * general External RunTime (ERT) integration
 * TODO: This will replace ErtIoType and ErtIoTypeNew in future
 */
struct ErtTensorType {
public:
  /**
   * Pointer to the buffer data, if buffer interface is used
   * NOTE: For CPU bufffers, this points to the user-owned memory.
   *       For Device buffers, this points to the memory allocated by FlexmlRT
   */
  void* data = nullptr;

  /**
   * Buffer index from external runtime to match against the argument index
   * of forward function
   */
  int idx = 0;

  /**
   * offset of the buffer data
   * NOTE: Used only for weight buffers
   */
  size_t offset = 0;

  /**
   * Indicates whether the buffer is provided as a BO or host buffer pointer
   */
  bool isBo = false;

  /**
   * Indicates whether pre/post processing should be skipped
   * This is used to skip the pre/post processing steps for the input/output
   * tensors.  This is useful when the user wants to handle the pre/post
   * processing themselves, or when the model does not require any pre/post
   * processing. Tensor is assumed to be in the correct format and type.
   * Default is false, which means pre/post processing will be done by FlexmlRT
   */
  bool skipPrePostProcessing = false;

  // Function interfaces for complex objects

  /**
   * @brief Get the XRT BO object
   * @return const reference to the XRT BO object
   */
  const xrt::bo& getBo() const { return pImpl->getBo(); }

  /**
   * @brief Get Mutable version of XRT BO object
   * @return reference to the XRT BO object
   */
  xrt::bo& getMutableBo() { return pImpl->getMutableBo(); }

  /**
   * @brief Set the XRT BO object
   * @param bo the XRT BO object to set
   */
  void setBo(const xrt::bo& bo) { pImpl->setBo(bo); }

  /**
   * @brief Immutable Metadata for the tensor buffer
   * This contains information about the tensor, such as its size, data type,
   * shape, strides, format, and quantization parameters.
   * Immutable as it is set by the model using getIOTensor API and cannot be
   * changed by the user.
   * @return const reference to the tensor metadata
   */
  const TensorMetadataType& getMetadata() const { return pImpl->getMetadata(); }

  /**
   * @brief Get the XRT device object
   * @return const reference to the XRT device object
   */
  const xrt::device& getDevice() const { return pImpl->getDevice(); }

  /**
   * @brief Get the XRT hw_context object
   * @return const reference to the XRT hw_context object
   */
  const xrt::hw_context& getHwContext() const { return pImpl->getHwContext(); }

  /**
   * @brief Constructor for ErtTensorType
   * @param metadata is the metadata for the tensor
   * This constructor initializes the immutable metadata during the creation of
   * the tensor.
   */
  ErtTensorType(TensorMetadataType metadata) {
    try {
      pImpl = createImpl(flexmlrtLibname, metadata);
    } catch (const impl::LoaderException& ex) {
      flexmlrtLibname = impl::getLibPath("");
      pImpl = createImpl(flexmlrtLibname, metadata);
    } catch (const std::exception& e) {
      FLEXML_LOADER_DEBUG_PRINT(
          "ErtTensorType createImpl failed: " << e.what());
    }
  }

  // empty constructor for vector resize
  ErtTensorType() : pImpl(nullptr) {}

  // constructor from another ErtTensorType and new metadata
  ErtTensorType(const ErtTensorType& other, TensorMetadataType metadata)
      : data(other.data), idx(other.idx), offset(other.offset),
        isBo(other.isBo), skipPrePostProcessing(other.skipPrePostProcessing),
        flexmlrtLibname(other.flexmlrtLibname) {
    pImpl = createImpl(flexmlrtLibname, metadata);
    pImpl->setBo(other.getBo());
  }

  // Copy constructor
  ErtTensorType(const ErtTensorType& other)
      : data(other.data), idx(other.idx), offset(other.offset),
        isBo(other.isBo), skipPrePostProcessing(other.skipPrePostProcessing),
        flexmlrtLibname(other.flexmlrtLibname) {
    pImpl = createImpl(flexmlrtLibname,
                       const_cast<TensorMetadataType&>(other.getMetadata()));
    pImpl->setBo(other.getBo());
  }

  // Copy assignment operator
  ErtTensorType& operator=(const ErtTensorType& other) {
    if (this != &other) {
      destroyImpl(flexmlrtLibname, pImpl);

      data = other.data;
      idx = other.idx;
      offset = other.offset;
      isBo = other.isBo;
      skipPrePostProcessing = other.skipPrePostProcessing;
      flexmlrtLibname = other.flexmlrtLibname;

      pImpl = createImpl(flexmlrtLibname,
                         const_cast<TensorMetadataType&>(other.getMetadata()));
      pImpl->setBo(other.getBo());
    }
    return *this;
  }

  // Move constructor
  ErtTensorType(ErtTensorType&& other) noexcept
      : data(other.data), idx(other.idx), offset(other.offset),
        isBo(other.isBo), skipPrePostProcessing(other.skipPrePostProcessing),
        pImpl(other.pImpl), flexmlrtLibname(std::move(other.flexmlrtLibname)) {
    other.pImpl = nullptr;
    other.data = nullptr;
  }

  // Move assignment operator
  ErtTensorType& operator=(ErtTensorType&& other) noexcept {
    if (this != &other) {
      destroyImpl(flexmlrtLibname, pImpl);

      data = other.data;
      idx = other.idx;
      offset = other.offset;
      isBo = other.isBo;
      skipPrePostProcessing = other.skipPrePostProcessing;
      pImpl = other.pImpl;
      flexmlrtLibname = std::move(other.flexmlrtLibname);

      other.pImpl = nullptr;
      other.data = nullptr;
    }
    return *this;
  }

  // Destructor
  ~ErtTensorType() { destroyImpl(flexmlrtLibname, pImpl); };

private:
  // Pimpl pointer - only declaration, no data members exposed
  ErtTensorTypeImplBase* pImpl;
  std::string flexmlrtLibname = FLEXMLRT_LIB_NAME;

  static ErtTensorTypeImplBase* createImpl(const std::string& libName,
                                           TensorMetadataType& metadata) {
    using CreateFuncType =
        ErtTensorTypeImplBase* (*)(const TensorMetadataType&);
    CreateFuncType pCreateFunc = nullptr;
#if FlexmlRT_IS_STATIC_LIB == 1
    pCreateFunc = impl::getStaticFunction<CreateFuncType>(
        "flexml_client_ErtTensorType_createImpl");
#endif
    // Load the dynamic function from the shared library if static not found
    if (pCreateFunc == nullptr) {
      pCreateFunc = impl::getDynamicFunction<CreateFuncType>(
          libName, "flexml_client_ErtTensorType_createImpl");
    }

    if (pCreateFunc == nullptr) {
      throw impl::LoaderException("ERROR: Cannot find function "
                                  "flexml_client_ErtTensorType_createImpl");
    }
    return pCreateFunc(metadata);
  }

  static void destroyImpl(const std::string& libName,
                          ErtTensorTypeImplBase* pImpl) {
    try {
      using DestroyFuncType = void (*)(ErtTensorTypeImplBase*);
      DestroyFuncType pDestroyFunc = nullptr;
#if FlexmlRT_IS_STATIC_LIB == 1
      pDestroyFunc = impl::getStaticFunction<DestroyFuncType>(
          "flexml_client_ErtTensorType_destroyImpl");
#endif
      if (pDestroyFunc == nullptr) {
        pDestroyFunc = impl::getDynamicFunction<DestroyFuncType>(
            libName, "flexml_client_ErtTensorType_destroyImpl");
      }

      if (pDestroyFunc == nullptr) {
        throw impl::LoaderException("ERROR: Cannot find function "
                                    "flexml_client_ErtTensorType_destroyImpl");
      }
      pDestroyFunc(pImpl);
    } catch (const impl::LoaderException& ex) {
      (void)ex; // usage is debug only; causes warning in release mode
      FLEXML_LOADER_DEBUG_PRINT(
          "getDynamicFunction(destroyFunc) failed: " << ex.what());
    }
  }

  // Factory method for internal library use
  friend void createFromImpl(ErtTensorType& tensor,
                             ErtTensorTypeImplBase* impl);
};

/**
 * @brief Struct containing FlexML API configuration options
 */
struct Options {
  /**
   * File path to the directory containing the compiled ML model.
   */
  std::string modelPath;

  /**
   * The name of the device to run the model on.
   */
  std::string deviceName;

  /**
   * debug flag to turn on debug messages
   */
  bool debug = false;

  /**
   * executeMode indicates the mode of execution
   *   0: Pass the data as it is. No quantization or reformatting.
   *   1:
   */
  uint32_t executeMode = 1;

  /**
   * apiVersion sets the version of APIs to used.
   *   1: APIs support a single input
   *   2: Default. Latest API version that supports multiple inputs
   */
  uint32_t apiVersion = (MAJOR_VER << 16) + MINOR_VER;

  /**
   * subgraph name for custom Ops/Graphs models
   *   used to identify the original subgraph named by
   *   an external runtime like ORT
   */
  std::string subgraphName;

  /**
   * Additional options for the model.
   */
  std::map<std::string, std::any> extOptions;

  /**
   * Destroys this Options object.
   */
  ~Options() = default;

  /**
   * Constructs an Options object with default values.
   */
  Options() = default;

  /**
   * Copy constructor, which deep-copies the reference Options object.
   * @param opt the Options object to copy from
   */
  Options(const Options& opt) { copyIn(opt); }

  /**
   * Deep-copies the reference Options object into this Options object.
   *
   * @param opt the Options object to copy from
   * @return this Options object
   */
  Options& operator=(const Options& opt) {
    copyIn(opt);
    return *this;
  }

private:
  void copyIn(const Options& opt) {
    modelPath = opt.modelPath;
    deviceName = opt.deviceName;
    debug = opt.debug;
    executeMode = opt.executeMode;
    subgraphName = opt.subgraphName;
    for (auto [key, value] : opt.extOptions) {
      extOptions[key] = value;
    }
  }
};

/**
 * @brief Tensor to pass as input or output of a model.
 */
struct Tensor {
  using Dimension = std::size_t;

  /**
   * Pointer to tensor values.  The user owns the memory pointed to.
   */
  float* data = nullptr;

  /**
   * The shape of the tensor, one vector element per dimenion.
   */
  std::vector<Dimension> shape;
};

/**
 * @brief Tensor to pass as input or output of a model.
 */
struct OpsTensor {
  using Dimension = std::int64_t;

  /**
   * Pointer to tensor values.  The user owns the memory pointed to.
   */
  void* data = nullptr;

  /**
   * The shape of the tensor, one vector element per dimenion.
   */
  std::vector<Dimension> shape;

  /**
   * Datatype of the tensor.
   */
  std::string dtype;
};

class ImplBase;

// #############################################################################

/// @cond INTERNAL

class ImplBase {
public:
  virtual ~ImplBase() = default;
  virtual float* forward(const float* ifm) = 0;
  virtual float* forward(unsigned batchSize, const float* ifm) = 0;

  // TODO: Remove this function from here
  virtual void forward(const float* ifm,
                       const std::vector<std::size_t>& inShape, float* ofm,
                       const std::vector<std::size_t>& outShape);

  virtual void forward(const std::vector<Tensor>& ifms,
                       std::vector<Tensor>& ofms) = 0;

  virtual void forward(std::vector<ErtIoType>& ifms,
                       std::vector<ErtIoType>& ofms) = 0;

  virtual void forward(std::vector<ErtIoTypeNew>& ifms,
                       std::vector<ErtIoTypeNew>& ofms,
                       std::vector<ErtIoTypeNew>& wts) = 0;

  [[nodiscard]] virtual std::vector<std::vector<std::size_t>>
  getOfmShapes() const = 0;

  virtual void timestampStart(const std::string& label);
  virtual void timestampEnd(const std::string& label);

  virtual std::vector<ErtTensorType> getIOTensors(std::string type,
                                                  bool hwTensor) = 0;
  virtual void releaseIOTensors(const std::vector<ErtTensorType>& tensors) = 0;

  virtual void setAIAnalyzerProfiling();
  virtual void unsetAIAnalyzerProfiling();
  virtual bool good() = 0;

  // This function signature needs to be kept distinct to prevent ABI confusion
  virtual void forwardErtTensor(const std::vector<ErtTensorType>& ifms,
                                std::vector<ErtTensorType>& ofms) = 0;
  virtual void
  forwardErtTensorBatch(const std::vector<std::vector<ErtTensorType>>& ifms,
                        std::vector<std::vector<ErtTensorType>>& ofms) = 0;

  // New Async forward API
  virtual std::future<void> forwardAsync(const std::vector<ErtTensorType>& ifms,
                                         std::vector<ErtTensorType>& ofms) = 0;
  virtual std::future<void>
  forwardAsync(const std::vector<std::vector<ErtTensorType>>& ifms,
               std::vector<std::vector<ErtTensorType>>& ofms) = 0;
  virtual std::future_status
  waitForwardAsync(std::future<void>& future,
                   std::chrono::milliseconds timeout) = 0;
};
/// @endcond

/**
 * @brief ML Model API class.
 *
 * This class provides the API for the compiled ML model.  Use this class to run
 * the model from a C++ application.
 */
class Model {
public:
  /**
   * Constructs a Model object.
   *
   * @param options the configuration options for the model.  See the Options
   * struct for details.
   */
  Model(const Options& options)
      : options(options),
        libName(options.modelPath + "/" + impl::modelLibName) {

    try {
      pImpl = createImpl(flexmlrtLibname, options);
    } catch (const impl::LoaderException& ex) {
      flexmlrtLibname = impl::getLibPath(options.modelPath);
      pImpl = createImpl(flexmlrtLibname, options);
    } catch (const std::exception& e) {
      FLEXML_LOADER_DEBUG_PRINT("createImpl failed: " << e.what());
    }
  }

  /**
   * Destroys this Model object.
   */
  ~Model() { destroyImpl(flexmlrtLibname, pImpl); }

  /**
   * Runs the compiled ML model.
   *
   * @param ifm the input tensor (input feature map)
   * @return the output tensor (output feature map)
   */
  float* forward(const float* ifm) { return pImpl->forward(ifm); }

  float* forward(unsigned batchSize, const float* ifm) {
    return pImpl->forward(batchSize, ifm);
  }

  void forward(const float* ifm, const std::vector<size_t>& ishape, float* ofm,
               const std::vector<size_t>& oshape) {
    return pImpl->forward(ifm, ishape, ofm, oshape);
  }

  void forward(const std::vector<Tensor>& ifms, std::vector<Tensor>& ofms) {
    pImpl->forward(ifms, ofms);
  }

  void forward(std::vector<ErtIoType>& ifms, std::vector<ErtIoType>& ofms) {
    pImpl->forward(ifms, ofms);
  }

  void forward(std::vector<ErtIoTypeNew>& ifms, std::vector<ErtIoTypeNew>& ofms,
               std::vector<ErtIoTypeNew>& wts) {
    pImpl->forward(ifms, ofms, wts);
  }

  void forward(const std::vector<ErtTensorType>& ifms,
               std::vector<ErtTensorType>& ofms) {
    pImpl->forwardErtTensor(ifms, ofms);
  }

  void forward(const std::vector<std::vector<ErtTensorType>>& ifms,
               std::vector<std::vector<ErtTensorType>>& ofms) {
    pImpl->forwardErtTensorBatch(ifms, ofms);
  }

  [[nodiscard]] std::vector<std::vector<std::size_t>> getOfmShapes() const {
    return pImpl->getOfmShapes();
  }

  /**
   * @brief Get I/O Tensors with metadata
   * This function retrieves the PRE-ALLOCATED (see TODO below) input or output
   * tensors for the Model with IMMUTABLE metadata of expected size, type,
   * shape, format, quantization constants, and offset. Metadata is immutable as
   * its set by the model. The data pointer optionally (in case of HW tensor)
   * points to a preallocated buffer that application/ERT can use to directly
   * load data into (input) or read data from (output).
   *
   * @param type is the type of Tensors to retrieve, "input"/"output"
   * @param hwTensor is a boolean to indicate whether to get hardware Tensor or
   * Cpu Tensor
   *
   * @return std::vector<ErtTensorType> containing the tensors with metadata
   */
  std::vector<ErtTensorType> getIOTensors(std::string type,
                                          bool hwTensor) const {
    return pImpl->getIOTensors(type, hwTensor);
  }

  /**
   * @brief Release I/O Tensors
   * This function releases the input or output tensors previously retrieved
   * using getIOTensors API. This is used to release it back to the internal
   * memory pool
   *
   * @param type is the type of Tensors to release, "input"/"output"
   */
  void releaseIOTensors(const std::vector<ErtTensorType>& tensors) {
    pImpl->releaseIOTensors(tensors);
  }

  void setAIAnalyzerProfiling() { pImpl->setAIAnalyzerProfiling(); }
  void unsetAIAnalyzerProfiling() { pImpl->unsetAIAnalyzerProfiling(); }
  bool good() { return pImpl != nullptr ? pImpl->good() : false; }

  // New Async forward API
  std::future<void> forwardAsync(std::vector<ErtTensorType>& ifms,
                                 std::vector<ErtTensorType>& ofms) {
    return pImpl->forwardAsync(ifms, ofms);
  }

  std::future<void>
  forwardAsync(std::vector<std::vector<ErtTensorType>>& ifms,
               std::vector<std::vector<ErtTensorType>>& ofms) {
    return pImpl->forwardAsync(ifms, ofms);
  }

  std::future_status waitForwardAsync(std::future<void>& future,
                                      std::chrono::milliseconds timeout) {
    return pImpl->waitForwardAsync(future, timeout);
  }

private:
  Options options;
  std::string libName;
  std::string flexmlrtLibname = FLEXMLRT_LIB_NAME;
  ImplBase* pImpl = nullptr;

  static ImplBase* createImpl(const std::string& libName,
                              const flexmlrt::client::Options& options) {
    using CreateFunc =
        flexmlrt::client::ImplBase* (*)(const void*, std::size_t);
    FLEXML_LOADER_DEBUG_PRINT("Loading FlexmlRT API shared library dynamically "
                              "(via dlopen) from "
                              << libName);
    CreateFunc pCreateFunc = nullptr;
#if FlexmlRT_IS_STATIC_LIB == 1
    pCreateFunc = impl::getStaticFunction<CreateFunc>(
        "flexml_client_Model_createImpl", true);
#endif
    // Load the dynamic function from the shared library if static not found
    if (pCreateFunc == nullptr) {
      pCreateFunc = impl::getDynamicFunction<CreateFunc>(
          libName, "flexml_client_Model_createImpl");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return pCreateFunc(reinterpret_cast<const void*>(&options),
                       sizeof(Options));
  }

  static void destroyImpl(const std::string& libName,
                          flexmlrt::client::ImplBase* pImpl) {
    using DestroyFunc = void (*)(ImplBase*);
    try {
      DestroyFunc pDestroyFunc = nullptr;
#if FlexmlRT_IS_STATIC_LIB == 1
      pDestroyFunc = impl::getStaticFunction<DestroyFunc>(
          "flexml_client_Model_destroyImpl", true);
#endif
      // Load the dynamic function from the shared library if static not found
      if (pDestroyFunc == nullptr) {
        pDestroyFunc = impl::getDynamicFunction<DestroyFunc>(
            libName, "flexml_client_Model_destroyImpl");
      }
      pDestroyFunc(pImpl);
    } catch (const impl::LoaderException& ex) {
      (void)ex; // usage is debug only; causes warning in release mode
      FLEXML_LOADER_DEBUG_PRINT(
          "getDynamicFunction(destroyFunc) failed: " << ex.what());
    }
  }
};

} // namespace flexmlrt::client
