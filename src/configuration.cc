#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include <caf/io/middleman.hpp>
#include <caf/openssl/manager.hpp>
#include <caf/atom.hpp>

#include "broker/version.hh"
#include "broker/data.hh"
#include "broker/endpoint.hh"
#include "broker/snapshot.hh"
#include "broker/store.hh"
#include "broker/time.hh"
#include "broker/address.hh"
#include "broker/internal_command.hh"
#include "broker/port.hh"
#include "broker/status.hh"
#include "broker/subnet.hh"
#include "broker/topic.hh"

#include "broker/configuration.hh"

namespace broker {

configuration::configuration(broker_options opts) : options_(std::move(opts)) {
  add_message_types(*this);
  load<caf::io::middleman>();
  if (! options_.disable_ssl)
    load<caf::openssl::manager>();

  set("logger.file-name", "broker_[PID]_[TIMESTAMP].log");
  /*
  set("logger.verbosity", caf::atom("INFO"));
  set("logger.component-filter", "broker");
  */

  if (auto env = getenv("BROKER_DEBUG_VERBOSE")) {
    if (*env && *env != '0') {
      set("logger.verbosity", caf::atom("DEBUG"));
      set("logger.component-filter", "");
    }
  }

  if (auto env = getenv("BROKER_DEBUG_LEVEL")) {
    char level[10];
    strncpy(level, env, sizeof(level));
    level[sizeof(level) - 1] = '\0';
    set("logger.verbosity", caf::atom(level));
  }

  if (auto env = getenv("BROKER_DEBUG_COMPONENT_FILTER")) {
    set("logger.component-filter", env);
  }

  set("middleman.app-identifier",
      "broker.v" + std::to_string(version::protocol));
}

configuration::configuration(int argc, char** argv) : configuration{} {
  parse(argc, argv);
}

#define ADD_MSG_TYPE(name) cfg.add_message_type<name>(#name)

void configuration::add_message_types(caf::actor_system_config& cfg) {
  ADD_MSG_TYPE(broker::data);
  ADD_MSG_TYPE(broker::data);
  ADD_MSG_TYPE(broker::address);
  ADD_MSG_TYPE(broker::subnet);
  ADD_MSG_TYPE(broker::port);
  ADD_MSG_TYPE(broker::timespan);
  ADD_MSG_TYPE(broker::timestamp);
  ADD_MSG_TYPE(broker::enum_value);
  ADD_MSG_TYPE(broker::vector);
  ADD_MSG_TYPE(broker::set);
  ADD_MSG_TYPE(broker::status);
  ADD_MSG_TYPE(broker::table);
  ADD_MSG_TYPE(broker::topic);
  ADD_MSG_TYPE(std::vector<broker::topic>);
  ADD_MSG_TYPE(broker::optional<broker::timestamp>);
  ADD_MSG_TYPE(broker::optional<broker::timespan>);
  ADD_MSG_TYPE(broker::snapshot);
  ADD_MSG_TYPE(broker::internal_command);
  ADD_MSG_TYPE(broker::command_message);
  ADD_MSG_TYPE(broker::data_message);
  ADD_MSG_TYPE(broker::node_message);
  ADD_MSG_TYPE(broker::set_command);
  ADD_MSG_TYPE(broker::store::stream_type::value_type);
  ADD_MSG_TYPE(std::vector<broker::store::stream_type::value_type>);
  ADD_MSG_TYPE(broker::endpoint::stream_type::value_type);
  ADD_MSG_TYPE(std::vector<broker::endpoint::stream_type::value_type>);
}

#undef ADD_MSG_TYPE

} // namespace broker
