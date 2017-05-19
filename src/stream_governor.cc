#include "broker/detail/stream_governor.hh"

#include <caf/event_based_actor.hpp>
#include <caf/message.hpp>
#include <caf/stream.hpp>

#include <caf/policy/broadcast.hpp>
#include <caf/policy/greedy.hpp>

#include "broker/detail/core_actor.hh"
#include "broker/detail/stream_relay.hh"

namespace broker {
namespace detail {

namespace {

caf::message generic_factory(const caf::stream_id& x) {
  return caf::make_message(caf::stream<caf::message>{x});
}

} // namespace <anonymous>

// --- nested types ------------------------------------------------------------

stream_governor::peer_data::peer_data(stream_governor* parent, filter_type y,
                                      const caf::stream_id& downstream_sid,
                                      caf::abstract_downstream::policy_ptr pp)
  : filter(std::move(y)),
    out(parent->state()->self, downstream_sid, std::move(pp)),
    relay(caf::make_counted<stream_relay>(parent, downstream_sid,
                                          generic_factory)) {
  // nop
}

stream_governor::peer_data::~peer_data() {
  // nop
}

void stream_governor::peer_data::send_stream_handshake() {
  CAF_LOG_TRACE("");
  auto self = static_cast<caf::scheduled_actor*>(out.self());
  caf::stream<caf::message> token{out.sid()};
  auto data = caf::make_message(token, atom::ok::value,
                                caf::actor_cast<caf::actor>(self->ctrl()));
  remote_core->enqueue(caf::make_mailbox_element(
                         self->ctrl(), caf::message_id::make(), {self->ctrl()},
                         caf::make<caf::stream_msg::open>(
                           token.id(), std::move(data), self->ctrl(), hdl(),
                           caf::stream_priority::normal, false)),
                       self->context());
  self->streams().emplace(out.sid(), relay);
}

const caf::strong_actor_ptr& stream_governor::peer_data::hdl() const {
  auto& l = out.paths();
  CAF_ASSERT(l.size() == 1);
  return l.front()->hdl;
}

// --- constructors and destructors --------------------------------------------

stream_governor::stream_governor(core_state* state)
  : state_(state),
    in_(state->self, caf::policy::greedy::make()),
    workers_(state->self, state->self->make_stream_id(),
             caf::policy::broadcast::make()),
    stores_(state->self, state->self->make_stream_id(),
            caf::policy::broadcast::make()) {
  CAF_LOG_DEBUG("started governor with workers SID"
                << workers_.sid() << "and stores SID" << stores_.sid());
}

stream_governor::peer_data*
stream_governor::peer(const caf::actor& remote_core) {
  auto i = peers_.find(remote_core);
  return i != peers_.end() ? i->second.get() : nullptr;
}

stream_governor::~stream_governor() {
  // nop
}

stream_governor::peer_data*
stream_governor::add_peer(caf::strong_actor_ptr downstream_handle,
                          caf::actor remote_core, const caf::stream_id& sid,
                          filter_type filter) {
  CAF_LOG_TRACE(CAF_ARG(downstream_handle)
                << CAF_ARG(remote_core) << CAF_ARG(sid) << CAF_ARG(filter));
  auto pp = caf::policy::broadcast::make();
  auto ptr = caf::make_counted<peer_data>(this, std::move(filter),
                                          sid, std::move(pp));
  ptr->out.add_path(downstream_handle);
  ptr->remote_core = remote_core;
  auto res = peers_.emplace(std::move(remote_core), ptr);
  if (res.second) {
    auto self = static_cast<caf::scheduled_actor*>(ptr->out.self());
    self->streams().emplace(sid, ptr->relay);
    input_to_peers_.emplace(sid, ptr);
    return ptr.get(); // safe, because two more refs to ptr exist
  }
  return nullptr;
}

bool stream_governor::update_peer(const caf::actor& hdl, filter_type filter) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(filter));
  auto i = peers_.find(hdl);
  if (i == peers_.end()) {
    CAF_LOG_DEBUG("cannot update filter on unknown peer");
    return false;
  }
  i->second->filter = std::move(filter);
  return true;
}

caf::error stream_governor::add_downstream(const caf::stream_id& sid,
                                           caf::strong_actor_ptr&) {
  CAF_LOG_ERROR("add_downstream on governor called");
  return caf::sec::invalid_stream_state;
}

void stream_governor::local_push(worker_element&& x) {
  workers_.push(std::move(x));
  workers_.policy().push(workers_);
}

void stream_governor::local_push(topic&& t, data&& x) {
  worker_element e{std::move(t), std::move(x)};
  local_push(std::move(e));
}

void stream_governor::push(topic&& t, data&& x) {
  CAF_LOG_TRACE(CAF_ARG(t) << CAF_ARG(x));
  auto selected = [](const filter_type& f, const worker_element& e) -> bool {
    for (auto& key : f)
      if (key == e.first)
        return true;
    return false;
  };
  worker_element e{std::move(t), std::move(x)};
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (selected(kvp.second->filter, e)) {
      out.push(caf::make_message(e.first, e.second));
      out.policy().push(out);
    }
  }
  local_push(std::move(e));
}

