#ifndef CHAMPSIM_CONTROL_FLOW_HISTORY_H
#define CHAMPSIM_CONTROL_FLOW_HISTORY_H

#include <cstdint>
#include <map>

namespace champsim
{
// Branch PCs observed at the front end, shared by reuse predictors on this CPU.
// No branch outcomes or target addresses are used by CHiRP.
struct control_flow_history {
  uint64_t conditional = 0;
  uint64_t indirect = 0;

  void observe(uint64_t pc, bool is_conditional, bool is_indirect)
  {
    if (is_conditional)
      conditional = (conditional << 8) | ((pc >> 4) & 0xff);
    if (is_indirect)
      indirect = (indirect << 8) | ((pc >> 4) & 0xff);
  }
};

inline std::map<uint32_t, control_flow_history> cpu_control_flow_history;
} // namespace champsim

#endif
