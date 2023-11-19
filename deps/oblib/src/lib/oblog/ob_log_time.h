#ifndef OB_LOG_TIME
#define OB_LOG_TIME

#include "lib/time/ob_time_utility.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
  using namespace common;

#define _STR(x) _VAL(x)  
#define _VAL(x) #x

#define __LINESTR__ ":" _STR(__LINE__)
#define __FILENAME__ (strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/') + 1):__FILE__)

#define PTIME_CURRENT_FUNC(funcname) \
struct _PrintTimeGuard_ { \
  int64_t start_;\
  _PrintTimeGuard_() { start_ = ObTimeUtility::current_time(); } \
  ~_PrintTimeGuard_() {\
    auto now = ObTimeUtility::current_time();\
    auto cost = now - start_;\
    LOG_INFO("print time in function: " #funcname, K(cost));\
  }\
} _print_time_guard\

#define PTIME_UPTONOW() do {\
  auto now = ObTimeUtility::current_time();\
  auto cost = now - ObTimeUtility::last_time_;\
  ObTimeUtility::last_time_ = now;\
  LOG_INFO("print time up to ", __FILE__ __LINESTR__ ,K(cost));\
  } while(0)\

#define PTIME_UPTONOW_STR(arg) do {\
  auto now = ObTimeUtility::current_time();\
  auto cost = now - ObTimeUtility::last_time_;\
  ObTimeUtility::last_time_ = now;\
  LOG_INFO("print time up to " #arg __LINESTR__, K(cost));\
  } while(0)\

#define PTIME_IF(arg) \
  if (\
    [&]() -> bool { \
      auto now = ObTimeUtility::current_time();\
      auto cost = now - ObTimeUtility::last_time_;\
      ObTimeUtility::last_time_ = now;\
      LOG_INFO("print time up to " #arg ,K(cost));\
      bool _ret = arg;\
      auto _now = ObTimeUtility::current_time();\
      auto _cost = _now - ObTimeUtility::last_time_;\
      LOG_INFO("print time during " #arg ,K(_cost));\
      ObTimeUtility::last_time_ = _now;\
      return _ret;\
    }()\
  )

#define PTIME_FUNC(func) \
  [&]() -> decltype(func) {\
    auto now = ObTimeUtility::current_time();\
    auto cost = now - ObTimeUtility::last_time_;\
    ObTimeUtility::last_time_ = now;\
    LOG_INFO("print time up to " #func ,K(cost));\
    auto _ret = func;\
    auto _now = ObTimeUtility::current_time();\
    auto _cost = _now - ObTimeUtility::last_time_;\
    LOG_INFO("print time during " #func ,K(_cost));\
    ObTimeUtility::last_time_ = _now;\
    return _ret;\
  }()

} // end namespace oceanbase

#endif