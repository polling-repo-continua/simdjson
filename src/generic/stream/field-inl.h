namespace simdjson {
namespace SIMDJSON_IMPLEMENTATION {
namespace stream {

really_inline raw_json_string field::key() noexcept { return json->peek(-2)+1; }
really_inline field::field(stream::json *_json, int _depth) noexcept : value(_json, _depth) {}

} // namespace stream
} // namespace SIMDJSON_IMPLEMENTATION
} // namespace simdjson
