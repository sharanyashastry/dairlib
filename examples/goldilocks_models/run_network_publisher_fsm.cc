#include <string>

#include <gflags/gflags.h>

#include "dairlib/lcmt_dairlib_signal.hpp"
#include "examples/goldilocks_models/controller/network_publisher.h"

namespace dairlib {

DEFINE_string(channel_in, "FSM_T", "");
DEFINE_string(channel_out, "FSM_T", "");
DEFINE_int32(n_publishes, 3, "number of publishes after getting a message");

int do_main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  goldilocks_models::NetworkPublisher<dairlib::lcmt_dairlib_signal>(
      FLAGS_channel_in, FLAGS_channel_out, FLAGS_n_publishes);

  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) { return dairlib::do_main(argc, argv); }