void stream_governor::push(topic&& t, internal_command&& x) {
  CAF_LOG_TRACE(CAF_ARG(t) << CAF_ARG(x));
  auto selected = [](const filter_type& f, const store_element& e) -> bool {
    for (auto& key : f)
      if (key == e.first)
        return true;
    return false;
  };
  CAF_LOG_DEBUG("push internal to command to" << peers_.size() << "peers and"
                << stores_.paths().size() << "data stores");
  store_element e{std::move(t), std::move(x)};
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (selected(kvp.second->filter, e)) {
      out.push(caf::make_message(e.first, e.second));
      out.policy().push(out);
    }
  }
  stores_.push(std::move(e));
  stores_.policy().push(stores_);
}

caf::error
stream_governor::confirm_downstream(const caf::stream_id& sid,
                                    const caf::strong_actor_ptr& rebind_from,
                                    caf::strong_actor_ptr& hdl,
                                    long initial_demand, bool redeployable) {
  CAF_LOG_TRACE(CAF_ARG(rebind_from) << CAF_ARG(hdl)
                << CAF_ARG(initial_demand) << CAF_ARG(redeployable));
  CAF_IGNORE_UNUSED(redeployable);
  if (workers_.confirm_path(rebind_from, hdl, initial_demand)) {
    CAF_LOG_DEBUG("Confirmed path to local worker" << hdl);
    return downstream_demand(sid, hdl, initial_demand);
  }
  if (stores_.confirm_path(rebind_from, hdl, initial_demand)) {
    CAF_LOG_DEBUG("Confirmed path to local data store" << hdl);
    return downstream_demand(sid, hdl, initial_demand);
  }
  auto i = input_to_peers_.find(sid);
  if (i == input_to_peers_.end()) {
    CAF_LOG_ERROR("Cannot confirm path to unknown downstream.");
    return caf::sec::invalid_downstream;
  }
  CAF_LOG_DEBUG("Confirmed path to remote core" << i->second->remote_core);
  i->second->out.confirm_path(rebind_from, hdl, initial_demand);
  return downstream_demand(sid, hdl, initial_demand);
}

caf::error stream_governor::downstream_demand(const caf::stream_id& sid,
                                              caf::strong_actor_ptr& hdl,
                                              long value) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(value));
  auto wpath = workers_.find(hdl);
  if (wpath) {
    wpath->open_credit += value;
    return push();
  }
  auto spath = stores_.find(hdl);
  if (spath) {
    spath->open_credit += value;
    return push();
  }
  auto i = input_to_peers_.find(sid);
  if (i != input_to_peers_.end()) {
    auto pp = i->second->out.find(hdl);
    if (!pp)
      return caf::sec::invalid_stream_state;
    CAF_LOG_DEBUG("grant" << value << "new credit to" << hdl);
    pp->open_credit += value;
    return push();
  }
  return caf::sec::invalid_downstream;
}

caf::error stream_governor::push() {
  CAF_LOG_TRACE("");
  if (workers_.buf_size() > 0)
    workers_.policy().push(workers_);
  if (stores_.buf_size() > 0)
    stores_.policy().push(stores_);
  for (auto& kvp : peers_) {
    auto& out = kvp.second->out;
    if (out.buf_size() > 0)
      out.policy().push(out);
  }
  return caf::none;
}

caf::expected<long> stream_governor::add_upstream(const caf::stream_id&,
                                                  caf::strong_actor_ptr& hdl,
                                                  const caf::stream_id& up_sid,
                                                  caf::stream_priority prio) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(up_sid) << CAF_ARG(prio));
  if (hdl)
    return in_.add_path(hdl, up_sid, prio, total_downstream_net_credit());
  return caf::sec::invalid_argument;
}

