namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace stream {

really_inline value && json::as_value() noexcept { return std::move(value); }

really_inline simdjson_result<array> json::get_array() noexcept { return as_value().get_array(); }
really_inline simdjson_result<object> json::get_object() noexcept { return as_value().get_object(); }
really_inline simdjson_result<uint64_t> json::get_uint64() noexcept { return as_value().get_uint64(); }
really_inline simdjson_result<int64_t> json::get_int64() noexcept { return as_value().get_int64(); }
really_inline simdjson_result<double> json::get_double() noexcept { return as_value().get_double(); }
really_inline simdjson_result<std::string_view> json::get_string() noexcept { return as_value().get_string(); }
really_inline simdjson_result<raw_json_string> json::get_raw_json_string() noexcept { return as_value().get_raw_json_string(); }
really_inline simdjson_result<bool> json::get_bool() noexcept { return as_value().get_bool(); }

#if SIMDJSON_EXCEPTIONS
really_inline json::operator array() noexcept(false) { return as_value(); }
really_inline json::operator object() noexcept(false) { return as_value(); }
really_inline json::operator uint64_t() noexcept(false) { return as_value(); }
really_inline json::operator int64_t() noexcept(false) { return as_value(); }
really_inline json::operator double() noexcept(false) { return as_value(); }
really_inline json::operator std::string_view() noexcept(false) { return as_value(); }
really_inline json::operator raw_json_string() noexcept(false) { return as_value(); }
really_inline json::operator bool() noexcept(false) { return as_value(); }
#endif

really_inline array json::begin() noexcept { return as_value().begin(); }
really_inline array json::end() noexcept { return {}; }
really_inline simdjson_result<value&> json::operator[](std::string_view key) noexcept { return as_value()[key]; }

really_inline json::json(json &&other) noexcept
  : index{other.index}, buf{other.buf}, string_buf{other.string_buf}, depth{other.depth}, value(this, other.value.depth) {}

really_inline json::json(const uint32_t *_index, const uint8_t *_buf, uint8_t *_string_buf, int _depth) noexcept
  : index{_index}, buf{_buf}, string_buf{_string_buf}, depth{_depth}, value(this, _depth) {}

really_inline const uint8_t *json::advance() noexcept { auto result = &buf[*index]; index++; return result; }
really_inline const uint8_t *json::peek(int n) const noexcept { return &buf[*(index+n)]; }
really_inline bool json::advance_if_start(uint8_t structural) noexcept {
  bool found = advance_if(structural);
  depth += found;
  return found;
}
really_inline bool json::advance_if_end(uint8_t structural) noexcept {
  bool found = advance_if(structural);
  depth -= found;
  return found;
}
really_inline bool json::advance_if(uint8_t structural) noexcept {
  bool found = buf[*index] == structural;
  index += found;
  return found;
}
really_inline bool json::advance_if(uint8_t structural, uint8_t structural2) noexcept {
  bool found = buf[*index] == structural && buf[*(index+1)] == structural2;
  index += found*2;
  return found;
}
really_inline bool json::advance_if(uint8_t structural, uint8_t structural2, uint8_t structural3) noexcept {
  bool found = buf[*index] == structural && buf[*(index+1)] == structural2 && buf[*(index+2)] == structural3;
  index += found*3;
  return found;
}

} // namespace stream
} // namespace SIMDJSON_IMPLEMENTATION

namespace {
  using namespace simdjson::SIMDJSON_IMPLEMENTATION::stream;
}

really_inline simdjson_result<json>::simdjson_result(json &&value) noexcept
    : internal::simdjson_result_base<json>(std::forward<json>(value)) {}
really_inline simdjson_result<json>::simdjson_result(json &&value, error_code error) noexcept
    : internal::simdjson_result_base<json>(std::forward<json>(value), error) {}

// TODO make sure the passing of a pointer here isn't about to cause us trouble
really_inline simdjson_result<stream::value&> simdjson_result<json>::as_value() noexcept {
  return simdjson_result<stream::value&>(first.value, error());
}
really_inline array simdjson_result<json>::begin() noexcept { return as_value().begin(); }
really_inline array simdjson_result<json>::end() noexcept { return as_value().end(); }
really_inline simdjson_result<value&> simdjson_result<json>::operator[](std::string_view key) && noexcept { return as_value()[key]; }

really_inline simdjson_result<array> simdjson_result<json>::get_array() && noexcept { return as_value().get_array(); }
really_inline simdjson_result<object> simdjson_result<json>::get_object() && noexcept { return as_value().get_object(); }
really_inline simdjson_result<uint64_t> simdjson_result<json>::get_uint64() && noexcept { return as_value().get_uint64(); }
really_inline simdjson_result<int64_t> simdjson_result<json>::get_int64() && noexcept { return as_value().get_int64(); }
really_inline simdjson_result<double> simdjson_result<json>::get_double() && noexcept { return as_value().get_double(); }
really_inline simdjson_result<std::string_view> simdjson_result<json>::get_string() && noexcept { return as_value().get_string(); }
really_inline simdjson_result<raw_json_string> simdjson_result<json>::get_raw_json_string() && noexcept { return as_value().get_raw_json_string(); }
really_inline simdjson_result<bool> simdjson_result<json>::get_bool() && noexcept { return as_value().get_bool(); }

#if SIMDJSON_EXCEPTIONS
really_inline simdjson_result<json>::operator array() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator object() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator uint64_t() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator int64_t() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator double() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator std::string_view() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator raw_json_string() && noexcept(false) { return as_value(); }
really_inline simdjson_result<json>::operator bool() && noexcept(false) { return as_value(); }
#endif

} // namespace simdjson
