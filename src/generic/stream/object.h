#include "simdjson/error.h"

namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace stream {

/**
 * A forward-only JSON object.
 */
class object {
public:
  really_inline object() noexcept;

  really_inline simdjson_result<value&> operator[](std::string_view key) noexcept;

  //
  // iterator interface
  //
  really_inline simdjson_result<stream::field&> operator*() noexcept;
  really_inline bool operator!=(object &other) noexcept;
  really_inline object &operator++() noexcept;

  really_inline object begin() noexcept;
  really_inline object end() noexcept;

protected:
  really_inline object(stream::json *_json, int _depth, bool _finished, error_code _error=SUCCESS) noexcept;
  really_inline object(stream::value &parent, error_code _error) noexcept;

  really_inline void advance() noexcept;
  really_inline static simdjson_result<object> try_begin(stream::value &parent) noexcept;
  really_inline static object begin(stream::value &parent, error_code error=SUCCESS) noexcept;
  really_inline static object begin(stream::value &parent, bool is_object, error_code error=SUCCESS) noexcept;

  stream::field field;
  bool finished;
  // TODO check whether this gets cleverly elided by the compiler or not. If not, store it in the json
  // so we don't keep multiple of them
  error_code error;

  friend class value;
  friend class simdjson_result<stream::object>;
};

} // namespace stream
} // namespace SIMDJSON_IMPLEMENTATION

namespace {
  using namespace simdjson::SIMDJSON_IMPLEMENTATION::stream;
}

template<>
struct simdjson_result<stream::object> : public internal::simdjson_result_base<stream::object> {
public:
  really_inline simdjson_result(stream::object &&value) noexcept; ///< @private
  really_inline simdjson_result(stream::value &parent, error_code error) noexcept; ///< @private

  really_inline stream::object begin() noexcept;
  really_inline stream::object end() noexcept;
  really_inline simdjson_result<stream::value&> operator[](std::string_view key) noexcept;
};

} // namespace simdjson
