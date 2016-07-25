#include "broker/error.hh"

#include "broker/detail/appliers.hh"
#include "broker/detail/memory_backend.hh"

namespace broker {
namespace detail {


memory_backend::memory_backend(backend_options opts)
  : options_{std::move(opts)} {
  // nop
}

expected<void>
memory_backend::put(const data& key, data value, optional<time::point> expiry) {
  store_[key] = {std::move(value), expiry};
  return {};
}

expected<void> memory_backend::add(const data& key, const data& value,
                                   optional<time::point> expiry) {
  auto i = store_.find(key);
  if (i == store_.end())
    return ec::no_such_key;
  auto result = visit(adder{value}, i->second.first);
  if (result)
    i->second.second = expiry;
  return result;
}

expected<void> memory_backend::remove(const data& key, const data& value,
                                      optional<time::point> expiry) {
  auto i = store_.find(key);
  if (i == store_.end())
    return ec::no_such_key;
  auto result = visit(remover{value}, i->second.first);
  if (result)
    i->second.second = expiry;
  return result;
}

expected<void> memory_backend::erase(const data& key) {
  store_.erase(key);
  return {};
}

expected<bool> memory_backend::expire(const data& key) {
  auto now = time::now();
  auto i = store_.find(key);
  if (i == store_.end())
    return ec::no_such_key;
  if (!i->second.second || now < i->second.second)
    return false;
  store_.erase(i);
  return true;
}

expected<data> memory_backend::get(const data& key) const {
  auto i = store_.find(key);
  if (i == store_.end())
    return ec::no_such_key;
  return i->second.first;
}

expected<data> memory_backend::get(const data& key, const data& value) const {
  auto i = store_.find(key);
  if (i == store_.end())
    return ec::no_such_key;
  // We do not use the default implementation because operating directly on the
  // stored data element is more efficient in case the visitation returns an
  // error.
  return visit(retriever{value}, i->second.first);
}

expected<bool> memory_backend::exists(const data& key) const {
  return store_.count(key) == 1;
}

expected<uint64_t> memory_backend::size() const {
  return store_.size();
}

expected<snapshot> memory_backend::snapshot() const {
  broker::snapshot ss;
  for (auto& p : store_)
    ss.entries.emplace(p.first, p.second.first);
  return ss;
}

} // namespace detail
} // namespace broker