caf::error stream_governor::upstream_batch(const caf::stream_id& sid,
                                           caf::strong_actor_ptr& hdl,
                                           long xs_size, caf::message& xs) {
  CAF_LOG_TRACE(CAF_ARG(sid) << CAF_ARG(hdl)
                << CAF_ARG(xs_size) << CAF_ARG(xs));
  // Sanity checking.
  auto path = in_.find(hdl);
  if (!path)
    return caf::sec::invalid_upstream;
  if (xs_size > path->assigned_credit)
    return caf::sec::invalid_stream_state;
  // Process messages from local workers.
  if (xs.match_elements<std::vector<worker_element>>()) {
    // Predicate for matching a single element against the filters of a peers.
    auto selected = [](const filter_type& f, const worker_element& x) -> bool {
      using std::get;
      for (auto& key : f)
        if (key == x.first)
          return true;
      return false;
    };
    auto& vec = xs.get_mutable_as<std::vector<worker_element>>(0);
    // Decrease credit assigned to `hdl` and get currently available downstream
    // credit on all paths.
    CAF_LOG_DEBUG(CAF_ARG(path->assigned_credit) << CAF_ARG(xs_size));
    path->assigned_credit -= xs_size;
    // Forward data to all other peers.
    for (auto& kvp : peers_)
      if (kvp.second->out.sid() != sid) {
        auto& out = kvp.second->out;
        for (const auto& x : vec)
          if (selected(kvp.second->filter, x))
            out.push(caf::make_message(x.first, x.second));
        if (out.buf_size() > 0) {
          out.policy().push(out);
        }
      }
    // Move elements from `xs` to the buffer for local subscribers.
    CAF_LOG_DEBUG("local subs: " << workers_.num_paths());
    if (!workers_.lanes().empty())
      for (auto& x : vec)
        workers_.push(std::move(x));
    workers_.policy().push(workers_);
    // Grant new credit to upstream if possible.
    auto available = total_downstream_net_credit();
    if (available > 0)
      in_.assign_credit(available);
    return caf::none;
  }
  // Process messages from peers.
  if (!xs.match_elements<std::vector<peer_element>>())
    return caf::sec::unexpected_message;
  // Predicate for matching a single element against the filters of a peers.
  auto selected = [](const filter_type& f, const peer_element& x) -> bool {
    CAF_ASSERT(x.size() == 2 && x.match_element<topic>(0));
    using std::get;
    for (auto& key : f)
      if (key == x.get_as<topic>(0))
        return true;
    return false;
  };
  // Unwrap `xs`.
  auto& vec = xs.get_mutable_as<std::vector<peer_element>>(0);
  // Decrease credit assigned to `hdl` and get currently available downstream
  // credit on all paths.
  CAF_LOG_DEBUG(CAF_ARG(path->assigned_credit) << CAF_ARG(xs_size));
  path->assigned_credit -= xs_size;
  // Forward data to all other peers.
  for (auto& kvp : peers_)
    if (kvp.second->out.sid() != sid) {
      auto& out = kvp.second->out;
      for (const auto& x : vec)
        if (selected(kvp.second->filter, x))
          out.push(x);
      if (out.buf_size() > 0) {
        out.policy().push(out);
      }
    }
  // Move elements from `xs` to the buffer for local subscribers.
  if (!workers_.lanes().empty())
    for (auto& x : vec)
      if (x.match_element<data>(1)) {
        x.force_unshare();
        workers_.push(std::make_pair(x.get_as<topic>(0),
                                     std::move(x.get_mutable_as<data>(1))));
      }
  workers_.policy().push(workers_);
  if (!stores_.lanes().empty())
    for (auto& x : vec)
      if (x.match_element<internal_command>(1)) {
        x.force_unshare();
        stores_.push(
          std::make_pair(x.get_as<topic>(0),
                         std::move(x.get_mutable_as<internal_command>(1))));
      }
  stores_.policy().push(stores_);
  // Grant new credit to upstream if possible.
  CAF_LOG_DEBUG("pushed data to" << peers_.size() << "peers,"
                << workers_.paths().size() << "workers, and"
                << workers_.paths().size() << "stores");
  auto available = total_downstream_net_credit();
  if (available > 0)
    in_.assign_credit(available);
  return caf::none;
}

caf::error stream_governor::close_upstream(const caf::stream_id& sid,
                                           caf::strong_actor_ptr& hdl) {
  CAF_LOG_TRACE(CAF_ARG(hdl));
  if (in_.remove_path(hdl))
    return caf::none;
  return caf::sec::invalid_upstream;
}

void stream_governor::abort(const caf::stream_id& sid,
                            caf::strong_actor_ptr& hdl,
                            const caf::error& reason) {
  CAF_LOG_TRACE(CAF_ARG(hdl) << CAF_ARG(reason));
  if (hdl == nullptr) {
    // actor shutdown
    if (!workers_.lanes().empty())
      workers_.abort(hdl, reason);
    if (!stores_.lanes().empty())
      stores_.abort(hdl, reason);
    if (!peers_.empty()) {
      for (auto& kvp : peers_)
        kvp.second->out.abort(hdl, reason);
      peers_.clear();
    }
  }
  if (workers_.remove_path(hdl))
    return;
  if (stores_.remove_path(hdl))
    return;
  auto i = input_to_peers_.find(sid);
  if (i == input_to_peers_.end()) {
    CAF_LOG_DEBUG("Abort from unknown stream ID.");
    return;
  }
  auto& pd = *i->second;
  auto j = peers_.find(pd.remote_core);
  if (j != peers_.end()) {
    j->second->out.abort(hdl, reason);
    peers_.erase(j);
  }
  input_to_peers_.erase(i);
}

long stream_governor::total_downstream_net_credit() const {
  auto net_credit = std::min(workers_.total_net_credit(),
                             stores_.total_net_credit());
  for (auto& kvp : peers_)
    net_credit = std::min(net_credit, kvp.second->out.total_net_credit());
  return net_credit;
}

void intrusive_ptr_add_ref(stream_governor* x) {
  x->ref();
}

void intrusive_ptr_release(stream_governor* x) {
  x->deref();
}

} // namespace detail
} // namespace broker
