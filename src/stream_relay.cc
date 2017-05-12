#include "broker/detail/stream_relay.hh"

#include "broker/detail/stream_governor.hh"

namespace broker {
namespace detail {

stream_relay::stream_relay(caf::intrusive_ptr<stream_governor> gov,
                           const caf::stream_id& sid)
  : governor_(std::move(gov)),
    sid_(std::move(sid)) {
  // nop
}

stream_relay::~stream_relay() {
  // nop
}

caf::error stream_relay::add_downstream(caf::strong_actor_ptr& hdl) {
  return governor_->add_downstream(sid_, hdl);
}

caf::error
stream_relay::confirm_downstream(const caf::strong_actor_ptr& rebind_from,
                                 caf::strong_actor_ptr& hdl,
                                 long initial_demand, bool redeployable) {
  return governor_->confirm_downstream(sid_, rebind_from, hdl, initial_demand,
                                       redeployable);
}

caf::error stream_relay::downstream_demand(caf::strong_actor_ptr& hdl,
                                           long new_demand) {
  return governor_->downstream_demand(sid_, hdl, new_demand);
}

caf::error stream_relay::push(long*) {
  return governor_->push();
}

caf::expected<long> stream_relay::add_upstream(caf::strong_actor_ptr& hdl,
                                               const caf::stream_id& sid,
                                               caf::stream_priority prio) {
  return governor_->add_upstream(sid_, hdl, sid, prio);
}

caf::error stream_relay::upstream_batch(caf::strong_actor_ptr& hdl,
                                        long xs_size, caf::message& xs) {
  return governor_->upstream_batch(sid_, hdl, xs_size, xs);
}

caf::error stream_relay::close_upstream(caf::strong_actor_ptr& hdl) {
  return governor_->close_upstream(sid_, hdl);
}

void stream_relay::abort(caf::strong_actor_ptr& hdl,
                         const caf::error& reason) {
  return governor_->abort(sid_, hdl, reason);
}

bool stream_relay::done() const {
  return false;
}

caf::message stream_relay::make_output_token(const caf::stream_id& x) const {
  return caf::make_message(caf::stream<element_type>{x});
}

} // namespace broker
} // namespace detail